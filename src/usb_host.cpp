#include "usb_host.h"
#include "logger.h"

// ── USB MSC Host: nur kompilieren wenn Komponente vorhanden ──────────
#if __has_include("usb/msc_host.h")
  #include <dirent.h>
  #include <sys/stat.h>
  #include <sys/statvfs.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include "usb/usb_host.h"
  #include "usb/msc_host.h"
  #include "usb/msc_host_vfs.h"
  #include "esp_vfs_fat.h"
  #define HAS_MSC 1
#else
  #define HAS_MSC 0
#endif

// ── Stub (kein Treiber) ───────────────────────────────────────────────
#if !HAS_MSC

void UsbHost::begin() {
    LOGI("[USB] Kein USB-MSC-Treiber – components/usb_host_msc fehlt.");
}
void     UsbHost::loop()                                                         {}
String   UsbHost::getDeviceInfo()  const                                         { return "kein Treiber"; }
DiskInfo UsbHost::getDiskInfo()    const                                         { return {}; }
bool     UsbHost::listDirectory(const String&, std::vector<FileEntry>&)          { return false; }
bool     UsbHost::readTextFile(const String&, String&)                           { return false; }
bool     UsbHost::writeTextFile(const String&, const String&)                    { return false; }
size_t   UsbHost::getFileSize(const String&)                                     { return 0; }
bool     UsbHost::fileExists(const String&)                                      { return false; }
int      UsbHost::readFileChunk(const String&, size_t, uint8_t*, size_t)         { return -1; }
bool     UsbHost::writeFileChunk(const String&, size_t, const uint8_t*, size_t, bool) { return false; }
bool     UsbHost::deleteEntry(const String&)                                     { return false; }
bool     UsbHost::createDirectory(const String&)                                 { return false; }
bool     UsbHost::renameEntry(const String&, const String&)                      { return false; }
String   UsbHost::absPath(const String& r) const                                 { return r; }
void     UsbHost::usbLibTask(void*)                                              { vTaskDelete(nullptr); }
void     UsbHost::mscClientTask(void*)                                           { vTaskDelete(nullptr); }

#else
// ── Volle Implementierung ─────────────────────────────────────────────

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static msc_host_device_handle_t s_msc_device  = nullptr;
static msc_host_vfs_handle_t    s_vfs_handle  = nullptr;
static EventGroupHandle_t       s_evt_group   = nullptr;

// Gerätadresse aus dem Event speichern (Hub: nicht immer Adresse 1)
static uint8_t s_pending_addr = 0;

#define EVT_CONNECTED    (1 << 0)
#define EVT_DISCONNECTED (1 << 1)

static void msc_event_cb(const msc_host_event_t* event, void* /*arg*/) {
    if (event->event == MSC_HOST_DEVICE_CONNECTED) {
        s_pending_addr = event->device.address;
        xEventGroupSetBits(s_evt_group, EVT_CONNECTED);
        LOGI("[USB] Gerät verbunden (addr=%d)", s_pending_addr);
    } else if (event->event == MSC_HOST_DEVICE_DISCONNECTED) {
        xEventGroupSetBits(s_evt_group, EVT_DISCONNECTED);
        LOGI("[USB] Gerät getrennt (addr=%d)", event->device.address);
    }
}

// ── USB Library Daemon ────────────────────────────────────────────────

void UsbHost::usbLibTask(void* /*arg*/) {
    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)   break;
    }
    vTaskDelete(nullptr);
}

// ── MSC Client Task ───────────────────────────────────────────────────

void UsbHost::mscClientTask(void* arg) {
    UsbHost* self = static_cast<UsbHost*>(arg);

    msc_host_driver_config_t msc_cfg = {};
    msc_cfg.task_priority         = MSC_TASK_PRIO;
    msc_cfg.stack_size            = MSC_TASK_STACK;
    msc_cfg.callback              = msc_event_cb;
    msc_cfg.callback_arg          = nullptr;
    msc_cfg.create_backround_task = true;
    ESP_ERROR_CHECK(msc_host_install(&msc_cfg));

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            s_evt_group,
            EVT_CONNECTED | EVT_DISCONNECTED,
            pdTRUE, pdFALSE, portMAX_DELAY);

        // ── Stick verbunden ──────────────────────────────────────────
        if (bits & EVT_CONNECTED) {
            uint8_t addr = s_pending_addr;
            esp_err_t err = msc_host_install_device(addr, &s_msc_device);
            if (err != ESP_OK) {
                LOGE("[USB] install_device addr=%d: %s", addr, esp_err_to_name(err));
                s_msc_device = nullptr;
                continue;
            }

            // Bezeichnung auslesen
            msc_host_device_info_t info = {};
            if (msc_host_get_device_info(s_msc_device, &info) == ESP_OK) {
                snprintf(self->_vendor,  sizeof(self->_vendor),  "%s",
                         info.idVendor  ? (const char*)info.idVendor  : "USB");
                snprintf(self->_product, sizeof(self->_product), "%s",
                         info.idProduct ? (const char*)info.idProduct : "Stick");
            }

            // FAT mounten
            esp_vfs_fat_mount_config_t fat = {};
            fat.format_if_mount_failed = false;
            fat.max_files              = 10;
            err = msc_host_vfs_register(s_msc_device, USB_MOUNT_POINT,
                                        &fat, &s_vfs_handle);
            if (err == ESP_OK) {
                self->_mounted = true;
                LOGI("[USB] Gemountet: %s %s → %s",
                     self->_vendor, self->_product, USB_MOUNT_POINT);
            } else {
                LOGE("[USB] VFS-Mount fehlgeschlagen: %s", esp_err_to_name(err));
                msc_host_uninstall_device(s_msc_device);
                s_msc_device = nullptr;
            }
        }

        // ── Stick getrennt ───────────────────────────────────────────
        if (bits & EVT_DISCONNECTED) {
            self->_mounted = false;
            if (s_vfs_handle) {
                msc_host_vfs_unregister(s_vfs_handle);
                s_vfs_handle = nullptr;
            }
            if (s_msc_device) {
                msc_host_uninstall_device(s_msc_device);
                s_msc_device = nullptr;
            }
            memset(self->_vendor,  0, sizeof(self->_vendor));
            memset(self->_product, 0, sizeof(self->_product));
            LOGI("[USB] Stick ausgehängt");
        }
    }
    msc_host_uninstall();
    vTaskDelete(nullptr);
}

// ── Public ────────────────────────────────────────────────────────────

void UsbHost::begin() {
    LOGI("[USB] Initialisiere USB Host (OTG) ...");
    s_evt_group = xEventGroupCreate();

    usb_host_config_t cfg = {};
    cfg.skip_phy_setup = false;
    cfg.intr_flags     = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&cfg));

    xTaskCreate(usbLibTask,    "usb_lib", USB_HOST_TASK_STACK, nullptr, USB_HOST_TASK_PRIO, nullptr);
    xTaskCreate(mscClientTask, "msc_cli", MSC_TASK_STACK,      this,    MSC_TASK_PRIO,      nullptr);
    LOGI("[USB] Warte auf USB-Stick (auch über Hub) ...");
}

void UsbHost::loop() {}

String UsbHost::getDeviceInfo() const {
    if (!_mounted) return "nicht verbunden";
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %s", _vendor, _product);
    return String(buf);
}

DiskInfo UsbHost::getDiskInfo() const {
    DiskInfo d;
    if (!_mounted) return d;
    struct statvfs st;
    if (statvfs(USB_MOUNT_POINT, &st) == 0) {
        d.totalBytes = (uint64_t)st.f_blocks * st.f_frsize;
        d.freeBytes  = (uint64_t)st.f_bfree  * st.f_bsize;
        d.valid      = true;
    }
    return d;
}

String UsbHost::absPath(const String& rel) const {
    if (rel.startsWith(USB_MOUNT_POINT)) return rel;
    if (rel.startsWith("/")) return String(USB_MOUNT_POINT) + rel;
    return String(USB_MOUNT_POINT) + "/" + rel;
}

// ── Verzeichnis ───────────────────────────────────────────────────────

bool UsbHost::listDirectory(const String& path, std::vector<FileEntry>& entries) {
    if (!_mounted) return false;
    DIR* dir = opendir(absPath(path).c_str());
    if (!dir) return false;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        FileEntry fe;
        fe.name  = ent->d_name;
        fe.isDir = (ent->d_type == DT_DIR);
        fe.size  = 0;
        if (!fe.isDir) {
            struct stat st;
            if (stat((absPath(path) + "/" + ent->d_name).c_str(), &st) == 0)
                fe.size = (size_t)st.st_size;
        }
        entries.push_back(fe);
    }
    closedir(dir);
    return true;
}

// ── Text-Editor ───────────────────────────────────────────────────────

bool UsbHost::readTextFile(const String& path, String& content) {
    if (!_mounted) return false;
    String full = absPath(path);
    struct stat st;
    if (stat(full.c_str(), &st) != 0 || (size_t)st.st_size > EDITOR_MAX_SIZE)
        return false;
    FILE* f = fopen(full.c_str(), "r");
    if (!f) return false;
    content.reserve((size_t)st.st_size + 1);
    char buf[512];
    while (!feof(f)) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n > 0) content.concat(buf, n);
    }
    fclose(f);
    return true;
}

bool UsbHost::writeTextFile(const String& path, const String& content) {
    if (!_mounted) return false;
    FILE* f = fopen(absPath(path).c_str(), "w");
    if (!f) return false;
    size_t w = fwrite(content.c_str(), 1, content.length(), f);
    fclose(f);
    return w == content.length();
}

// ── Datei-Operationen ─────────────────────────────────────────────────

size_t UsbHost::getFileSize(const String& path) {
    if (!_mounted) return 0;
    struct stat st;
    if (stat(absPath(path).c_str(), &st) != 0) return 0;
    return (size_t)st.st_size;
}

bool UsbHost::fileExists(const String& path) {
    if (!_mounted) return false;
    struct stat st;
    return stat(absPath(path).c_str(), &st) == 0;
}

int UsbHost::readFileChunk(const String& path, size_t offset, uint8_t* buf, size_t len) {
    if (!_mounted) return -1;
    FILE* f = fopen(absPath(path).c_str(), "rb");
    if (!f) return -1;
    fseek(f, (long)offset, SEEK_SET);
    int n = (int)fread(buf, 1, len, f);
    fclose(f);
    return n;
}

bool UsbHost::writeFileChunk(const String& path, size_t offset,
                             const uint8_t* data, size_t len, bool /*isFinal*/) {
    if (!_mounted) return false;
    FILE* f = fopen(absPath(path).c_str(), offset == 0 ? "wb" : "ab");
    if (!f) return false;
    bool ok = (fwrite(data, 1, len, f) == len);
    fclose(f);
    return ok;
}

static bool removeRecursive(const String& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (!d) return false;
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            if (!strcmp(ent->d_name,".") || !strcmp(ent->d_name,"..")) continue;
            removeRecursive(path + "/" + ent->d_name);
        }
        closedir(d);
        return rmdir(path.c_str()) == 0;
    }
    return unlink(path.c_str()) == 0;
}

bool UsbHost::deleteEntry(const String& path) {
    if (!_mounted) return false;
    return removeRecursive(absPath(path));
}

bool UsbHost::createDirectory(const String& path) {
    if (!_mounted) return false;
    return mkdir(absPath(path).c_str(), 0755) == 0;
}

bool UsbHost::renameEntry(const String& from, const String& to) {
    if (!_mounted) return false;
    return rename(absPath(from).c_str(), absPath(to).c_str()) == 0;
}

#endif // HAS_MSC

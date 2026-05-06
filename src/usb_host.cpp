#include "usb_host.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// ESP-IDF USB Host + MSC VFS
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "esp_vfs_fat.h"

// ── Interne Handles ──────────────────────────────────────────────────

static msc_host_device_handle_t  s_msc_device  = nullptr;
static msc_host_vfs_handle_t     s_vfs_handle  = nullptr;
static EventGroupHandle_t        s_evt_group   = nullptr;

#define EVT_CONNECTED    (1 << 0)
#define EVT_DISCONNECTED (1 << 1)
#define EVT_QUIT         (1 << 2)

// ── MSC-Callback (vom USB-Host-Task aufgerufen) ─────────────────────

static void msc_event_cb(const msc_host_event_t* event, void* arg) {
    UsbHost* host = static_cast<UsbHost*>(arg);
    if (event->event == MSC_HOST_DEVICE_CONNECTED) {
        xEventGroupSetBits(s_evt_group, EVT_CONNECTED);
    } else if (event->event == MSC_HOST_DEVICE_DISCONNECTED) {
        xEventGroupSetBits(s_evt_group, EVT_DISCONNECTED);
    }
}

// ── USB Library Daemon (verarbeitet generische USB-Bus-Events) ───────

void UsbHost::usbLibTask(void* arg) {
    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            // Alle Geräte freigegeben – Library kann deinstalliert werden
            break;
        }
    }
    vTaskDelete(nullptr);
}

// ── MSC Client Task (Mount/Unmount des USB-Sticks) ───────────────────

void UsbHost::mscClientTask(void* arg) {
    UsbHost* self = static_cast<UsbHost*>(arg);

    msc_host_driver_config_t msc_cfg = {
        .create_backround_task = true,
        .task_priority         = MSC_TASK_PRIO,
        .stack_size            = MSC_TASK_STACK,
        .callback              = msc_event_cb,
        .callback_arg          = self,
    };
    ESP_ERROR_CHECK(msc_host_install(&msc_cfg));

    bool quit = false;
    while (!quit) {
        EventBits_t bits = xEventGroupWaitBits(
            s_evt_group,
            EVT_CONNECTED | EVT_DISCONNECTED | EVT_QUIT,
            pdTRUE,          // clear on exit
            pdFALSE,         // wait for any
            portMAX_DELAY);

        if (bits & EVT_CONNECTED) {
            // Ersten verfügbaren MSC-Port öffnen
            uint8_t dev_addr = 1;
            esp_err_t err = msc_host_install_device(dev_addr, &s_msc_device);
            if (err != ESP_OK) {
                Serial.printf("[USB] msc_host_install_device failed: %s\n",
                              esp_err_to_name(err));
                continue;
            }

            // Gerätebeschreibung auslesen
            msc_host_device_info_t info;
            if (msc_host_get_device_info(s_msc_device, &info) == ESP_OK) {
                snprintf(self->_vendor,  sizeof(self->_vendor),  "%s",
                         info.idVendor  ? info.idVendor  : "?");
                snprintf(self->_product, sizeof(self->_product), "%s",
                         info.idProduct ? info.idProduct : "USB-Stick");
            }

            // FAT-Dateisystem auf VFS mounten
            esp_vfs_fat_mount_config_t fat_cfg = {
                .format_if_mount_failed      = false,
                .max_files                   = 8,
                .allocation_unit_size        = 0,
                .disk_status_check_enable    = false,
            };
            err = msc_host_vfs_register(s_msc_device, USB_MOUNT_POINT,
                                        &fat_cfg, &s_vfs_handle);
            if (err == ESP_OK) {
                self->_mounted = true;
                Serial.printf("[USB] Gemountet: %s %s → %s\n",
                              self->_vendor, self->_product, USB_MOUNT_POINT);
            } else {
                Serial.printf("[USB] VFS-Mount fehlgeschlagen: %s\n",
                              esp_err_to_name(err));
                msc_host_uninstall_device(s_msc_device);
                s_msc_device = nullptr;
            }
        }

        if (bits & EVT_DISCONNECTED) {
            self->_mounted = false;
            Serial.println("[USB] Stick getrennt – unmounte ...");

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
        }

        if (bits & EVT_QUIT) {
            quit = true;
        }
    }

    msc_host_uninstall();
    vTaskDelete(nullptr);
}

// ── Public API ────────────────────────────────────────────────────────

void UsbHost::begin() {
    Serial.println("[USB] Initialisiere USB Host ...");

    s_evt_group = xEventGroupCreate();

    // USB Host Library installieren
    usb_host_config_t host_cfg = {
        .skip_phy_setup      = false,
        .intr_flags          = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));

    // USB Library Daemon starten
    xTaskCreate(usbLibTask, "usb_lib", USB_HOST_TASK_STACK,
                nullptr, USB_HOST_TASK_PRIO, nullptr);

    // MSC Client Task starten
    xTaskCreate(mscClientTask, "msc_client", MSC_TASK_STACK,
                this, MSC_TASK_PRIO, nullptr);

    Serial.println("[USB] Warte auf USB-Stick ...");
}

void UsbHost::loop() {
    // Alles via Tasks – hier nichts zu tun
}

String UsbHost::getDeviceInfo() const {
    if (!_mounted) return "nicht verbunden";
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %s", _vendor, _product);
    return String(buf);
}

// ── Hilfsfunktion: relativen Pfad zu absolutem USB-Pfad ─────────────

String UsbHost::absPath(const String& rel) const {
    if (rel.startsWith(USB_MOUNT_POINT)) return rel;
    if (rel.startsWith("/")) return String(USB_MOUNT_POINT) + rel;
    return String(USB_MOUNT_POINT) + "/" + rel;
}

// ── Verzeichnis-Listing ───────────────────────────────────────────────

bool UsbHost::listDirectory(const String& path, std::vector<FileEntry>& entries) {
    if (!_mounted) return false;

    String full = absPath(path);
    DIR* dir = opendir(full.c_str());
    if (!dir) {
        Serial.printf("[USB] opendir '%s' fehlgeschlagen: %s\n",
                      full.c_str(), strerror(errno));
        return false;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;  // . und .. überspringen

        FileEntry fe;
        fe.name  = ent->d_name;
        fe.isDir = (ent->d_type == DT_DIR);
        fe.size  = 0;

        if (!fe.isDir) {
            String fp = full + "/" + ent->d_name;
            struct stat st;
            if (stat(fp.c_str(), &st) == 0) {
                fe.size = (size_t)st.st_size;
            }
        }
        entries.push_back(fe);
    }
    closedir(dir);
    return true;
}

// ── Text-Datei lesen (Editor) ─────────────────────────────────────────

bool UsbHost::readTextFile(const String& path, String& content) {
    if (!_mounted) return false;

    String full = absPath(path);
    struct stat st;
    if (stat(full.c_str(), &st) != 0) return false;
    if ((size_t)st.st_size > EDITOR_MAX_SIZE) {
        Serial.printf("[USB] readTextFile: Datei zu groß (%u Bytes)\n",
                      (unsigned)st.st_size);
        return false;
    }

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

// ── Text-Datei schreiben (Editor) ─────────────────────────────────────

bool UsbHost::writeTextFile(const String& path, const String& content) {
    if (!_mounted) return false;

    String full = absPath(path);
    FILE* f = fopen(full.c_str(), "w");
    if (!f) {
        Serial.printf("[USB] writeTextFile: fopen '%s' fehlgeschlagen: %s\n",
                      full.c_str(), strerror(errno));
        return false;
    }

    size_t written = fwrite(content.c_str(), 1, content.length(), f);
    fclose(f);
    return written == content.length();
}

// ── Dateigröße / Existenz ─────────────────────────────────────────────

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

// ── Chunk-Lesen für Download-Streaming ───────────────────────────────

int UsbHost::readFileChunk(const String& path, size_t offset,
                           uint8_t* buf, size_t len) {
    if (!_mounted) return -1;

    FILE* f = fopen(absPath(path).c_str(), "rb");
    if (!f) return -1;

    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    int n = (int)fread(buf, 1, len, f);
    fclose(f);
    return n;
}

// ── Chunk-Schreiben für Upload ────────────────────────────────────────

bool UsbHost::writeFileChunk(const String& path, size_t offset,
                             const uint8_t* data, size_t len, bool isFinal) {
    if (!_mounted) return false;

    const char* mode = (offset == 0) ? "wb" : "ab";
    FILE* f = fopen(absPath(path).c_str(), mode);
    if (!f) return false;

    bool ok = (fwrite(data, 1, len, f) == len);
    fclose(f);
    return ok;
}

// ── Löschen ───────────────────────────────────────────────────────────

static bool removeRecursive(const String& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;

    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (!d) return false;
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 ||
                strcmp(ent->d_name, "..") == 0) continue;
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

// ── Verzeichnis anlegen ───────────────────────────────────────────────

bool UsbHost::createDirectory(const String& path) {
    if (!_mounted) return false;
    return mkdir(absPath(path).c_str(), 0755) == 0;
}

// ── Umbenennen / Verschieben ──────────────────────────────────────────

bool UsbHost::renameEntry(const String& from, const String& to) {
    if (!_mounted) return false;
    return rename(absPath(from).c_str(), absPath(to).c_str()) == 0;
}

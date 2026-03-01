#include "usb_host.h"

void UsbHost::begin() {
    Serial.println("[USB] Initializing USB Host ...");

    // TODO: Initialize ESP32-S3 USB Host with MSC (Mass Storage Class)
    // The ESP32-S3 native USB OTG supports Host mode.
    // Implementation depends on the ESP-IDF USB Host library.
    //
    // Steps:
    // 1. usb_host_install()
    // 2. Register MSC client
    // 3. Mount FAT filesystem from Garmin device
    //
    // For now, this is a scaffold. The actual USB Host MSC driver
    // requires ESP-IDF components (usb_host_msc) which need to be
    // added as a PlatformIO lib or ESP-IDF component.

    Serial.println("[USB] USB Host initialized (waiting for device)");
}

void UsbHost::loop() {
    // TODO: Poll USB Host events
    // usb_host_lib_handle_events()
    // Check for device connect/disconnect
}

bool UsbHost::isDeviceConnected() {
    return _connected;
}

String UsbHost::getDeviceInfo() {
    if (!_connected) return "No device connected";
    return _deviceInfo;
}

void UsbHost::onDeviceConnected() {
    _connected = true;
    _deviceInfo = "Garmin Zumo";
    Serial.println("[USB] Device connected: " + _deviceInfo);
}

void UsbHost::onDeviceDisconnected() {
    _connected = false;
    _deviceInfo = "";
    Serial.println("[USB] Device disconnected");
}

// ── File system operations (scaffold) ───────────────────────────────

bool UsbHost::listDirectory(const String& path, std::vector<FileEntry>& entries) {
    if (!_connected) return false;

    // TODO: Read FAT directory from mounted Garmin USB storage
    // Use standard POSIX calls (opendir/readdir) on mounted path

    Serial.printf("[USB] listDirectory: %s\n", path.c_str());
    return false;
}

bool UsbHost::readFile(const String& path, uint8_t* buffer, size_t& length) {
    if (!_connected) return false;

    // TODO: Read file from Garmin USB storage
    Serial.printf("[USB] readFile: %s\n", path.c_str());
    return false;
}

bool UsbHost::writeFile(const String& path, const uint8_t* data, size_t length) {
    if (!_connected) return false;

    // TODO: Write file to Garmin USB storage
    Serial.printf("[USB] writeFile: %s (%d bytes)\n", path.c_str(), length);
    return false;
}

bool UsbHost::deleteFile(const String& path) {
    if (!_connected) return false;

    // TODO: Delete file on Garmin USB storage
    Serial.printf("[USB] deleteFile: %s\n", path.c_str());
    return false;
}

bool UsbHost::createDirectory(const String& path) {
    if (!_connected) return false;

    // TODO: Create directory on Garmin USB storage
    Serial.printf("[USB] createDirectory: %s\n", path.c_str());
    return false;
}

size_t UsbHost::getFileSize(const String& path) {
    if (!_connected) return 0;

    // TODO: Get file size from Garmin USB storage
    return 0;
}

bool UsbHost::fileExists(const String& path) {
    if (!_connected) return false;

    // TODO: Check if file exists on Garmin USB storage
    return false;
}

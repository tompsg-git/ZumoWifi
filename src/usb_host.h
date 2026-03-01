#pragma once

#include <Arduino.h>
#include <USB.h>
#include "config.h"

// File entry for directory listings
struct FileEntry {
    String name;
    size_t size;
    bool   isDir;
};

class UsbHost {
public:
    void begin();
    void loop();

    bool isDeviceConnected();
    String getDeviceInfo();

    // File system operations on Garmin device
    bool listDirectory(const String& path, std::vector<FileEntry>& entries);
    bool readFile(const String& path, uint8_t* buffer, size_t& length);
    bool writeFile(const String& path, const uint8_t* data, size_t length);
    bool deleteFile(const String& path);
    bool createDirectory(const String& path);
    size_t getFileSize(const String& path);
    bool fileExists(const String& path);

private:
    bool _connected = false;
    String _deviceInfo = "";

    void onDeviceConnected();
    void onDeviceDisconnected();
};

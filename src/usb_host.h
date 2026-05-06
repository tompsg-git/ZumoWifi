#pragma once

#include <Arduino.h>
#include <vector>
#include "config.h"

struct FileEntry {
    String name;
    size_t size;
    bool   isDir;
};

struct DiskInfo {
    uint64_t totalBytes = 0;
    uint64_t freeBytes  = 0;
    bool     valid      = false;
};

class UsbHost {
public:
    void begin();
    void loop();

    bool isMounted() const { return _mounted; }
    bool isDeviceConnected() const { return _mounted; }
    String getDeviceInfo() const;
    DiskInfo getDiskInfo() const;

    bool listDirectory(const String& path, std::vector<FileEntry>& entries);
    bool readTextFile(const String& path, String& content);
    bool writeTextFile(const String& path, const String& content);
    size_t getFileSize(const String& path);
    bool   fileExists(const String& path);
    int    readFileChunk(const String& path, size_t offset, uint8_t* buf, size_t len);
    bool   writeFileChunk(const String& path, size_t offset,
                          const uint8_t* data, size_t len, bool isFinal);
    bool deleteEntry(const String& path);
    bool createDirectory(const String& path);
    bool renameEntry(const String& from, const String& to);

private:
    volatile bool    _mounted = false;
    char _vendor[16]  = {};
    char _product[32] = {};

    String absPath(const String& rel) const;

    static void usbLibTask(void* arg);
    static void mscClientTask(void* arg);
};

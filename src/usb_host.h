#pragma once

#include <Arduino.h>
#include <vector>
#include "config.h"

struct FileEntry {
    String name;
    size_t size;
    bool   isDir;
};

class UsbHost {
public:
    void begin();
    void loop();

    bool isMounted() const { return _mounted; }
    bool isDeviceConnected() const { return _mounted; }
    String getDeviceInfo() const;

    // Verzeichnis-Listing vom USB-Stick
    bool listDirectory(const String& path, std::vector<FileEntry>& entries);

    // Text-Datei lesen (für Editor, max. EDITOR_MAX_SIZE)
    bool readTextFile(const String& path, String& content);

    // Text-Datei schreiben (vom Editor)
    bool writeTextFile(const String& path, const String& content);

    // Binär-Download (chunked, gibt Dateigröße zurück)
    size_t getFileSize(const String& path);
    bool   fileExists(const String& path);

    // Rohdaten-Lesen für Download-Streaming
    // Öffnet Datei und liest ab `offset` bis zu `len` Bytes in `buf`.
    // Gibt tatsächlich gelesene Bytes zurück, -1 bei Fehler.
    int readFileChunk(const String& path, size_t offset, uint8_t* buf, size_t len);

    // Datei schreiben (Upload, chunk-weise aufgerufen)
    bool writeFileChunk(const String& path, size_t offset,
                        const uint8_t* data, size_t len, bool isFinal);

    // Datei / Verzeichnis löschen
    bool deleteEntry(const String& path);

    // Verzeichnis anlegen
    bool createDirectory(const String& path);

    // Datei umbenennen / verschieben
    bool renameEntry(const String& from, const String& to);

private:
    volatile bool _mounted = false;
    char _vendor[16]  = {};
    char _product[32] = {};

    // Gibt absoluten Pfad auf dem USB-Stick zurück
    String absPath(const String& rel) const;

    // IDF USB-Host Daemon (läuft als FreeRTOS-Task)
    static void usbLibTask(void* arg);
    // MSC Class-Client Task
    static void mscClientTask(void* arg);
};

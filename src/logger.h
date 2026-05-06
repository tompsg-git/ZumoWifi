#pragma once
#include <Arduino.h>

#define LOG_MAX_LINES  300
#define LOG_LINE_MAX   160

class WebLogger {
public:
    void begin();

    // Zeile hinzufügen (auch nach Serial)
    void log(const char* level, const char* msg);
    void logf(const char* level, const char* fmt, ...);

    // Alle Zeilen als JSON-Array (ab fromIndex)
    String toJson(int fromIndex = 0) const;

    // Anzahl gespeicherter Zeilen
    int count() const { return _count; }

    // Globaler Index der letzten Zeile (monoton steigend)
    int lastIndex() const { return _totalWritten - 1; }

private:
    struct Line {
        char     text[LOG_LINE_MAX];
        uint32_t ms;
    };

    Line _buf[LOG_MAX_LINES];
    int  _head        = 0;  // nächste Schreibposition
    int  _count       = 0;  // belegte Einträge (max LOG_MAX_LINES)
    int  _totalWritten = 0; // Gesamtzahl je geschriebener Zeilen

    void write(const char* level, const char* text);

    // ESP-IDF log hook
    static int idfLogHook(const char* fmt, va_list args);
};

extern WebLogger logger;

// Convenience-Makros
#define LOGI(fmt, ...) logger.logf("I", fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) logger.logf("W", fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) logger.logf("E", fmt, ##__VA_ARGS__)

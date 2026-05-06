#include "logger.h"
#include <stdarg.h>
#include <stdio.h>
#include "esp_log.h"

WebLogger logger;

// ── ESP-IDF Log-Hook ─────────────────────────────────────────────────

int WebLogger::idfLogHook(const char* fmt, va_list args) {
    char buf[LOG_LINE_MAX];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);

    int len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';

    if (len > 0) logger.write("", buf);

    Serial.println(buf);
    return n;
}

void WebLogger::begin() {
    esp_log_set_vprintf(idfLogHook);
}

// ── Intern: Zeile in Ring-Buffer schreiben ────────────────────────────

void WebLogger::write(const char* level, const char* text) {
    Line& l = _buf[_head];
    l.ms = millis();
    if (level && level[0]) {
        snprintf(l.text, sizeof(l.text), "[%s] %s", level, text);
    } else {
        snprintf(l.text, sizeof(l.text), "%s", text);
    }
    _head = (_head + 1) % LOG_MAX_LINES;
    if (_count < LOG_MAX_LINES) _count++;
    _totalWritten++;
}

void WebLogger::log(const char* level, const char* msg) {
    Serial.println(msg);
    write(level, msg);
}

void WebLogger::logf(const char* level, const char* fmt, ...) {
    char buf[LOG_LINE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
    write(level, buf);
}

// ── JSON-Export ───────────────────────────────────────────────────────

String WebLogger::toJson(int fromIndex) const {
    String out = "{\"total\":";
    out += _totalWritten;
    out += ",\"lines\":[";

    // Ältesten verfügbaren Index berechnen
    int oldest = (_totalWritten > LOG_MAX_LINES)
                 ? (_totalWritten - LOG_MAX_LINES)
                 : 0;

    // fromIndex auf gültigen Bereich klemmen
    if (fromIndex < oldest) fromIndex = oldest;

    bool first = true;
    for (int i = fromIndex; i < _totalWritten; i++) {
        // Puffer-Position des globalen Index i
        int pos = i % LOG_MAX_LINES;
        const Line& l = _buf[pos];

        if (!first) out += ',';
        first = false;

        out += "{\"i\":";
        out += i;
        out += ",\"ms\":";
        out += l.ms;
        out += ",\"t\":\"";
        // Text HTML-escapen
        for (const char* p = l.text; *p; p++) {
            if (*p == '"')       out += "\\\"";
            else if (*p == '\\') out += "\\\\";
            else                 out += *p;
        }
        out += "\"}";
    }
    out += "]}";
    return out;
}

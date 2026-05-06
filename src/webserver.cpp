#include "webserver.h"
#include "wifi_manager.h"
#include <WiFi.h>

extern WifiManager wifiManager;

void Webserver::begin(UsbHost* usb) {
    _usb = usb;
    setupRoutes();
    _server.begin();
    Serial.printf("[WEB] Server gestartet auf Port %d\n", WEB_PORT);
}

void Webserver::setupRoutes() {
    // Web-Interface aus LittleFS
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Status
    _server.on("/api/status", HTTP_GET,
        [this](AsyncWebServerRequest* r) { handleStatus(r); });

    // Datei-Browser
    _server.on("/api/files", HTTP_GET,
        [this](AsyncWebServerRequest* r) { handleListFiles(r); });

    _server.on("/api/download", HTTP_GET,
        [this](AsyncWebServerRequest* r) { handleDownloadFile(r); });

    _server.on("/api/delete", HTTP_POST,
        [this](AsyncWebServerRequest* r) { handleDeleteFile(r); });

    _server.on("/api/rename", HTTP_POST,
        [this](AsyncWebServerRequest* r) { handleRenameEntry(r); });

    _server.on("/api/mkdir", HTTP_POST,
        [this](AsyncWebServerRequest* r) { handleCreateDir(r); });

    _server.on("/api/upload", HTTP_POST,
        [this](AsyncWebServerRequest* r) { handleUploadStart(r); },
        [this](AsyncWebServerRequest* r, const String& fn,
               size_t idx, uint8_t* data, size_t len, bool fin) {
            handleUploadData(r, fn, idx, data, len, fin);
        });

    // Text-Editor
    _server.on("/api/file/read", HTTP_GET,
        [this](AsyncWebServerRequest* r) { handleReadFile(r); });

    _server.on("/api/file/write", HTTP_POST,
        [this](AsyncWebServerRequest* r) {},
        nullptr,
        [this](AsyncWebServerRequest* r, uint8_t* d,
               size_t len, size_t idx, size_t total) {
            handleWriteFile(r, d, len, idx, total);
        });

    // WiFi
    _server.on("/api/wifi/scan", HTTP_GET,
        [this](AsyncWebServerRequest* r) { handleWifiScan(r); });

    _server.on("/api/wifi/connect", HTTP_POST,
        [this](AsyncWebServerRequest* r) { handleWifiConnect(r); });

    _server.onNotFound([](AsyncWebServerRequest* r) {
        r->send(404, "application/json", "{\"error\":\"Nicht gefunden\"}");
    });
}

// ── Status ───────────────────────────────────────────────────────────

void Webserver::handleStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["device"]    = _usb->isDeviceConnected()
                       ? _usb->getDeviceInfo()
                       : "disconnected";
    doc["wifi_mode"] = wifiManager.getMode();
    doc["wifi_ssid"] = wifiManager.getSSID();
    doc["wifi_ip"]   = wifiManager.getIP();
    doc["heap_free"] = ESP.getFreeHeap();
    doc["uptime"]    = millis() / 1000;

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

// ── Datei-Listing ────────────────────────────────────────────────────

void Webserver::handleListFiles(AsyncWebServerRequest* req) {
    String path = req->hasParam("path") ? req->getParam("path")->value() : "/";

    std::vector<FileEntry> entries;
    if (!_usb->listDirectory(path, entries)) {
        req->send(503, "application/json",
                  "{\"error\":\"Stick nicht verbunden oder Lesefehler\"}");
        return;
    }

    JsonDocument doc;
    doc["path"] = path;
    JsonArray files = doc["files"].to<JsonArray>();

    for (auto& e : entries) {
        JsonObject obj = files.add<JsonObject>();
        obj["name"]  = e.name;
        obj["size"]  = e.size;
        obj["isDir"] = e.isDir;
    }

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

// ── Download (chunked) ────────────────────────────────────────────────

void Webserver::handleDownloadFile(AsyncWebServerRequest* req) {
    if (!req->hasParam("path")) {
        req->send(400, "application/json", "{\"error\":\"path fehlt\"}");
        return;
    }

    String path = req->getParam("path")->value();
    size_t fileSize = _usb->getFileSize(path);

    if (fileSize == 0 && !_usb->fileExists(path)) {
        req->send(404, "application/json", "{\"error\":\"Datei nicht gefunden\"}");
        return;
    }

    AsyncWebServerResponse* response = req->beginChunkedResponse(
        "application/octet-stream",
        [this, path, fileSize](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
            if (index >= fileSize) return 0;
            int n = _usb->readFileChunk(path, index, buf, maxLen);
            return (n > 0) ? (size_t)n : 0;
        });

    int lastSlash = path.lastIndexOf('/');
    String fn = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
    response->addHeader("Content-Disposition",
                        "attachment; filename=\"" + fn + "\"");
    req->send(response);
}

// ── Löschen ───────────────────────────────────────────────────────────

void Webserver::handleDeleteFile(AsyncWebServerRequest* req) {
    if (!req->hasParam("path", true)) {
        req->send(400, "application/json", "{\"error\":\"path fehlt\"}");
        return;
    }
    String path = req->getParam("path", true)->value();
    bool ok = _usb->deleteEntry(path);
    req->send(ok ? 200 : 500, "application/json",
              ok ? "{\"ok\":true}" : "{\"error\":\"Löschen fehlgeschlagen\"}");
}

// ── Umbenennen ────────────────────────────────────────────────────────

void Webserver::handleRenameEntry(AsyncWebServerRequest* req) {
    if (!req->hasParam("from", true) || !req->hasParam("to", true)) {
        req->send(400, "application/json", "{\"error\":\"from/to fehlt\"}");
        return;
    }
    String from = req->getParam("from", true)->value();
    String to   = req->getParam("to",   true)->value();

    if (_usb->renameEntry(from, to)) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"Umbenennen fehlgeschlagen\"}");
    }
}

// ── Verzeichnis anlegen ───────────────────────────────────────────────

void Webserver::handleCreateDir(AsyncWebServerRequest* req) {
    if (!req->hasParam("path", true)) {
        req->send(400, "application/json", "{\"error\":\"path fehlt\"}");
        return;
    }
    String path = req->getParam("path", true)->value();
    if (_usb->createDirectory(path)) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"Ordner anlegen fehlgeschlagen\"}");
    }
}

// ── Upload ────────────────────────────────────────────────────────────

void Webserver::handleUploadStart(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
}

void Webserver::handleUploadData(AsyncWebServerRequest* req, const String& filename,
                                  size_t index, uint8_t* data, size_t len, bool final) {
    String dir = req->hasParam("path", true)
                 ? req->getParam("path", true)->value()
                 : "/";
    String fullPath = (dir == "/") ? "/" + filename : dir + "/" + filename;

    if (!_usb->writeFileChunk(fullPath, index, data, len, final)) {
        Serial.printf("[WEB] Upload-Fehler bei '%s'\n", fullPath.c_str());
    }

    if (final) {
        Serial.printf("[WEB] Upload abgeschlossen: %s (%u Bytes)\n",
                      fullPath.c_str(), (unsigned)(index + len));
    }
}

// ── Text-Editor: Datei lesen ──────────────────────────────────────────

void Webserver::handleReadFile(AsyncWebServerRequest* req) {
    if (!req->hasParam("path")) {
        req->send(400, "application/json", "{\"error\":\"path fehlt\"}");
        return;
    }
    String path = req->getParam("path")->value();

    String content;
    if (!_usb->readTextFile(path, content)) {
        req->send(500, "application/json",
                  "{\"error\":\"Lesen fehlgeschlagen oder Datei zu groß (>64KB)\"}");
        return;
    }

    // Roher Text als Antwort – JavaScript nutzt dies direkt im Editor
    req->send(200, "text/plain; charset=utf-8", content);
}

// ── Text-Editor: Datei schreiben ──────────────────────────────────────

void Webserver::handleWriteFile(AsyncWebServerRequest* req,
                                 uint8_t* data, size_t len,
                                 size_t index, size_t total) {
    // Body akkumulieren (max. EDITOR_MAX_SIZE)
    static String s_path;
    static String s_body;

    if (index == 0) {
        s_body = "";
        s_path = req->hasParam("path")
                 ? req->getParam("path")->value()
                 : "";
        s_body.reserve(total);
    }

    s_body.concat(reinterpret_cast<const char*>(data), len);

    if (index + len == total) {
        if (s_path.isEmpty()) {
            req->send(400, "application/json", "{\"error\":\"path fehlt\"}");
            return;
        }
        if (_usb->writeTextFile(s_path, s_body)) {
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(500, "application/json", "{\"error\":\"Schreiben fehlgeschlagen\"}");
        }
    }
}

// ── WiFi-Scan ─────────────────────────────────────────────────────────

void Webserver::handleWifiScan(AsyncWebServerRequest* req) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
        req->send(202, "application/json", "{\"scanning\":true}");
        return;
    }

    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
        JsonObject net = networks.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

// ── WiFi-Connect ──────────────────────────────────────────────────────

void Webserver::handleWifiConnect(AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid", true) || !req->hasParam("password", true)) {
        req->send(400, "application/json", "{\"error\":\"ssid/password fehlt\"}");
        return;
    }
    String ssid = req->getParam("ssid",     true)->value();
    String pass = req->getParam("password", true)->value();

    req->send(200, "application/json",
              "{\"ok\":true,\"message\":\"Verbinde ...\"}");

    wifiManager.connectToNetwork(ssid.c_str(), pass.c_str());
}

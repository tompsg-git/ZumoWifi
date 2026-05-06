#include "webserver.h"
#include "wifi_manager.h"
#include "logger.h"
#include <WiFi.h>

extern WifiManager wifiManager;

void Webserver::begin(UsbHost* usb) {
    _usb = usb;
    setupRoutes();
    _server.begin();
    LOGI("[WEB] Server auf Port %d gestartet", WEB_PORT);
}

void Webserver::setupRoutes() {
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    _server.on("/api/status",     HTTP_GET,  [this](AsyncWebServerRequest* r){ handleStatus(r); });
    _server.on("/api/files",      HTTP_GET,  [this](AsyncWebServerRequest* r){ handleListFiles(r); });
    _server.on("/api/download",   HTTP_GET,  [this](AsyncWebServerRequest* r){ handleDownloadFile(r); });
    _server.on("/api/delete",     HTTP_POST, [this](AsyncWebServerRequest* r){ handleDeleteFile(r); });
    _server.on("/api/rename",     HTTP_POST, [this](AsyncWebServerRequest* r){ handleRenameEntry(r); });
    _server.on("/api/mkdir",      HTTP_POST, [this](AsyncWebServerRequest* r){ handleCreateDir(r); });
    _server.on("/api/usb/status", HTTP_GET,  [this](AsyncWebServerRequest* r){ handleUsbStatus(r); });
    _server.on("/api/log",        HTTP_GET,  [this](AsyncWebServerRequest* r){ handleLog(r); });
    _server.on("/api/wifi/scan",  HTTP_GET,  [this](AsyncWebServerRequest* r){ handleWifiScan(r); });
    _server.on("/api/wifi/connect", HTTP_POST, [this](AsyncWebServerRequest* r){ handleWifiConnect(r); });

    _server.on("/api/upload", HTTP_POST,
        [this](AsyncWebServerRequest* r){ handleUploadStart(r); },
        [this](AsyncWebServerRequest* r, const String& fn,
               size_t idx, uint8_t* d, size_t len, bool fin){
            handleUploadData(r, fn, idx, d, len, fin);
        });

    _server.on("/api/file/read",  HTTP_GET,
        [this](AsyncWebServerRequest* r){ handleReadFile(r); });

    _server.on("/api/file/write", HTTP_POST,
        [this](AsyncWebServerRequest* r){},
        nullptr,
        [this](AsyncWebServerRequest* r, uint8_t* d, size_t len, size_t idx, size_t total){
            handleWriteFile(r, d, len, idx, total);
        });

    _server.onNotFound([](AsyncWebServerRequest* r){
        r->send(404, "application/json", "{\"error\":\"Nicht gefunden\"}");
    });
}

// ── Status ────────────────────────────────────────────────────────────

void Webserver::handleStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["device"]    = _usb->isDeviceConnected() ? _usb->getDeviceInfo() : "disconnected";
    doc["wifi_mode"] = wifiManager.getMode();
    doc["wifi_ssid"] = wifiManager.getSSID();
    doc["wifi_ip"]   = wifiManager.getIP();
    doc["heap_free"] = ESP.getFreeHeap();
    doc["uptime"]    = millis() / 1000;
    String j; serializeJson(doc, j);
    req->send(200, "application/json", j);
}

// ── USB Status + Disk Space ───────────────────────────────────────────

void Webserver::handleUsbStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["mounted"] = _usb->isMounted();
    doc["device"]  = _usb->isDeviceConnected() ? _usb->getDeviceInfo() : "";

    DiskInfo disk = _usb->getDiskInfo();
    if (disk.valid) {
        doc["disk_total"] = disk.totalBytes;
        doc["disk_free"]  = disk.freeBytes;
        doc["disk_used"]  = disk.totalBytes - disk.freeBytes;
    }
    String j; serializeJson(doc, j);
    req->send(200, "application/json", j);
}

// ── Log-Viewer ────────────────────────────────────────────────────────

void Webserver::handleLog(AsyncWebServerRequest* req) {
    // ?from=N → nur Zeilen ab globalem Index N liefern (für Polling)
    int fromIndex = 0;
    if (req->hasParam("from")) {
        fromIndex = req->getParam("from")->value().toInt();
    }
    String json = logger.toJson(fromIndex);
    req->send(200, "application/json", json);
}

// ── Datei-Browser ────────────────────────────────────────────────────

void Webserver::handleListFiles(AsyncWebServerRequest* req) {
    String path = req->hasParam("path") ? req->getParam("path")->value() : "/";
    std::vector<FileEntry> entries;
    if (!_usb->listDirectory(path, entries)) {
        req->send(503, "application/json", "{\"error\":\"Stick nicht verbunden\"}");
        return;
    }
    JsonDocument doc;
    doc["path"] = path;
    JsonArray files = doc["files"].to<JsonArray>();
    for (auto& e : entries) {
        JsonObject o = files.add<JsonObject>();
        o["name"]  = e.name;
        o["size"]  = e.size;
        o["isDir"] = e.isDir;
    }
    String j; serializeJson(doc, j);
    req->send(200, "application/json", j);
}

// ── Download ──────────────────────────────────────────────────────────

void Webserver::handleDownloadFile(AsyncWebServerRequest* req) {
    if (!req->hasParam("path")) {
        req->send(400, "application/json", "{\"error\":\"path fehlt\"}");
        return;
    }
    String path = req->getParam("path")->value();
    size_t fileSize = _usb->getFileSize(path);
    if (!fileSize && !_usb->fileExists(path)) {
        req->send(404, "application/json", "{\"error\":\"nicht gefunden\"}");
        return;
    }
    auto* response = req->beginChunkedResponse(
        "application/octet-stream",
        [this, path, fileSize](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
            if (index >= fileSize) return 0;
            int n = _usb->readFileChunk(path, index, buf, maxLen);
            return n > 0 ? (size_t)n : 0;
        });
    int sl = path.lastIndexOf('/');
    String fn = (sl >= 0) ? path.substring(sl + 1) : path;
    response->addHeader("Content-Disposition", "attachment; filename=\"" + fn + "\"");
    req->send(response);
}

// ── Löschen ───────────────────────────────────────────────────────────

void Webserver::handleDeleteFile(AsyncWebServerRequest* req) {
    if (!req->hasParam("path", true)) {
        req->send(400, "application/json", "{\"error\":\"path fehlt\"}"); return;
    }
    String path = req->getParam("path", true)->value();
    bool ok = _usb->deleteEntry(path);
    req->send(ok ? 200 : 500, "application/json",
              ok ? "{\"ok\":true}" : "{\"error\":\"Löschen fehlgeschlagen\"}");
}

// ── Umbenennen ────────────────────────────────────────────────────────

void Webserver::handleRenameEntry(AsyncWebServerRequest* req) {
    if (!req->hasParam("from", true) || !req->hasParam("to", true)) {
        req->send(400, "application/json", "{\"error\":\"from/to fehlt\"}"); return;
    }
    bool ok = _usb->renameEntry(req->getParam("from", true)->value(),
                                req->getParam("to",   true)->value());
    req->send(ok ? 200 : 500, "application/json",
              ok ? "{\"ok\":true}" : "{\"error\":\"Umbenennen fehlgeschlagen\"}");
}

// ── Verzeichnis ───────────────────────────────────────────────────────

void Webserver::handleCreateDir(AsyncWebServerRequest* req) {
    if (!req->hasParam("path", true)) {
        req->send(400, "application/json", "{\"error\":\"path fehlt\"}"); return;
    }
    bool ok = _usb->createDirectory(req->getParam("path", true)->value());
    req->send(ok ? 200 : 500, "application/json",
              ok ? "{\"ok\":true}" : "{\"error\":\"Ordner anlegen fehlgeschlagen\"}");
}

// ── Upload ────────────────────────────────────────────────────────────

void Webserver::handleUploadStart(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
}

void Webserver::handleUploadData(AsyncWebServerRequest* req, const String& filename,
                                  size_t index, uint8_t* data, size_t len, bool final) {
    String dir = req->hasParam("path", true) ? req->getParam("path", true)->value() : "/";
    String fp  = (dir == "/") ? "/" + filename : dir + "/" + filename;
    _usb->writeFileChunk(fp, index, data, len, final);
    if (final) LOGI("[WEB] Upload: %s (%u B)", fp.c_str(), (unsigned)(index + len));
}

// ── Text-Editor ───────────────────────────────────────────────────────

void Webserver::handleReadFile(AsyncWebServerRequest* req) {
    if (!req->hasParam("path")) {
        req->send(400, "application/json", "{\"error\":\"path fehlt\"}"); return;
    }
    String content;
    if (!_usb->readTextFile(req->getParam("path")->value(), content)) {
        req->send(500, "application/json", "{\"error\":\"Lesen fehlgeschlagen oder >64KB\"}");
        return;
    }
    req->send(200, "text/plain; charset=utf-8", content);
}

void Webserver::handleWriteFile(AsyncWebServerRequest* req,
                                 uint8_t* data, size_t len,
                                 size_t index, size_t total) {
    static String s_path, s_body;
    if (index == 0) {
        s_body = "";
        s_path = req->hasParam("path") ? req->getParam("path")->value() : "";
        s_body.reserve(total);
    }
    s_body.concat(reinterpret_cast<const char*>(data), len);
    if (index + len == total) {
        if (s_path.isEmpty()) { req->send(400, "application/json", "{\"error\":\"path fehlt\"}"); return; }
        bool ok = _usb->writeTextFile(s_path, s_body);
        req->send(ok ? 200 : 500, "application/json",
                  ok ? "{\"ok\":true}" : "{\"error\":\"Schreiben fehlgeschlagen\"}");
    }
}

// ── WiFi ──────────────────────────────────────────────────────────────

void Webserver::handleWifiScan(AsyncWebServerRequest* req) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
        req->send(202, "application/json", "{\"scanning\":true}");
        return;
    }
    JsonDocument doc;
    JsonArray nets = doc["networks"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
        JsonObject o = nets.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    String j; serializeJson(doc, j);
    req->send(200, "application/json", j);
}

void Webserver::handleWifiConnect(AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid", true) || !req->hasParam("password", true)) {
        req->send(400, "application/json", "{\"error\":\"ssid/password fehlt\"}"); return;
    }
    String ssid = req->getParam("ssid",     true)->value();
    String pass = req->getParam("password", true)->value();
    req->send(200, "application/json", "{\"ok\":true}");
    wifiManager.connectToNetwork(ssid.c_str(), pass.c_str());
}

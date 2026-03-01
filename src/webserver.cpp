#include "webserver.h"

extern WifiManager wifiManager;  // from main.cpp

void Webserver::begin(UsbHost* usb) {
    _usb = usb;
    setupRoutes();
    _server.begin();
    Serial.printf("[WEB] Server started on port %d\n", WEB_PORT);
}

void Webserver::setupRoutes() {
    // Serve web interface from LittleFS
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // API endpoints
    _server.on("/api/status", HTTP_GET,
        [this](AsyncWebServerRequest* req) { handleStatus(req); });

    _server.on("/api/files", HTTP_GET,
        [this](AsyncWebServerRequest* req) { handleListFiles(req); });

    _server.on("/api/download", HTTP_GET,
        [this](AsyncWebServerRequest* req) { handleDownloadFile(req); });

    _server.on("/api/delete", HTTP_POST,
        [this](AsyncWebServerRequest* req) { handleDeleteFile(req); });

    _server.on("/api/mkdir", HTTP_POST,
        [this](AsyncWebServerRequest* req) { handleCreateDir(req); });

    _server.on("/api/upload", HTTP_POST,
        [this](AsyncWebServerRequest* req) { handleUploadStart(req); },
        [this](AsyncWebServerRequest* req, const String& filename,
               size_t index, uint8_t* data, size_t len, bool final) {
            handleUploadData(req, filename, index, data, len, final);
        });

    _server.on("/api/wifi/scan", HTTP_GET,
        [this](AsyncWebServerRequest* req) { handleWifiScan(req); });

    _server.on("/api/wifi/connect", HTTP_POST,
        [this](AsyncWebServerRequest* req) { handleWifiConnect(req); });

    // 404
    _server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"error\":\"Not found\"}");
    });
}

// ── API Handlers ────────────────────────────────────────────────────

void Webserver::handleStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["device"]    = _usb->isDeviceConnected() ? _usb->getDeviceInfo() : "disconnected";
    doc["wifi_mode"] = wifiManager.getMode();
    doc["wifi_ssid"] = wifiManager.getSSID();
    doc["wifi_ip"]   = wifiManager.getIP();
    doc["heap_free"] = ESP.getFreeHeap();
    doc["uptime"]    = millis() / 1000;

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

void Webserver::handleListFiles(AsyncWebServerRequest* req) {
    String path = req->hasParam("path") ? req->getParam("path")->value() : "/";

    std::vector<FileEntry> entries;
    if (!_usb->listDirectory(path, entries)) {
        req->send(503, "application/json", "{\"error\":\"Device not connected or read error\"}");
        return;
    }

    JsonDocument doc;
    doc["path"] = path;
    JsonArray files = doc["files"].to<JsonArray>();

    for (auto& entry : entries) {
        JsonObject obj = files.add<JsonObject>();
        obj["name"]  = entry.name;
        obj["size"]  = entry.size;
        obj["isDir"] = entry.isDir;
    }

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

void Webserver::handleDownloadFile(AsyncWebServerRequest* req) {
    if (!req->hasParam("path")) {
        req->send(400, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
    }

    String path = req->getParam("path")->value();
    size_t fileSize = _usb->getFileSize(path);

    if (fileSize == 0) {
        req->send(404, "application/json", "{\"error\":\"File not found\"}");
        return;
    }

    // Stream file from Garmin device
    // For large files, use chunked response
    AsyncWebServerResponse* response = req->beginChunkedResponse(
        "application/octet-stream",
        [this, path, fileSize](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
            if (index >= fileSize) return 0;

            size_t toRead = min(maxLen, fileSize - index);
            size_t bytesRead = toRead;

            if (!_usb->readFile(path, buffer, bytesRead)) {
                return 0;
            }
            return bytesRead;
        });

    // Extract filename from path
    int lastSlash = path.lastIndexOf('/');
    String filename = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
    response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");

    req->send(response);
}

void Webserver::handleDeleteFile(AsyncWebServerRequest* req) {
    if (!req->hasParam("path", true)) {
        req->send(400, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
    }

    String path = req->getParam("path", true)->value();

    if (_usb->deleteFile(path)) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    }
}

void Webserver::handleCreateDir(AsyncWebServerRequest* req) {
    if (!req->hasParam("path", true)) {
        req->send(400, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
    }

    String path = req->getParam("path", true)->value();

    if (_usb->createDirectory(path)) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"Create directory failed\"}");
    }
}

void Webserver::handleUploadStart(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
}

void Webserver::handleUploadData(AsyncWebServerRequest* req, const String& filename,
                                  size_t index, uint8_t* data, size_t len, bool final) {
    String targetDir = req->hasParam("path", true) ? req->getParam("path", true)->value() : "/";
    String fullPath = targetDir + "/" + filename;

    if (index == 0) {
        Serial.printf("[WEB] Upload start: %s\n", fullPath.c_str());
    }

    // Write chunk to Garmin device
    _usb->writeFile(fullPath, data, len);

    if (final) {
        Serial.printf("[WEB] Upload complete: %s (%d bytes)\n", fullPath.c_str(), index + len);
    }
}

void Webserver::handleWifiScan(AsyncWebServerRequest* req) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);  // async scan
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

void Webserver::handleWifiConnect(AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid", true) || !req->hasParam("password", true)) {
        req->send(400, "application/json", "{\"error\":\"Missing ssid or password\"}");
        return;
    }

    String ssid = req->getParam("ssid", true)->value();
    String pass = req->getParam("password", true)->value();

    // Respond immediately, connect in background
    req->send(200, "application/json", "{\"ok\":true,\"message\":\"Connecting...\"}");

    wifiManager.connectToNetwork(ssid.c_str(), pass.c_str());
}

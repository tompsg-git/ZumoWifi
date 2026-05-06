#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "usb_host.h"

class Webserver {
public:
    void begin(UsbHost* usb);

private:
    AsyncWebServer _server{WEB_PORT};
    UsbHost* _usb = nullptr;

    void setupRoutes();

    void handleStatus(AsyncWebServerRequest* req);
    void handleListFiles(AsyncWebServerRequest* req);
    void handleDownloadFile(AsyncWebServerRequest* req);
    void handleDeleteFile(AsyncWebServerRequest* req);
    void handleRenameEntry(AsyncWebServerRequest* req);
    void handleCreateDir(AsyncWebServerRequest* req);
    void handleUploadStart(AsyncWebServerRequest* req);
    void handleUploadData(AsyncWebServerRequest* req, const String& filename,
                          size_t index, uint8_t* data, size_t len, bool final);

    void handleReadFile(AsyncWebServerRequest* req);
    void handleWriteFile(AsyncWebServerRequest* req,
                         uint8_t* data, size_t len, size_t index, size_t total);

    void handleUsbStatus(AsyncWebServerRequest* req);
    void handleLog(AsyncWebServerRequest* req);

    void handleWifiScan(AsyncWebServerRequest* req);
    void handleWifiConnect(AsyncWebServerRequest* req);
};

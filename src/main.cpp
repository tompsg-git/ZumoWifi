#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "logger.h"
#include "wifi_manager.h"
#include "usb_host.h"
#include "webserver.h"

WifiManager wifiManager;
UsbHost     usbHost;
Webserver   webserver;

void setup() {
    Serial.begin(115200);
    delay(1500);  // Längere Wartezeit – Spannungsrampe bei Pin-Versorgung

    // Logger zuerst – ab jetzt landen alle LOGI/LOGW/LOGE im Web-Log
    logger.begin();

    LOGI("============================");
    LOGI("  ZumoWifi  –  ESP32-S2");
    LOGI("============================");

    if (!LittleFS.begin(true)) {
        LOGE("[FS] LittleFS Mount fehlgeschlagen!");
    } else {
        LOGI("[FS] LittleFS bereit");
    }

    wifiManager.begin();
    usbHost.begin();
    webserver.begin(&usbHost);

    LOGI("[BEREIT] http://%s", WiFi.softAPIP().toString().c_str());
}

void loop() {
    wifiManager.loop();
    usbHost.loop();
    delay(10);
}

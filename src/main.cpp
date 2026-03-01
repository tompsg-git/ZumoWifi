#include <Arduino.h>
#include "config.h"
#include "wifi_manager.h"
#include "usb_host.h"
#include "webserver.h"

WifiManager wifiManager;
UsbHost     usbHost;
Webserver   webserver;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=============================");
    Serial.println("  ZumoWifi – Garmin Gateway");
    Serial.println("=============================");
    Serial.println();

    // Initialize LittleFS for web interface files
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mount failed");
    } else {
        Serial.println("[FS] LittleFS mounted");
    }

    // Start WiFi (AP mode by default, can connect to existing network)
    wifiManager.begin();

    // Initialize USB Host for Garmin Zumo
    usbHost.begin();

    // Start web server
    webserver.begin(&usbHost);

    Serial.println();
    Serial.println("[READY] ZumoWifi is running");
    Serial.printf("[READY] Web interface: http://%s\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
    wifiManager.loop();
    usbHost.loop();
    delay(10);
}

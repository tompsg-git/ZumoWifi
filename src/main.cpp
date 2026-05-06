#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "wifi_manager.h"
#include "usb_host.h"
#include "webserver.h"

WifiManager wifiManager;
UsbHost     usbHost;
Webserver   webserver;

void setup() {
    // Debug-Serial via UART0 (GPIO 1 TX / GPIO 3 RX).
    // USB-Port ist im OTG-Host-Modus – KEIN USB-CDC.
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("============================");
    Serial.println("  ZumoWifi  –  ESP32-S2");
    Serial.println("============================");

    // Web-Interface aus LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS Mount fehlgeschlagen!");
    } else {
        Serial.println("[FS] LittleFS bereit");
    }

    // WiFi – AP-Modus als Standard
    wifiManager.begin();

    // USB Host für USB-Stick (Port im OTG-Host-Modus nach Boot)
    usbHost.begin();

    // Webserver
    webserver.begin(&usbHost);

    Serial.println("[BEREIT] ZumoWifi läuft");
    Serial.printf("[BEREIT] Web: http://%s\n",
                  WiFi.softAPIP().toString().c_str());
}

void loop() {
    wifiManager.loop();
    usbHost.loop();
    delay(10);
}

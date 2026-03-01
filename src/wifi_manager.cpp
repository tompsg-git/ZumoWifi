#include "wifi_manager.h"

void WifiManager::begin() {
    startAP();
}

void WifiManager::startAP() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    _apMode = true;

    Serial.printf("[WiFi] AP started: %s\n", AP_SSID);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

bool WifiManager::connectToNetwork(const char* ssid, const char* password) {
    Serial.printf("[WiFi] Connecting to %s ...\n", ssid);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }

    Serial.println("[WiFi] Connection failed");
    return false;
}

bool WifiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String WifiManager::getIP() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return WiFi.softAPIP().toString();
}

String WifiManager::getSSID() {
    if (isConnected()) {
        return WiFi.SSID();
    }
    return AP_SSID;
}

String WifiManager::getMode() {
    return isConnected() ? "STA" : "AP";
}

void WifiManager::loop() {
    if (millis() - _lastCheck < 30000) return;
    _lastCheck = millis();

    if (!_apMode) {
        if (!isConnected()) {
            Serial.println("[WiFi] Connection lost, restarting AP");
            startAP();
        }
    }
}

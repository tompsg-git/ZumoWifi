#include "wifi_manager.h"

void WifiManager::begin() {
    startAP();
}

void WifiManager::startAP() {
    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_AP);

    // Stabiler AP: Channel 6, max 4 Clients, SSID sichtbar
    bool ok = (AP_PASSWORD[0] == '\0')
              ? WiFi.softAP(AP_SSID, nullptr, 6, 0, 4)
              : WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 4);

    if (!ok) {
        Serial.println("[WiFi] softAP fehlgeschlagen – nochmal ...");
        delay(500);
        WiFi.softAP(AP_SSID, AP_PASSWORD[0] ? AP_PASSWORD : nullptr, 6, 0, 4);
    }

    // Sendeleistung maximieren (hilft bei instabiler Verbindung)
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // Power-Save deaktivieren
    esp_wifi_set_ps(WIFI_PS_NONE);

    _apMode = true;
    Serial.printf("[WiFi] AP: %s  IP: %s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
}

bool WifiManager::connectToNetwork(const char* ssid, const char* password) {
    Serial.printf("[WiFi] Verbinde mit %s ...\n", ssid);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        _apMode = false;
        Serial.printf("[WiFi] Verbunden! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }

    Serial.println("[WiFi] Verbindung fehlgeschlagen");
    return false;
}

bool WifiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String WifiManager::getIP() {
    return isConnected() ? WiFi.localIP().toString()
                         : WiFi.softAPIP().toString();
}

String WifiManager::getSSID() {
    return isConnected() ? WiFi.SSID() : String(AP_SSID);
}

String WifiManager::getMode() {
    return isConnected() ? "STA" : "AP";
}

void WifiManager::loop() {
    if (millis() - _lastCheck < 30000) return;
    _lastCheck = millis();

    if (!_apMode && !isConnected()) {
        Serial.println("[WiFi] Verbindung verloren – AP neu starten");
        startAP();
    }
}

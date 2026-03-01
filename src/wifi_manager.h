#pragma once

#include <WiFi.h>
#include "config.h"

class WifiManager {
public:
    void begin();
    void loop();

    bool connectToNetwork(const char* ssid, const char* password);
    void startAP();
    bool isConnected();

    String getIP();
    String getSSID();
    String getMode();

private:
    bool _apMode = true;
    unsigned long _lastCheck = 0;
};

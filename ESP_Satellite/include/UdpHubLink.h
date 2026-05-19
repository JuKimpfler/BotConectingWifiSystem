#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "messages.h"
#include "sat_config.h"

class UdpHubLink {
public:
    bool begin();
    void tick();
    bool sendFrame(const Frame_t *frame);
    bool readFrame(Frame_t *outFrame);
    bool isWifiConnected() const { return WiFi.status() == WL_CONNECTED; }
    IPAddress localIp() const { return WiFi.localIP(); }
    IPAddress hubIp() const { return _hubIp; }

private:
    WiFiUDP   _udp;
    IPAddress _hubIp;
    uint32_t  _lastReconnectAttempt = 0;
    bool      _udpBegun = false;
};

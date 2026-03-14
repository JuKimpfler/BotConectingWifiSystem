#pragma once
// ============================================================
//  ESP_Satellite/include/EspNowBridge.h
//  Fast P2P bridge between SAT1 and SAT2 (existing core feature)
//  Also handles hub command link
// ============================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "messages.h"
#include "sat_config.h"

typedef void (*SatRecvCb)(const uint8_t *mac, const Frame_t *frame);

class EspNowBridge {
public:
    bool begin(uint8_t channel);
    bool addPeer(const uint8_t *mac, bool encrypt = false, const char *ltk16 = nullptr);
    bool send(const uint8_t *mac, const Frame_t *frame);

    void setRecvCallback(SatRecvCb cb) { _recvCb = cb; }

    static EspNowBridge &instance();

private:
    EspNowBridge() = default;
    static void _onSent(const uint8_t *mac, esp_now_send_status_t s);
    static void _onRecv(const uint8_t *mac, const uint8_t *data, int len);

    SatRecvCb _recvCb = nullptr;
    uint8_t   _channel = DEFAULT_CHANNEL;
};

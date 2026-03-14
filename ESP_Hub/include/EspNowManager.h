#pragma once
// ============================================================
//  ESP_Hub/include/EspNowManager.h
//  Initialises WiFi AP + ESP-NOW, sets PMK, fires send/recv
// ============================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "messages.h"
#include "hub_config.h"

// Callback type for received frames
typedef void (*FrameRecvCb)(const uint8_t *mac, const Frame_t *frame);

class EspNowManager {
public:
    bool begin(uint8_t channel, const char *pmk16 = nullptr);
    bool addPeer(const uint8_t *mac, const char *ltk16 = nullptr);
    bool removePeer(const uint8_t *mac);
    bool send(const uint8_t *mac, const Frame_t *frame);

    void setRecvCallback(FrameRecvCb cb) { _recvCb = cb; }

    static EspNowManager &instance();

private:
    EspNowManager() = default;
    static void _onSent(const uint8_t *mac, esp_now_send_status_t s);
    static void _onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);

    FrameRecvCb _recvCb = nullptr;
    uint8_t     _channel = DEFAULT_CHANNEL;
};

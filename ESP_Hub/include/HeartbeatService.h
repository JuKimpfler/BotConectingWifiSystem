#pragma once
// ============================================================
//  ESP_Hub/include/HeartbeatService.h
//  Sends heartbeat frames to all online peers, tracks timeouts
// ============================================================

#include <Arduino.h>
#include "PeerRegistry.h"
#include "EspNowManager.h"

class HeartbeatService {
public:
    void begin(uint32_t intervalMs, uint32_t timeoutMs);
    void tick(PeerRegistry &peers, EspNowManager &espnow);

private:
    uint32_t _intervalMs = 1000;
    uint32_t _timeoutMs  = 4000;
    uint32_t _lastSent   = 0;
    uint8_t  _seq        = 0;
};

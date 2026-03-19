// ============================================================
//  ESP_Hub/src/HeartbeatService.cpp
// ============================================================

#include "HeartbeatService.h"
#include "messages.h"
#include "crc16.h"
#include "hub_config.h"

void HeartbeatService::begin(uint32_t intervalMs, uint32_t timeoutMs) {
    _intervalMs = intervalMs;
    _timeoutMs  = timeoutMs;
}

void HeartbeatService::tick(PeerRegistry &peers, EspNowManager &espnow) {
    uint32_t now = millis();

    // Check timeouts
    peers.tickTimeouts(_timeoutMs);

    // Send heartbeats
    if ((now - _lastSent) < _intervalMs) return;
    _lastSent = now;

    HeartbeatPayload_t hb;
    hb.uptime_ms = now;
    hb.rssi      = 0;
    hb.queue_len = 0;

    Frame_t frame = {};
    frame.magic      = FRAME_MAGIC;
    frame.msg_type   = MSG_HEARTBEAT;
    frame.seq        = _seq++;
    frame.src_role   = ROLE_HUB;
    frame.dst_role   = ROLE_BROADCAST;
    frame.flags      = 0;
    frame.network_id = HUB_NETWORK_ID;
    frame.len        = sizeof(HeartbeatPayload_t);
    memcpy(frame.payload, &hb, sizeof(hb));

    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + frame.len);
    memcpy(frame.payload + frame.len, &crc, 2);

    for (int i = 0; i < peers.count(); i++) {
        PeerInfo *p = peers.get(i);
        if (p) {
            espnow.send(p->mac, &frame);
        }
    }
}

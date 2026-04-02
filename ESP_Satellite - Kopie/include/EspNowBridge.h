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

// Maximum number of ESP-NOW peers tracked for fail-streak recovery
#define BRIDGE_MAX_PEERS             4
// After this many consecutive MAC-level send failures, remove and re-add the peer
#define BRIDGE_FAIL_STREAK_THRESHOLD 5

class EspNowBridge {
public:
    bool begin(uint8_t channel);
    bool addPeer(const uint8_t *mac, bool encrypt = false, const char *ltk16 = nullptr);
    bool removePeer(const uint8_t *mac);
    bool send(const uint8_t *mac, const Frame_t *frame);

    /** Call from loop() to apply any pending peer-recovery requests. */
    void tick();

    void setRecvCallback(SatRecvCb cb) { _recvCb = cb; }

    static EspNowBridge &instance();

private:
    EspNowBridge() = default;
    static void _onSent(const uint8_t *mac, esp_now_send_status_t s);
    static void _onRecv(const uint8_t *mac, const uint8_t *data, int len);

    // Per-peer state for fail-streak recovery
    struct PeerState {
        uint8_t  mac[6];
        uint8_t  failStreak;
        uint32_t lastOkMs;
        bool     needsRecovery;  // Set from _onSent, acted on in tick()
        bool     valid;
    };

    // Stored peer config so we can re-register after recovery
    struct StoredPeer {
        uint8_t  mac[6];
        uint8_t  lmk[16];
        bool     encrypt;
        bool     valid;
    };

    PeerState   _peerStates[BRIDGE_MAX_PEERS]  = {};
    StoredPeer  _storedPeers[BRIDGE_MAX_PEERS] = {};

    PeerState  *_findState(const uint8_t *mac);
    PeerState  *_findOrCreateState(const uint8_t *mac);
    StoredPeer *_findStoredPeer(const uint8_t *mac);
    void        _doRecovery(const uint8_t *mac);

    SatRecvCb _recvCb  = nullptr;
    uint8_t   _channel = DEFAULT_CHANNEL;
};

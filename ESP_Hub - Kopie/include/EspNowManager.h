#pragma once
// ============================================================
//  ESP_Hub/include/EspNowManager.h
//  Initialises WiFi AP + ESP-NOW, sets PMK, fires send/recv
// ============================================================

#include <Arduino.h>
#include <DNSServer.h>
#include <esp_now.h>
#include <WiFi.h>
#include "messages.h"
#include "hub_config.h"

// Callback type for received frames
typedef void (*FrameRecvCb)(const uint8_t *mac, const Frame_t *frame);

// After this many consecutive MAC-level send failures to a peer, remove and re-add it
#define HUB_FAIL_STREAK_THRESHOLD 5

class EspNowManager {
public:
    bool begin(uint8_t channel, const char *pmk16 = nullptr);
    bool addPeer(const uint8_t *mac, const char *ltk16 = nullptr);
    bool removePeer(const uint8_t *mac);
    bool send(const uint8_t *mac, const Frame_t *frame);

    void setRecvCallback(FrameRecvCb cb) { _recvCb = cb; }

    /** Update the runtime network identity used to filter incoming frames.
     *  0x00 = legacy / accept-any. 0x01-0xFF = system-specific identity. */
    void    setNetworkId(uint8_t nid) { _networkId = nid; }
    uint8_t getNetworkId() const      { return _networkId; }

    /** Call from loop() to apply any pending peer-recovery requests. */
    void processDns() { _dns.processNextRequest(); }
    void tick();

    static EspNowManager &instance();

private:
    EspNowManager() = default;
    static void _onSent(const uint8_t *mac, esp_now_send_status_t s);
    static void _onRecv(const uint8_t *mac, const uint8_t *data, int len);

    // Per-peer send-failure tracking for automatic recovery
    struct PeerState {
        uint8_t  mac[6];
        uint8_t  failStreak;
        uint32_t lastOkMs;
        bool     needsRecovery;
        bool     valid;
    };

    // Stored peer config for re-registration after recovery
    struct StoredPeer {
        uint8_t  mac[6];
        uint8_t  lmk[16];
        bool     encrypt;
        bool     valid;
    };

    static const int MAX_HUB_PEERS = 4;
    PeerState   _peerStates[MAX_HUB_PEERS]  = {};
    StoredPeer  _storedPeers[MAX_HUB_PEERS] = {};

    PeerState  *_findState(const uint8_t *mac);
    PeerState  *_findOrCreateState(const uint8_t *mac);
    StoredPeer *_findStoredPeer(const uint8_t *mac);
    void        _doRecovery(const uint8_t *mac);

    FrameRecvCb _recvCb    = nullptr;
    uint8_t     _channel   = DEFAULT_CHANNEL;
    uint8_t     _networkId = HUB_NETWORK_ID;
    DNSServer   _dns;
};

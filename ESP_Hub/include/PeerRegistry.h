#pragma once
// ============================================================
//  ESP_Hub/include/PeerRegistry.h
//  Manages the in-RAM peer table (max 2 satellites)
//  Persisted to LittleFS as part of ConfigStore JSON
// ============================================================

#include <Arduino.h>
#include "messages.h"

#define MAX_PEERS 2

// Milliseconds after last successful data delivery before data_path_ok turns false
#define DATA_PATH_TIMEOUT_MS 5000

struct PeerInfo {
    char    name[17];      // Human-readable name + null
    uint8_t role;          // ROLE_SAT1 / ROLE_SAT2
    uint8_t mac[6];
    char    ltk_hex[33];   // 16-byte LTK as 32 hex chars + null
    bool    online;
    uint32_t lastSeen;     // millis() of last heartbeat
    uint32_t lastDataOkMs; // millis() of last confirmed data delivery (ACK or DBG telemetry)
};

class PeerRegistry {
public:
    void        clear();
    bool        addOrUpdate(const PeerInfo &info);
    bool        remove(const uint8_t *mac);
    PeerInfo   *findByMac(const uint8_t *mac);
    PeerInfo   *findByRole(uint8_t role);
    int         count() const;
    PeerInfo   *get(int idx);
    void        markOnline(const uint8_t *mac, bool online);
    /** Record that the data path to this peer is confirmed working right now. */
    void        markDataOk(const uint8_t *mac);
    void        tickTimeouts(uint32_t timeoutMs);

private:
    PeerInfo _peers[MAX_PEERS];
    int      _count = 0;
};

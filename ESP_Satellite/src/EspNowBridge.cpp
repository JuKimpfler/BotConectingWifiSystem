// ============================================================
//  ESP_Satellite/src/EspNowBridge.cpp
//  P2P ESP-NOW bridge (SAT1 <-> SAT2) + hub link
// ============================================================

#include "EspNowBridge.h"
#include "crc16.h"
#include "sat_config.h"
#include <esp_wifi.h>

EspNowBridge &EspNowBridge::instance() {
    static EspNowBridge instance;
    return instance;
}

bool EspNowBridge::begin(uint8_t channel) {
    _channel = channel;

    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        return false;
    }

    esp_now_register_send_cb(_onSent);
    esp_now_register_recv_cb(_onRecv);

    // Register broadcast peer so discovery scans can be sent
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    addPeer(bcast);

    return true;
}

bool EspNowBridge::addPeer(const uint8_t *mac, bool encrypt, const char *ltk16) {
    // Remove any stale entry first so we can re-configure cleanly
    esp_now_del_peer(mac);  // Intentionally ignore error if not registered

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = _channel;
    peer.ifidx   = WIFI_IF_STA;

    if (encrypt && ltk16 && strnlen(ltk16, ESP_NOW_KEY_LEN) == ESP_NOW_KEY_LEN) {
        peer.encrypt = true;
        memcpy(peer.lmk, ltk16, ESP_NOW_KEY_LEN);
    }

    bool ok = (esp_now_add_peer(&peer) == ESP_OK);

    // Persist peer config for potential recovery
    StoredPeer *sp = _findStoredPeer(mac);
    if (!sp) {
        for (int i = 0; i < BRIDGE_MAX_PEERS; i++) {
            if (!_storedPeers[i].valid) { sp = &_storedPeers[i]; break; }
        }
    }
    if (sp) {
        memcpy(sp->mac, mac, 6);
        sp->encrypt = encrypt;
        if (encrypt && ltk16 && strnlen(ltk16, ESP_NOW_KEY_LEN) == ESP_NOW_KEY_LEN)
            memcpy(sp->lmk, ltk16, ESP_NOW_KEY_LEN);
        sp->valid = ok;
    }

    if (ok) {
        // Reset fail streak on successful (re-)registration
        PeerState *ps = _findOrCreateState(mac);
        if (ps) {
            ps->failStreak    = 0;
            ps->needsRecovery = false;
            ps->lastOkMs      = millis();
        }
    }

    return ok;
}

bool EspNowBridge::removePeer(const uint8_t *mac) {
    bool ok = (esp_now_del_peer(mac) == ESP_OK);
    StoredPeer *sp = _findStoredPeer(mac);
    if (sp) sp->valid = false;
    PeerState  *ps = _findState(mac);
    if (ps) ps->valid = false;
    return ok;
}

bool EspNowBridge::send(const uint8_t *mac, const Frame_t *frame) {
    // Ensure peer is registered before attempting send (unicast and broadcast alike)
    if (!esp_now_is_peer_exist(mac)) {
        StoredPeer *sp = _findStoredPeer(mac);
        if (sp && sp->valid) {
            addPeer(mac, sp->encrypt, sp->encrypt ? (const char *)sp->lmk : nullptr);
        } else {
            addPeer(mac);
        }
    }

    uint16_t totalLen = FRAME_HEADER_SIZE + frame->len + sizeof(uint16_t);
    esp_err_t err = esp_now_send(mac, (const uint8_t *)frame, totalLen);

    return (err == ESP_OK);
}

void EspNowBridge::tick() {
    for (int i = 0; i < BRIDGE_MAX_PEERS; i++) {
        PeerState &ps = _peerStates[i];
        if (!ps.valid || !ps.needsRecovery) continue;
        ps.needsRecovery = false;
        _doRecovery(ps.mac);
    }
}

void EspNowBridge::_doRecovery(const uint8_t *mac) {
    StoredPeer *sp = _findStoredPeer(mac);
    if (sp && sp->valid) {
        addPeer(mac, sp->encrypt, sp->encrypt ? (const char *)sp->lmk : nullptr);
    } else {
        addPeer(mac);
    }
}

void EspNowBridge::_onSent(const uint8_t *mac, esp_now_send_status_t s) {
    EspNowBridge &self = instance();

    if (s == ESP_NOW_SEND_SUCCESS) {
        PeerState *ps = self._findOrCreateState(mac);
        if (ps) {
            ps->failStreak = 0;
            ps->lastOkMs   = millis();
        }
        return;
    }

    // MAC-level delivery failure
    PeerState *ps   = self._findOrCreateState(mac);
    uint8_t  streak = ps ? ++(ps->failStreak) : 0;

    if (ps && streak >= BRIDGE_FAIL_STREAK_THRESHOLD) {
        ps->needsRecovery = true;
        ps->failStreak    = 0;  // Reset so we don't trigger recovery every call
    }
}

void EspNowBridge::_onRecv(const uint8_t *mac,
                            const uint8_t *data, int len) {
    if (len < (int)(FRAME_HEADER_SIZE + 2)) return;

    const Frame_t *f = reinterpret_cast<const Frame_t *>(data);
    if (f->magic != FRAME_MAGIC) return;
    if (f->len > FRAME_MAX_PAYLOAD) return;

    // Validate that the received length matches the claimed payload length
    if (len < (int)(FRAME_HEADER_SIZE + f->len + 2)) {
        return;
    }

    uint16_t calcCrc = crc16_buf(data, FRAME_HEADER_SIZE + f->len);
    uint16_t rxCrc;
    memcpy(&rxCrc, data + FRAME_HEADER_SIZE + f->len, 2);
    if (calcCrc != rxCrc) {
        return;
    }

    // Anti-mis-pairing: reject frames from other BotConnectingWifiSystem deployments.
    // 0x00 means legacy / accept-any and is always allowed.
    uint8_t incoming_nid = f->network_id;
    if (incoming_nid != 0x00 &&
        ESPNOW_NETWORK_ID != 0x00 &&
        incoming_nid != (uint8_t)ESPNOW_NETWORK_ID) {
        return;
    }

    EspNowBridge &self = instance();
    if (self._recvCb) {
        self._recvCb(mac, f);
    }
}

// ── Private helpers ────────────────────────────────────────────

EspNowBridge::PeerState *EspNowBridge::_findState(const uint8_t *mac) {
    for (int i = 0; i < BRIDGE_MAX_PEERS; i++) {
        if (_peerStates[i].valid && memcmp(_peerStates[i].mac, mac, 6) == 0)
            return &_peerStates[i];
    }
    return nullptr;
}

EspNowBridge::PeerState *EspNowBridge::_findOrCreateState(const uint8_t *mac) {
    PeerState *ps = _findState(mac);
    if (ps) return ps;
    for (int i = 0; i < BRIDGE_MAX_PEERS; i++) {
        if (!_peerStates[i].valid) {
            memcpy(_peerStates[i].mac, mac, 6);
            _peerStates[i].failStreak    = 0;
            _peerStates[i].lastOkMs      = millis();
            _peerStates[i].needsRecovery = false;
            _peerStates[i].valid         = true;
            return &_peerStates[i];
        }
    }
    return nullptr;  // Table full
}

EspNowBridge::StoredPeer *EspNowBridge::_findStoredPeer(const uint8_t *mac) {
    for (int i = 0; i < BRIDGE_MAX_PEERS; i++) {
        if (_storedPeers[i].valid && memcmp(_storedPeers[i].mac, mac, 6) == 0)
            return &_storedPeers[i];
    }
    return nullptr;
}

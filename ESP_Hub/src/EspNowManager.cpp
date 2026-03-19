// ============================================================
//  ESP_Hub/src/EspNowManager.cpp
// ============================================================

#include "EspNowManager.h"
#include "crc16.h"
#include <esp_wifi.h>

EspNowManager &EspNowManager::instance() {
    static EspNowManager instance;
    return instance;
}

bool EspNowManager::begin(uint8_t channel, const char *pmk16) {
    _channel = channel;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD, channel);

    // Start DNS server so the hub is reachable as http://esp.hub
    if (!_dns.start(53, DNS_HOSTNAME, WiFi.softAPIP())) {
        Serial.println("[DNS] failed to start DNS server on port 53");
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] init failed");
        return false;
    }

    if (pmk16 && strnlen(pmk16, 16) == 16) {
        esp_now_set_pmk((const uint8_t *)pmk16);
    }

    esp_now_register_send_cb(_onSent);
    esp_now_register_recv_cb(_onRecv);

    // Register broadcast peer so discovery scans can be sent
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    addPeer(bcast, nullptr);

    Serial.printf("[ESPNOW] ready ch=%u\n", channel);
    return true;
}

bool EspNowManager::addPeer(const uint8_t *mac, const char *ltk16) {
    // Remove any stale entry first so we can re-configure cleanly
    esp_now_del_peer(mac);  // Intentionally ignore error if not registered

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = _channel;
    peer.ifidx   = WIFI_IF_AP;

    if (ltk16 && strnlen(ltk16, ESP_NOW_KEY_LEN) == ESP_NOW_KEY_LEN) {
        peer.encrypt = true;
        memcpy(peer.lmk, ltk16, ESP_NOW_KEY_LEN);
    }

    bool ok = (esp_now_add_peer(&peer) == ESP_OK);

    // Persist peer config for potential recovery
    StoredPeer *sp = _findStoredPeer(mac);
    if (!sp) {
        for (int i = 0; i < MAX_HUB_PEERS; i++) {
            if (!_storedPeers[i].valid) { sp = &_storedPeers[i]; break; }
        }
    }
    if (sp) {
        memcpy(sp->mac, mac, 6);
        sp->encrypt = (ltk16 && strnlen(ltk16, ESP_NOW_KEY_LEN) == ESP_NOW_KEY_LEN);
        if (sp->encrypt) memcpy(sp->lmk, ltk16, ESP_NOW_KEY_LEN);
        sp->valid = ok;
    }

    if (ok) {
        PeerState *ps = _findOrCreateState(mac);
        if (ps) {
            ps->failStreak    = 0;
            ps->needsRecovery = false;
            ps->lastOkMs      = millis();
        }
    }

    return ok;
}

bool EspNowManager::removePeer(const uint8_t *mac) {
    bool ok = (esp_now_del_peer(mac) == ESP_OK);
    StoredPeer *sp = _findStoredPeer(mac);
    if (sp) sp->valid = false;
    PeerState  *ps = _findState(mac);
    if (ps) ps->valid = false;
    return ok;
}

bool EspNowManager::send(const uint8_t *mac, const Frame_t *frame) {
    // Ensure peer is registered before attempting send
    if (!esp_now_is_peer_exist(mac)) {
        StoredPeer *sp = _findStoredPeer(mac);
        if (sp && sp->valid) {
            addPeer(mac, sp->encrypt ? (const char *)sp->lmk : nullptr);
        } else {
            addPeer(mac, nullptr);
        }
        Serial.printf("[ESPNOW] peer auto-restored: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    uint16_t frameLen = FRAME_HEADER_SIZE + frame->len;
    esp_err_t err = esp_now_send(mac, (const uint8_t *)frame, frameLen + sizeof(uint16_t));

    if (err != ESP_OK) {
        uint8_t ch = 0;
        esp_wifi_get_channel(&ch, nullptr);
        Serial.printf("[ESPNOW] esp_now_send err=%s mac=%02X:%02X:%02X:%02X:%02X:%02X "
                      "ch=%u type=0x%02X seq=%u\n",
                      esp_err_to_name(err),
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      ch, frame->msg_type, frame->seq);
    }
    return (err == ESP_OK);
}

void EspNowManager::tick() {
    for (int i = 0; i < MAX_HUB_PEERS; i++) {
        PeerState &ps = _peerStates[i];
        if (!ps.valid || !ps.needsRecovery) continue;
        ps.needsRecovery = false;
        _doRecovery(ps.mac);
    }
}

void EspNowManager::_doRecovery(const uint8_t *mac) {
    Serial.printf("[ESPNOW] peer recovery: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    StoredPeer *sp = _findStoredPeer(mac);
    if (sp && sp->valid) {
        addPeer(mac, sp->encrypt ? (const char *)sp->lmk : nullptr);
    } else {
        addPeer(mac, nullptr);
    }
}

void EspNowManager::_onSent(const uint8_t *mac, esp_now_send_status_t s) {
    EspNowManager &self = instance();

    if (s == ESP_NOW_SEND_SUCCESS) {
        PeerState *ps = self._findOrCreateState(mac);
        if (ps) {
            ps->failStreak = 0;
            ps->lastOkMs   = millis();
        }
        return;
    }

    // MAC-level delivery failure
    uint8_t ch = 0;
    esp_wifi_get_channel(&ch, nullptr);
    bool peerExists = esp_now_is_peer_exist(mac);

    PeerState *ps   = self._findOrCreateState(mac);
    uint8_t  streak = ps ? ++(ps->failStreak) : 0;
    uint32_t msOk   = ps ? (millis() - ps->lastOkMs) : 0;

    Serial.printf("[ESPNOW] send failed to %02X:%02X:%02X:%02X:%02X:%02X "
                  "streak=%u ch=%u peer=%d ms_since_ok=%lu heap=%lu\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  streak, ch, (int)peerExists,
                  (unsigned long)msOk,
                  (unsigned long)ESP.getFreeHeap());

    if (ps && streak >= HUB_FAIL_STREAK_THRESHOLD) {
        ps->needsRecovery = true;
        ps->failStreak    = 0;
    }
}

void EspNowManager::_onRecv(const uint8_t *mac,
                            const uint8_t *data, int len) {
    if (len < FRAME_HEADER_SIZE + 2) return;

    const Frame_t *f = reinterpret_cast<const Frame_t *>(data);
    if (f->magic != FRAME_MAGIC) return;
    if (f->len > FRAME_MAX_PAYLOAD) return;

    // Verify CRC
    uint16_t calcCrc = crc16_buf(data, FRAME_HEADER_SIZE + f->len);
    uint16_t rxCrc;
    memcpy(&rxCrc, data + FRAME_HEADER_SIZE + f->len, 2);
    if (calcCrc != rxCrc) {
        Serial.println("[ESPNOW] CRC mismatch – frame dropped");
        return;
    }

    Serial.printf("[ESPNOW] rx from %02X:%02X:%02X:%02X:%02X:%02X type=0x%02X seq=%u role=%u\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  f->msg_type, f->seq, f->src_role);

    EspNowManager &self = instance();
    if (self._recvCb) {
        self._recvCb(mac, f);
    }
}

// ── Private helpers ────────────────────────────────────────────

EspNowManager::PeerState *EspNowManager::_findState(const uint8_t *mac) {
    for (int i = 0; i < MAX_HUB_PEERS; i++) {
        if (_peerStates[i].valid && memcmp(_peerStates[i].mac, mac, 6) == 0)
            return &_peerStates[i];
    }
    return nullptr;
}

EspNowManager::PeerState *EspNowManager::_findOrCreateState(const uint8_t *mac) {
    PeerState *ps = _findState(mac);
    if (ps) return ps;
    for (int i = 0; i < MAX_HUB_PEERS; i++) {
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

EspNowManager::StoredPeer *EspNowManager::_findStoredPeer(const uint8_t *mac) {
    for (int i = 0; i < MAX_HUB_PEERS; i++) {
        if (_storedPeers[i].valid && memcmp(_storedPeers[i].mac, mac, 6) == 0)
            return &_storedPeers[i];
    }
    return nullptr;
}

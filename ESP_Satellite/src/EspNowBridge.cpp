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
       // Serial.println("[BRIDGE] ESP-NOW init failed");
        return false;
    }

    esp_now_register_send_cb(_onSent);
    esp_now_register_recv_cb(_onRecv);

    // Register broadcast peer so discovery scans can be sent
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    addPeer(bcast);

   // Serial.printf("[BRIDGE] SAT%d ready ch=%u\n", SAT_ID, channel);
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
       // Serial.printf("[BRIDGE] peer auto-restored: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   //   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    uint16_t totalLen = FRAME_HEADER_SIZE + frame->len + sizeof(uint16_t);
    esp_err_t err = esp_now_send(mac, (const uint8_t *)frame, totalLen);

    if (err != ESP_OK) {
        uint8_t ch = 0;
        esp_wifi_get_channel(&ch, nullptr);
       // Serial.printf("[BRIDGE] esp_now_send err=%s mac=%02X:%02X:%02X:%02X:%02X:%02X "
                      //"ch=%u type=0x%02X seq=%u\n",
                    //  esp_err_to_name(err),
                     // mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                    //  ch, frame->msg_type, frame->seq);
    }
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
   // Serial.printf("[BRIDGE] peer recovery: %02X:%02X:%02X:%02X:%02X:%02X\n",
                 // mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
    uint8_t ch = 0;
    esp_wifi_get_channel(&ch, nullptr);
    bool peerExists = esp_now_is_peer_exist(mac);

    PeerState *ps   = self._findOrCreateState(mac);
    uint8_t  streak = ps ? ++(ps->failStreak) : 0;
    uint32_t msOk   = ps ? (millis() - ps->lastOkMs) : 0;

   // Serial.printf("[BRIDGE] send failed to %02X:%02X:%02X:%02X:%02X:%02X "
            //      "streak=%u ch=%u peer=%d ms_since_ok=%lu heap=%lu\n",
            //      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
            //      streak, ch, (int)peerExists,
            //      (unsigned long)msOk,
            //      (unsigned long)ESP.getFreeHeap());

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
       // Serial.println("[BRIDGE] Length mismatch - frame truncated");
        return;
    }

    uint16_t calcCrc = crc16_buf(data, FRAME_HEADER_SIZE + f->len);
    uint16_t rxCrc;
    memcpy(&rxCrc, data + FRAME_HEADER_SIZE + f->len, 2);
    if (calcCrc != rxCrc) {
       // Serial.println("[BRIDGE] CRC error");
        return;
    }

   // Serial.printf("[BRIDGE] rx from %02X:%02X:%02X:%02X:%02X:%02X type=0x%02X seq=%u role=%u nid=0x%02X\n",
                //  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                //  f->msg_type, f->seq, f->src_role, f->network_id);

    // Anti-mis-pairing: reject frames from other BotConnectingWifiSystem deployments.
    // 0x00 means legacy / accept-any and is always allowed.
    uint8_t incoming_nid = f->network_id;
    if (incoming_nid != 0x00 &&
        ESPNOW_NETWORK_ID != 0x00 &&
        incoming_nid != (uint8_t)ESPNOW_NETWORK_ID) {
       // Serial.printf("[BRIDGE] DROPPED – foreign network_id 0x%02X (ours 0x%02X) "
                    //  "from %02X:%02X:%02X:%02X:%02X:%02X\n",
                    //  incoming_nid, (uint8_t)ESPNOW_NETWORK_ID,
                    //  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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

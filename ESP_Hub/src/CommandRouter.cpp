// ============================================================
//  ESP_Hub/src/CommandRouter.cpp
//  Routes WebSocket JSON commands to ESP-NOW frames and back
// ============================================================

#include "CommandRouter.h"
#include "messages.h"
#include "crc16.h"
#include <ArduinoJson.h>

static bool parseMacStr(const char *macStr, uint8_t out[6]) {
    if (!macStr || !out) return false;
    return sscanf(macStr,
                  "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &out[0], &out[1], &out[2],
                  &out[3], &out[4], &out[5]) == 6;
}

void CommandRouter::begin(AsyncWebSocket *ws, EspNowManager *espnow,
                          PeerRegistry *peers, TelemetryBuffer *telem,
                          ConfigStore *cfgStore, HubConfig *hubCfg) {
    _ws       = ws;
    _espnow   = espnow;
    _peers    = peers;
    _telem    = telem;
    _cfgStore = cfgStore;
    _hubCfg   = hubCfg;
}

// ─── WebSocket inbound ──────────────────────────────────────
void CommandRouter::onWsMessage(uint8_t *data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;

    const char *type = doc["type"] | "";

    if (strcmp(type, "ctrl")       == 0) _handleCtrl(doc["data"].as<const char *>());
    else if (strcmp(type, "mode")       == 0) _handleMode(doc["data"].as<const char *>());
    else if (strcmp(type, "cal")        == 0) _handleCal(doc["data"].as<const char *>());
    else if (strcmp(type, "settings")   == 0) _handleSettings(doc["data"].as<const char *>());
    else if (strcmp(type, "pair")       == 0) _handlePair(doc["data"].as<const char *>());
    else if (strcmp(type, "add_peer")   == 0) _handleAddPeer(doc["data"].as<const char *>());
    else if (strcmp(type, "rename_peer") == 0) _handleRenamePeer(doc["data"].as<const char *>());
    else if (strcmp(type, "delete_peer") == 0) _handleDeletePeer(doc["data"].as<const char *>());
    else if (strcmp(type, "clear_peers") == 0) _handleClearPeers();
    else if (strcmp(type, "get_status") == 0) broadcastPeerStatus();
}

// ─── ESP-NOW inbound ────────────────────────────────────────
void CommandRouter::onEspNowFrame(const uint8_t *mac, const Frame_t *frame) {
    // Capture online state before updating so we can detect the offline→online transition.
    // markOnline() only updates existing entries in-place; it never adds or removes peers.
    bool wasOnline = false;
    {
        PeerInfo *p = _peers->findByMac(mac);
        if (p) wasOnline = p->online;
    }
    _peers->markOnline(mac, true);
    // Re-fetch to confirm the transition happened (peer must have been in the registry)
    PeerInfo *peer = _peers->findByMac(mac);
    if (!wasOnline && peer && peer->online) {
        // Peer just came online – push status to UI right away
        Serial.printf("[ROUTER] peer MAC %02X:%02X:%02X:%02X:%02X:%02X came online\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        broadcastPeerStatus();
    }

    switch (frame->msg_type) {
    case MSG_DBG: {
        const TelemetryEntry_t *entry =
            reinterpret_cast<const TelemetryEntry_t *>(frame->payload);
        Serial.printf("[ROUTER] telemetry from role=%u: %s\n",
                      frame->src_role, entry->name);
        _telem->ingest(entry);
        // Confirmed data receipt – mark data path healthy
        _peers->markDataOk(mac);
        break;
    }
    case MSG_ACK: {
        const AckPayload_t *ack =
            reinterpret_cast<const AckPayload_t *>(frame->payload);
        Serial.printf("[ROUTER] ACK from role=%u seq=%u status=0x%02X msg_type=0x%02X\n",
                      frame->src_role, ack->ack_seq, ack->status, ack->msg_type);
        // Confirmed data-path ACK
        _peers->markDataOk(mac);
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"ack\",\"seq\":%u,\"status\":%u,\"msg_type\":%u}",
                 ack->ack_seq, ack->status, ack->msg_type);
        if (_ws) _ws->textAll(buf);
        break;
    }
    case MSG_HEARTBEAT:
        Serial.printf("[ROUTER] heartbeat from role=%u seq=%u\n",
                      frame->src_role, frame->seq);
        broadcastPeerStatus();
        break;
    case MSG_DISCOVERY: {
        const DiscoveryPayload_t *disc =
            reinterpret_cast<const DiscoveryPayload_t *>(frame->payload);
        if (disc->action == 1) {
            // Announce from satellite – register peer and notify UI
            char macStr[18];
            snprintf(macStr, sizeof(macStr),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            Serial.printf("[ROUTER] discovery announce: role=%u name=%s mac=%s ch=%u\n",
                          disc->role, disc->name, macStr, disc->channel);

            // Add / update peer in registry
            PeerInfo info = {};
            strlcpy(info.name, disc->name, sizeof(info.name));
            info.role     = disc->role;
            memcpy(info.mac, mac, 6);
            info.online   = true;
            info.lastSeen = millis();
            _peers->addOrUpdate(info);

            // Register peer in ESP-NOW so we can send to it
            _espnow->addPeer(mac, nullptr);

            // Send scan_result to UI
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"type\":\"scan_result\",\"name\":\"%s\","
                     "\"role\":%u,\"mac\":\"%s\",\"channel\":%u}",
                     disc->name, disc->role, macStr, disc->channel);
            if (_ws) _ws->textAll(buf);

            broadcastPeerStatus();
        }
        break;
    }
    default:
        Serial.printf("[ROUTER] unknown frame type=0x%02X from role=%u\n",
                      frame->msg_type, frame->src_role);
        break;
    }
}

// ─── Periodic tick ───────────────────────────────────────────
void CommandRouter::tick() {
    if (_telem && _telem->tick()) {
        _broadcastTelemetry();
    }
}

void CommandRouter::broadcastPeerStatus() {
    if (!_ws || !_peers) return;

    JsonDocument doc;
    doc["type"] = "peer_status";
    JsonArray arr = doc["peers"].to<JsonArray>();
    uint32_t now = millis();
    for (int i = 0; i < _peers->count(); i++) {
        PeerInfo *p = _peers->get(i);
        if (!p) continue;
        JsonObject o = arr.add<JsonObject>();
        o["name"]         = p->name;
        o["role"]         = (p->role == ROLE_SAT1) ? "SAT1" : "SAT2";
        o["online"]       = p->online;
        o["data_path_ok"] = p->online && (now - p->lastDataOkMs) < DATA_PATH_TIMEOUT_MS;
        char macStr[18];
        snprintf(macStr, sizeof(macStr),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 p->mac[0], p->mac[1], p->mac[2],
                 p->mac[3], p->mac[4], p->mac[5]);
        o["mac"] = macStr;
    }
    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    _ws->textAll(buf, n);
}

// ─── Private helpers ─────────────────────────────────────────
void CommandRouter::_broadcastTelemetry() {
    if (!_ws || !_telem) return;
    int cnt = _telem->streamCount();
    if (cnt == 0) return;

    JsonDocument doc;
    doc["type"] = "telemetry";
    JsonArray arr = doc["streams"].to<JsonArray>();
    for (int i = 0; i < cnt; i++) {
        StreamStat *s = _telem->getStream(i);
        if (!s || !s->valid) continue;
        JsonObject o = arr.add<JsonObject>();
        o["name"]    = s->name;
        o["current"] = s->current;
        o["min"]     = s->minVal;
        o["max"]     = s->maxVal;
    }
    char buf[1024];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    _ws->textAll(buf, n);
}

void CommandRouter::_handleCtrl(const char *json) {
    if (!json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    CtrlPayload_t ctrl = {};
    ctrl.speed       = doc["speed"]  | 0;
    ctrl.angle       = doc["angle"]  | 0;
    ctrl.switches    = doc["sw"]     | 0;
    ctrl.buttons     = doc["btn"]    | 0;
    ctrl.start       = doc["start"]  | 0;
    ctrl.target_role = doc["target"] | (uint8_t)ROLE_SAT1;

    _buildAndSend(ctrl.target_role, MSG_CTRL,
                  (const uint8_t *)&ctrl, sizeof(ctrl), 0);
}

void CommandRouter::_handleMode(const char *json) {
    if (!json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    ModePayload_t mode = {};
    mode.mode_id     = doc["mode_id"]    | 1;
    mode.target_role = doc["target"]     | (uint8_t)ROLE_SAT1;

    _buildAndSend(mode.target_role, MSG_MODE,
                  (const uint8_t *)&mode, sizeof(mode), FLAG_ACK_REQ);
}

void CommandRouter::_handleCal(const char *json) {
    if (!json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    CalPayload_t cal = {};
    cal.cal_cmd     = doc["cal_cmd"]  | CAL_IR_MAX;
    cal.target_role = doc["target"]   | (uint8_t)ROLE_SAT1;

    _buildAndSend(cal.target_role, MSG_CAL,
                  (const uint8_t *)&cal, sizeof(cal), FLAG_ACK_REQ);
}

void CommandRouter::_handleSettings(const char *json) {
    if (!json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    Serial.printf("[ROUTER] settings update: %s\n", json);

    if (!_hubCfg || !_cfgStore || !_peers) {
        Serial.println("[ROUTER] settings: cfgStore/hubCfg not available");
        if (_ws) _ws->textAll("{\"type\":\"ack\",\"seq\":0,\"status\":1,\"msg_type\":9}");
        return;
    }

    // Apply settings to runtime config
    if (doc.containsKey("channel")) {
        _hubCfg->channel = doc["channel"] | _hubCfg->channel;
    }
    if (doc.containsKey("pmk")) {
        const char *pmk = doc["pmk"] | "";
        strlcpy(_hubCfg->pmk_hex, pmk, sizeof(_hubCfg->pmk_hex));
    }
    if (doc.containsKey("telemetry_max_hz")) {
        _hubCfg->telemetry_max_hz = doc["telemetry_max_hz"] | _hubCfg->telemetry_max_hz;
        // Update telemetry interval at runtime
        if (_telem && _hubCfg->telemetry_max_hz > 0) {
            _telem->begin(1000 / _hubCfg->telemetry_max_hz);
        }
    }

    // Persist to flash
    bool ok = _cfgStore->save(*_hubCfg, *_peers);

    // Send feedback to UI
    char buf[80];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"ack\",\"seq\":%u,\"status\":%u,\"msg_type\":%u}",
             _seq, ok ? ACK_OK : ACK_ERR_UNKNOWN, MSG_SETTINGS);
    if (_ws) _ws->textAll(buf);

    Serial.printf("[ROUTER] settings saved: %s\n", ok ? "OK" : "FAIL");
}

void CommandRouter::_handlePair(const char *json) {
    if (!json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    uint8_t action = doc["action"] | 0;

    if (action == 0) {
        // Scan: broadcast MSG_DISCOVERY so all satellites can announce themselves
        DiscoveryPayload_t disc = {};
        disc.action  = 0;  // scan request
        disc.role    = ROLE_HUB;
        strlcpy(disc.name, "HUB", sizeof(disc.name));
        WiFi.softAPmacAddress(disc.mac);

        uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        Frame_t frame = {};
        frame.magic    = FRAME_MAGIC;
        frame.msg_type = MSG_DISCOVERY;
        frame.seq      = _seq++;
        frame.src_role = ROLE_HUB;
        frame.dst_role = ROLE_BROADCAST;
        frame.flags    = 0;
        frame.len      = sizeof(DiscoveryPayload_t);
        memcpy(frame.payload, &disc, sizeof(disc));

        uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + frame.len);
        memcpy(frame.payload + frame.len, &crc, 2);
        _espnow->send(bcast, &frame);
        Serial.println("[ROUTER] Discovery scan broadcast sent");
    } else {
        // action==2: unpair – keep original MSG_PAIR logic
        PairPayload_t pair = {};
        pair.action = action;
        pair.role   = doc["role"] | (uint8_t)ROLE_SAT1;
        strlcpy(pair.name, doc["name"] | "", sizeof(pair.name));

        uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        Frame_t frame = {};
        frame.magic    = FRAME_MAGIC;
        frame.msg_type = MSG_PAIR;
        frame.seq      = _seq++;
        frame.src_role = ROLE_HUB;
        frame.dst_role = ROLE_BROADCAST;
        frame.flags    = FLAG_ACK_REQ;
        frame.len      = sizeof(PairPayload_t);
        memcpy(frame.payload, &pair, sizeof(pair));

        uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + frame.len);
        memcpy(frame.payload + frame.len, &crc, 2);
        _espnow->send(bcast, &frame);
    }
}

void CommandRouter::_handleAddPeer(const char *json) {
    if (!json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    const char *macStr = doc["mac"]  | "";
    const char *name   = doc["name"] | "SAT";
    uint8_t role       = doc["role"] | (uint8_t)ROLE_SAT1;

    uint8_t mac[6] = {};
    if (!parseMacStr(macStr, mac)) {
        Serial.printf("[ROUTER] add_peer: invalid MAC '%s'\n", macStr);
        if (_ws) _ws->textAll("{\"type\":\"error\",\"msg\":\"Invalid MAC address\"}");
        return;
    }

    PeerInfo info = {};
    strlcpy(info.name, name, sizeof(info.name));
    info.role     = role;
    memcpy(info.mac, mac, 6);
    info.online   = false;
    info.lastSeen = 0;
    _peers->addOrUpdate(info);
    _espnow->addPeer(mac, nullptr);

    Serial.printf("[ROUTER] Manual peer added: name=%s role=%u mac=%s\n", name, role, macStr);
    if (_cfgStore && _hubCfg) _cfgStore->save(*_hubCfg, *_peers);
    broadcastPeerStatus();
}

void CommandRouter::_handleRenamePeer(const char *json) {
    if (!json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    const char *macStr = doc["mac"]  | "";
    const char *name   = doc["name"] | "";
    if (name[0] == '\0') {
        if (_ws) _ws->textAll("{\"type\":\"error\",\"msg\":\"Name must not be empty\"}");
        return;
    }

    uint8_t mac[6] = {};
    if (!parseMacStr(macStr, mac)) {
        if (_ws) _ws->textAll("{\"type\":\"error\",\"msg\":\"Invalid MAC address\"}");
        return;
    }

    PeerInfo *peer = _peers ? _peers->findByMac(mac) : nullptr;
    if (!peer) {
        if (_ws) _ws->textAll("{\"type\":\"error\",\"msg\":\"Peer not found\"}");
        return;
    }

    strlcpy(peer->name, name, sizeof(peer->name));
    if (_cfgStore && _hubCfg) _cfgStore->save(*_hubCfg, *_peers);
    Serial.printf("[ROUTER] Peer renamed: %s -> %s\n", macStr, peer->name);
    broadcastPeerStatus();
}

void CommandRouter::_handleDeletePeer(const char *json) {
    if (!json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    const char *macStr = doc["mac"] | "";
    uint8_t mac[6] = {};
    if (!parseMacStr(macStr, mac)) {
        if (_ws) _ws->textAll("{\"type\":\"error\",\"msg\":\"Invalid MAC address\"}");
        return;
    }

    bool removed = _peers && _peers->remove(mac);
    if (removed && _espnow) _espnow->removePeer(mac);
    if (removed && _cfgStore && _hubCfg) _cfgStore->save(*_hubCfg, *_peers);
    Serial.printf("[ROUTER] Delete peer %s: %s\n", macStr, removed ? "ok" : "not found");
    broadcastPeerStatus();
}

void CommandRouter::_handleClearPeers() {
    if (!_peers) return;
    for (int i = _peers->count() - 1; i >= 0; --i) {
        PeerInfo *p = _peers->get(i);
        if (!p) continue;
        if (_espnow) _espnow->removePeer(p->mac);
    }
    _peers->clear();
    if (_cfgStore && _hubCfg) _cfgStore->save(*_hubCfg, *_peers);
    Serial.println("[ROUTER] All peers cleared");
    broadcastPeerStatus();
}

void CommandRouter::_buildAndSend(uint8_t role, uint8_t msgType,
                                   const uint8_t *payload, uint8_t payLen,
                                   uint8_t flags) {
    PeerInfo *peer = _peers->findByRole(role);
    if (!peer || !peer->online) {
        Serial.printf("[ROUTER] peer role %u not online\n", role);
        return;
    }

    Frame_t frame = {};
    frame.magic    = FRAME_MAGIC;
    frame.msg_type = msgType;
    frame.seq      = _seq++;
    frame.src_role = ROLE_HUB;
    frame.dst_role = role;
    frame.flags    = flags;
    frame.len      = payLen;
    if (payLen > FRAME_MAX_PAYLOAD) return;  // bounds check
    memcpy(frame.payload, payload, payLen);

    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + payLen);
    memcpy(frame.payload + payLen, &crc, 2);

    Serial.printf("[ROUTER] tx type=0x%02X seq=%u -> role=%u (%s)\n",
                  msgType, frame.seq, role, peer->name);
    _espnow->send(peer->mac, &frame);
}

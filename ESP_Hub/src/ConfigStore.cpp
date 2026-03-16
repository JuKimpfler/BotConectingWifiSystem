// ============================================================
//  ESP_Hub/src/ConfigStore.cpp
//  JSON config persistence in LittleFS; secrets in NVS
// ============================================================

#include "ConfigStore.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#define NVS_NAMESPACE "hubcfg"

bool ConfigStore::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[CFG] LittleFS mount failed – formatting");
        return false;
    }
    Serial.println("[CFG] LittleFS mounted");
    return true;
}

bool ConfigStore::load(HubConfig &cfg, PeerRegistry &peers) {
    // Defaults
    cfg.version               = 1;
    cfg.channel               = 6;
    cfg.pmk_hex[0]            = '\0';
    cfg.telemetry_max_hz      = 20;
    cfg.heartbeat_interval_ms = 1000;
    cfg.heartbeat_timeout_ms  = 4000;
    cfg.ui_rate_limit_ms      = 20;
    strncpy(cfg.ui_theme, "dark", sizeof(cfg.ui_theme));
    peers.clear();

    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println("[CFG] No config file – using defaults");
        return true;
    }

    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[CFG] JSON parse error: %s\n", err.c_str());
        return false;
    }

    cfg.version          = doc["version"]                        | 1;
    cfg.channel          = doc["channel"]                        | 6;
    cfg.telemetry_max_hz = doc["telemetry"]["max_rate_hz"]       | 20;
    cfg.heartbeat_interval_ms = doc["heartbeat"]["interval_ms"]  | 1000;
    cfg.heartbeat_timeout_ms  = doc["heartbeat"]["timeout_ms"]   | 4000;
    cfg.ui_rate_limit_ms = doc["ui"]["rate_limit_ms"]            | 20;
    strlcpy(cfg.ui_theme, doc["ui"]["theme"] | "dark", sizeof(cfg.ui_theme));

    // Load PMK from NVS
    _readNvsSecret("pmk", cfg.pmk_hex, sizeof(cfg.pmk_hex));

    // Load peers
    JsonArray arr = doc["peers"].as<JsonArray>();
    for (JsonObject p : arr) {
        PeerInfo info = {};
        strlcpy(info.name,    p["name"]    | "",  sizeof(info.name));
        strlcpy(info.ltk_hex, p["ltk"]     | "",  sizeof(info.ltk_hex));
        const char *roleStr = p["role"] | "";
        info.role = (strcmp(roleStr, "SAT1") == 0) ? ROLE_SAT1 : ROLE_SAT2;

        const char *macStr = p["mac"] | "";
        uint32_t b[6];
        if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            for (int i = 0; i < 6; i++) info.mac[i] = (uint8_t)b[i];
        }

        // Load LTK from NVS by MAC key
        char nvsKey[20];
        snprintf(nvsKey, sizeof(nvsKey), "ltk_%02x%02x", info.mac[4], info.mac[5]);
        _readNvsSecret(nvsKey, info.ltk_hex, sizeof(info.ltk_hex));

        peers.addOrUpdate(info);
    }

    Serial.printf("[CFG] Loaded: ch=%u, %d peers\n", cfg.channel, peers.count());
    return true;
}

bool ConfigStore::save(const HubConfig &cfg, const PeerRegistry &peers) {
    JsonDocument doc;
    doc["version"] = cfg.version;
    doc["channel"] = cfg.channel;

    doc["telemetry"]["max_rate_hz"]     = cfg.telemetry_max_hz;
    doc["heartbeat"]["interval_ms"]     = cfg.heartbeat_interval_ms;
    doc["heartbeat"]["timeout_ms"]      = cfg.heartbeat_timeout_ms;
    doc["ui"]["rate_limit_ms"]          = cfg.ui_rate_limit_ms;
    doc["ui"]["theme"]                  = cfg.ui_theme;

    JsonArray arr = doc["peers"].to<JsonArray>();
    for (int i = 0; i < peers.count(); i++) {
        const PeerInfo *p = const_cast<PeerRegistry &>(peers).get(i);
        if (!p) continue;
        JsonObject o = arr.add<JsonObject>();
        o["name"] = p->name;
        o["role"] = (p->role == ROLE_SAT1) ? "SAT1" : "SAT2";
        char macStr[18];
        snprintf(macStr, sizeof(macStr),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 p->mac[0], p->mac[1], p->mac[2],
                 p->mac[3], p->mac[4], p->mac[5]);
        o["mac"] = macStr;
        // LTK stored in NVS, not JSON
    }

    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();

    // Save secrets to NVS
    _writeNvsSecret("pmk", cfg.pmk_hex);
    for (int i = 0; i < peers.count(); i++) {
        const PeerInfo *p = const_cast<PeerRegistry &>(peers).get(i);
        if (!p) continue;
        char nvsKey[20];
        snprintf(nvsKey, sizeof(nvsKey), "ltk_%02x%02x", p->mac[4], p->mac[5]);
        _writeNvsSecret(nvsKey, p->ltk_hex);
    }

    Serial.println("[CFG] Saved");
    return true;
}

bool ConfigStore::factoryReset() {
    if (LittleFS.exists(CONFIG_FILE)) {
        LittleFS.remove(CONFIG_FILE);
    }
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    Serial.println("[CFG] Factory reset done");
    return true;
}

bool ConfigStore::_readNvsSecret(const char *key, char *out, size_t maxLen) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    size_t n = prefs.getString(key, out, (unsigned int)maxLen);
    prefs.end();
    return n > 0;
}

bool ConfigStore::_writeNvsSecret(const char *key, const char *val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(key, val);
    prefs.end();
    return true;
}

void ConfigStore::hexToBytes(const char *hex, uint8_t *out, int len) {
    if (!hex || !out || len <= 0) return;
    size_t hexLen = strlen(hex);
    for (int i = 0; i < len; i++) {
        if ((size_t)(i * 2 + 2) > hexLen) {
            out[i] = 0;
            continue;
        }
        uint32_t b = 0;
        sscanf(hex + i * 2, "%02x", &b);
        out[i] = (uint8_t)b;
    }
}

void ConfigStore::bytesToHex(const uint8_t *in, int len, char *out, size_t outLen) {
    if (!in || !out || len <= 0 || outLen < (size_t)(len * 2 + 1)) return;
    for (int i = 0; i < len; i++) {
        snprintf(out + i * 2, outLen - (size_t)(i * 2), "%02X", in[i]);
    }
    out[len * 2] = '\0';
}

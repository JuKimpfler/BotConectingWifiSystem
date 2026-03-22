// ============================================================
//  ESP_Hub/src/ConfigStore.cpp
//  JSON config persistence in LittleFS; secrets in NVS
// ============================================================

#include "ConfigStore.h"
#include "hub_config.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#define NVS_NAMESPACE "hubcfg"

static void setDefaultUiLabels(HubConfig &cfg) {
    static const char *kModeDefaults[MODE_CHANNEL_COUNT] = {
        "Mode 1", "Mode 2", "Mode 3", "Mode 4", "Mode 5"
    };
    static const char *kCalDefaults[CAL_CHANNEL_COUNT] = {
        "Calib 1", "Calib 2", "Calib 3", "Calib 4", "Calib 5"
    };
    for (uint8_t i = 0; i < MODE_CHANNEL_COUNT; i++) {
        strlcpy(cfg.mode_labels[i], kModeDefaults[i], sizeof(cfg.mode_labels[i]));
    }
    for (uint8_t i = 0; i < CAL_CHANNEL_COUNT; i++) {
        strlcpy(cfg.cal_labels[i], kCalDefaults[i], sizeof(cfg.cal_labels[i]));
    }
}

static void loadUiLabelsFromDoc(HubConfig &cfg, const JsonDocument &doc) {
    JsonArrayConst modeLabels = doc["ui"]["mode_labels"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < MODE_CHANNEL_COUNT; i++) {
        if (i < modeLabels.size() && modeLabels[i].is<const char *>()) {
            strlcpy(cfg.mode_labels[i], modeLabels[i] | "", sizeof(cfg.mode_labels[i]));
        }
    }
    JsonArrayConst calLabels = doc["ui"]["cal_labels"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < CAL_CHANNEL_COUNT; i++) {
        if (i < calLabels.size() && calLabels[i].is<const char *>()) {
            strlcpy(cfg.cal_labels[i], calLabels[i] | "", sizeof(cfg.cal_labels[i]));
        }
    }
}

static void writeUiLabelsToDoc(const HubConfig &cfg, JsonDocument &doc) {
    JsonArray modeArr = doc["ui"]["mode_labels"].to<JsonArray>();
    for (uint8_t i = 0; i < MODE_CHANNEL_COUNT; i++) modeArr.add(cfg.mode_labels[i]);
    JsonArray calArr = doc["ui"]["cal_labels"].to<JsonArray>();
    for (uint8_t i = 0; i < CAL_CHANNEL_COUNT; i++) calArr.add(cfg.cal_labels[i]);
}

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
    cfg.network_id            = HUB_NETWORK_ID;
    strncpy(cfg.ui_theme, "dark", sizeof(cfg.ui_theme));
    setDefaultUiLabels(cfg);
    peers.clear();

    if (!LittleFS.exists(CONFIG_FILE)) {
        if (strlen(HUB_CONFIG_DEFAULT_JSON) > 0) {
            JsonDocument defaultDoc;
            if (deserializeJson(defaultDoc, HUB_CONFIG_DEFAULT_JSON) == DeserializationError::Ok) {
                cfg.version          = defaultDoc["version"]                        | cfg.version;
                cfg.channel          = defaultDoc["channel"]                        | cfg.channel;
                cfg.network_id       = defaultDoc["network_id"]                     | cfg.network_id;
                cfg.telemetry_max_hz = defaultDoc["telemetry"]["max_rate_hz"]       | cfg.telemetry_max_hz;
                cfg.heartbeat_interval_ms = defaultDoc["heartbeat"]["interval_ms"]  | cfg.heartbeat_interval_ms;
                cfg.heartbeat_timeout_ms  = defaultDoc["heartbeat"]["timeout_ms"]   | cfg.heartbeat_timeout_ms;
                cfg.ui_rate_limit_ms = defaultDoc["ui"]["rate_limit_ms"]            | cfg.ui_rate_limit_ms;
                strlcpy(cfg.ui_theme, defaultDoc["ui"]["theme"] | cfg.ui_theme, sizeof(cfg.ui_theme));
                strlcpy(cfg.pmk_hex, defaultDoc["pmk"] | "", sizeof(cfg.pmk_hex));
                loadUiLabelsFromDoc(cfg, defaultDoc);

                JsonArray arr = defaultDoc["peers"].as<JsonArray>();
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
                        bool valid = true;
                        for (int i = 0; i < 6; i++) {
                            if (b[i] > 0xFF) {
                                valid = false;
                                break;
                            }
                        }
                        if (valid) {
                            for (int i = 0; i < 6; i++) info.mac[i] = (uint8_t)b[i];
                        }
                    }

                    peers.addOrUpdate(info);
                }
                Serial.println("[CFG] No file, loaded compile-time HUB_CONFIG_DEFAULT_JSON");
                return true;
            }
            Serial.println("[CFG] HUB_CONFIG_DEFAULT_JSON parse failed, using defaults");
        }
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
    cfg.network_id       = doc["network_id"]                     | (uint8_t)HUB_NETWORK_ID;
    cfg.telemetry_max_hz = doc["telemetry"]["max_rate_hz"]       | 20;
    cfg.heartbeat_interval_ms = doc["heartbeat"]["interval_ms"]  | 1000;
    cfg.heartbeat_timeout_ms  = doc["heartbeat"]["timeout_ms"]   | 4000;
    cfg.ui_rate_limit_ms = doc["ui"]["rate_limit_ms"]            | 20;
    strlcpy(cfg.ui_theme, doc["ui"]["theme"] | "dark", sizeof(cfg.ui_theme));
    loadUiLabelsFromDoc(cfg, doc);

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
            // Validate that all bytes are in valid range (0x00-0xFF)
            bool valid = true;
            for (int i = 0; i < 6; i++) {
                if (b[i] > 0xFF) {
                    valid = false;
                    Serial.printf("[CONFIG] Invalid MAC byte %d: 0x%x (> 0xFF)\n", i, b[i]);
                    break;
                }
            }
            if (valid) {
                for (int i = 0; i < 6; i++) info.mac[i] = (uint8_t)b[i];
            } else {
                // Set to invalid MAC on error
                memset(info.mac, 0, 6);
            }
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
    doc["version"]    = cfg.version;
    doc["channel"]    = cfg.channel;
    doc["network_id"] = cfg.network_id;

    doc["telemetry"]["max_rate_hz"]     = cfg.telemetry_max_hz;
    doc["heartbeat"]["interval_ms"]     = cfg.heartbeat_interval_ms;
    doc["heartbeat"]["timeout_ms"]      = cfg.heartbeat_timeout_ms;
    doc["ui"]["rate_limit_ms"]          = cfg.ui_rate_limit_ms;
    doc["ui"]["theme"]                  = cfg.ui_theme;
    writeUiLabelsToDoc(cfg, doc);

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

bool ConfigStore::exportJson(const HubConfig &cfg, const PeerRegistry &peers, String &out) {
    JsonDocument doc;
    doc["version"]    = cfg.version;
    doc["channel"]    = cfg.channel;
    doc["network_id"] = cfg.network_id;
    doc["pmk"]        = cfg.pmk_hex;
    doc["telemetry"]["max_rate_hz"]     = cfg.telemetry_max_hz;
    doc["heartbeat"]["interval_ms"]     = cfg.heartbeat_interval_ms;
    doc["heartbeat"]["timeout_ms"]      = cfg.heartbeat_timeout_ms;
    doc["ui"]["rate_limit_ms"]          = cfg.ui_rate_limit_ms;
    doc["ui"]["theme"]                  = cfg.ui_theme;
    writeUiLabelsToDoc(cfg, doc);

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
        o["ltk"] = p->ltk_hex;
    }

    out = "";
    serializeJsonPretty(doc, out);
    return out.length() > 0;
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

    // Validate that hex string has exactly the expected length (2 chars per byte)
    if (hexLen != (size_t)(len * 2)) {
        Serial.printf("[CONFIG] Warning: hex string length mismatch (expected %d chars, got %zu)\n",
                      len * 2, hexLen);
        // Zero-fill the output buffer on mismatch
        memset(out, 0, len);
        return;
    }

    for (int i = 0; i < len; i++) {
        uint32_t b = 0;
        if (sscanf(hex + i * 2, "%02x", &b) == 1) {
            out[i] = (uint8_t)b;
        } else {
            out[i] = 0;
        }
    }
}

void ConfigStore::bytesToHex(const uint8_t *in, int len, char *out, size_t outLen) {
    if (!in || !out || len <= 0 || outLen < (size_t)(len * 2 + 1)) return;
    for (int i = 0; i < len; i++) {
        snprintf(out + i * 2, outLen - (size_t)(i * 2), "%02X", in[i]);
    }
    out[len * 2] = '\0';
}

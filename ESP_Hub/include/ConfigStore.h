#pragma once
// ============================================================
//  ESP_Hub/include/ConfigStore.h
//  Loads/saves JSON config from LittleFS; NVS for secrets
// ============================================================

#include <Arduino.h>
#include "PeerRegistry.h"
#include "hub_config.h"

constexpr uint8_t MODE_CHANNEL_COUNT = 5;
constexpr uint8_t CAL_CHANNEL_COUNT  = 5;
constexpr size_t  UI_LABEL_MAX_LEN   = 24;

struct HubConfig {
    uint8_t  version;
    uint8_t  channel;
    char     pmk_hex[33];
    uint8_t  telemetry_max_hz;
    uint32_t heartbeat_interval_ms;
    uint32_t heartbeat_timeout_ms;
    uint16_t ui_rate_limit_ms;
    char     ui_theme[8];
    char     mode_labels[MODE_CHANNEL_COUNT][UI_LABEL_MAX_LEN];
    char     cal_labels[CAL_CHANNEL_COUNT][UI_LABEL_MAX_LEN];
    uint8_t  network_id;   // Anti-mis-pairing system ID (0=legacy, 1-255=system-specific)
};

class ConfigStore {
public:
    bool begin();
    bool load(HubConfig &cfg, PeerRegistry &peers);
    bool save(const HubConfig &cfg, const PeerRegistry &peers);
    bool exportJson(const HubConfig &cfg, const PeerRegistry &peers, String &out);
    bool factoryReset();

    static void hexToBytes(const char *hex, uint8_t *out, int len);
    static void bytesToHex(const uint8_t *in, int len, char *out, size_t outLen);

private:
    bool _readNvsSecret(const char *key, char *out, size_t maxLen);
    bool _writeNvsSecret(const char *key, const char *val);
};

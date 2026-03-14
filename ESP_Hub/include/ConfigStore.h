#pragma once
// ============================================================
//  ESP_Hub/include/ConfigStore.h
//  Loads/saves JSON config from LittleFS; NVS for secrets
// ============================================================

#include <Arduino.h>
#include "PeerRegistry.h"
#include "hub_config.h"

struct HubConfig {
    uint8_t  version;
    uint8_t  channel;
    char     pmk_hex[33];
    uint8_t  telemetry_max_hz;
    uint32_t heartbeat_interval_ms;
    uint32_t heartbeat_timeout_ms;
    uint16_t ui_rate_limit_ms;
    char     ui_theme[8];
};

class ConfigStore {
public:
    bool begin();
    bool load(HubConfig &cfg, PeerRegistry &peers);
    bool save(const HubConfig &cfg, const PeerRegistry &peers);
    bool factoryReset();

    static void hexToBytes(const char *hex, uint8_t *out, int len);
    static void bytesToHex(const uint8_t *in, int len, char *out);

private:
    bool _readNvsSecret(const char *key, char *out, size_t maxLen);
    bool _writeNvsSecret(const char *key, const char *val);
};

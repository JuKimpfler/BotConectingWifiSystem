// ============================================================
//  ESP_Hub/src/main_light.cpp
//  Hub Light – telemetry-only, ultra-low-overhead firmware.
//
//  Only compiled when HUB_LIGHT_MODE is defined (see platformio.ini
//  envs esp_hub_light / esp_hub_c6_light).
//
//  Differences vs main.cpp:
//    – No StatusLeds / BatteryMonitor / HeartbeatService
//    – No reset-button handler
//    – AP SSID: "ESP-Hub-Light", DNS: "esp.light"
//    – TELEMETRY_MAX_HZ = 50  (20 ms flush interval)
//    – All Serial.printf on the telemetry hot path are suppressed
//    – _broadcastTelemetry() uses a fast hand-rolled JSON builder
// ============================================================

#ifdef HUB_LIGHT_MODE

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "hub_config.h"
#include "messages.h"
#include "EspNowManager.h"
#include "PeerRegistry.h"
#include "ConfigStore.h"
#include "CommandRouter.h"
#include "TelemetryBuffer.h"

// ── Light-mode network identity ──────────────────────────────
// AP_SSID / AP_PASSWORD / DNS_HOSTNAME are set via build_flags in
// platformio.ini; they resolve to "ESP-Hub-Light" / "hub12345" / "esp.light".
// TELEMETRY_MAX_HZ is also set to 50 via build_flags.

// ── Global objects ───────────────────────────────────────────
static AsyncWebServer  server(WS_PORT);
static AsyncWebSocket  ws(WS_PATH);
static PeerRegistry    peers;
static ConfigStore     cfgStore;
static HubConfig       hubCfg;
static TelemetryBuffer telem;
static CommandRouter   router;

// ── ESP-NOW receive callback ─────────────────────────────────
static void onEspNowFrame(const uint8_t *mac, const Frame_t *frame) {
    router.onEspNowFrame(mac, frame);
}

// ── WebSocket event handler ──────────────────────────────────
static void onWsEvent(AsyncWebSocket * /*srv*/, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[HUB-LIGHT] WS client #%u connected\n", client->id());
        router.broadcastPeerStatus();
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[HUB-LIGHT] WS client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->opcode == WS_TEXT) {
            router.onWsMessage(data, len);
        }
    }
}

// ── Setup ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[HUB-LIGHT] Booting...");
    Serial.printf("[HUB-LIGHT] AP SSID: %s  DNS: %s  Telem Hz: %u\n",
                  AP_SSID, DNS_HOSTNAME, (unsigned)TELEMETRY_MAX_HZ);

    // Mount filesystem and load persistent config
    cfgStore.begin();
    cfgStore.load(hubCfg, peers);

    // Initialise ESP-NOW (and start soft-AP + DNS)
    EspNowManager &espnow = EspNowManager::instance();
    uint8_t pmkBytes[16] = {};
    const int PMK_HEX_LEN = 32;
    if (strnlen(hubCfg.pmk_hex, PMK_HEX_LEN + 1) == (size_t)PMK_HEX_LEN) {
        ConfigStore::hexToBytes(hubCfg.pmk_hex, pmkBytes, 16);
        espnow.begin(hubCfg.channel, (const char *)pmkBytes,
                     AP_SSID, AP_PASSWORD, DNS_HOSTNAME);
    } else {
        espnow.begin(hubCfg.channel, nullptr,
                     AP_SSID, AP_PASSWORD, DNS_HOSTNAME);
    }
    espnow.setRecvCallback(onEspNowFrame);
    espnow.setNetworkId(hubCfg.network_id);

    // Re-register persisted peers
    const int LTK_HEX_LEN = 32;
    for (int i = 0; i < peers.count(); i++) {
        PeerInfo *p = peers.get(i);
        if (!p) continue;
        uint8_t ltk[16] = {};
        const char *ltkPtr = nullptr;
        if (strnlen(p->ltk_hex, LTK_HEX_LEN + 1) == (size_t)LTK_HEX_LEN) {
            ConfigStore::hexToBytes(p->ltk_hex, ltk, 16);
            ltkPtr = (const char *)ltk;
        }
        espnow.addPeer(p->mac, ltkPtr);
    }

    // Telemetry buffer – in light mode enforce at least TELEMETRY_MAX_HZ (50 Hz).
    // The persisted config may contain a lower value (e.g. 20 Hz default).
    // This override is intentional: the light hub is purpose-built for high-
    // frequency telemetry and should always run at the maximum rate.  The
    // change is NOT written back to flash so the stored config is preserved
    // and will take full effect again when flashing the normal firmware.
    if (hubCfg.telemetry_max_hz < TELEMETRY_MAX_HZ) {
        Serial.printf("[HUB-LIGHT] Stored telemetry_max_hz=%u below light-mode minimum; "
                      "overriding to %u Hz for this session only.\n",
                      hubCfg.telemetry_max_hz, (unsigned)TELEMETRY_MAX_HZ);
        hubCfg.telemetry_max_hz = TELEMETRY_MAX_HZ;
    }
    telem.begin(1000u / hubCfg.telemetry_max_hz);

    // Command router (no heartbeat_interval_ms needed in light mode)
    router.begin(&ws, &espnow, &peers, &telem, &cfgStore, &hubCfg);

    // WebSocket
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // Serve light UI from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.onNotFound([](AsyncWebServerRequest *req) {
        req->send(404, "text/plain", "Not found");
    });

    // REST: GET /api/config
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!LittleFS.exists(CONFIG_FILE)) {
            req->send(404, "application/json", "{\"error\":\"no config\"}");
            return;
        }
        req->send(LittleFS, CONFIG_FILE, "application/json");
    });

    server.on("/api/config_export", HTTP_GET, [](AsyncWebServerRequest *req) {
        String json;
        if (!cfgStore.exportJson(hubCfg, peers, json)) {
            req->send(500, "application/json", "{\"error\":\"export failed\"}");
            return;
        }
        req->send(200, "application/json", json);
    });

    server.on("/api/factory_reset", HTTP_POST, [](AsyncWebServerRequest *req) {
        cfgStore.factoryReset();
        req->send(200, "application/json", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });

    server.begin();
    Serial.printf("[HUB-LIGHT] HTTP server started. Connect to Wi-Fi: %s\n", AP_SSID);
    Serial.printf("[HUB-LIGHT] IP: %s  |  URL: http://%s\n",
                  WiFi.softAPIP().toString().c_str(), DNS_HOSTNAME);
}

// ── Loop ─────────────────────────────────────────────────────
// Kept intentionally lean: only the essential services run here.
// HeartbeatService, StatusLeds, BatteryMonitor and the reset-button
// handler are all omitted to free CPU for telemetry throughput.
void loop() {
    EspNowManager::instance().processDns();
    EspNowManager::instance().tick();
    ws.cleanupClients(4);
    router.tick();
}

#endif // HUB_LIGHT_MODE

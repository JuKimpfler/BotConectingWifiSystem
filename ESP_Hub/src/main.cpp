// ============================================================
//  ESP_Hub/src/main.cpp
//  Boot sequence, WebSocket server, main loop
// ============================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "hub_config.h"
#include "messages.h"
#include "EspNowManager.h"
#include "PeerRegistry.h"
#include "ConfigStore.h"
#include "CommandRouter.h"
#include "TelemetryBuffer.h"
#include "HeartbeatService.h"

// ─── Global objects ───────────────────────────────────────────
AsyncWebServer  server(WS_PORT);
AsyncWebSocket  ws(WS_PATH);
PeerRegistry    peers;
ConfigStore     cfgStore;
HubConfig       hubCfg;
TelemetryBuffer telem;
HeartbeatService heartbeat;
CommandRouter   router;

// ─── ESP-NOW receive callback ─────────────────────────────────
static void onEspNowFrame(const uint8_t *mac, const Frame_t *frame) {
    router.onEspNowFrame(mac, frame);
}

// ─── WebSocket event handler ──────────────────────────────────
static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS] client #%u connected\n", client->id());
        router.broadcastPeerStatus();
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS] client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->opcode == WS_TEXT) {
            router.onWsMessage(data, len);
        }
    }
}

// ─── Setup ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n[HUB] Booting...");

    // Mount filesystem and load config
    cfgStore.begin();
    cfgStore.load(hubCfg, peers);

    // Initialise ESP-NOW
    EspNowManager &espnow = EspNowManager::instance();
    uint8_t pmkBytes[16] = {};
    // PMK_HEX_LEN = 32 hex chars representing 16 key bytes
    const int PMK_HEX_LEN = 32;
    if (strnlen(hubCfg.pmk_hex, PMK_HEX_LEN + 1) == (size_t)PMK_HEX_LEN) {
        ConfigStore::hexToBytes(hubCfg.pmk_hex, pmkBytes, 16);
        espnow.begin(hubCfg.channel, (const char *)pmkBytes);
    } else {
        espnow.begin(hubCfg.channel, nullptr);
    }
    espnow.setRecvCallback(onEspNowFrame);

    // Re-register persisted peers
    const int LTK_HEX_LEN = 32;  // 16 key bytes = 32 hex chars
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

    // Telemetry buffer
    uint32_t telemInterval = 1000 / (hubCfg.telemetry_max_hz > 0 ? hubCfg.telemetry_max_hz : 20);
    telem.begin(telemInterval);

    // Heartbeat service
    heartbeat.begin(hubCfg.heartbeat_interval_ms, hubCfg.heartbeat_timeout_ms);

    // Command router
    router.begin(&ws, &espnow, &peers, &telem, &cfgStore, &hubCfg);

    // WebSocket
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // Serve bundled UI from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Fallback 404
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

    // REST: POST /api/config (receives JSON body)
    server.on("/api/factory_reset", HTTP_POST, [](AsyncWebServerRequest *req) {
        cfgStore.factoryReset();
        req->send(200, "application/json", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });

    server.begin();
    Serial.printf("[HUB] HTTP server started, AP SSID: %s\n", AP_SSID);
    Serial.printf("[HUB] IP: %s  |  URL: http://%s\n",
                  WiFi.softAPIP().toString().c_str(), DNS_HOSTNAME);
}

// ─── Loop ─────────────────────────────────────────────────────
void loop() {
    EspNowManager::instance().processDns();
    ws.cleanupClients(4);
    router.tick();
    heartbeat.tick(peers, EspNowManager::instance());
}

// ╔══════════════════════════════════════════════════════════╗
//   ESP_2 – HUB
//   • ESP-NOW Empfang von ESP_1 (alle 7ms) + ESP_3 (alle 100ms)
//   • ESP-NOW Senden an ESP_1 + ESP_3
//   • Lokaler Webserver (AP-Modus) mit WebSocket
//   • Webinterface zeigt Daten beider Nodes + Texteingabe
// ╚══════════════════════════════════════════════════════════╝

#include <Arduino.h>
#include <ESPAsyncWebServer.h>  // lib: ESPAsyncWebServer-esphome
#include <ArduinoJson.h>        // lib: ArduinoJson

// THIS_DEVICE muss 2 sein → in config.h prüfen
#include "config.h"
#include "espnow_mesh.h"
#include "hub_html.h"

// ══════════════════════════════════════════════════════
//  Webserver + WebSocket
// ══════════════════════════════════════════════════════
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ── WebSocket Event Handler ────────────────────────────
void onWsEvent(AsyncWebSocket       *server,
               AsyncWebSocketClient *client,
               AwsEventType          type,
               void                 *arg,
               uint8_t              *data,
               size_t                len)
{
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS] Client #%u verbunden\n", client->id());

    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS] Client #%u getrennt\n", client->id());

    } else if (type == WS_EVT_DATA) {
        // Nachricht vom Browser → an ESP_1 + ESP_3 weiterleiten
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->opcode == WS_TEXT) {

            // JSON parsen
            StaticJsonDocument<128> doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok)
                return;

            const char *msgType = doc["type"] | "";
            if (strcmp(msgType, "send") != 0) return;

            const char *payload = doc["payload"] | "";
            size_t plen = strnlen(payload, 9);
            if (plen == 0) return;

            // Message aufbauen
            static uint32_t outId = 0;
            Message msg;
            msg.type      = MSG_TYPE_TEXT;
            msg.sender    = 2;            // Hub
            msg.target    = 0;            // 0 = Broadcast
            msg.msgId     = outId++;
            msg.timestamp = millis();
            memset(msg.payload, 0, sizeof(msg.payload));
            strncpy(msg.payload, payload, 8); // max 8 + null

            uint8_t mac1[] = MAC_ESP1;
            uint8_t mac3[] = MAC_ESP3;
            bool ok1 = meshSend(mac1, msg);
            bool ok3 = meshSend(mac3, msg);

            Serial.printf("[WS] Text '%s' → ESP1:%s ESP3:%s\n",
                          msg.payload,
                          ok1 ? "OK" : "FAIL",
                          ok3 ? "OK" : "FAIL");
        }
    }
}

// ── JSON an alle WS-Clients senden ────────────────────
void wsBroadcastMsg(uint8_t from, uint32_t id,
                    const char *payload, uint32_t ts)
{
    if (ws.count() == 0) return;

    StaticJsonDocument<128> doc;
    doc["type"]    = "msg";
    doc["from"]    = from;
    doc["id"]      = id;
    doc["payload"] = payload;
    doc["ts"]      = ts;

    char buf[128];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    ws.textAll(buf, n);
}

void wsBroadcastStats() {
    if (ws.count() == 0) return;

    StaticJsonDocument<128> doc;
    doc["type"] = "stats";
    doc["sent"] = meshStats.sent;
    doc["recv"] = meshStats.received;
    doc["fail"] = meshStats.failed;
    doc["drop"] = meshStats.dropped;

    char buf[128];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    ws.textAll(buf, n);
}

// ══════════════════════════════════════════════════════
//  Empfangene ESP-NOW Nachrichten verarbeiten
// ══════════════════════════════════════════════════════
void processIncoming() {
    Message msg;
    while (xQueueReceive(rxQueue, &msg, 0) == pdTRUE) {

        // Payload sichern (null-terminated)
        char safe[10];
        memcpy(safe, msg.payload, 9);
        safe[9] = '\0';

        uint32_t latency = millis() - msg.timestamp;

        Serial.printf("[RX] ESP%d → Hub | ID:%lu | Typ:%d | "
                      "Payload:'%s' | Latenz:%lums\n",
                      msg.sender, msg.msgId,
                      msg.type, safe, latency);

        // Nur Nachrichten von ESP_1 und ESP_3 anzeigen
        if (msg.sender == 1 || msg.sender == 3) {
            wsBroadcastMsg(msg.sender, msg.msgId, safe, msg.timestamp);
        }
    }
}

// ══════════════════════════════════════════════════════
//  Eigene zyklische Nachrichten des Hubs
// ══════════════════════════════════════════════════════
static uint32_t lastSendToESP1  = 0;
static uint32_t lastSendToESP3  = 0;
static uint32_t lastStats       = 0;
static uint32_t hubMsgCounter   = 0;

void sendCyclicMessages() {
    uint32_t now = millis();
    uint8_t mac1[] = MAC_ESP1;
    uint8_t mac3[] = MAC_ESP3;

    // ── Schnell: Hub → ESP_1 (alle 7ms) ──────────────
    if (now - lastSendToESP1 >= INTERVAL_FAST_MS) {
        lastSendToESP1 = now;

        Message msg;
        msg.type      = MSG_TYPE_DATA;
        msg.sender    = 2;
        msg.target    = 1;
        msg.msgId     = hubMsgCounter++;
        msg.timestamp = now;
        memset(msg.payload, 0, 9);
        snprintf(msg.payload, 9, "H%07lu", hubMsgCounter % 10000000UL);

        meshSend(mac1, msg);
    }

    // ── Langsam: Hub → ESP_3 (alle 100ms) ────────────
    if (now - lastSendToESP3 >= INTERVAL_SLOW_MS) {
        lastSendToESP3 = now;

        Message msg;
        msg.type      = MSG_TYPE_EVENT;
        msg.sender    = 2;
        msg.target    = 3;
        msg.msgId     = hubMsgCounter++;
        msg.timestamp = now;
        memset(msg.payload, 0, 9);
        snprintf(msg.payload, 9, "E%07lu", hubMsgCounter % 10000000UL);

        meshSend(mac3, msg);
    }

    // ── Statistiken alle 5s ────────────────────────
    if (now - lastStats >= STATS_INTERVAL_MS) {
        lastStats = now;
        wsBroadcastStats();
        Serial.printf("[STATS] TX:%lu RX:%lu FAIL:%lu DROP:%lu | "
                      "WS-Clients:%u | AP-Clients:%d\n",
                      meshStats.sent, meshStats.received,
                      meshStats.failed, meshStats.dropped,
                      ws.count(), WiFi.softAPgetStationNum());
    }
}

// ══════════════════════════════════════════════════════
//  Setup
// ══════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP_2 HUB startet ===");

    // 1) Mesh initialisieren (AP + ESP-NOW)
    if (!initMeshHub()) {
        Serial.println("KRITISCHER FEHLER: Mesh-Init fehlgeschlagen!");
        while (true) delay(1000);
    }

    // 2) WebSocket registrieren
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // 3) Root → HTML ausliefern
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", HUB_HTML);
    });

    // 4) 404
    server.onNotFound([](AsyncWebServerRequest *req) {
        req->send(404, "text/plain", "Not Found");
    });

    // 5) Server starten
    server.begin();
    Serial.printf("[HTTP] Webserver gestartet auf http://%s\n", AP_IP);
    Serial.printf("[HTTP] WLAN: SSID='%s' PW='%s'\n", AP_SSID, AP_PASSWORD);
    Serial.println("\n► Browser öffnen und zu http://192.168.4.1 navigieren");
}

// ══════════════════════════════════════════════════════
//  Loop
// ══════════════════════════════════════════════════════
void loop() {
    // WebSocket Cleanup (dead clients entfernen)
    ws.cleanupClients();

    // ESP-NOW Empfang verarbeiten
    processIncoming();

    // Zyklische Nachrichten senden
    sendCyclicMessages();

    // Kurze Pause – verhindert Watchdog-Reset, gibt anderen Tasks Luft
    yield();
}

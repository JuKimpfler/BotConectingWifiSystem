// ╔══════════════════════════════════════════════════════════╗
//   ESP_1 – NODE
//   • Sendet alle 7ms an Hub (ESP_2)
//   • Empfängt von Hub + ESP_3
//   • Verarbeitet MSG_TYPE_TEXT → gibt Webinterface-Text aus
// ╚══════════════════════════════════════════════════════════╝

// config.h: #define THIS_DEVICE 1

#include <Arduino.h>
#include <esp_wifi.h>
#include "config.h"
#include "espnow_mesh.h"

uint8_t mac2[] = MAC_ESP2;
uint8_t mac3[] = MAC_ESP3;

static uint32_t msgCounter    = 0;
static uint32_t lastSendHub   = 0;
static uint32_t lastSendESP3  = 0;
static uint32_t lastStats     = 0;

// ── Nachricht aufbauen ─────────────────────────────────
Message buildMsg(uint8_t target, uint8_t type, const char *data = nullptr) {
    Message msg;
    msg.type      = type;
    msg.sender    = 1;
    msg.target    = target;
    msg.msgId     = msgCounter++;
    msg.timestamp = millis();
    memset(msg.payload, 0, sizeof(msg.payload));

    if (data) {
        strncpy(msg.payload, data, 8);
    } else {
        // Standard: Analogwert als kompakter String
        snprintf(msg.payload, 9, "A%04d", analogRead(A0) & 0xFFFF);
    }
    return msg;
}

// ── Empfang verarbeiten ────────────────────────────────
void processIncoming() {
    Message msg;
    while (xQueueReceive(rxQueue, &msg, 0) == pdTRUE) {
        char safe[10];
        memcpy(safe, msg.payload, 9);
        safe[9] = '\0';

        if (msg.type == MSG_TYPE_TEXT) {
            // Webinterface-Nachricht
            Serial.printf("[WEB→ESP1] '%s'\n", safe);
            // Hier eigene Aktion einfügen (z.B. LED steuern, Relais, etc.)

        } else {
            Serial.printf("[RX] Von ESP%d | ID:%lu | '%s'\n",
                          msg.sender, msg.msgId, safe);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== ESP_1 startet ===");
    initMeshNode();
}

void loop() {
    uint32_t now = millis();

    // ── Schnell: ESP1 → Hub (alle 7ms) ──────────────
    if (now - lastSendHub >= INTERVAL_FAST_MS) {
        lastSendHub = now;
        Message msg = buildMsg(2, MSG_TYPE_DATA);
        meshSend(mac2, msg);
    }

    // ── Gelegentlich: ESP1 → ESP3 (alle 100ms) ──────
    if (now - lastSendESP3 >= INTERVAL_SLOW_MS) {
        lastSendESP3 = now;
        Message msg = buildMsg(3, MSG_TYPE_EVENT);
        meshSend(mac3, msg);
    }

    processIncoming();

    if (now - lastStats >= STATS_INTERVAL_MS) {
        lastStats = now;
        Serial.printf("[STATS] TX:%lu RX:%lu FAIL:%lu\n",
                      meshStats.sent, meshStats.received, meshStats.failed);
    }

    yield();
}

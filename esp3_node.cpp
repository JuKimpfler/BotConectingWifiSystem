// ╔══════════════════════════════════════════════════════════╗
//   ESP_3 – NODE
//   • Sendet alle 100ms an Hub (ESP_2) und ESP_1
//   • Empfängt von Hub + ESP_1
//   • Verarbeitet MSG_TYPE_TEXT → gibt Webinterface-Text aus
// ╚══════════════════════════════════════════════════════════╝

// config.h: #define THIS_DEVICE 3

#include <Arduino.h>
#include <esp_wifi.h>
#include "config.h"
#include "espnow_mesh.h"

uint8_t mac1[] = MAC_ESP1;
uint8_t mac2[] = MAC_ESP2;

static uint32_t msgCounter   = 0;
static uint32_t lastSendHub  = 0;
static uint32_t lastSendESP1 = 0;
static uint32_t lastStats    = 0;

Message buildMsg(uint8_t target, uint8_t type, const char *data = nullptr) {
    Message msg;
    msg.type      = type;
    msg.sender    = 3;
    msg.target    = target;
    msg.msgId     = msgCounter++;
    msg.timestamp = millis();
    memset(msg.payload, 0, sizeof(msg.payload));

    if (data) {
        strncpy(msg.payload, data, 8);
    } else {
        snprintf(msg.payload, 9, "B%04d", analogRead(A0) & 0xFFFF);
    }
    return msg;
}

void processIncoming() {
    Message msg;
    while (xQueueReceive(rxQueue, &msg, 0) == pdTRUE) {
        char safe[10];
        memcpy(safe, msg.payload, 9);
        safe[9] = '\0';

        if (msg.type == MSG_TYPE_TEXT) {
            Serial.printf("[WEB→ESP3] '%s'\n", safe);
            // Eigene Aktion hier
        } else {
            Serial.printf("[RX] Von ESP%d | ID:%lu | '%s'\n",
                          msg.sender, msg.msgId, safe);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== ESP_3 startet ===");
    initMeshNode();
}

void loop() {
    uint32_t now = millis();

    // ESP3 → Hub (alle 100ms)
    if (now - lastSendHub >= INTERVAL_SLOW_MS) {
        lastSendHub = now;
        Message msg = buildMsg(2, MSG_TYPE_DATA);
        meshSend(mac2, msg);
    }

    // ESP3 → ESP1 (alle 100ms, versetzt)
    if (now - lastSendESP1 >= INTERVAL_SLOW_MS + 50) {
        lastSendESP1 = now;
        Message msg = buildMsg(1, MSG_TYPE_EVENT);
        meshSend(mac1, msg);
    }

    processIncoming();

    if (now - lastStats >= STATS_INTERVAL_MS) {
        lastStats = now;
        Serial.printf("[STATS] TX:%lu RX:%lu FAIL:%lu\n",
                      meshStats.sent, meshStats.received, meshStats.failed);
    }

    yield();
}

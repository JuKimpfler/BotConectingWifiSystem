#pragma once
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"

// ══════════════════════════════════════════════════════
//  Globale Variablen
// ══════════════════════════════════════════════════════
QueueHandle_t rxQueue;

volatile bool  sendBusy        = false;
volatile bool  lastSendSuccess = false;

struct MeshStats {
    uint32_t sent;
    uint32_t received;
    uint32_t failed;
    uint32_t dropped;
} meshStats = {0, 0, 0, 0};

// ══════════════════════════════════════════════════════
//  Callbacks
// ══════════════════════════════════════════════════════
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    sendBusy = false;
    if (status == ESP_NOW_SEND_SUCCESS) {
        meshStats.sent++;
        lastSendSuccess = true;
    } else {
        meshStats.failed++;
        lastSendSuccess = false;
        Serial.printf("[MESH] Send FAILED zu %02X:%02X:%02X\n",
                      mac[3], mac[4], mac[5]);
    }
}

void onDataRecv(const esp_now_recv_info_t *info,
                const uint8_t *data, int len) {
    if (len != sizeof(Message)) {
        Serial.printf("[MESH] Ungültige Paketgröße: %d\n", len);
        return;
    }

    Message msg;
    memcpy(&msg, data, sizeof(Message));
    meshStats.received++;

    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(rxQueue, &msg, &woken) != pdTRUE) {
        meshStats.dropped++;
        Serial.println("[MESH] RX-Queue VOLL – Paket verworfen!");
    }
    portYIELD_FROM_ISR(woken);
}

// ══════════════════════════════════════════════════════
//  Peer hinzufügen
// ══════════════════════════════════════════════════════
bool addPeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = AP_CHANNEL;
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_AP;  // Hub nutzt AP-Interface

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        Serial.printf("[MESH] addPeer FAILED: %d\n", err);
        return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════
//  Nachricht senden (mit Busy-Guard)
// ══════════════════════════════════════════════════════
bool meshSend(const uint8_t *targetMac, Message &msg) {
    uint32_t deadline = millis() + 50;
    while (sendBusy && millis() < deadline) {
        delayMicroseconds(200);
    }
    if (sendBusy) {
        meshStats.failed++;
        return false;
    }

    sendBusy = true;
    esp_err_t err = esp_now_send(targetMac, (uint8_t *)&msg, sizeof(Message));
    if (err != ESP_OK) {
        sendBusy = false;
        meshStats.failed++;
        Serial.printf("[MESH] esp_now_send Fehler: %d\n", err);
        return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════
//  Initialisierung (nur für ESP_2 / Hub mit AP-Modus)
// ══════════════════════════════════════════════════════
bool initMeshHub() {
    rxQueue = xQueueCreate(RX_QUEUE_SIZE, sizeof(Message));
    if (!rxQueue) {
        Serial.println("[MESH] Queue-Erstellung fehlgeschlagen!");
        return false;
    }

    // AP + STA Dual-Mode → ESP-NOW läuft über AP-Interface
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);

    IPAddress ip, gw, sn;
    ip.fromString(AP_IP);
    gw.fromString(AP_IP);
    sn.fromString("255.255.255.0");
    WiFi.softAPConfig(ip, gw, sn);

    delay(200);
    Serial.printf("[MESH] AP gestartet: %s | IP: %s | Kanal: %d\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str(), AP_CHANNEL);
    Serial.printf("[MESH] Hub-MAC: %s\n", WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[MESH] esp_now_init FEHLGESCHLAGEN!");
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    uint8_t mac1[] = MAC_ESP1;
    uint8_t mac3[] = MAC_ESP3;
    addPeer(mac1);
    addPeer(mac3);

    Serial.println("[MESH] Initialisierung abgeschlossen ✓");
    return true;
}

// ══════════════════════════════════════════════════════
//  Initialisierung für ESP_1 und ESP_3 (STA-Modus)
// ══════════════════════════════════════════════════════
bool initMeshNode() {
    rxQueue = xQueueCreate(RX_QUEUE_SIZE, sizeof(Message));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Auf AP-Kanal des Hubs setzen
    esp_wifi_set_channel(AP_CHANNEL, WIFI_SECOND_CHAN_NONE);
    delay(100);

    Serial.printf("[MESH] Node-MAC: %s\n", WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[MESH] esp_now_init FEHLGESCHLAGEN!");
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    // Peers als STA-Interface registrieren
    uint8_t mac1[] = MAC_ESP1;
    uint8_t mac2[] = MAC_ESP2;
    uint8_t mac3[] = MAC_ESP3;

    #if THIS_DEVICE != 1
        {
            esp_now_peer_info_t p = {};
            memcpy(p.peer_addr, mac1, 6);
            p.channel = AP_CHANNEL;
            p.ifidx   = WIFI_IF_STA;
            esp_now_add_peer(&p);
        }
    #endif
    #if THIS_DEVICE != 2
        {
            esp_now_peer_info_t p = {};
            memcpy(p.peer_addr, mac2, 6);
            p.channel = AP_CHANNEL;
            p.ifidx   = WIFI_IF_STA;
            esp_now_add_peer(&p);
        }
    #endif
    #if THIS_DEVICE != 3
        {
            esp_now_peer_info_t p = {};
            memcpy(p.peer_addr, mac3, 6);
            p.channel = AP_CHANNEL;
            p.ifidx   = WIFI_IF_STA;
            esp_now_add_peer(&p);
        }
    #endif

    Serial.println("[MESH] Node bereit ✓");
    return true;
}

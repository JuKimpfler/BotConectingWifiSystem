#pragma once
#include <Arduino.h>

// ╔══════════════════════════════════════════════════════╗
//  MAC-Adressen – mit Serial.println(WiFi.macAddress())
//  auslesen und hier eintragen
// ╚══════════════════════════════════════════════════════╝
#define MAC_ESP1  {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x01}
#define MAC_ESP2  {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x02}  // HUB
#define MAC_ESP3  {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x03}

// ── Gerätekonfiguration ────────────────────────────────
// Welches Gerät ist das? (1, 2 oder 3)
// ESP_2 = HUB mit Webserver
#define THIS_DEVICE  2

// ── WLAN Access Point (nur ESP_2 / Hub) ───────────────
#define AP_SSID     "ESP-Mesh-Hub"
#define AP_PASSWORD "mesh1234"
#define AP_CHANNEL  6          // WICHTIG: muss mit ESP-NOW Kanal übereinstimmen
#define AP_IP       "192.168.4.1"

// ── Nachrichtentypen ───────────────────────────────────
#define MSG_TYPE_DATA    0x01  // Sensor-/Nutzdaten
#define MSG_TYPE_ACK     0x02  // Bestätigung
#define MSG_TYPE_TEXT    0x03  // Text vom Webinterface
#define MSG_TYPE_EVENT   0x04  // Ereignismeldung

// ── Timing ────────────────────────────────────────────
#define INTERVAL_FAST_MS    7    // ESP1 <-> ESP2
#define INTERVAL_SLOW_MS    100  // ESP3 Nachrichten
#define STATS_INTERVAL_MS   5000

// ── Puffer ────────────────────────────────────────────
#define RX_QUEUE_SIZE   30
#define WS_BUFFER_SIZE  512

// ── Nachrichtenstruktur (20 Bytes, packed) ─────────────
struct __attribute__((packed)) Message {
    uint8_t  type;        // 1 Byte  — MSG_TYPE_*
    uint8_t  sender;      // 1 Byte  — Absender (1/2/3)
    uint8_t  target;      // 1 Byte  — Ziel (1/2/3 / 0=alle)
    uint32_t msgId;       // 4 Bytes — fortlaufende ID
    uint32_t timestamp;   // 4 Bytes — millis()
    char     payload[9];  // 9 Bytes — Text / Nutzdaten (null-terminated)
};
// Größe prüfen: static_assert(sizeof(Message) == 20, "Message != 20 Bytes");

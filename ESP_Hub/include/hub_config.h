#pragma once
// ============================================================
//  ESP_Hub/include/hub_config.h
//  Compile-time defaults and tuneable constants for the Hub
// ============================================================

#include <Arduino.h>

// ── Hardware ─────────────────────────────────────────────────
#define PIN_LED_STATUS       10   // Seeed XIAO ESP32-C3 onboard LED
#define PIN_LED_POWER        PIN_LED_STATUS
#define PIN_LED_BAT_LOW      D2   // External LED: steady=low battery, blink=charging
#define PIN_LED_WEBSERVER    D3
#define PIN_LED_SAT1         D4
#define PIN_LED_SAT2         D5
#define PIN_BTN_RESET        D6   // Active-low reset button to GND
#define PIN_CHARGE_STATUS    D7   // Optional: charger STAT (active low)
#define PIN_BATTERY_SENSE    A1   // Analog battery divider input

#define BATTERY_VDIVIDER           2.0f   // Voltage divider factor (e.g. 100k/100k -> 2.0)
#define BATTERY_LOW_MV             3600   // LED on when battery below this (mV)
#define BATTERY_SAMPLE_INTERVAL_MS 5000
#define BATTERY_CHARGE_BLINK_MS     500

// ── WiFi / ESP-NOW ────────────────────────────────────────────
#define DEFAULT_CHANNEL      6
#define AP_SSID              "ESP-Hub"
#define AP_PASSWORD          "hub12345"
#define DNS_HOSTNAME         "esp.hub"

// ── Config file in LittleFS ───────────────────────────────────
#define CONFIG_FILE          "/config.json"

// ── WebSocket ─────────────────────────────────────────────────
#define WS_PORT              80
#define WS_PATH              "/ws"

// ── Telemetry throttle ────────────────────────────────────────
#define TELEMETRY_MAX_HZ     20
#define TELEMETRY_MIN_INTERVAL_MS  (1000 / TELEMETRY_MAX_HZ)

// ── Heartbeat ─────────────────────────────────────────────────
#define HEARTBEAT_INTERVAL_MS   1000
#define HEARTBEAT_TIMEOUT_MS    4000

// ── ACK timeout ───────────────────────────────────────────────
#define ACK_TIMEOUT_MS       500
#define ACK_MAX_RETRIES      3

// ── ESP-NOW ───────────────────────────────────────────────────
#define ESPNOW_MAX_PAYLOAD   180

// ── Anti-mis-pairing: system / network identity ───────────────
// The network_id is carried in every frame's reserved byte.
// Only frames whose network_id matches HUB_NETWORK_ID (or is 0x00 = legacy)
// are accepted. Devices of other BotConnectingWifiSystem systems in range
// will be silently rejected.
//
// IMPORTANT: Change this value (1–255) to match ESPNOW_NETWORK_ID in
// ESP_Satellite/include/sat_config.h when isolating deployments.
#ifndef HUB_NETWORK_ID
#define HUB_NETWORK_ID       0x01
#endif

#pragma once
// ============================================================
//  ESP_Hub/include/hub_config.h
//  Compile-time defaults and tuneable constants for the Hub
// ============================================================

#include <Arduino.h>

// ── Hardware ─────────────────────────────────────────────────
#define PIN_LED_STATUS       10   // Seeed XIAO ESP32-C3 onboard LED

// ── WiFi / ESP-NOW ────────────────────────────────────────────
#define DEFAULT_CHANNEL      6
#define AP_SSID              "ESP-Hub"
#define AP_PASSWORD          "hub12345"

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

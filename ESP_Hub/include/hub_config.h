#pragma once
// ============================================================
//  ESP_Hub/include/hub_config.h
//  Compile-time defaults and tuneable constants for the Hub
//
//  Target selection (set via build_flags in platformio.ini):
//    -DBCWS_TARGET_C6  →  ESP32-C6 (e.g. Seeed XIAO ESP32-C6)
//    (default)         →  ESP32-C3 (e.g. Seeed XIAO ESP32-C3)
//
//  Quick switch example in platformio.ini:
//    build_flags = ... -DBCWS_TARGET_C6
// ============================================================

#include <Arduino.h>

// ── Target auto-detection / explicit override ─────────────────
// Prefer the explicit flag; fall back to IDF target macros so
// firmware compiled with the correct board SDK automatically
// picks the right settings without manual flag changes.
#if defined(BCWS_TARGET_C6)
  // Explicit C6 override – takes priority
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  #define BCWS_TARGET_C6
#else
  #define BCWS_TARGET_C3
#endif

#if defined(BCWS_TARGET_C6)
  #define BCWS_TARGET_NAME  "ESP32-C6"
#else
  #define BCWS_TARGET_NAME  "ESP32-C3"
#endif

// ── Target-specific hardware pins ────────────────────────────
// External LEDs/buttons use the same silkscreen labels (D2-D7, A1)
// on both XIAO boards – the Arduino BSP maps them to the correct
// GPIO numbers per chip.  Only the onboard status-LED differs.
#if defined(BCWS_TARGET_C6)
  // Seeed XIAO ESP32-C6: onboard LED is on GPIO15 (LED_BUILTIN = 15)
  #define PIN_LED_STATUS       15

  // Battery: same external 2:1 voltage divider on A1.
  // ESP32-C6 ADC uses eFuse two-point calibration automatically via
  // analogReadMilliVolts(), giving ~±5 mV accuracy vs ~±30 mV on C3.
  #define PIN_BATTERY_SENSE    A1
  // Calibration hint: C6 eFuse cal is applied transparently by the SDK.
  // Scaling factor and low-threshold remain identical to the C3 setup.
  #define BATTERY_ADC_NOTE     "ESP32-C6: eFuse two-point ADC calibration active"
#else
  // Seeed XIAO ESP32-C3: onboard LED is on GPIO10 (LED_BUILTIN = 10)
  #define PIN_LED_STATUS       10

  // Battery: external 2:1 voltage divider on A1; basic ADC calibration.
  #define PIN_BATTERY_SENSE    A1
  #define BATTERY_ADC_NOTE     "ESP32-C3: basic ADC calibration (attenuation linearisation)"
#endif

// ── Common hardware pins (same silkscreen on C3 and C6) ───────
#define PIN_LED_POWER        PIN_LED_STATUS
#define PIN_LED_BAT_LOW      D2   // External LED: steady=low battery, blink=charging
#define PIN_LED_WEBSERVER    D3
#define PIN_LED_SAT1         D4
#define PIN_LED_SAT2         D5
#define PIN_BTN_RESET        D6   // Active-low reset button to GND
#define PIN_CHARGE_STATUS    D7   // Optional: charger STAT pin (active low)
                                  // C3: D7=GPIO20, C6: D7=GPIO22 (mapped by BSP)

// ── Battery measurement parameters ───────────────────────────
// External voltage divider: R_top = R_bot = 100 kΩ → factor 2.0
// Ensure A1 sees at most 3.3 V (i.e. battery ≤ 6.6 V with 2:1 divider).
// Recalibrate BATTERY_VDIVIDER if you use different resistor values.
#define BATTERY_VDIVIDER           2.0f   // Voltage divider factor (100k/100k → 2.0)
#define BATTERY_LOW_MV             3600   // mV: battery-low LED threshold
#define BATTERY_SAMPLE_INTERVAL_MS 5000
#define BATTERY_CHARGE_BLINK_MS     500

// ── WiFi / ESP-NOW ────────────────────────────────────────────
#define DEFAULT_CHANNEL      6
// AP_SSID, AP_PASSWORD and DNS_HOSTNAME are wrapped in #ifndef so they can be
// overridden at compile time via build_flags (e.g. for the light mode environment).
#ifndef AP_SSID
#define AP_SSID              "ESP-Hub"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD          "hub12345"
#endif
#ifndef DNS_HOSTNAME
#define DNS_HOSTNAME         "esp.hub"
#endif

// ── Config file in LittleFS ───────────────────────────────────
#define CONFIG_FILE          "/config.json"
#ifndef HUB_CONFIG_DEFAULT_JSON
#define HUB_CONFIG_DEFAULT_JSON ""
#endif

// ── WebSocket ─────────────────────────────────────────────────
#define WS_PORT              80
#define WS_PATH              "/ws"

// ── Telemetry throttle ────────────────────────────────────────
// TELEMETRY_MAX_HZ can be overridden via build_flags for the light environment.
#ifndef TELEMETRY_MAX_HZ
#define TELEMETRY_MAX_HZ     20
#endif
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

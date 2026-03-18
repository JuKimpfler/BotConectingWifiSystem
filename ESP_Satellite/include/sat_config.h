#pragma once
// ============================================================
//  ESP_Satellite/include/sat_config.h
//  Compile-time and runtime constants for satellite firmware
// ============================================================

#ifdef ARDUINO
#include <Arduino.h>
#endif

// SAT_ID must be 1 or 2, set via -DSAT_ID=n in platformio.ini
#ifndef SAT_ID
  #error "SAT_ID must be defined (1 or 2)"
#endif

// ── Hardware ─────────────────────────────────────────────────
#define PIN_LED_STATUS       10   // Seeed XIAO onboard LED
#define HW_UART_RX_PIN       20   // D7 on XIAO ESP32-C3
#define HW_UART_TX_PIN       21   // D6 on XIAO ESP32-C3
#define HW_UART_BAUD         115200

// ── ESP-NOW ───────────────────────────────────────────────────
#define DEFAULT_CHANNEL      6
#define ESPNOW_MAX_PAYLOAD   180

// ── Timing ───────────────────────────────────────────────────
#define P2P_INTERVAL_MS      7    // SAT1 <-> SAT2 fast bridge
#define HUB_INTERVAL_MS      100  // Hub command poll interval
#define HEARTBEAT_INTERVAL_MS 1000
#define HEARTBEAT_TIMEOUT_MS  4000
#define ACK_TIMEOUT_MS        500
#define ACK_MAX_RETRIES       3

// ── UART buffer ───────────────────────────────────────────────
#define UART_RX_BUF_SIZE     512
#define UART_TX_BUF_SIZE     256

// ── DBG prefix for Teensy communication ──────────────────────
// Prefixes are defined in shared/messages.h (DBG_PREFIX_SAT1/SAT2)

// ── NVS ──────────────────────────────────────────────────────
#define NVS_NAMESPACE        "satcfg"
#define NVS_KEY_HUB_MAC      "hub_mac"
#define NVS_KEY_PEER_MAC     "peer_mac"
#define NVS_KEY_CHANNEL      "channel"

// ── UART Bridge to USB (USB-only mode) ────────────────────────
// When UART_BRIDGE_USB is defined:
//   • HW UART TX/RX is disabled.
//   • All UART payload traffic is routed via USB Serial instead.
//   • USB debug output is suppressed.
// Without this flag, HW UART communicates normally with the Teensy and
// USB Serial mirrors UART payload traffic plus lightweight debug output.
// Enable by adding -DUART_BRIDGE_USB to build_flags in platformio.ini,
// or by using the esp_sat1_usb_bridge / esp_sat2_usb_bridge environments.
// #define UART_BRIDGE_USB

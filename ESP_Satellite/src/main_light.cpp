// ============================================================
//  ESP_Satellite/src/main_light.cpp
//  LIGHT TELEMETRY-ONLY VERSION
//  Optimized for maximum telemetry throughput and smooth plotting
//  NO P2P / NO CONTROL / NO MODES / NO LEDs / NO BUTTONS
//  Software compatible with existing hub (hub unchanged)
// ============================================================

#include <Arduino.h>
#include <Preferences.h>
#include "sat_config.h"
#include "messages.h"
#include "crc16.h"
#include "EspNowBridge.h"

// ─── MAC addresses (loaded from NVS or set via Serial config) ─
static uint8_t g_hubMac[6]  = {0};
static uint8_t g_channel    = DEFAULT_CHANNEL;
static bool    g_hubKnown   = false;

// ─── Global objects ───────────────────────────────────────────
static uint8_t g_seq = 0;

// UART1 for Teensy
HardwareSerial TeensySerial(1);

// ── Heartbeat tracking ────────────────────────────────────────
static uint32_t g_lastHubHeartbeat = 0;
static uint32_t g_lastHbSent = 0;
static bool     g_hubOnline  = false;

// ── Telemetry optimization for high-rate smooth plotting ──────
#define LIGHT_TELEM_MAX_STREAMS 64  // Increased from 32
#define LIGHT_TELEM_BATCH_SIZE  20  // Increased from 16 for better packing

struct TelemetryStreamMapEntry {
    char    name[TELEM_NAME_MAX_LEN];
    uint8_t id;
    bool    used;
    bool    announced;
};

static TelemetryStreamMapEntry g_telemStreamMap[LIGHT_TELEM_MAX_STREAMS] = {};
static uint8_t g_telemStreamCount = 0;
static TelemetryCompactValue_t g_telemQueue[LIGHT_TELEM_BATCH_SIZE] = {};
static uint8_t g_telemQueueLen = 0;
static uint32_t g_lastTelemFlush = 0;

// Performance counters
static uint32_t g_telemSentCount = 0;
static uint32_t g_telemDropCount = 0;

static int findTelemetryStreamByName(const char *name) {
    if (!name || name[0] == '\0') return -1;
    for (int i = 0; i < g_telemStreamCount; i++) {
        if (g_telemStreamMap[i].used &&
            strncmp(g_telemStreamMap[i].name, name, sizeof(g_telemStreamMap[i].name)) == 0) {
            return i;
        }
    }
    return -1;
}

static void resetTelemAnnouncedFlags() {
    for (int i = 0; i < g_telemStreamCount; i++) {
        g_telemStreamMap[i].announced = false;
    }
}

static int ensureTelemetryStream(const char *name) {
    int idx = findTelemetryStreamByName(name);
    if (idx >= 0) return idx;
    if (g_telemStreamCount >= LIGHT_TELEM_MAX_STREAMS) return -1;
    idx = g_telemStreamCount++;
    memset(&g_telemStreamMap[idx], 0, sizeof(g_telemStreamMap[idx]));
    g_telemStreamMap[idx].used = true;
    g_telemStreamMap[idx].id   = (uint8_t)idx;
    strlcpy(g_telemStreamMap[idx].name, name, sizeof(g_telemStreamMap[idx].name));
    return idx;
}

static bool sendTelemetryDict(uint8_t streamId) {
    if (!g_hubKnown) return false;
    if (streamId >= g_telemStreamCount) return false;
    TelemetryStreamMapEntry *entry = &g_telemStreamMap[streamId];
    if (!entry->used || entry->announced) return true;

    TelemetryDictPayload_t dict = {};
    dict.stream_id = streamId;
    strlcpy(dict.name, entry->name, sizeof(dict.name));

    Frame_t frame = {};
    frame.magic      = FRAME_MAGIC;
    frame.msg_type   = MSG_TELEM_DICT;
    frame.seq        = g_seq++;
    frame.src_role   = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
    frame.dst_role   = ROLE_HUB;
    frame.flags      = 0;
    frame.network_id = ESPNOW_NETWORK_ID;
    frame.len        = sizeof(dict);
    memcpy(frame.payload, &dict, sizeof(dict));

    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + frame.len);
    memcpy(frame.payload + frame.len, &crc, 2);

    bool ok = EspNowBridge::instance().send(g_hubMac, &frame);
    if (ok) entry->announced = true;
    return ok;
}

// Optimized flush interval for ultra-smooth plotting
// Much more aggressive than standard version
static uint32_t telemetryFlushIntervalMs(uint8_t pendingValues) {
    // Light version: optimize for MINIMUM latency and MAXIMUM update rate
    // Goal: super smooth plotter without lag
    if (pendingValues <= 2) return 2;   // Ultra-low latency for sparse data
    if (pendingValues <= 5) return 3;   // Still very fast
    if (pendingValues <= 10) return 5;  // Balance speed and packing
    if (pendingValues <= 15) return 7;  // Good packing efficiency
    return 10;  // Maximum interval even when queue is full
}

static bool flushTelemetryQueue(bool force) {
    if (!g_hubKnown || g_telemQueueLen == 0) return false;
    uint32_t now = millis();
    uint32_t minInterval = telemetryFlushIntervalMs(g_telemQueueLen);
    if (!force && (uint32_t)(now - g_lastTelemFlush) < minInterval) return false;

    Frame_t frame = {};
    frame.magic      = FRAME_MAGIC;
    frame.msg_type   = MSG_TELEM_BATCH;
    frame.seq        = g_seq++;
    frame.src_role   = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
    frame.dst_role   = ROLE_HUB;
    frame.flags      = 0;
    frame.network_id = ESPNOW_NETWORK_ID;
    frame.len        = g_telemQueueLen * sizeof(TelemetryCompactValue_t);
    memcpy(frame.payload, g_telemQueue, frame.len);

    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + frame.len);
    memcpy(frame.payload + frame.len, &crc, 2);

    bool ok = EspNowBridge::instance().send(g_hubMac, &frame);
    if (ok) {
        g_telemSentCount += g_telemQueueLen;
        g_telemQueueLen = 0;
        g_lastTelemFlush = now;
    }
    return ok;
}

static bool forwardTelemetryLine(const char *line) {
    if (!line) return false;

    // Parse telemetry line: DBG:name=value
    if (strncmp(line, DBG_PREFIX, strlen(DBG_PREFIX)) != 0) return false;

    const char *payload = line + strlen(DBG_PREFIX);
    char name[TELEM_NAME_MAX_LEN] = {0};
    char valueStr[32] = {0};

    const char *eq = strchr(payload, '=');
    if (!eq) {
        return false;
    }

    size_t nameLen = (size_t)(eq - payload);
    if (nameLen == 0 || nameLen >= TELEM_NAME_MAX_LEN) {
        return false;
    }

    strncpy(name, payload, nameLen);
    name[nameLen] = '\0';

    strncpy(valueStr, eq + 1, sizeof(valueStr) - 1);
    valueStr[sizeof(valueStr) - 1] = '\0';

    if (!g_hubKnown) {
        g_telemDropCount++;
        return false;
    }

    int streamIdx = ensureTelemetryStream(name);
    if (streamIdx < 0) {
        g_telemDropCount++;
        return false;
    }

    if (!sendTelemetryDict((uint8_t)streamIdx)) {
        return false;
    }

    // If queue is full, flush immediately
    if (g_telemQueueLen >= LIGHT_TELEM_BATCH_SIZE) {
        flushTelemetryQueue(true);
        if (g_telemQueueLen >= LIGHT_TELEM_BATCH_SIZE) {
            g_telemDropCount++;
            return false;
        }
    }

    TelemetryCompactValue_t &v = g_telemQueue[g_telemQueueLen];
    v.stream_id = (uint8_t)streamIdx;

    // Detect type and encode value
    if (strchr(valueStr, '.') != nullptr) {
        // Float
        v.vtype = 1;
        float f = atof(valueStr);
        memcpy(&v.raw, &f, sizeof(v.raw));
    } else {
        // Integer or bool
        int32_t i = atol(valueStr);
        if (i == 0 || i == 1) {
            v.vtype = 2;  // bool
        } else {
            v.vtype = 0;  // int32
        }
        v.raw = i;
    }

    g_telemQueueLen++;
    flushTelemetryQueue(false);  // Try to flush (respects interval)

    return true;
}

// ─── Load config from NVS ────────────────────────────────────
static void loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    g_channel = (uint8_t)prefs.getUInt(NVS_KEY_CHANNEL, DEFAULT_CHANNEL);

    size_t n = prefs.getBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
    g_hubKnown = (n == 6);

    prefs.end();
}

// ─── Build and send a frame helper ───────────────────────────
static bool sendFrame(const uint8_t *mac, uint8_t msgType,
                      const uint8_t *payload, uint8_t payLen,
                      uint8_t flags = 0) {
    Frame_t frame = {};
    frame.magic      = FRAME_MAGIC;
    frame.msg_type   = msgType;
    frame.seq        = g_seq++;
    frame.src_role   = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
    frame.dst_role   = ROLE_HUB;
    frame.flags      = flags;
    frame.network_id = ESPNOW_NETWORK_ID;
    frame.len        = payLen;
    if (payLen > 0 && payload) {
        if (payLen > FRAME_MAX_PAYLOAD) return false;
        memcpy(frame.payload, payload, payLen);
    }
    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + payLen);
    memcpy(frame.payload + payLen, &crc, 2);

    return EspNowBridge::instance().send(mac, &frame);
}

// ─── ESP-NOW receive callback ─────────────────────────────────
static void onFrame(const uint8_t *mac, const Frame_t *frame) {
    if (frame->msg_type == MSG_HEARTBEAT) {
        // Hub heartbeat received
        if (frame->src_role == ROLE_HUB) {
            bool wasOnline = g_hubOnline;
            g_lastHubHeartbeat = millis();
            g_hubOnline = true;

            if (!wasOnline) {
                resetTelemAnnouncedFlags();
            }

            // Always update hub MAC when it changes
            bool macChanged = memcmp(g_hubMac, mac, 6) != 0;
            if (!g_hubKnown || macChanged) {
                if (g_hubKnown && macChanged) {
                    EspNowBridge::instance().removePeer(g_hubMac);
                    resetTelemAnnouncedFlags();
                }
                memcpy(g_hubMac, mac, 6);
                g_hubKnown = true;
                EspNowBridge::instance().addPeer(g_hubMac);
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
                prefs.end();
            }
        }
    } else if (frame->msg_type == MSG_DISCOVERY) {
        const DiscoveryPayload_t *disc =
            reinterpret_cast<const DiscoveryPayload_t *>(frame->payload);

        // Validate system identity
        uint8_t src_nid = frame->network_id;
        if (src_nid != 0x00 && ESPNOW_NETWORK_ID != 0x00 &&
            src_nid != (uint8_t)ESPNOW_NETWORK_ID) {
            return;
        }

        if (disc->action == 0 && frame->src_role == ROLE_HUB) {
            // Discovery request from hub
            bool macChanged = memcmp(g_hubMac, mac, 6) != 0;
            if (!g_hubKnown || macChanged) {
                if (g_hubKnown && macChanged) {
                    EspNowBridge::instance().removePeer(g_hubMac);
                }
                memcpy(g_hubMac, mac, 6);
                g_hubKnown = true;
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
                prefs.end();
            }

            EspNowBridge::instance().addPeer(mac);

            // Announce ourselves
            DiscoveryPayload_t resp = {};
            resp.action  = 1;
            resp.role    = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
            resp.channel = g_channel;
            snprintf(resp.name, sizeof(resp.name), "SAT%d-LIGHT", SAT_ID);
            WiFi.macAddress(resp.mac);
            sendFrame(mac, MSG_DISCOVERY, (const uint8_t *)&resp, sizeof(resp));
        }
    }
    // ALL OTHER MESSAGE TYPES IGNORED IN LIGHT VERSION
}

// ─── Setup ────────────────────────────────────────────────────
void setup() {
    loadConfig();

    // NO LEDs in light version

    TeensySerial.begin(HW_UART_BAUD, SERIAL_8N1, HW_UART_RX_PIN, HW_UART_TX_PIN);

    EspNowBridge &bridge = EspNowBridge::instance();
    bridge.begin(g_channel);
    bridge.setRecvCallback(onFrame);

    // Register hub if known
    if (g_hubKnown) bridge.addPeer(g_hubMac);
}

// ─── Loop ─────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // ── Read from Teensy UART ──────────────────────────────────
    static char uartLine[UART_RX_BUF_SIZE];
    static int  uartIdx = 0;

    while (TeensySerial.available()) {
        char c = (char)TeensySerial.read();
        if (c == '\n' || c == '\r') {
            if (uartIdx > 0) {
                uartLine[uartIdx] = '\0';
                // Only process DBG: telemetry lines
                forwardTelemetryLine(uartLine);
                uartIdx = 0;
            }
        } else {
            if (uartIdx < (int)(sizeof(uartLine) - 1)) {
                uartLine[uartIdx++] = c;
            } else {
                uartIdx = 0;  // Buffer overflow
            }
        }
    }

    // ── Heartbeat to hub ──────────────────────────────────────
    if (g_hubKnown && (now - g_lastHbSent) >= HEARTBEAT_INTERVAL_MS) {
        g_lastHbSent = now;
        HeartbeatPayload_t hb;
        hb.uptime_ms = now;
        hb.rssi      = 0;
        hb.queue_len = g_telemQueueLen;
        sendFrame(g_hubMac, MSG_HEARTBEAT, (const uint8_t *)&hb, sizeof(hb));
    }

    // ── Hub online/offline detection ──────────────────────────
    if (g_hubOnline && g_hubKnown &&
        (now - g_lastHubHeartbeat) > HEARTBEAT_TIMEOUT_MS) {
        g_hubOnline = false;
    }

    // ── Telemetry flush tick (ultra-aggressive) ───────────────
    flushTelemetryQueue(false);

    // ── ESP-NOW tick ──────────────────────────────────────────
    EspNowBridge::instance().tick();
}

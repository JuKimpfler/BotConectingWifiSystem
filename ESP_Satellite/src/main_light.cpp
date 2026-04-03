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

#define LIGHT_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define LIGHT_DEBUG_PRINT(...)  Serial.print(__VA_ARGS__)
#define LIGHT_DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)

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
static uint32_t g_lastStatsReport = 0;

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

    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Flushing telem batch: qlen=%d seq=%d force=%d\n",
                       SAT_ID, g_telemQueueLen, frame.seq, force ? 1 : 0);

    bool ok = EspNowBridge::instance().send(g_hubMac, &frame);
    if (ok) {
        g_telemSentCount += g_telemQueueLen;
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem batch sent OK: %d values, total sent=%lu\n",
                           SAT_ID, g_telemQueueLen, g_telemSentCount);
        g_telemQueueLen = 0;
        g_lastTelemFlush = now;
    } else {
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem batch send FAILED\n", SAT_ID);
    }
    return ok;
}

// ─── USB serial command handler ──────────────────────────────
static char s_serialCmdBuf[64];
static int  s_serialCmdIdx = 0;

static bool forwardTelemetryLine(const char *line) {
    if (!line) return false;

    // Parse telemetry line: DBG:name=value
    if (strncmp(line, DBG_PREFIX, strlen(DBG_PREFIX)) != 0) return false;

    const char *payload = line + strlen(DBG_PREFIX);
    char name[TELEM_NAME_MAX_LEN] = {0};
    char valueStr[32] = {0};

    const char *eq = strchr(payload, '=');
    if (!eq) {
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem parse error: no '=' in line\n", SAT_ID);
        return false;
    }

    size_t nameLen = (size_t)(eq - payload);
    if (nameLen == 0 || nameLen >= TELEM_NAME_MAX_LEN) {
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem parse error: name length %u\n", SAT_ID, (unsigned)nameLen);
        return false;
    }

    strncpy(name, payload, nameLen);
    name[nameLen] = '\0';

    strncpy(valueStr, eq + 1, sizeof(valueStr) - 1);
    valueStr[sizeof(valueStr) - 1] = '\0';

    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem recv: %s=%s\n", SAT_ID, name, valueStr);

    if (!g_hubKnown) {
        g_telemDropCount++;
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem dropped: hub unknown\n", SAT_ID);
        return false;
    }

    int streamIdx = ensureTelemetryStream(name);
    if (streamIdx < 0) {
        g_telemDropCount++;
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem dropped: stream table full\n", SAT_ID);
        return false;
    }

    if (!sendTelemetryDict((uint8_t)streamIdx)) {
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem dict send failed for stream %d\n", SAT_ID, streamIdx);
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
    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telem queued: stream=%d type=%d qlen=%d\n",
                       SAT_ID, streamIdx, v.vtype, g_telemQueueLen);
    flushTelemetryQueue(false);  // Try to flush (respects interval)

    return true;
}

static void handleSerialCmd(const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;

    if (strcasecmp(cmd, "info") == 0 || strcasecmp(cmd, "mac") == 0) {
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Own MAC : %s\n", SAT_ID, WiFi.macAddress().c_str());
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Channel : %u\n", SAT_ID, g_channel);
        if (g_hubKnown) {
            LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Hub MAC : %02X:%02X:%02X:%02X:%02X:%02X\n",
                          SAT_ID,
                          g_hubMac[0], g_hubMac[1], g_hubMac[2],
                          g_hubMac[3], g_hubMac[4], g_hubMac[5]);
        } else {
            LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Hub MAC : unknown\n", SAT_ID);
        }
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Hub online: %s\n", SAT_ID, g_hubOnline ? "yes" : "no");
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Streams: %u/%u\n", SAT_ID, g_telemStreamCount, LIGHT_TELEM_MAX_STREAMS);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Sent: %lu  Dropped: %lu\n", SAT_ID, g_telemSentCount, g_telemDropCount);
    } else if (strcasecmp(cmd, "stats") == 0) {
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] === Performance Stats ===\n", SAT_ID);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Uptime      : %lu ms\n", SAT_ID, (unsigned long)millis());
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Streams     : %u/%u\n", SAT_ID, g_telemStreamCount, LIGHT_TELEM_MAX_STREAMS);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Queue depth : %u/%u\n", SAT_ID, g_telemQueueLen, LIGHT_TELEM_BATCH_SIZE);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Sent values : %lu\n", SAT_ID, g_telemSentCount);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Dropped     : %lu\n", SAT_ID, g_telemDropCount);
        uint32_t rate = (millis() > 0) ? (g_telemSentCount * 1000UL / millis()) : 0;
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Avg rate    : %lu values/sec\n", SAT_ID, rate);
    } else if (strcasecmp(cmd, "clearmac") == 0) {
        g_hubKnown = false;
        memset(g_hubMac, 0, 6);
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.remove(NVS_KEY_HUB_MAC);
        prefs.end();
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Stored Hub MAC cleared\n", SAT_ID);
    } else if (strcasecmp(cmd, "help") == 0) {
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] === LIGHT TELEMETRY-ONLY VERSION ===\n", SAT_ID);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Commands: info | stats | clearmac | help\n", SAT_ID);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Telemetry: DBG:<name>=<value>\n", SAT_ID);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Features: TELEMETRY ONLY\n", SAT_ID);
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Disabled: P2P, Control, Modes, LEDs, Buttons\n", SAT_ID);
    } else if (forwardTelemetryLine(cmd)) {
        // Telemetry injected via USB
    } else {
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Unknown command '%s'. Type 'help'.\n", SAT_ID, cmd);
    }
}

// ─── Load config from NVS ────────────────────────────────────
static void loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    g_channel = (uint8_t)prefs.getUInt(NVS_KEY_CHANNEL, DEFAULT_CHANNEL);

    size_t n = prefs.getBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
    g_hubKnown = (n == 6);

    prefs.end();
    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] ch=%u hub=%s\n",
                  SAT_ID, g_channel,
                  g_hubKnown ? "known" : "unknown");
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
    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Frame rx: type=0x%02X seq=%u src_role=%u\n",
                       SAT_ID, frame->msg_type, frame->seq, frame->src_role);

    if (frame->msg_type == MSG_HEARTBEAT) {
        // Hub heartbeat received
        if (frame->src_role == ROLE_HUB) {
            bool wasOnline = g_hubOnline;
            g_lastHubHeartbeat = millis();
            g_hubOnline = true;

            if (!wasOnline) {
                // Hub came back online – its in-memory telemetry dictionary
                // (stream_id → name) has been lost. Reset all announced flags so
                // MSG_TELEM_DICT frames are re-sent before the next batch, otherwise
                // the hub silently drops every MSG_TELEM_BATCH entry.
                resetTelemAnnouncedFlags();
                LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Hub came ONLINE – re-announcing %d dict entries\n",
                                   SAT_ID, g_telemStreamCount);
            }

            // Always update hub MAC when it changes
            bool macChanged = memcmp(g_hubMac, mac, 6) != 0;
            if (!g_hubKnown || macChanged) {
                if (g_hubKnown && macChanged) {
                    EspNowBridge::instance().removePeer(g_hubMac);
                    // Also reset dict flags – new hub instance has empty dictionary
                    resetTelemAnnouncedFlags();
                    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Hub MAC changed – re-announcing dict\n", SAT_ID);
                }
                memcpy(g_hubMac, mac, 6);
                g_hubKnown = true;
                EspNowBridge::instance().addPeer(g_hubMac);
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
                prefs.end();
                LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Hub MAC saved: %02X:%02X:%02X:%02X:%02X:%02X\n",
                              SAT_ID,
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
                LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Hub MAC learned via discovery: "
                              "%02X:%02X:%02X:%02X:%02X:%02X\n",
                              SAT_ID,
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
    Serial.begin(115200);
    delay(400);
    LIGHT_DEBUG_PRINTF("\n[SAT%d-LIGHT] === TELEMETRY-ONLY VERSION ===\n", SAT_ID);
    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Optimized for high-rate smooth plotting\n", SAT_ID);

    loadConfig();

    // NO LEDs in light version

    TeensySerial.begin(HW_UART_BAUD, SERIAL_8N1, HW_UART_RX_PIN, HW_UART_TX_PIN);

    EspNowBridge &bridge = EspNowBridge::instance();
    bridge.begin(g_channel);
    bridge.setRecvCallback(onFrame);

    // Register hub if known
    if (g_hubKnown) bridge.addPeer(g_hubMac);

    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Ready – MAC: %s  ch=%u\n",
                  SAT_ID, WiFi.macAddress().c_str(), g_channel);
    LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Type 'help' for commands\n", SAT_ID);
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
        bool hbOk = sendFrame(g_hubMac, MSG_HEARTBEAT, (const uint8_t *)&hb, sizeof(hb));
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Heartbeat sent: %s (qlen=%d)\n",
                           SAT_ID, hbOk ? "OK" : "FAIL", g_telemQueueLen);
    }

    // ── Hub online/offline detection ──────────────────────────
    if (g_hubOnline && g_hubKnown &&
        (now - g_lastHubHeartbeat) > HEARTBEAT_TIMEOUT_MS) {
        g_hubOnline = false;
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Hub went OFFLINE (timeout=%lums)\n",
                           SAT_ID, (unsigned long)(now - g_lastHubHeartbeat));
    }

    // ── Telemetry flush tick (ultra-aggressive) ───────────────
    flushTelemetryQueue(false);

    // ── ESP-NOW tick ──────────────────────────────────────────
    EspNowBridge::instance().tick();

    // ── USB serial command handler ────────────────────────────
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_serialCmdIdx > 0) {
                s_serialCmdBuf[s_serialCmdIdx] = '\0';
                handleSerialCmd(s_serialCmdBuf);
                s_serialCmdIdx = 0;
            }
        } else {
            if (s_serialCmdIdx < (int)(sizeof(s_serialCmdBuf) - 1)) {
                s_serialCmdBuf[s_serialCmdIdx++] = c;
            } else {
                s_serialCmdIdx = 0;  // Buffer overflow
            }
        }
    }

    // ── Periodic stats output ─────────────────────────────────
    if ((now - g_lastStatsReport) >= 30000) {
        g_lastStatsReport = now;
        uint32_t rate = (now > 0) ? (g_telemSentCount * 1000UL / now) : 0;
        LIGHT_DEBUG_PRINTF("[SAT%d-LIGHT] Stats: %u streams, %lu sent, %lu dropped, %lu val/s\n",
                      SAT_ID, g_telemStreamCount, g_telemSentCount, g_telemDropCount, rate);
    }
}

// ============================================================
//  ESP_Satellite/src/main.cpp
//  Satellite firmware – shared code for SAT1 and SAT2
//  SAT_ID (1 or 2) is set at compile time via -DSAT_ID=n
// ============================================================

#include <Arduino.h>
#include <ctype.h>
#include <Preferences.h>
#include "sat_config.h"
#include "messages.h"
#include "crc16.h"
#include "EspNowBridge.h"
#include "AckManager.h"
#include "CommandParser.h"

// ─── MAC addresses (loaded from NVS or set via Serial config) ─
// NOTE: g_hubMac / g_peerMac are written from the ESP-NOW callback (WiFi task)
// and read from the Arduino loop task.  Writes happen exactly once (discovery),
// before the corresponding _Known flag is set, so the ordering is safe on the
// single-core ESP32-C3.
static uint8_t g_hubMac[6]  = {0};
static uint8_t g_peerMac[6] = {0};  // The other satellite
static uint8_t g_channel    = DEFAULT_CHANNEL;
static bool    g_hubKnown   = false;
static bool    g_peerKnown  = false;

// ─── Global objects ───────────────────────────────────────────
AckManager    ackMgr;
CommandParser parser;
static uint8_t g_seq = 0;

// UART1 for Teensy
HardwareSerial TeensySerial(1);

#ifdef UART_BRIDGE_USB
#define USB_DEBUG_PRINTF(...) do {} while (0)
#define USB_DEBUG_PRINT(...)  do {} while (0)
#define USB_DEBUG_PRINTLN(...) do {} while (0)
#else
#define USB_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define USB_DEBUG_PRINT(...)  Serial.print(__VA_ARGS__)
#define USB_DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#endif

// ── Heartbeat tracking ────────────────────────────────────────
static uint32_t g_lastHubHeartbeat = 0;
static uint32_t g_lastHbSent = 0;
static bool     g_hubOnline  = false;

// ── P2P send interval ─────────────────────────────────────────
static uint32_t g_lastP2pSend = 0;

// ─── USB serial command handler ──────────────────────────────
static char s_serialCmdBuf[64];
static int  s_serialCmdIdx = 0;

enum MonitorMode : uint8_t {
    MONITOR_WEB = 0,
    MONITOR_BRIDGE,
    MONITOR_STATUS,
};

static MonitorMode g_monitorMode = MONITOR_WEB;  // Default mode at startup

static const char *monitorModeName(MonitorMode mode) {
    switch (mode) {
        case MONITOR_WEB:    return "Web";
        case MONITOR_BRIDGE: return "Bridge";
        case MONITOR_STATUS: return "Status";
        default:             return "Unknown";
    }
}

static bool equalsIgnoreCase(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static bool tryParseMonitorMode(const char *cmd, MonitorMode *outMode) {
    if (!cmd || !outMode) return false;
    if (strncasecmp(cmd, "modi+", 5) != 0) return false;
    const char *modeStr = cmd + 5;
    if (equalsIgnoreCase(modeStr, "web")) {
        *outMode = MONITOR_WEB;
        return true;
    }
    if (equalsIgnoreCase(modeStr, "bridge")) {
        *outMode = MONITOR_BRIDGE;
        return true;
    }
    if (equalsIgnoreCase(modeStr, "status")) {
        *outMode = MONITOR_STATUS;
        return true;
    }
    return false;
}

static bool forwardTelemetryLine(const char *line, const char *srcLabel) {
    if (!line || !srcLabel) return false;
    Frame_t frame;
    if (!parser.uartLineToFrame(line, SAT_ID, &frame)) return false;
    uint8_t seq = g_seq++;
    frame.seq = seq;
    // Recompute CRC after assigning the final sequence number
    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + frame.len);
    memcpy(frame.payload + frame.len, &crc, 2);

    if (g_hubKnown) {
        bool ok = EspNowBridge::instance().send(g_hubMac, &frame);
        if (g_monitorMode == MONITOR_BRIDGE) {
            USB_DEBUG_PRINTF("[SAT%d] %s DBG '", SAT_ID, srcLabel);
            USB_DEBUG_PRINT(line);
            USB_DEBUG_PRINTF("' -> hub %s\n", ok ? "ok" : "fail");
        }
    } else {
        if (g_monitorMode == MONITOR_BRIDGE || g_monitorMode == MONITOR_STATUS) {
            USB_DEBUG_PRINTF("[SAT%d] %s DBG '", SAT_ID, srcLabel);
            USB_DEBUG_PRINT(line);
            USB_DEBUG_PRINTLN("' – hub unknown, dropped");
        }
    }
    // DBG lines are NOT forwarded to peer satellite – they are hub-only telemetry
    return true;
}

// ─── Forward raw UART line to peer satellite (transparent bridge) ─
static bool forwardUartRawToPeer(const char *line) {
    if (!line || !g_peerKnown) return false;

    size_t len = strlen(line);
    if (len == 0 || len > FRAME_MAX_PAYLOAD) return false;

    Frame_t frame = {};
    frame.magic    = FRAME_MAGIC;
    frame.msg_type = MSG_UART_RAW;
    frame.seq      = g_seq++;
    frame.src_role = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
    frame.dst_role = (SAT_ID == 1) ? ROLE_SAT2 : ROLE_SAT1;
    frame.flags    = 0;
    frame.len      = (uint8_t)len;
    memcpy(frame.payload, line, len);

    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + frame.len);
    memcpy(frame.payload + frame.len, &crc, 2);

    bool ok = EspNowBridge::instance().send(g_peerMac, &frame);
    if (g_monitorMode == MONITOR_BRIDGE) {
        USB_DEBUG_PRINTF("[SAT%d] UART raw -> peer %s\n", SAT_ID, ok ? "ok" : "fail");
    }
    return ok;
}

static bool routePayloadLine(const char *line, const char *srcLabel) {
    if (!line || !srcLabel || line[0] == '\0') return false;
    if (strncmp(line, DBG_PREFIX, strlen(DBG_PREFIX)) == 0) {
        return forwardTelemetryLine(line, srcLabel);
    }
    return forwardUartRawToPeer(line);
}

static void handleSerialCmd(const char *cmd) {
    MonitorMode newMode;
    if (tryParseMonitorMode(cmd, &newMode)) {
        g_monitorMode = newMode;
        USB_DEBUG_PRINTF("[SAT%d] Monitor mode switched to: %s\n", SAT_ID, monitorModeName(g_monitorMode));
    } else if (equalsIgnoreCase(cmd, "mac") || equalsIgnoreCase(cmd, "info")) {
        USB_DEBUG_PRINTF("[SAT%d] Own MAC : %s\n", SAT_ID, WiFi.macAddress().c_str());
        USB_DEBUG_PRINTF("[SAT%d] Channel : %u\n", SAT_ID, g_channel);
        if (g_hubKnown) {
            USB_DEBUG_PRINTF("[SAT%d] Hub MAC : %02X:%02X:%02X:%02X:%02X:%02X\n",
                          SAT_ID,
                          g_hubMac[0], g_hubMac[1], g_hubMac[2],
                          g_hubMac[3], g_hubMac[4], g_hubMac[5]);
        } else {
            USB_DEBUG_PRINTF("[SAT%d] Hub MAC : unknown\n", SAT_ID);
        }
        if (g_peerKnown) {
            USB_DEBUG_PRINTF("[SAT%d] Peer MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          SAT_ID,
                          g_peerMac[0], g_peerMac[1], g_peerMac[2],
                          g_peerMac[3], g_peerMac[4], g_peerMac[5]);
        } else {
            USB_DEBUG_PRINTF("[SAT%d] Peer MAC: unknown\n", SAT_ID);
        }
        USB_DEBUG_PRINTF("[SAT%d] Hub online: %s\n", SAT_ID, g_hubOnline ? "yes" : "no");
    } else if (equalsIgnoreCase(cmd, "debug")) {
        USB_DEBUG_PRINTF("[SAT%d] === Debug Status ===\n", SAT_ID);
        USB_DEBUG_PRINTF("[SAT%d] Uptime    : %lu ms\n", SAT_ID, (unsigned long)millis());
        USB_DEBUG_PRINTF("[SAT%d] Own MAC   : %s\n", SAT_ID, WiFi.macAddress().c_str());
        USB_DEBUG_PRINTF("[SAT%d] Channel   : %u\n", SAT_ID, g_channel);
        USB_DEBUG_PRINTF("[SAT%d] Hub       : %s (%s)\n", SAT_ID,
                      g_hubKnown  ? "known"   : "unknown",
                      g_hubOnline ? "online"  : "offline");
        USB_DEBUG_PRINTF("[SAT%d] Peer      : %s\n", SAT_ID,
                      g_peerKnown ? "known"   : "unknown");
        USB_DEBUG_PRINTF("[SAT%d] ACK queue : %u pending\n", SAT_ID, ackMgr.pendingCount());
    } else if (equalsIgnoreCase(cmd, "clearmac")) {
        g_hubKnown  = false;
        g_peerKnown = false;
        memset(g_hubMac,  0, 6);
        memset(g_peerMac, 0, 6);
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.remove(NVS_KEY_HUB_MAC);
        prefs.remove(NVS_KEY_PEER_MAC);
        prefs.end();
        USB_DEBUG_PRINTF("[SAT%d] Stored MACs cleared\n", SAT_ID);
    } else if (equalsIgnoreCase(cmd, "help")) {
        USB_DEBUG_PRINTF("[SAT%d] USB commands: mac | info | debug | clearmac | Modi+Web | Modi+Bridge | Modi+Status | help\n", SAT_ID);
        USB_DEBUG_PRINTF("[SAT%d] Current monitor mode: %s\n", SAT_ID, monitorModeName(g_monitorMode));
        USB_DEBUG_PRINTF("[SAT%d] USB telemetry inject: DBG:<name>=<value>\n", SAT_ID);
#ifdef UART_BRIDGE_USB
        USB_DEBUG_PRINTF("[SAT%d] *** UART_BRIDGE_USB active – HW UART disabled ***\n", SAT_ID);
        USB_DEBUG_PRINTF("[SAT%d]   UART payload traffic is routed via USB only\n", SAT_ID);
#endif
    } else if (routePayloadLine(cmd, "USB")) {
        // accepted and forwarded with the same routing as hardware UART input
    } else {
        USB_DEBUG_PRINTF("[SAT%d] Unknown command '%s'. Type 'help'.\n", SAT_ID, cmd);
    }
}

// ─── Load config from NVS ────────────────────────────────────
static void loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    g_channel = (uint8_t)prefs.getUInt(NVS_KEY_CHANNEL, DEFAULT_CHANNEL);

    size_t n = prefs.getBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
    g_hubKnown = (n == 6);

    n = prefs.getBytes(NVS_KEY_PEER_MAC, g_peerMac, 6);
    g_peerKnown = (n == 6);

    prefs.end();
    USB_DEBUG_PRINTF("[SAT%d] ch=%u hub=%s peer=%s\n",
                  SAT_ID, g_channel,
                  g_hubKnown  ? "known" : "unknown",
                  g_peerKnown ? "known" : "unknown");
}

// ─── Build and send a frame helper ───────────────────────────
static bool sendFrame(const uint8_t *mac, uint8_t msgType,
                      const uint8_t *payload, uint8_t payLen,
                      uint8_t flags = 0) {
    Frame_t frame = {};
    frame.magic    = FRAME_MAGIC;
    frame.msg_type = msgType;
    frame.seq      = g_seq++;
    frame.src_role = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
    frame.dst_role = ROLE_HUB;
    frame.flags    = flags;
    frame.len      = payLen;
    if (payLen > 0 && payload) {
        if (payLen > FRAME_MAX_PAYLOAD) return false;  // bounds check
        memcpy(frame.payload, payload, payLen);
    }
    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + payLen);
    memcpy(frame.payload + payLen, &crc, 2);

    bool ok = EspNowBridge::instance().send(mac, &frame);
    if (ok && (flags & FLAG_ACK_REQ)) {
        ackMgr.track(mac, &frame, FRAME_HEADER_SIZE + payLen + 2);
    }
    return ok;
}

// ─── ESP-NOW receive callback ─────────────────────────────────
static void onFrame(const uint8_t *mac, const Frame_t *frame) {
    switch (frame->msg_type) {

    case MSG_HEARTBEAT:
        // Hub or peer heartbeat received
        if (frame->src_role == ROLE_HUB) {
            bool wasOnline = g_hubOnline;
            g_lastHubHeartbeat = millis();
            g_hubOnline = true;
            if (!wasOnline) {
                if (g_monitorMode == MONITOR_STATUS) {
                    USB_DEBUG_PRINTF("[SAT%d] Hub back online\n", SAT_ID);
                }
            }
            // Always update hub MAC when it changes (handles hub reboot / NVS stale MAC)
            bool macChanged = memcmp(g_hubMac, mac, 6) != 0;
            if (!g_hubKnown || macChanged) {
                if (g_hubKnown && macChanged) {
                    // MAC changed – remove old peer entry before adding new one
                    EspNowBridge::instance().removePeer(g_hubMac);
                    USB_DEBUG_PRINTF("[SAT%d] Hub MAC changed, updating\n", SAT_ID);
                }
                memcpy(g_hubMac, mac, 6);
                g_hubKnown = true;
                // Register hub as ESP-NOW peer so we can send frames back
                EspNowBridge::instance().addPeer(g_hubMac);
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
                prefs.end();
                USB_DEBUG_PRINTF("[SAT%d] Hub MAC saved: %02X:%02X:%02X:%02X:%02X:%02X\n",
                              SAT_ID,
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }
        break;

    case MSG_CTRL:
    case MSG_MODE:
    case MSG_CAL: {
        // Forward to Teensy via UART (or USB when UART_BRIDGE_USB is active)
        char uartBuf[128];
        int n = parser.hubFrameToUart(frame, uartBuf, sizeof(uartBuf));
        if (n > 0) {
#ifdef UART_BRIDGE_USB
            // USB-only mode: route command output directly via USB (no debug prefix)
            Serial.print(uartBuf);
#else
            TeensySerial.print(uartBuf);
            // Standard mode: show UART TX traffic on USB monitor as well
            Serial.print(uartBuf);
#endif
        } else {
            USB_DEBUG_PRINTF("[SAT%d] cmd type=0x%02X seq=%u – UART encode failed\n",
                          SAT_ID, frame->msg_type, frame->seq);
        }
        // Send ACK to hub if requested
        if ((frame->flags & FLAG_ACK_REQ) && g_hubKnown) {
            AckPayload_t ack;
            ack.ack_seq  = frame->seq;
            ack.status   = ACK_OK;
            ack.msg_type = frame->msg_type;
            bool ok = sendFrame(g_hubMac, MSG_ACK, (const uint8_t *)&ack, sizeof(ack));
            if (g_monitorMode == MONITOR_BRIDGE) {
                USB_DEBUG_PRINTF("[SAT%d] ACK seq=%u sent=%s\n", SAT_ID, frame->seq, ok ? "ok" : "fail");
            }
        }
        break;
    }

    case MSG_ACK: {
        const AckPayload_t *ack = reinterpret_cast<const AckPayload_t *>(frame->payload);
        if (g_monitorMode == MONITOR_BRIDGE) {
            USB_DEBUG_PRINTF("[SAT%d] ACK received ack_seq=%u status=0x%02X\n",
                          SAT_ID, ack->ack_seq, ack->status);
        }
        ackMgr.onAck(ack->ack_seq);
        break;
    }

    case MSG_DBG:
        // Telemetry from peer satellite – forward to hub if available
        if (g_hubKnown) {
            EspNowBridge::instance().send(g_hubMac, frame);
            if (g_monitorMode == MONITOR_BRIDGE) {
                USB_DEBUG_PRINTF("[SAT%d] Peer DBG frame forwarded to hub\n", SAT_ID);
            }
        }
        break;

    case MSG_UART_RAW: {
        // Transparent UART bridge: raw data from peer satellite → Teensy UART
        // Output without any prefix so the Teensy sees it as if it came directly
        // from the other robot.
        if (frame->len > 0 && frame->len <= FRAME_MAX_PAYLOAD) {
            char rawBuf[FRAME_MAX_PAYLOAD + 2];
            memcpy(rawBuf, frame->payload, frame->len);
            rawBuf[frame->len] = '\n';
#ifdef UART_BRIDGE_USB
            Serial.write((const uint8_t *)rawBuf, frame->len + 1);
#else
            TeensySerial.write((const uint8_t *)rawBuf, frame->len + 1);
            // Standard mode: show UART RX traffic on USB monitor as well
            Serial.write((const uint8_t *)rawBuf, frame->len + 1);
#endif
            if (g_monitorMode == MONITOR_BRIDGE) {
                USB_DEBUG_PRINTF("[SAT%d] P2P raw -> Teensy %u bytes\n", SAT_ID, frame->len);
            }
        }
        break;
    }

    case MSG_DISCOVERY: {
        const DiscoveryPayload_t *disc =
            reinterpret_cast<const DiscoveryPayload_t *>(frame->payload);
        if (disc->action == 0) {
            // If the request came from the hub, always update its MAC
            // (handles hub reboot or NVS containing a stale MAC)
            if (frame->src_role == ROLE_HUB) {
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
                    USB_DEBUG_PRINTF("[SAT%d] Hub MAC learned via discovery: "
                                  "%02X:%02X:%02X:%02X:%02X:%02X\n",
                                  SAT_ID,
                                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                }
            }

            // Ensure the requester is registered as an ESP-NOW peer so we can reply
            EspNowBridge::instance().addPeer(mac);

            // Announce ourselves
            DiscoveryPayload_t resp = {};
            resp.action  = 1;
            resp.role    = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
            resp.channel = g_channel;
            snprintf(resp.name, sizeof(resp.name), "SAT%d", SAT_ID);
            WiFi.macAddress(resp.mac);
            USB_DEBUG_PRINTF("[SAT%d] Discovery request received – sending announce\n", SAT_ID);
            sendFrame(mac, MSG_DISCOVERY, (const uint8_t *)&resp, sizeof(resp));
        }
        break;
    }

    default:
        USB_DEBUG_PRINTF("[SAT%d] unknown frame type=0x%02X seq=%u\n",
                      SAT_ID, frame->msg_type, frame->seq);
        break;
    }
}

// ─── Setup ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(400);
    USB_DEBUG_PRINTF("\n[SAT%d] Booting...\n", SAT_ID);

    loadConfig();

#ifdef UART_BRIDGE_USB
    USB_DEBUG_PRINTF("[SAT%d] *** UART_BRIDGE_USB active – HW UART pins disabled ***\n", SAT_ID);
    USB_DEBUG_PRINTF("[SAT%d]   UART payload traffic is routed via USB only.\n", SAT_ID);
#else
    TeensySerial.begin(HW_UART_BAUD, SERIAL_8N1, HW_UART_RX_PIN, HW_UART_TX_PIN);
#endif

    EspNowBridge &bridge = EspNowBridge::instance();
    bridge.begin(g_channel);
    bridge.setRecvCallback(onFrame);

    // Register known peers
    if (g_hubKnown)  bridge.addPeer(g_hubMac);
    if (g_peerKnown) bridge.addPeer(g_peerMac);

    ackMgr.begin();

    USB_DEBUG_PRINTF("[SAT%d] Ready – MAC: %s  ch=%u\n",
                  SAT_ID, WiFi.macAddress().c_str(), g_channel);
    USB_DEBUG_PRINTF("[SAT%d] Type 'help' for USB commands\n", SAT_ID);
    USB_DEBUG_PRINTF("[SAT%d] Monitor mode default: %s\n", SAT_ID, monitorModeName(g_monitorMode));
}

// ─── Loop ─────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // ── Read from Teensy UART ──────────────────────────────────
#ifndef UART_BRIDGE_USB
    static char uartLine[UART_RX_BUF_SIZE];
    static int  uartIdx = 0;

    while (TeensySerial.available()) {
        char c = (char)TeensySerial.read();
        if (c == '\n' || c == '\r') {
            if (uartIdx > 0) {
                uartLine[uartIdx] = '\0';
                // Route based on prefix:
                // "DBG:" prefix → telemetry/debug to hub
                // no prefix    → transparent P2P bridge to peer satellite
                bool routed = routePayloadLine(uartLine, "UART");
                if (routed) {
                    // Standard mode: show relevant UART RX payload traffic on USB monitor as well
                    Serial.println(uartLine);
                }
                uartIdx = 0;
            }
        } else {
            // Add character only if there's space, discard line on overflow
            if (uartIdx < (int)(sizeof(uartLine) - 1)) {
                uartLine[uartIdx++] = c;
            } else {
                // Buffer overflow - discard the entire line
                uartIdx = 0;
            }
        }
    }
#endif  // !UART_BRIDGE_USB
    // When UART_BRIDGE_USB is defined, Teensy telemetry input is handled via
    // the USB serial command handler below (type DBG<ID>:<name>=<value>).

    // ── Heartbeat to hub ──────────────────────────────────────
    if (g_hubKnown && (now - g_lastHbSent) >= HEARTBEAT_INTERVAL_MS) {
        g_lastHbSent = now;
        HeartbeatPayload_t hb;
        hb.uptime_ms = now;
        hb.rssi      = 0;
        hb.queue_len = 0;
        sendFrame(g_hubMac, MSG_HEARTBEAT, (const uint8_t *)&hb, sizeof(hb));
    }

    // ── Hub online/offline detection ──────────────────────────
    if (g_hubOnline && g_hubKnown &&
        (now - g_lastHubHeartbeat) > HEARTBEAT_TIMEOUT_MS) {
        g_hubOnline = false;
        if (g_monitorMode == MONITOR_STATUS) {
            USB_DEBUG_PRINTF("[SAT%d] Hub offline – P2P bridge still active\n", SAT_ID);
        }
    }

    // ── ACK retry tick ────────────────────────────────────────
    ackMgr.tick([](const uint8_t *mac, const Frame_t *frame) -> bool {
        return EspNowBridge::instance().send(mac, frame);
    });

    // ── Peer recovery tick (re-register peers after fail-streak) ─
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
            // Add character only if there's space, discard line on overflow
            if (s_serialCmdIdx < (int)(sizeof(s_serialCmdBuf) - 1)) {
                s_serialCmdBuf[s_serialCmdIdx++] = c;
            } else {
                // Buffer overflow - discard the entire line
                s_serialCmdIdx = 0;
            }
        }
    }

    // ── Periodic debug output via USB ─────────────────────────
    static uint32_t s_lastDbgPrint = 0;
    if (g_monitorMode == MONITOR_STATUS && (now - s_lastDbgPrint) >= 10000) {
        s_lastDbgPrint = now;
        USB_DEBUG_PRINTF("[SAT%d] uptime=%lums mac=%s ch=%u hub=%s peer=%s\n",
                      SAT_ID, (unsigned long)now,
                      WiFi.macAddress().c_str(),
                      g_channel,
                      g_hubKnown  ? (g_hubOnline ? "online" : "offline") : "unknown",
                      g_peerKnown ? "known" : "unknown");
    }
}

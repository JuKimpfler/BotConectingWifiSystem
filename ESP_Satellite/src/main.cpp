// ============================================================
//  ESP_Satellite/src/main.cpp
//  Satellite firmware – shared code for SAT1 and SAT2
//  SAT_ID (1 or 2) is set at compile time via -DSAT_ID=n
// ============================================================

#include <Arduino.h>
#include <Preferences.h>
#include "sat_config.h"
#include "messages.h"
#include "crc16.h"
#include "EspNowBridge.h"
#include "AckManager.h"
#include "CommandParser.h"

// ─── MAC addresses (loaded from NVS or set via Serial config) ─
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

// ── Heartbeat tracking ────────────────────────────────────────
static uint32_t g_lastHubHeartbeat = 0;
static uint32_t g_lastHbSent = 0;
static bool     g_hubOnline  = false;

// ── P2P send interval ─────────────────────────────────────────
static uint32_t g_lastP2pSend = 0;

// ─── USB serial command handler ──────────────────────────────
static char s_serialCmdBuf[64];
static int  s_serialCmdIdx = 0;

static void handleSerialCmd(const char *cmd) {
    if (strncasecmp(cmd, "mac", 3) == 0 || strncasecmp(cmd, "info", 4) == 0) {
        Serial.printf("[SAT%d] Own MAC : %s\n", SAT_ID, WiFi.macAddress().c_str());
        Serial.printf("[SAT%d] Channel : %u\n", SAT_ID, g_channel);
        if (g_hubKnown) {
            Serial.printf("[SAT%d] Hub MAC : %02X:%02X:%02X:%02X:%02X:%02X\n",
                          SAT_ID,
                          g_hubMac[0], g_hubMac[1], g_hubMac[2],
                          g_hubMac[3], g_hubMac[4], g_hubMac[5]);
        } else {
            Serial.printf("[SAT%d] Hub MAC : unknown\n", SAT_ID);
        }
        if (g_peerKnown) {
            Serial.printf("[SAT%d] Peer MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          SAT_ID,
                          g_peerMac[0], g_peerMac[1], g_peerMac[2],
                          g_peerMac[3], g_peerMac[4], g_peerMac[5]);
        } else {
            Serial.printf("[SAT%d] Peer MAC: unknown\n", SAT_ID);
        }
        Serial.printf("[SAT%d] Hub online: %s\n", SAT_ID, g_hubOnline ? "yes" : "no");
    } else if (strncasecmp(cmd, "debug", 5) == 0) {
        Serial.printf("[SAT%d] === Debug Status ===\n", SAT_ID);
        Serial.printf("[SAT%d] Uptime    : %lu ms\n", SAT_ID, (unsigned long)millis());
        Serial.printf("[SAT%d] Own MAC   : %s\n", SAT_ID, WiFi.macAddress().c_str());
        Serial.printf("[SAT%d] Channel   : %u\n", SAT_ID, g_channel);
        Serial.printf("[SAT%d] Hub       : %s (%s)\n", SAT_ID,
                      g_hubKnown  ? "known"   : "unknown",
                      g_hubOnline ? "online"  : "offline");
        Serial.printf("[SAT%d] Peer      : %s\n", SAT_ID,
                      g_peerKnown ? "known"   : "unknown");
        Serial.printf("[SAT%d] ACK queue : %u pending\n", SAT_ID, ackMgr.pendingCount());
    } else if (strncasecmp(cmd, "clearmac", 8) == 0) {
        g_hubKnown  = false;
        g_peerKnown = false;
        memset(g_hubMac,  0, 6);
        memset(g_peerMac, 0, 6);
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.remove(NVS_KEY_HUB_MAC);
        prefs.remove(NVS_KEY_PEER_MAC);
        prefs.end();
        Serial.printf("[SAT%d] Stored MACs cleared\n", SAT_ID);
    } else if (strncasecmp(cmd, "help", 4) == 0) {
        Serial.printf("[SAT%d] USB commands: mac | info | debug | clearmac | help\n", SAT_ID);
    } else {
        Serial.printf("[SAT%d] Unknown command '%s'. Type 'help'.\n", SAT_ID, cmd);
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
    Serial.printf("[SAT%d] ch=%u hub=%s peer=%s\n",
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
                Serial.printf("[SAT%d] Hub back online\n", SAT_ID);
            }
            if (!g_hubKnown) {
                memcpy(g_hubMac, mac, 6);
                g_hubKnown = true;
                // Register hub as ESP-NOW peer so we can send frames back
                EspNowBridge::instance().addPeer(g_hubMac);
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
                prefs.end();
                Serial.printf("[SAT%d] Hub MAC saved: %02X:%02X:%02X:%02X:%02X:%02X\n",
                              SAT_ID,
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }
        break;

    case MSG_CTRL:
    case MSG_MODE:
    case MSG_CAL: {
        // Forward to Teensy via UART
        char uartBuf[128];
        int n = parser.hubFrameToUart(frame, uartBuf, sizeof(uartBuf));
        if (n > 0) {
            TeensySerial.print(uartBuf);
            // Mirror UART output to USB with "UART:" prefix for monitoring
            Serial.print("UART: ");
            Serial.print(uartBuf);
        } else {
            Serial.printf("[SAT%d] cmd type=0x%02X seq=%u – UART encode failed\n",
                          SAT_ID, frame->msg_type, frame->seq);
        }
        // Send ACK to hub if requested
        if ((frame->flags & FLAG_ACK_REQ) && g_hubKnown) {
            AckPayload_t ack;
            ack.ack_seq  = frame->seq;
            ack.status   = ACK_OK;
            ack.msg_type = frame->msg_type;
            bool ok = sendFrame(g_hubMac, MSG_ACK, (const uint8_t *)&ack, sizeof(ack));
            Serial.printf("[SAT%d] ACK seq=%u sent=%s\n", SAT_ID, frame->seq, ok ? "ok" : "fail");
        }
        break;
    }

    case MSG_ACK: {
        const AckPayload_t *ack = reinterpret_cast<const AckPayload_t *>(frame->payload);
        Serial.printf("[SAT%d] ACK received ack_seq=%u status=0x%02X\n",
                      SAT_ID, ack->ack_seq, ack->status);
        ackMgr.onAck(ack->ack_seq);
        break;
    }

    case MSG_DBG:
        // Telemetry from peer satellite – forward to hub if available
        if (g_hubKnown) {
            EspNowBridge::instance().send(g_hubMac, frame);
        }
        break;

    case MSG_DISCOVERY: {
        const DiscoveryPayload_t *disc =
            reinterpret_cast<const DiscoveryPayload_t *>(frame->payload);
        if (disc->action == 0) {
            // Ensure the requester is registered as an ESP-NOW peer so we can reply
            EspNowBridge::instance().addPeer(mac);

            // If the request came from the hub, learn its MAC
            if (frame->src_role == ROLE_HUB && !g_hubKnown) {
                memcpy(g_hubMac, mac, 6);
                g_hubKnown = true;
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
                prefs.end();
                Serial.printf("[SAT%d] Hub MAC learned via discovery: "
                              "%02X:%02X:%02X:%02X:%02X:%02X\n",
                              SAT_ID,
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }

            // Announce ourselves
            DiscoveryPayload_t resp = {};
            resp.action  = 1;
            resp.role    = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
            resp.channel = g_channel;
            snprintf(resp.name, sizeof(resp.name), "SAT%d", SAT_ID);
            WiFi.macAddress(resp.mac);
            Serial.printf("[SAT%d] Discovery request received – sending announce\n", SAT_ID);
            sendFrame(mac, MSG_DISCOVERY, (const uint8_t *)&resp, sizeof(resp));
        }
        break;
    }

    default:
        Serial.printf("[SAT%d] unknown frame type=0x%02X seq=%u\n",
                      SAT_ID, frame->msg_type, frame->seq);
        break;
    }
}

// ─── Setup ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.printf("\n[SAT%d] Booting...\n", SAT_ID);

    loadConfig();

    TeensySerial.begin(HW_UART_BAUD, SERIAL_8N1, HW_UART_RX_PIN, HW_UART_TX_PIN);

    EspNowBridge &bridge = EspNowBridge::instance();
    bridge.begin(g_channel);
    bridge.setRecvCallback(onFrame);

    // Register known peers
    if (g_hubKnown)  bridge.addPeer(g_hubMac);
    if (g_peerKnown) bridge.addPeer(g_peerMac);

    ackMgr.begin();

    Serial.printf("[SAT%d] Ready – MAC: %s  ch=%u\n",
                  SAT_ID, WiFi.macAddress().c_str(), g_channel);
    Serial.printf("[SAT%d] Type 'help' for USB commands\n", SAT_ID);
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
                Frame_t frame;
                if (parser.uartLineToFrame(uartLine, SAT_ID, &frame)) {
                    frame.seq = g_seq++;
                    // Send telemetry to hub
                    if (g_hubKnown) {
                        bool ok = EspNowBridge::instance().send(g_hubMac, &frame);
                        Serial.printf("[SAT%d] UART telem '", SAT_ID);
                        Serial.print(uartLine);
                        Serial.printf("' -> hub %s\n", ok ? "ok" : "fail");
                    } else {
                        Serial.printf("[SAT%d] UART telem '", SAT_ID);
                        Serial.print(uartLine);
                        Serial.println("' – hub unknown, dropped");
                    }
                    // Forward to peer satellite (P2P bridge)
                    if (g_peerKnown) {
                        EspNowBridge::instance().send(g_peerMac, &frame);
                    }
                }
                uartIdx = 0;
            }
        } else if (uartIdx < (int)(sizeof(uartLine) - 1)) {
            uartLine[uartIdx++] = c;
        }
    }

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
        Serial.printf("[SAT%d] Hub offline – P2P bridge still active\n", SAT_ID);
    }

    // ── ACK retry tick ────────────────────────────────────────
    ackMgr.tick([](const uint8_t *mac, const Frame_t *frame) -> bool {
        return EspNowBridge::instance().send(mac, frame);
    });

    // ── USB serial command handler ────────────────────────────
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_serialCmdIdx > 0) {
                s_serialCmdBuf[s_serialCmdIdx] = '\0';
                handleSerialCmd(s_serialCmdBuf);
                s_serialCmdIdx = 0;
            }
        } else if (s_serialCmdIdx < (int)(sizeof(s_serialCmdBuf) - 1)) {
            s_serialCmdBuf[s_serialCmdIdx++] = c;
        }
    }

    // ── Periodic debug output via USB ─────────────────────────
    static uint32_t s_lastDbgPrint = 0;
    if ((now - s_lastDbgPrint) >= 10000) {
        s_lastDbgPrint = now;
        Serial.printf("[SAT%d] uptime=%lums mac=%s ch=%u hub=%s peer=%s\n",
                      SAT_ID, (unsigned long)now,
                      WiFi.macAddress().c_str(),
                      g_channel,
                      g_hubKnown  ? (g_hubOnline ? "online" : "offline") : "unknown",
                      g_peerKnown ? "known" : "unknown");
    }
}

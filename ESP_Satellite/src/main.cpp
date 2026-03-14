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
            g_lastHubHeartbeat = millis();
            g_hubOnline = true;
            if (!g_hubKnown) {
                memcpy(g_hubMac, mac, 6);
                g_hubKnown = true;
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putBytes(NVS_KEY_HUB_MAC, g_hubMac, 6);
                prefs.end();
                Serial.println("[SAT] Hub MAC saved");
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
        }
        // Send ACK to hub if requested
        if ((frame->flags & FLAG_ACK_REQ) && g_hubKnown) {
            AckPayload_t ack;
            ack.ack_seq  = frame->seq;
            ack.status   = ACK_OK;
            ack.msg_type = frame->msg_type;
            sendFrame(g_hubMac, MSG_ACK, (const uint8_t *)&ack, sizeof(ack));
        }
        break;
    }

    case MSG_ACK: {
        const AckPayload_t *ack = reinterpret_cast<const AckPayload_t *>(frame->payload);
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
            // Announce ourselves
            DiscoveryPayload_t resp = {};
            resp.action  = 1;
            resp.role    = (SAT_ID == 1) ? ROLE_SAT1 : ROLE_SAT2;
            resp.channel = g_channel;
            snprintf(resp.name, sizeof(resp.name), "SAT%d", SAT_ID);
            WiFi.macAddress(resp.mac);
            sendFrame(mac, MSG_DISCOVERY, (const uint8_t *)&resp, sizeof(resp));
        }
        break;
    }

    default:
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

    Serial.printf("[SAT%d] Ready\n", SAT_ID);
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
                        EspNowBridge::instance().send(g_hubMac, &frame);
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
        Serial.println("[SAT] Hub offline – P2P bridge still active");
    }

    // ── ACK retry tick ────────────────────────────────────────
    ackMgr.tick([](const uint8_t *mac, const Frame_t *frame) -> bool {
        return EspNowBridge::instance().send(mac, frame);
    });
}

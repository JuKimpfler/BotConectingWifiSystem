#pragma once
// ============================================================
//  ESP_Hub/include/CommandRouter.h
//  Routes commands from WebSocket UI to satellites and
//  forwards satellite frames back to the UI
// ============================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "PeerRegistry.h"
#include "EspNowManager.h"
#include "TelemetryBuffer.h"

class CommandRouter {
public:
    void begin(AsyncWebSocket *ws, EspNowManager *espnow,
               PeerRegistry *peers, TelemetryBuffer *telem);

    // Called when a WebSocket message arrives from the browser
    void onWsMessage(uint8_t *data, size_t len);

    // Called when ESP-NOW frame arrives from a satellite
    void onEspNowFrame(const uint8_t *mac, const Frame_t *frame);

    // Called from main loop to flush telemetry / pending ACKs
    void tick();

    // Send current peer status to all WS clients
    void broadcastPeerStatus();

private:
    AsyncWebSocket  *_ws     = nullptr;
    EspNowManager   *_espnow = nullptr;
    PeerRegistry    *_peers  = nullptr;
    TelemetryBuffer *_telem  = nullptr;

    uint8_t _seq = 0;

    void _handleCtrl(const char *json);
    void _handleMode(const char *json);
    void _handleCal(const char *json);
    void _handleSettings(const char *json);
    void _handlePair(const char *json);
    void _sendAckToUi(uint8_t seq, uint8_t status, const char *label);
    void _broadcastTelemetry();
    void _buildAndSend(uint8_t role, uint8_t msgType,
                       const uint8_t *payload, uint8_t payLen,
                       uint8_t flags = 0);
};

#pragma once
// ============================================================
//  ESP_Satellite/include/AckManager.h
//  Tracks outgoing frames that require ACK, handles retries
// ============================================================

#include <Arduino.h>
#include "messages.h"

#define ACK_QUEUE_SIZE 8

struct PendingAck {
    uint8_t  seq;
    uint8_t  mac[6];
    uint8_t  frameData[FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + 2];
    uint8_t  frameLen;
    uint8_t  retries;
    uint32_t sentAt;
    bool     active;
};

class AckManager {
public:
    void begin();
    // Queue a frame for ACK tracking (frame must have FLAG_ACK_REQ set)
    bool track(const uint8_t *mac, const Frame_t *frame, uint8_t frameLen);
    // Called when an ACK frame is received
    void onAck(uint8_t seq);
    // Called in loop – resends expired frames
    void tick(bool (*sendFn)(const uint8_t *mac, const Frame_t *frame));
    // Returns number of entries currently waiting for ACK
    int  pendingCount() const;

private:
    PendingAck _queue[ACK_QUEUE_SIZE];
};

// ============================================================
//  ESP_Satellite/src/AckManager.cpp
// ============================================================

#include "AckManager.h"
#include "sat_config.h"
#include <string.h>

#ifndef ACK_TIMEOUT_MS
#define ACK_TIMEOUT_MS 500
#endif

#ifndef ACK_MAX_RETRIES
#define ACK_MAX_RETRIES 3
#endif

void AckManager::begin() {
    memset(_queue, 0, sizeof(_queue));
}

bool AckManager::track(const uint8_t *mac, const Frame_t *frame, uint8_t frameLen) {
    for (int i = 0; i < ACK_QUEUE_SIZE; i++) {
        if (!_queue[i].active) {
            _queue[i].seq      = frame->seq;
            _queue[i].retries  = 0;
            _queue[i].sentAt   = millis();
            _queue[i].active   = true;
            _queue[i].frameLen = frameLen;
            memcpy(_queue[i].mac, mac, 6);
            memcpy(_queue[i].frameData, frame, frameLen);
            return true;
        }
    }
    return false; // Queue full
}

void AckManager::onAck(uint8_t seq) {
    for (int i = 0; i < ACK_QUEUE_SIZE; i++) {
        if (_queue[i].active && _queue[i].seq == seq) {
            _queue[i].active = false;
            return;
        }
    }
}

void AckManager::tick(bool (*sendFn)(const uint8_t *mac, const Frame_t *frame)) {
    uint32_t now = millis();
    for (int i = 0; i < ACK_QUEUE_SIZE; i++) {
        if (!_queue[i].active) continue;
        if ((now - _queue[i].sentAt) < ACK_TIMEOUT_MS) continue;

        if (_queue[i].retries >= ACK_MAX_RETRIES) {
            Serial.printf("[ACK] seq %u timed out after %d retries\n",
                          _queue[i].seq, ACK_MAX_RETRIES);
            _queue[i].active = false;
            continue;
        }

        // Retry
        _queue[i].retries++;
        _queue[i].sentAt = now;
        sendFn(_queue[i].mac, reinterpret_cast<Frame_t *>(_queue[i].frameData));
        Serial.printf("[ACK] resending seq %u (retry %u)\n",
                      _queue[i].seq, _queue[i].retries);
    }
}

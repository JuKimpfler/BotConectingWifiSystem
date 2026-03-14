#pragma once
#include <stdint.h>

// ============================================================
//  shared/crc16.h
//  CRC-16/IBM (poly 0xA001, init 0xFFFF) implementation
//  Suitable for both ESP32 (Arduino) and host-based unit tests
// ============================================================

static inline uint16_t crc16_update(uint16_t crc, uint8_t byte) {
    crc ^= byte;
    for (uint8_t i = 0; i < 8; i++) {
        if (crc & 0x0001) {
            crc = (crc >> 1) ^ 0xA001;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

static inline uint16_t crc16_buf(const uint8_t *buf, uint16_t len) {
    uint16_t crc = 0xFFFF;
    if (!buf) return crc;
    for (uint16_t i = 0; i < len; i++) {
        crc = crc16_update(crc, buf[i]);
    }
    return crc;
}

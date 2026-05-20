#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstddef>
#include <cstdint>
#endif

// CRC-16/IBM (MODBUS) implementation.
// init=0xFFFF, poly=0xA001
static inline uint16_t crc16_update(uint16_t crc, uint8_t data) {
    crc ^= data;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x01) {
            crc = (crc >> 1) ^ 0xA001;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

static inline uint16_t crc16_buf(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    if (!data || len == 0) {
        return crc;
    }
    for (size_t i = 0; i < len; i++) {
        crc = crc16_update(crc, data[i]);
    }
    return crc;
}

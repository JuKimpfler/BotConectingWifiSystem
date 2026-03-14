// ============================================================
//  test/unit/test_crc16.cpp
//  Host-side unit tests for CRC-16/IBM implementation
//  Build and run:  g++ -std=c++17 -o test_crc16 test_crc16.cpp && ./test_crc16
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>

// Pull in the shared header directly (no Arduino dependency)
#define millis() 0UL
#include "../../shared/crc16.h"

static int g_tests = 0;
static int g_pass  = 0;

#define CHECK(expr, msg) do { \
    g_tests++; \
    if (expr) { g_pass++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s\n", msg); } \
} while(0)

int main() {
    // Known-good CRC16 vector
    // CRC16(init=0xFFFF, poly=0xA001) of "123456789" = 0x4B37 (CRC-16/MODBUS)
    const uint8_t testVec[] = {'1','2','3','4','5','6','7','8','9'};
    uint16_t crc = crc16_buf(testVec, sizeof(testVec));
    CHECK(crc == 0x4B37, "CRC16 known vector '123456789'");

    // Empty buffer
    uint16_t emptyCrc = crc16_buf(nullptr, 0);
    CHECK(emptyCrc == 0xFFFF, "CRC16 empty buffer = 0xFFFF init value");

    // Single byte
    uint8_t oneByte[] = {0x00};
    uint16_t singleCrc = crc16_buf(oneByte, 1);
    CHECK(singleCrc != 0xFFFF, "CRC16 single zero byte differs from init");

    // Incremental vs bulk should match
    uint16_t incCrc = 0xFFFF;
    for (size_t i = 0; i < sizeof(testVec); i++) {
        incCrc = crc16_update(incCrc, testVec[i]);
    }
    CHECK(incCrc == crc, "CRC16 incremental matches bulk");

    // Framing round-trip: changing a byte changes CRC
    uint8_t buf[4] = {0xBE, 0x01, 0x42, 0x00};
    uint16_t crc1 = crc16_buf(buf, 4);
    buf[2] ^= 0xFF;
    uint16_t crc2 = crc16_buf(buf, 4);
    CHECK(crc1 != crc2, "CRC16 detects bit-flip");

    printf("\nResult: %d/%d tests passed\n", g_pass, g_tests);
    return (g_pass == g_tests) ? 0 : 1;
}

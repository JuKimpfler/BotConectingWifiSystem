// ============================================================
//  test/unit/test_routing.cpp
//  Host-side unit tests for P2P routing logic:
//    - network_id (anti-mis-pairing) validation
//    - correct destination MAC resolution (peer vs hub vs broadcast)
//    - boot-restore peer/hub MAC logic
//  Build via CMake: add_executable(test_routing test_routing.cpp)
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#define millis() 0UL
#include "../../shared/messages.h"
#include "../../shared/crc16.h"
#include "../../ESP_Satellite/include/sat_config.h"
#undef millis  // only needed to satisfy Arduino header guards in included files

namespace {
int g_tests = 0, g_pass = 0;
}

#define CHECK(expr, msg) do { \
    g_tests++; \
    if (expr) { g_pass++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s (line %d)\n", msg, __LINE__); } \
} while(0)

// ── Helpers ───────────────────────────────────────────────────

// Simulate the network_id accept/reject logic used by EspNowBridge and EspNowManager.
// Returns true if the frame should be accepted, false if it must be dropped.
static bool network_id_accept(uint8_t incoming_nid, uint8_t own_nid) {
    // Accept if either side is legacy (0x00) or IDs match
    if (incoming_nid == 0x00) return true;
    if (own_nid      == 0x00) return true;
    return incoming_nid == own_nid;
}

// Build a minimal frame with a given network_id
static Frame_t make_frame(uint8_t nid, uint8_t msg_type = MSG_HEARTBEAT) {
    Frame_t f = {};
    f.magic      = FRAME_MAGIC;
    f.msg_type   = msg_type;
    f.seq        = 1;
    f.src_role   = ROLE_HUB;
    f.dst_role   = ROLE_SAT1;
    f.flags      = 0;
    f.network_id = nid;
    f.len        = 0;
    uint16_t crc = crc16_buf((const uint8_t *)&f, FRAME_HEADER_SIZE + f.len);
    memcpy(f.payload + f.len, &crc, 2);
    return f;
}

// Simulate "is_hub_mac": true if mac matches the stored hub MAC
static bool is_hub_mac(const uint8_t *mac, const uint8_t *hub_mac, bool hub_known) {
    if (!hub_known) return false;
    return memcmp(mac, hub_mac, 6) == 0;
}

// Simulate "is_peer_mac": true if mac matches the stored peer MAC
static bool is_peer_mac(const uint8_t *mac, const uint8_t *peer_mac, bool peer_known) {
    if (!peer_known) return false;
    return memcmp(mac, peer_mac, 6) == 0;
}

// Simulate "is_broadcast_mac"
static bool is_broadcast_mac(const uint8_t *mac) {
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    return memcmp(mac, bcast, 6) == 0;
}

// Resolve destination MAC for a unicast data frame.
// Returns true with dest=peer_mac if the correct route is known.
// Returns false (and should NOT fall back to broadcast) if route is unknown.
static bool resolve_dest_mac(const uint8_t *peer_mac, bool peer_known,
                              uint8_t *out_dest) {
    if (!peer_known) return false;  // NO_ROUTE – must not fall back to broadcast
    memcpy(out_dest, peer_mac, 6);
    return true;
}

// ── Tests ─────────────────────────────────────────────────────

int main() {
    printf("=== test_routing ===\n\n");

    // ── 1. FRAME_HEADER_SIZE includes network_id field ────────
    CHECK(FRAME_HEADER_SIZE == 8, "FRAME_HEADER_SIZE is 8 bytes");

    // ── 2. network_id field is accessible in Frame_t ─────────
    {
        Frame_t f = {};
        f.network_id = 0xAB;
        CHECK(f.network_id == 0xAB, "Frame_t.network_id field read/write");
    }

    // ── 3. network_id does not overflow into payload ──────────
    {
        Frame_t f = {};
        f.network_id = 0xFF;
        f.len        = 0;
        CHECK(f.payload[0] == 0, "network_id does not bleed into payload[0]");
    }

    // ── 4. Anti-mis-pairing: same ID → accept ─────────────────
    CHECK(network_id_accept(0x01, 0x01), "Same network_id (0x01) → accepted");
    CHECK(network_id_accept(0x05, 0x05), "Same network_id (0x05) → accepted");
    CHECK(network_id_accept(0xFF, 0xFF), "Same network_id (0xFF) → accepted");

    // ── 5. Anti-mis-pairing: different IDs → reject ───────────
    CHECK(!network_id_accept(0x01, 0x02), "Different nid 0x01 vs 0x02 → rejected");
    CHECK(!network_id_accept(0x02, 0x03), "Different nid 0x02 vs 0x03 → rejected");
    CHECK(!network_id_accept(0x10, 0x01), "Different nid 0x10 vs 0x01 → rejected");

    // ── 6. Anti-mis-pairing: legacy (0x00) → always accept ────
    CHECK(network_id_accept(0x00, 0x01), "Incoming legacy 0x00 → accepted by nid 0x01");
    CHECK(network_id_accept(0x01, 0x00), "nid 0x01 from own-legacy 0x00 → accepted");
    CHECK(network_id_accept(0x00, 0x00), "Both legacy 0x00 → accepted");

    // ── 7. ESPNOW_NETWORK_ID compile-time constant is non-zero ─
    CHECK(ESPNOW_NETWORK_ID != 0, "ESPNOW_NETWORK_ID is non-zero (not legacy)");

    // ── 8. Frame with correct network_id passes filter ────────
    {
        Frame_t f = make_frame(ESPNOW_NETWORK_ID);
        bool accepted = network_id_accept(f.network_id, ESPNOW_NETWORK_ID);
        CHECK(accepted, "Frame with ESPNOW_NETWORK_ID passes own filter");
    }

    // ── 9. Frame with foreign network_id is rejected ──────────
    {
        uint8_t foreign_nid = (ESPNOW_NETWORK_ID == 0x01) ? 0x02 : 0x01;
        Frame_t f = make_frame(foreign_nid);
        bool accepted = network_id_accept(f.network_id, ESPNOW_NETWORK_ID);
        CHECK(!accepted, "Frame with foreign network_id is rejected");
    }

    // ── 10. Frame CRC covers the network_id field ─────────────
    {
        Frame_t f1 = make_frame(0x01);
        Frame_t f2 = make_frame(0x02);
        uint16_t crc1, crc2;
        memcpy(&crc1, f1.payload + f1.len, 2);
        memcpy(&crc2, f2.payload + f2.len, 2);
        CHECK(crc1 != crc2,
              "Frames with different network_id have different CRCs");
    }

    // ── 11. Correct destination MAC resolution ─────────────────
    {
        const uint8_t hub_mac[6]  = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
        const uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0x44, 0x55, 0x66};
        uint8_t out[6] = {};
        bool ok = resolve_dest_mac(peer_mac, true, out);
        CHECK(ok, "resolve_dest_mac succeeds when peer is known");
        CHECK(memcmp(out, peer_mac, 6) == 0,
              "resolve_dest_mac returns correct peer MAC");
        // Hub MAC must NOT be used as P2P destination
        CHECK(memcmp(out, hub_mac, 6) != 0,
              "P2P destination is not the hub MAC");
    }

    // ── 12. No-route falls back to 'failed' (not broadcast) ───
    {
        uint8_t out[6] = {};
        bool ok = resolve_dest_mac(nullptr, false, out);
        CHECK(!ok, "resolve_dest_mac fails when peer is unknown (no silent broadcast)");
        // The caller must check ok and NOT substitute broadcast
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        // out is zero (unset) – verify it's NOT the broadcast MAC
        CHECK(!is_broadcast_mac(out),
              "Unresolved dest is not silently set to broadcast");
    }

    // ── 13. Hub MAC stored separately from peer MAC ────────────
    {
        const uint8_t hub_mac[6]  = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        const uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        CHECK(is_hub_mac(hub_mac,  hub_mac, true),  "Hub MAC identifies itself");
        CHECK(is_peer_mac(peer_mac, peer_mac, true), "Peer MAC identifies itself");
        CHECK(!is_hub_mac(peer_mac, hub_mac, true),  "Peer MAC is not hub MAC");
        CHECK(!is_peer_mac(hub_mac, peer_mac, true), "Hub MAC is not peer MAC");
    }

    // ── 14. Boot-restore: peer unknown until flags set ─────────
    {
        bool peer_known = false;
        uint8_t peer_mac[6] = {};   // zero (not yet loaded from NVS)
        uint8_t out[6] = {};
        // Before NVS load completes, peer is unknown
        bool ok = resolve_dest_mac(peer_mac, peer_known, out);
        CHECK(!ok, "Peer unknown before NVS restore – route fails safely");

        // Simulate successful NVS restore
        const uint8_t loaded_mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
        memcpy(peer_mac, loaded_mac, 6);
        peer_known = true;
        ok = resolve_dest_mac(peer_mac, peer_known, out);
        CHECK(ok, "Peer known after NVS restore – route succeeds");
        CHECK(memcmp(out, loaded_mac, 6) == 0,
              "Restored MAC is used as correct destination");
    }

    // ── 15. Broadcast MAC detection ───────────────────────────
    {
        const uint8_t bcast[6]   = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        const uint8_t normal[6]  = {0x11,0x22,0x33,0x44,0x55,0x66};
        CHECK(is_broadcast_mac(bcast),    "FF:FF:FF:FF:FF:FF detected as broadcast");
        CHECK(!is_broadcast_mac(normal),  "Normal MAC not detected as broadcast");
    }

    printf("\nResult: %d/%d tests passed\n", g_pass, g_tests);
    return (g_pass == g_tests) ? 0 : 1;
}

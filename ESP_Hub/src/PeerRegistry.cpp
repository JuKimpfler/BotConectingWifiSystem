// ============================================================
//  ESP_Hub/src/PeerRegistry.cpp
// ============================================================

#include "PeerRegistry.h"
#include <string.h>

void PeerRegistry::clear() {
    memset(_peers, 0, sizeof(_peers));
    _count = 0;
}

bool PeerRegistry::addOrUpdate(const PeerInfo &info) {
    // Update if MAC already exists
    for (int i = 0; i < _count; i++) {
        if (memcmp(_peers[i].mac, info.mac, 6) == 0) {
            _peers[i] = info;
            return true;
        }
    }
    if (_count >= MAX_PEERS) return false;
    _peers[_count++] = info;
    return true;
}

bool PeerRegistry::remove(const uint8_t *mac) {
    for (int i = 0; i < _count; i++) {
        if (memcmp(_peers[i].mac, mac, 6) == 0) {
            // Shift remaining entries
            for (int j = i; j < _count - 1; j++) {
                _peers[j] = _peers[j + 1];
            }
            _count--;
            return true;
        }
    }
    return false;
}

PeerInfo *PeerRegistry::findByMac(const uint8_t *mac) {
    for (int i = 0; i < _count; i++) {
        if (memcmp(_peers[i].mac, mac, 6) == 0) return &_peers[i];
    }
    return nullptr;
}

PeerInfo *PeerRegistry::findByRole(uint8_t role) {
    for (int i = 0; i < _count; i++) {
        if (_peers[i].role == role) return &_peers[i];
    }
    return nullptr;
}

int PeerRegistry::count() const {
    return _count;
}

PeerInfo *PeerRegistry::get(int idx) {
    if (idx < 0 || idx >= _count) return nullptr;
    return &_peers[idx];
}

void PeerRegistry::markOnline(const uint8_t *mac, bool online) {
    PeerInfo *p = findByMac(mac);
    if (p) {
        p->online    = online;
        p->lastSeen  = millis();
    }
}

void PeerRegistry::markDataOk(const uint8_t *mac) {
    PeerInfo *p = findByMac(mac);
    if (p) p->lastDataOkMs = millis();
}

void PeerRegistry::tickTimeouts(uint32_t timeoutMs) {
    uint32_t now = millis();
    for (int i = 0; i < _count; i++) {
        if (_peers[i].online && (now - _peers[i].lastSeen) > timeoutMs) {
            _peers[i].online = false;
            Serial.printf("[PEERS] %s went offline\n", _peers[i].name);
        }
    }
}

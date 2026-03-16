// ============================================================
//  ESP_Hub/src/EspNowManager.cpp
// ============================================================

#include "EspNowManager.h"
#include "crc16.h"
#include <esp_wifi.h>

EspNowManager &EspNowManager::instance() {
    static EspNowManager instance;
    return instance;
}

bool EspNowManager::begin(uint8_t channel, const char *pmk16) {
    _channel = channel;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD, channel);

    // Start DNS server so the hub is reachable as http://esp.hub
    if (!_dns.start(53, DNS_HOSTNAME, WiFi.softAPIP())) {
        Serial.println("[DNS] failed to start DNS server on port 53");
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] init failed");
        return false;
    }

    if (pmk16 && strnlen(pmk16, 16) == 16) {
        esp_now_set_pmk((const uint8_t *)pmk16);
    }

    esp_now_register_send_cb(_onSent);
    esp_now_register_recv_cb(_onRecv);

    // Register broadcast peer so discovery scans can be sent
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    addPeer(bcast, nullptr);

    Serial.printf("[ESPNOW] ready ch=%u\n", channel);
    return true;
}

bool EspNowManager::addPeer(const uint8_t *mac, const char *ltk16) {
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = _channel;
    peer.ifidx   = WIFI_IF_AP;

    if (ltk16 && strnlen(ltk16, 16) == 16) {
        peer.encrypt = true;
        memcpy(peer.lmk, ltk16, 16);
    }

    return esp_now_add_peer(&peer) == ESP_OK;
}

bool EspNowManager::removePeer(const uint8_t *mac) {
    return esp_now_del_peer(mac) == ESP_OK;
}

bool EspNowManager::send(const uint8_t *mac, const Frame_t *frame) {
    uint16_t frameLen = FRAME_HEADER_SIZE + frame->len;
    return esp_now_send(mac, (const uint8_t *)frame, frameLen + sizeof(uint16_t)) == ESP_OK;
}

void EspNowManager::_onSent(const uint8_t *mac, esp_now_send_status_t s) {
    if (s != ESP_NOW_SEND_SUCCESS) {
        Serial.printf("[ESPNOW] send failed to %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

void EspNowManager::_onRecv(const uint8_t *mac,
                            const uint8_t *data, int len) {
    if (len < FRAME_HEADER_SIZE + 2) return;

    const Frame_t *f = reinterpret_cast<const Frame_t *>(data);
    if (f->magic != FRAME_MAGIC) return;
    if (f->len > FRAME_MAX_PAYLOAD) return;

    // Verify CRC
    uint16_t calcCrc = crc16_buf(data, FRAME_HEADER_SIZE + f->len);
    uint16_t rxCrc;
    memcpy(&rxCrc, data + FRAME_HEADER_SIZE + f->len, 2);
    if (calcCrc != rxCrc) {
        Serial.println("[ESPNOW] CRC mismatch – frame dropped");
        return;
    }

    Serial.printf("[ESPNOW] rx from %02X:%02X:%02X:%02X:%02X:%02X type=0x%02X seq=%u role=%u\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  f->msg_type, f->seq, f->src_role);

    EspNowManager &self = instance();
    if (self._recvCb) {
        self._recvCb(mac, f);
    }
}

// ============================================================
//  ESP_Satellite/src/EspNowBridge.cpp
//  P2P ESP-NOW bridge (SAT1 <-> SAT2) + hub link
// ============================================================

#include "EspNowBridge.h"
#include "crc16.h"
#include <esp_wifi.h>

static EspNowBridge s_instance;

EspNowBridge &EspNowBridge::instance() {
    return s_instance;
}

bool EspNowBridge::begin(uint8_t channel) {
    _channel = channel;

    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[BRIDGE] ESP-NOW init failed");
        return false;
    }

    esp_now_register_send_cb(_onSent);
    esp_now_register_recv_cb(_onRecv);

    Serial.printf("[BRIDGE] SAT%d ready ch=%u\n", SAT_ID, channel);
    return true;
}

bool EspNowBridge::addPeer(const uint8_t *mac, bool encrypt, const char *ltk16) {
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = _channel;
    peer.ifidx   = WIFI_IF_STA;

    if (encrypt && ltk16 && strnlen(ltk16, 16) == 16) {
        peer.encrypt = true;
        memcpy(peer.lmk, ltk16, 16);
    }

    return esp_now_add_peer(&peer) == ESP_OK;
}

bool EspNowBridge::send(const uint8_t *mac, const Frame_t *frame) {
    uint16_t totalLen = FRAME_HEADER_SIZE + frame->len + sizeof(uint16_t);
    return esp_now_send(mac, (const uint8_t *)frame, totalLen) == ESP_OK;
}

void EspNowBridge::_onSent(const uint8_t *mac, esp_now_send_status_t s) {
    if (s != ESP_NOW_SEND_SUCCESS) {
        Serial.printf("[BRIDGE] send failed to %02X:%02X\n", mac[4], mac[5]);
    }
}

void EspNowBridge::_onRecv(const esp_now_recv_info_t *info,
                            const uint8_t *data, int len) {
    if (len < (int)(FRAME_HEADER_SIZE + 2)) return;

    const Frame_t *f = reinterpret_cast<const Frame_t *>(data);
    if (f->magic != FRAME_MAGIC) return;
    if (f->len > FRAME_MAX_PAYLOAD) return;

    uint16_t calcCrc = crc16_buf(data, FRAME_HEADER_SIZE + f->len);
    uint16_t rxCrc;
    memcpy(&rxCrc, data + FRAME_HEADER_SIZE + f->len, 2);
    if (calcCrc != rxCrc) {
        Serial.println("[BRIDGE] CRC error");
        return;
    }

    if (s_instance._recvCb) {
        s_instance._recvCb(info->src_addr, f);
    }
}

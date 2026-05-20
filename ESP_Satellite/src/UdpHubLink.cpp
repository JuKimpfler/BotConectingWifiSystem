#include "UdpHubLink.h"
#include "crc16.h"

bool UdpHubLink::begin() {
    _hubIp.fromString(HUB_HOST_IP);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_HOTSPOT_SSID, WIFI_HOTSPOT_PASS, WIFI_HOTSPOT_CHANNEL);
    _udpBegun = _udp.begin(SAT_UDP_LOCAL_PORT) == 1;
    _lastReconnectAttempt = millis();
    return _udpBegun;
}

void UdpHubLink::tick() {
    if (WiFi.status() == WL_CONNECTED) return;
    uint32_t now = millis();
    if ((uint32_t)(now - _lastReconnectAttempt) < WIFI_RECONNECT_INTERVAL_MS) return;
    _lastReconnectAttempt = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_HOTSPOT_SSID, WIFI_HOTSPOT_PASS, WIFI_HOTSPOT_CHANNEL);
}

bool UdpHubLink::sendFrame(const Frame_t *frame) {
    if (!_udpBegun || _hubIp == IPAddress(0, 0, 0, 0) || !frame || WiFi.status() != WL_CONNECTED) return false;
    uint16_t totalLen = FRAME_HEADER_SIZE + frame->len + sizeof(uint16_t);
    if (!_udp.beginPacket(_hubIp, HUB_UDP_PORT)) return false;
    size_t written = _udp.write(reinterpret_cast<const uint8_t *>(frame), totalLen);
    bool ok = (written == totalLen) && (_udp.endPacket() == 1);
    return ok;
}

bool UdpHubLink::readFrame(Frame_t *outFrame) {
    if (!_udpBegun || !outFrame) return false;
    int packetSize = _udp.parsePacket();
    if (packetSize <= 0) return false;
    if (packetSize > (FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + 2)) {
        while (_udp.available()) { _udp.read(); }
        return false;
    }

    uint8_t buf[FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + 2] = {0};
    int read = _udp.read(buf, packetSize);
    if (read < (FRAME_HEADER_SIZE + 2)) return false;

    const Frame_t *frame = reinterpret_cast<const Frame_t *>(buf);
    if (frame->magic != FRAME_MAGIC) return false;
    if (frame->len > FRAME_MAX_PAYLOAD) return false;
    if (read < (FRAME_HEADER_SIZE + frame->len + 2)) return false;

    uint16_t rxCrc = 0;
    memcpy(&rxCrc, buf + FRAME_HEADER_SIZE + frame->len, sizeof(rxCrc));
    uint16_t calcCrc = crc16_buf(buf, FRAME_HEADER_SIZE + frame->len);
    if (rxCrc != calcCrc) return false;

    memset(outFrame, 0, sizeof(Frame_t));
    memcpy(outFrame, buf, FRAME_HEADER_SIZE + frame->len + 2);
    return true;
}

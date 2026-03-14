// ============================================================
//  ESP_Hub/src/TelemetryBuffer.cpp
// ============================================================

#include "TelemetryBuffer.h"
#include <string.h>

void TelemetryBuffer::begin(uint32_t minIntervalMs) {
    _minIntervalMs = minIntervalMs;
    memset(_streams, 0, sizeof(_streams));
    _count = 0;
}

void TelemetryBuffer::ingest(const TelemetryEntry_t *entry) {
    StreamStat *s = _getOrCreate(entry->name);
    if (!s) return;

    float val = 0.0f;
    if (entry->vtype == 0) val = (float)entry->value.i32;
    else if (entry->vtype == 1) val = entry->value.f32;
    else if (entry->vtype == 2) val = entry->value.b ? 1.0f : 0.0f;

    s->vtype   = entry->vtype;
    s->current = val;
    if (!s->valid) {
        s->minVal = val;
        s->maxVal = val;
        s->valid  = true;
    } else {
        if (val < s->minVal) s->minVal = val;
        if (val > s->maxVal) s->maxVal = val;
    }
    s->lastUpdate = millis();
}

bool TelemetryBuffer::tick() {
    uint32_t now = millis();
    if ((now - _lastFlush) >= _minIntervalMs) {
        _lastFlush = now;
        return true;
    }
    return false;
}

StreamStat *TelemetryBuffer::getStream(int idx) {
    if (idx < 0 || idx >= _count) return nullptr;
    return &_streams[idx];
}

int TelemetryBuffer::streamCount() const {
    return _count;
}

StreamStat *TelemetryBuffer::findStream(const char *name) {
    for (int i = 0; i < _count; i++) {
        if (strncmp(_streams[i].name, name, 16) == 0) return &_streams[i];
    }
    return nullptr;
}

StreamStat *TelemetryBuffer::_getOrCreate(const char *name) {
    StreamStat *s = findStream(name);
    if (s) return s;
    if (_count >= TELEM_MAX_STREAMS) return nullptr;
    s = &_streams[_count++];
    memset(s, 0, sizeof(StreamStat));
    strlcpy(s->name, name, sizeof(s->name));
    return s;
}

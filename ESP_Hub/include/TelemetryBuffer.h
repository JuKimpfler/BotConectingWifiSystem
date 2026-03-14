#pragma once
// ============================================================
//  ESP_Hub/include/TelemetryBuffer.h
//  Rate-limits telemetry forwarding from satellites to UI
//  Supports up to 30 named streams
// ============================================================

#include <Arduino.h>
#include "messages.h"

#define TELEM_MAX_STREAMS  32

struct StreamStat {
    char     name[17];
    uint8_t  vtype;      // 0=int32, 1=float, 2=bool, 3=string
    float    current;
    float    minVal;
    float    maxVal;
    uint32_t lastUpdate;
    bool     valid;
};

class TelemetryBuffer {
public:
    void begin(uint32_t minIntervalMs);
    void ingest(const TelemetryEntry_t *entry);
    bool tick();           // Returns true if UI flush is due
    StreamStat *getStream(int idx);
    int         streamCount() const;
    StreamStat *findStream(const char *name);

private:
    StreamStat _streams[TELEM_MAX_STREAMS];
    int        _count = 0;
    uint32_t   _minIntervalMs;
    uint32_t   _lastFlush = 0;

    StreamStat *_getOrCreate(const char *name);
};

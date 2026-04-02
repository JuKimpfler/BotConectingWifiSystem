#pragma once
// ============================================================
//  ESP_Hub/include/StatusLeds.h
//  Battery monitoring and status LEDs for the Hub
// ============================================================

#include <Arduino.h>
#include "hub_config.h"

struct BatteryState {
    float voltage = 0.0f;  // Volts
    bool  valid   = false;
    bool  low     = false;
    bool  charging = false;
};

class BatteryMonitor {
public:
    void begin();
    void tick();
    const BatteryState &state() const { return _state; }

    /// Returns the scaled battery voltage in millivolts, or 0 if not yet valid.
    /// On ESP32-C6 the underlying ADC read uses eFuse two-point calibration
    /// automatically (via analogReadMilliVolts), giving higher accuracy than C3.
    uint32_t getBatteryVoltageMv() const;

private:
    BatteryState _state = {};
    uint32_t     _lastSampleMs = 0;
};

class StatusLeds {
public:
    void begin();
    void setWebActive(bool on);
    void setSatOnline(uint8_t role, bool online);
    void setBattery(const BatteryState &state);
    void tick();

private:
    bool         _webActive  = false;
    bool         _sat1Online = false;
    bool         _sat2Online = false;
    BatteryState _bat = {};
    bool         _blinkState = false;
    uint32_t     _lastBlink  = 0;

    static bool  _isValidPin(int pin);
    void         _writePin(int pin, bool on);
};

// ============================================================
//  ESP_Hub/src/StatusLeds.cpp
// ============================================================

#include "StatusLeds.h"
#include <esp32-hal-adc.h>

static constexpr float MV_TO_V = 1.0f / 1000.0f;

bool StatusLeds::_isValidPin(int pin) {
    return pin >= 0;
}

void BatteryMonitor::begin() {
#if PIN_BATTERY_SENSE >= 0
    pinMode(PIN_BATTERY_SENSE, INPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
#endif
#if PIN_CHARGE_STATUS >= 0
    pinMode(PIN_CHARGE_STATUS, INPUT_PULLUP);
#endif
    _lastSampleMs = millis() - BATTERY_SAMPLE_INTERVAL_MS;
}

void BatteryMonitor::tick() {
    uint32_t now = millis();
    if ((now - _lastSampleMs) < BATTERY_SAMPLE_INTERVAL_MS) return;
    _lastSampleMs = now;

    BatteryState next = _state;
    next.voltage = 0.0f;
    next.low     = false;
    next.valid    = false;
    next.charging = false;

#if PIN_BATTERY_SENSE >= 0
    uint32_t rawMv = analogReadMilliVolts(PIN_BATTERY_SENSE);
    if (rawMv > 0) {
        float scaledMv = rawMv * BATTERY_VDIVIDER;
        next.voltage = scaledMv * MV_TO_V;
        next.valid   = true;
        next.low     = (scaledMv <= BATTERY_LOW_MV);
    }
#endif
#if PIN_CHARGE_STATUS >= 0
    // Charger STAT pins are typically active-low
    next.charging = (digitalRead(PIN_CHARGE_STATUS) == LOW);
#endif

    _state = next;
}

void StatusLeds::begin() {
    const int ledPins[] = {
        PIN_LED_POWER,
        PIN_LED_BAT_LOW,
        PIN_LED_WEBSERVER,
        PIN_LED_SAT1,
        PIN_LED_SAT2,
    };
    for (int pin : ledPins) {
        if (_isValidPin(pin)) {
            pinMode(pin, OUTPUT);
            digitalWrite(pin, LOW);
        }
    }
}

void StatusLeds::setWebActive(bool on) {
    _webActive = on;
}

void StatusLeds::setSatOnline(uint8_t role, bool online) {
    if (role == 1) {
        _sat1Online = online;
    } else if (role == 2) {
        _sat2Online = online;
    }
}

void StatusLeds::setBattery(const BatteryState &state) {
    bool wasCharging = _bat.charging;
    _bat = state;
    // Reset blink state when leaving charging mode so LOW shows solid
    if (_bat.charging && !wasCharging) {
        _blinkState = true;
        _lastBlink  = millis();
    } else if (!_bat.charging) {
        _blinkState = _bat.low;
    }
}

void StatusLeds::tick() {
    uint32_t now = millis();

    bool batOn = false;
    if (_bat.charging) {
        if ((now - _lastBlink) >= BATTERY_CHARGE_BLINK_MS) {
            _blinkState = !_blinkState;
            _lastBlink  = now;
        }
        batOn = _blinkState;
    } else if (_bat.valid) {
        batOn = _bat.low;
    }

    _writePin(PIN_LED_POWER, true);
    _writePin(PIN_LED_BAT_LOW, batOn);
    _writePin(PIN_LED_WEBSERVER, _webActive);
    _writePin(PIN_LED_SAT1, _sat1Online);
    _writePin(PIN_LED_SAT2, _sat2Online);
}

void StatusLeds::_writePin(int pin, bool on) {
    if (!_isValidPin(pin)) return;
    digitalWrite(pin, on ? HIGH : LOW);
}

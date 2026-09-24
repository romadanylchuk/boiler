#include "EnergyMeter.h"
#include <Arduino.h>

EnergyMeter::EnergyMeter(AppState& state, TimeService& time)
    : _state(state), _time(time) {}

void EnergyMeter::begin() {
    _prefs.begin(NS, true);  // read-only
    _state.energy.totalWh = _prefs.getUInt("energyWh", 0);
    _state.energy.resetTs = _prefs.getUInt("energyRstTs", 0);
    _prefs.end();

    _savedWh    = _state.energy.totalWh;
    _lastMs     = millis();
    _lastSaveMs = _lastMs;
    Serial.printf("[EnergyMeter] Loaded %.3f kWh\n", _state.energy.totalWh / 1000.0);
}

void EnergyMeter::update() {
    uint32_t now = millis();
    uint32_t dt  = now - _lastMs;
    _lastMs = now;

    if (_state.energyResetRequested) {
        _state.energyResetRequested = false;
        reset();
    }

    bool on = _state.relays.heaterOn;
    if (on) {
        double watts = _state.config.heaterPowerDeciKw * 100.0;
        _fracWh += watts * dt / 3600000.0;
        uint32_t whole = (uint32_t)_fracWh;
        if (whole) {
            _state.energy.totalWh += whole;
            _fracWh -= whole;
        }
    }

    bool turnedOff = _wasHeaterOn && !on;
    _wasHeaterOn = on;

    if (_state.energy.totalWh != _savedWh &&
        (turnedOff || now - _lastSaveMs >= SAVE_INTERVAL_MS)) {
        save();
    }
}

void EnergyMeter::reset() {
    _fracWh = 0;
    _state.energy.totalWh = 0;
    _state.energy.resetTs = _time.isSynced() ? (uint32_t)_time.now() : 0;

    _prefs.begin(NS, false);
    _prefs.putUInt("energyRstTs", _state.energy.resetTs);
    _prefs.end();
    save();
    Serial.println("[EnergyMeter] Counter reset");
}

void EnergyMeter::save() {
    _prefs.begin(NS, false);
    _prefs.putUInt("energyWh", _state.energy.totalWh);
    _prefs.end();
    _savedWh    = _state.energy.totalWh;
    _lastSaveMs = millis();
}

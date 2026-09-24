#pragma once
#include <Preferences.h>
#include "../model/AppState.h"
#include "TimeService.h"

// ─────────────────────────────────────────────────────────────────────────────
//  EnergyMeter — estimated heater energy (kWh = heater power × ON time)
//
//  Counts in RAM every loop, persists total to NVS on heater OFF and every
//  10 min while heating (flash wear). Reset is requested from the web task
//  via AppState::energyResetRequested and applied here on the main loop.
// ─────────────────────────────────────────────────────────────────────────────

class EnergyMeter {
public:
    EnergyMeter(AppState& state, TimeService& time);
    void begin();   // load total from NVS — call after NvsConfig::load
    void update();  // call from loop

private:
    void reset();
    void save();

    AppState&    _state;
    TimeService& _time;
    Preferences  _prefs;

    double   _fracWh      = 0;      // sub-Wh remainder not yet added to totalWh
    uint32_t _lastMs      = 0;
    uint32_t _lastSaveMs  = 0;
    uint32_t _savedWh     = 0;      // value last written to NVS
    bool     _wasHeaterOn = false;

    static constexpr uint32_t SAVE_INTERVAL_MS = 10 * 60 * 1000;  // 10 min
    static constexpr const char* NS = "boiler";  // same namespace as NvsConfig
};

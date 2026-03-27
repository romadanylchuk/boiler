#pragma once
#include <Preferences.h>
#include "../model/AppState.h"

// Saves and loads Config to/from NVS flash.
// Uses ESP32 Preferences (key-value store over NVS).

class NvsConfig {
public:
    void load(Config& cfg);   // load from NVS → cfg (uses defaults if key missing)
    void save(const Config& cfg);
    void reset();             // wipe all NVS keys → factory defaults

    // Partial saves — call after changing a specific section
    void saveSetpoints(const Config& cfg);
    void saveTiming(const Config& cfg);
    void saveCredentials(const Config& cfg);
    void saveWifi(const Config& cfg);
    void saveMqtt(const Config& cfg);
    void saveSensorSources(const Config& cfg);
    void saveCurve(const Config& cfg);

private:
    Preferences _prefs;
    static constexpr const char* NS = "boiler";  // NVS namespace
};

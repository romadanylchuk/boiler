#pragma once
#include <NimBLEDevice.h>
#include "../model/AppState.h"

// ─────────────────────────────────────────────────────────────────────────────
//  BleScanner — passive BLE advertisement scanner
//  Reads IBS-TH2 / IBS-TH2 Plus temperature sensors by MAC address.
//  No connection, no pairing — advertisement only.
// ─────────────────────────────────────────────────────────────────────────────

struct IBSTh2Data {
    float    temperature;   // °C
    float    humidity;      // %
    uint8_t  battery;       // 0–100 %
    bool     valid;
};

class BleScanner : public NimBLEAdvertisedDeviceCallbacks {
public:
    explicit BleScanner(AppState& state);
    void begin();
    void update();   // call from loop — processes queued results

    // Parse IBS-TH2 manufacturer data
    static bool parseIBSTh2(const uint8_t* data, size_t len, IBSTh2Data& out);

private:
    // NimBLE callback — called from BLE task (different thread!)
    void onResult(NimBLEAdvertisedDevice* device) override;

    void applyToState(bool isRoom, const IBSTh2Data& data);

    AppState&     _state;
    NimBLEScan*   _scan = nullptr;

    // Thread-safe result queue (BLE task → main task)
    struct QueuedResult {
        bool       isRoom;
        IBSTh2Data data;
        bool       ready = false;
    };
    volatile QueuedResult _roomResult;
    volatile QueuedResult _outsideResult;

    static constexpr uint32_t SCAN_INTERVAL_MS = 10000;  // restart scan every 10s
    uint32_t _lastScanStart = 0;
};

#include "BleScanner.h"
#include <Arduino.h>

// IBS-TH2 manufacturer data layout:
//   [0..1] = company ID (0xFF 0xFF typically, varies)
//   [2..3] = temperature × 100, int16 LE
//   [4..5] = humidity × 100, uint16 LE
//   [6]    = battery %
// Total minimum length: 7 bytes

static constexpr size_t IBS_MIN_LEN       = 7;
static constexpr size_t IBS_COMPANY_BYTES = 2;
static constexpr size_t IBS_TEMP_OFFSET   = 2;
static constexpr size_t IBS_HUM_OFFSET    = 4;
static constexpr size_t IBS_BAT_OFFSET    = 6;

BleScanner::BleScanner(AppState& state)
    : _state(state)
{
    _roomResult.ready    = false;
    _outsideResult.ready = false;
}

void BleScanner::begin() {
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);

    _scan = NimBLEDevice::getScan();
    _scan->setScanCallbacks(this, false); // false = don't save results
    _scan->setActiveScan(false);          // passive scan only
    _scan->setInterval(100);
    _scan->setWindow(99);

    Serial.println("[BleScanner] NimBLE initialized, passive scan");
    _scan->start(0, false); // continuous scan
    _lastScanStart = millis();
}

void BleScanner::update() {
    uint32_t now = millis();

    // Flush queued BLE results into AppState (called from main task)
    if (_roomResult.ready) {
        _roomResult.ready = false;
        IBSTh2Data copy = const_cast<IBSTh2Data&>(_roomResult.data);
        applyToState(true, copy);
    }
    if (_outsideResult.ready) {
        _outsideResult.ready = false;
        IBSTh2Data copy = const_cast<IBSTh2Data&>(_outsideResult.data);
        applyToState(false, copy);
    }

    // Check BLE timeout for room sensor
    const Config& cfg = _state.config;
    if (cfg.roomSensor.mode == TempSourceMode::BLE) {
        if (_state.sensors.hasRoom()) {
            uint32_t lastSeen = _state.sensors.roomLastSeen;
            if (lastSeen > 0 && now - lastSeen > Limits::SENSOR_TIMEOUT_MS) {
                _state.sensors.roomTemp = NAN;
                _state.alarms.set(AlarmFlag::ROOM_SENSOR_LOST);
            }
        }
    }
    if (cfg.outsideSensor.mode == TempSourceMode::BLE) {
        if (_state.sensors.hasOutside()) {
            uint32_t lastSeen = _state.sensors.outsideLastSeen;
            if (lastSeen > 0 && now - lastSeen > Limits::SENSOR_TIMEOUT_MS) {
                _state.sensors.outsideTemp = NAN;
                _state.alarms.set(AlarmFlag::OUTSIDE_SENSOR_LOST);
            }
        }
    }
}

// Static method
bool BleScanner::parseIBSTh2(const uint8_t* data, size_t len, IBSTh2Data& out) {
    if (len < IBS_MIN_LEN) {
        out.valid = false;
        return false;
    }

    // Temperature: signed int16 LE at offset 2, value = temp × 100
    int16_t rawTemp = (int16_t)((uint16_t)data[IBS_TEMP_OFFSET] |
                                 ((uint16_t)data[IBS_TEMP_OFFSET + 1] << 8));

    // Humidity: uint16 LE at offset 4, value = hum × 100
    uint16_t rawHum = (uint16_t)data[IBS_HUM_OFFSET] |
                       ((uint16_t)data[IBS_HUM_OFFSET + 1] << 8);

    out.temperature = rawTemp / 100.0f;
    out.humidity    = rawHum  / 100.0f;
    out.battery     = data[IBS_BAT_OFFSET];
    out.valid       = true;

    // Sanity check
    if (out.temperature < -40.0f || out.temperature > 85.0f) {
        out.valid = false;
        return false;
    }

    return true;
}

void BleScanner::onResult(const NimBLEAdvertisedDevice* device) {
    if (!device->haveManufacturerData()) return;

    const std::string& mfr = device->getManufacturerData();
    const uint8_t* data = (const uint8_t*)mfr.data();
    size_t len = mfr.size();

    std::string addr = device->getAddress().toString();
    const Config& cfg = _state.config;

    bool isRoom    = (cfg.roomSensor.mode    == TempSourceMode::BLE &&
                      strcasecmp(addr.c_str(), cfg.roomSensor.bleMac)    == 0);
    bool isOutside = (cfg.outsideSensor.mode == TempSourceMode::BLE &&
                      strcasecmp(addr.c_str(), cfg.outsideSensor.bleMac) == 0);

    if (!isRoom && !isOutside) return;

    IBSTh2Data parsed;
    if (!parseIBSTh2(data, len, parsed)) return;

    if (isRoom) {
        _roomResult.isRoom = true;
        _roomResult.data   = parsed;
        _roomResult.ready  = true;
    }
    if (isOutside) {
        _outsideResult.isRoom = false;
        _outsideResult.data   = parsed;
        _outsideResult.ready  = true;
    }
}

void BleScanner::applyToState(bool isRoom, const IBSTh2Data& data) {
    uint32_t now = millis();

    if (isRoom) {
        _state.sensors.roomTemp     = data.temperature;
        _state.sensors.roomLastSeen = now;
        _state.alarms.clear(AlarmFlag::ROOM_SENSOR_LOST);

        if (data.battery < Limits::BLE_LOW_BATTERY_PCT) {
            _state.alarms.set(AlarmFlag::BLE_ROOM_LOW_BATTERY);
        } else {
            _state.alarms.clear(AlarmFlag::BLE_ROOM_LOW_BATTERY);
        }
    } else {
        _state.sensors.outsideTemp     = data.temperature;
        _state.sensors.outsideLastSeen = now;
        _state.alarms.clear(AlarmFlag::OUTSIDE_SENSOR_LOST);

        if (data.battery < Limits::BLE_LOW_BATTERY_PCT) {
            _state.alarms.set(AlarmFlag::BLE_OUTSIDE_LOW_BATTERY);
        } else {
            _state.alarms.clear(AlarmFlag::BLE_OUTSIDE_LOW_BATTERY);
        }
    }
}

#pragma once
#include <OneWire.h>
#include <DallasTemperature.h>
#include "../model/AppState.h"

class DS18B20Driver {
public:
    explicit DS18B20Driver(AppState& state);
    void begin();
    void update();   // non-blocking, uses async conversion

private:
    void requestConversion();
    void readResults();
    bool isFaultValue(float v) const;  // -127 or 85 = DS18B20 error codes

    AppState&        _state;
    OneWire          _owFlow;
    OneWire          _owReturn;
    DallasTemperature _sensorFlow;
    DallasTemperature _sensorReturn;

    uint32_t _convStarted    = 0;
    bool     _convPending    = false;
    static constexpr uint32_t CONV_MS      = 750;   // 12-bit conversion time
    static constexpr uint32_t READ_INTERVAL= 2000;  // read every 2s
};

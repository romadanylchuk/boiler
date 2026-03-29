#include "DS18B20Driver.h"
#include "../model/pins.h"
#include <Arduino.h>

DS18B20Driver::DS18B20Driver(AppState& state)
    : _state(state)
    , _owFlow(PIN_DS18B20_FLOW)
    , _owReturn(PIN_DS18B20_RETURN)
    , _sensorFlow(&_owFlow)
    , _sensorReturn(&_owReturn)
{}

void DS18B20Driver::begin() {
    _sensorFlow.begin();
    _sensorReturn.begin();
    _sensorFlow.setWaitForConversion(false);
    _sensorReturn.setWaitForConversion(false);
    _sensorFlow.setResolution(12);
    _sensorReturn.setResolution(12);

    // Initialize state to NAN / fault
    _state.sensors.flowTemp   = NAN;
    _state.sensors.returnTemp = NAN;
    _state.sensors.flowSensorFault   = true;
    _state.sensors.returnSensorFault = true;

    Serial.printf("[DS18B20] flow sensors: %d, return sensors: %d\n",
                  _sensorFlow.getDeviceCount(),
                  _sensorReturn.getDeviceCount());

    // Kick off first conversion immediately
    requestConversion();
}

void DS18B20Driver::update() {
    uint32_t now = millis();

    if (_convPending) {
        if (now - _convStarted >= CONV_MS) {
            readResults();
            _convPending = false;
        }
    } else {
        if (now - _convStarted >= READ_INTERVAL) {
            requestConversion();
        }
    }
}

void DS18B20Driver::requestConversion() {
    _sensorFlow.requestTemperatures();
    _sensorReturn.requestTemperatures();
    _convStarted = millis();
    _convPending = true;
}

void DS18B20Driver::readResults() {
    uint32_t now = millis();

    float flow   = _sensorFlow.getTempCByIndex(0);
    float ret    = _sensorReturn.getTempCByIndex(0);

    // DEVICE_DISCONNECTED_C is -127 from DallasTemperature
    bool flowFault   = isFaultValue(flow)   || flow == DEVICE_DISCONNECTED_C;
    bool returnFault = isFaultValue(ret)    || ret == DEVICE_DISCONNECTED_C;

    // DS18B20 runs in the Arduino main task — direct write is safe
    _state.sensors.flowSensorFault   = flowFault;
    _state.sensors.returnSensorFault = returnFault;

    if (!flowFault) {
        _state.sensors.flowTemp      = flow;
        _state.sensors.flowLastSeen  = now;
    } else {
        _state.sensors.flowTemp = NAN;
    }

    if (!returnFault) {
        _state.sensors.returnTemp     = ret;
        _state.sensors.returnLastSeen = now;
    } else {
        _state.sensors.returnTemp = NAN;
    }
}

bool DS18B20Driver::isFaultValue(float v) const {
    // DS18B20 error codes: -127.0 (DEVICE_DISCONNECTED) and 85.0 (power-on default)
    return (v <= -126.5f) || (v >= 84.5f && v <= 85.5f);
}

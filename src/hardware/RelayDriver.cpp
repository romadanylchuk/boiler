#include "RelayDriver.h"
#include "../model/pins.h"

RelayDriver::RelayDriver(AppState& state)
    : _state(state)
{}

void RelayDriver::begin() {
    pinMode(PIN_RELAY_HEATER, OUTPUT);
    pinMode(PIN_RELAY_PUMP,   OUTPUT);
    digitalWrite(PIN_RELAY_HEATER, LOW);
    digitalWrite(PIN_RELAY_PUMP,   LOW);

    _state.relays.heaterOn      = false;
    _state.relays.pumpOn        = false;
    _state.relays.heaterOnSince  = 0;
    _state.relays.heaterOffSince = millis();
    _state.relays.pumpOnSince    = 0;
    _state.relays.pumpOffSince   = millis();

    Serial.println("[RelayDriver] Relays initialized (both OFF)");
}

void RelayDriver::setHeater(bool on) {
    if (_state.relays.heaterOn == on) return;

    digitalWrite(PIN_RELAY_HEATER, on ? HIGH : LOW);
    _state.relays.heaterOn = on;

    uint32_t now = millis();
    if (on) {
        _state.relays.heaterOnSince  = now;
    } else {
        _state.relays.heaterOffSince = now;
    }

    Serial.printf("[RelayDriver] Heater %s\n", on ? "ON" : "OFF");
}

void RelayDriver::setPump(bool on) {
    if (_state.relays.pumpOn == on) return;

    digitalWrite(PIN_RELAY_PUMP, on ? HIGH : LOW);
    _state.relays.pumpOn = on;

    uint32_t now = millis();
    if (on) {
        _state.relays.pumpOnSince  = now;
    } else {
        _state.relays.pumpOffSince = now;
    }

    Serial.printf("[RelayDriver] Pump %s\n", on ? "ON" : "OFF");
}

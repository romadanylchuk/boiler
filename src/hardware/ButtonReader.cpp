#include "ButtonReader.h"
#include <Wire.h>
#include "../model/pins.h"

ButtonReader::ButtonReader(AppState& state)
    : _state(state)
{}

void ButtonReader::begin() {
    // PCF8574 is quasi-bidirectional: writing 1 enables the weak pull-up,
    // so the pin works as an input. Buttons pull pins to GND when pressed.
    Wire.beginTransmission(I2C_ADDR_PCF8574);
    Wire.write(0xFF);
    bool ok = Wire.endTransmission() == 0;

    _stable = _candidate = BTN_MASK;
    _candSince = _lastPoll = millis();
    _pending = ButtonEvent::NONE;

    Serial.printf("[ButtonReader] PCF8574 %s, polling every %lu ms\n",
                  ok ? "initialized" : "NOT FOUND", (unsigned long)POLL_MS);
}

void ButtonReader::update() {
    uint32_t now = millis();
    if (now - _lastPoll < POLL_MS) return;
    _lastPoll = now;

    if (Wire.requestFrom((uint8_t)I2C_ADDR_PCF8574, (uint8_t)1) != 1) return;  // bus error → skip
    uint8_t raw = Wire.read() & BTN_MASK;

    // Debounce: accept a new state only after it has been stable for DEBOUNCE_MS
    if (raw != _candidate) {
        _candidate = raw;
        _candSince = now;
        return;
    }
    if (now - _candSince < DEBOUNCE_MS || raw == _stable) return;

    uint8_t pressed = _stable & ~raw;  // 1→0 transitions = press
    _stable = raw;

    for (uint8_t pin = 0; pin < 5; pin++) {
        if (!(pressed & (1 << pin))) continue;
        ButtonEvent ev = mapPinToEvent(pin);
        if (ev == ButtonEvent::NONE) continue;
        _pending = ev;
        _state.lastHwButton = static_cast<uint8_t>(ev);
        Serial.printf("[ButtonReader] Button event: %d\n", (int)ev);
    }
}

ButtonEvent ButtonReader::mapPinToEvent(uint8_t pin) const {
    switch (pin) {
        case BTN_PIN_UP:       return ButtonEvent::UP;
        case BTN_PIN_DOWN:     return ButtonEvent::DOWN;
        case BTN_PIN_ENTER:    return ButtonEvent::ENTER;
        case BTN_PIN_BACK:     return ButtonEvent::BACK;
        case BTN_PIN_SETTINGS: return ButtonEvent::SETTINGS;
        default:               return ButtonEvent::NONE;
    }
}

ButtonEvent ButtonReader::consume() {
    ButtonEvent ev = _pending;
    _pending = ButtonEvent::NONE;
    return ev;
}

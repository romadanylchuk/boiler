#include "ButtonReader.h"
#include "../model/pins.h"

ButtonReader* ButtonReader::_instance = nullptr;

ButtonReader::ButtonReader(AppState& state)
    : _state(state)
    , _pcf(I2C_ADDR_PCF8574)
{}

void IRAM_ATTR ButtonReader::onInterrupt() {
    if (_instance) {
        _instance->_intFlag = true;
    }
}

void ButtonReader::begin() {
    _instance = this;

    _pcf.begin();
    for (uint8_t i = 0; i < 5; i++) _pcf.pinMode(i, INPUT);
    _lastState = 0xFF;

    memset(_lastPress, 0, sizeof(_lastPress));
    _pending = ButtonEvent::NONE;

    // Configure INT pin (active LOW, pulled up externally on the board)
    pinMode(PIN_PCF_INT, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_PCF_INT), onInterrupt, FALLING);

    Serial.println("[ButtonReader] PCF8574 initialized, INT on GPIO34");
}

void ButtonReader::update() {
    if (_intFlag) {
        _intFlag = false;
        readPcf();
    }
}

void ButtonReader::readPcf() {
    _pcf.readBuffer();
    uint8_t current = 0;
    for (uint8_t i = 0; i < 5; i++) {
        if (_pcf.digitalRead(i) == HIGH) current |= (1 << i);
    }

    // Detect falling edges (button press = pin goes LOW)
    uint8_t pressed = (_lastState & ~current) & 0x1F; // mask to P0-P4 only
    _lastState = current;

    if (pressed == 0) return;

    uint32_t now = millis();

    for (uint8_t pin = 0; pin < 5; pin++) {
        if (pressed & (1 << pin)) {
            if (isDebounced(pin)) {
                _lastPress[pin] = now;
                ButtonEvent ev = mapPinToEvent(pin);
                if (ev != ButtonEvent::NONE) {
                    _pending = ev;
                    Serial.printf("[ButtonReader] Button event: %d\n", (int)ev);
                }
            }
        }
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

bool ButtonReader::isDebounced(uint8_t pin) const {
    return (millis() - _lastPress[pin]) >= DEBOUNCE_MS;
}

ButtonEvent ButtonReader::consume() {
    ButtonEvent ev = _pending;
    _pending = ButtonEvent::NONE;
    return ev;
}

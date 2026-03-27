#pragma once
#include <Arduino.h>
#include <PCF8574.h>
#include "../model/AppState.h"

enum class ButtonEvent : uint8_t {
    NONE,
    UP,
    DOWN,
    ENTER,
    BACK,
    SETTINGS
};

class ButtonReader {
public:
    explicit ButtonReader(AppState& state);
    void begin();
    void update();  // call from loop — reads PCF8574 if INT fired

    // Returns pending event and clears it (consume pattern)
    ButtonEvent consume();
    bool        hasEvent() const { return _pending != ButtonEvent::NONE; }

private:
    static void IRAM_ATTR onInterrupt();  // ISR — sets _intFlag

    void readPcf();
    ButtonEvent mapPinToEvent(uint8_t pin) const;
    bool isDebounced(uint8_t pin) const;

    AppState&  _state;
    PCF8574    _pcf;

    volatile bool _intFlag    = false;
    uint8_t       _lastState  = 0xFF;  // all HIGH (unpressed) initially
    uint32_t      _lastPress[5] = {};  // debounce timestamps per button
    ButtonEvent   _pending    = ButtonEvent::NONE;

    static constexpr uint32_t DEBOUNCE_MS = 50;

    static ButtonReader* _instance;  // for ISR callback
};

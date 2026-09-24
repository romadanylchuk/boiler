#pragma once
#include <Arduino.h>
#include "../model/AppState.h"

enum class ButtonEvent : uint8_t {
    NONE,
    UP,
    DOWN,
    ENTER,
    BACK,
    SETTINGS
};

// Polls the PCF8574 over I2C every POLL_MS (INT pin is not used).
class ButtonReader {
public:
    explicit ButtonReader(AppState& state);
    void begin();
    void update();  // call from loop — polls PCF8574 and debounces

    // Returns pending event and clears it (consume pattern)
    ButtonEvent consume();
    bool        hasEvent() const { return _pending != ButtonEvent::NONE; }

private:
    ButtonEvent mapPinToEvent(uint8_t pin) const;

    AppState&  _state;

    uint8_t     _stable    = 0x1F;  // debounced P0–P4 state, 1 = released
    uint8_t     _candidate = 0x1F;  // last raw read
    uint32_t    _candSince = 0;     // when _candidate was first seen
    uint32_t    _lastPoll  = 0;
    ButtonEvent _pending   = ButtonEvent::NONE;

    static constexpr uint8_t  BTN_MASK    = 0x1F;  // P0–P4
    static constexpr uint32_t POLL_MS     = 20;
    static constexpr uint32_t DEBOUNCE_MS = 40;    // raw state must hold this long
};

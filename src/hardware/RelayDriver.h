#pragma once
#include <Arduino.h>
#include "../model/AppState.h"

class RelayDriver {
public:
    explicit RelayDriver(AppState& state);
    void begin();

    void setHeater(bool on);
    void setPump(bool on);

    bool heaterOn() const { return _state.relays.heaterOn; }
    bool pumpOn()   const { return _state.relays.pumpOn; }

private:
    AppState& _state;
};

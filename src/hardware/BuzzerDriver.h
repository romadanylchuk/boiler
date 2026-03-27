#pragma once
#include <Arduino.h>
#include "../model/AppState.h"

enum class BuzzerPattern : uint8_t {
    SILENT,
    CONTINUOUS,         // overheat
    FAULT_INTERMITTENT, // 3 beeps, 2s pause, repeat
    WARNING_SINGLE,     // 1 long beep
};

class BuzzerDriver {
public:
    explicit BuzzerDriver(AppState& state);
    void begin();
    void update();  // call from loop — manages non-blocking patterns

    void setPattern(BuzzerPattern pattern);
    void beep(uint8_t count, uint16_t durationMs);  // one-shot beep
    void stop();

private:
    AppState&      _state;
    BuzzerPattern  _pattern    = BuzzerPattern::SILENT;
    uint32_t       _patternAt  = 0;
    uint8_t        _beepCount  = 0;
    uint8_t        _beepsDone  = 0;
    uint16_t       _beepDurMs  = 0;
    bool           _oneShot    = false;
};

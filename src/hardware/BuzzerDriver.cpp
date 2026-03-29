#include "BuzzerDriver.h"
#include "../model/pins.h"

// Timing constants for FAULT_INTERMITTENT pattern
// 3 beeps of 200ms with 200ms gaps, then 2000ms pause, repeat
static constexpr uint16_t FAULT_BEEP_ON_MS  = 200;
static constexpr uint16_t FAULT_BEEP_OFF_MS = 200;
static constexpr uint16_t FAULT_PAUSE_MS    = 2000;
static constexpr uint8_t  FAULT_BEEP_COUNT  = 3;

// WARNING_SINGLE: one long 800ms beep then 2000ms pause
static constexpr uint16_t WARN_BEEP_ON_MS   = 800;
static constexpr uint16_t WARN_PAUSE_MS      = 2000;

BuzzerDriver::BuzzerDriver(AppState& state)
    : _state(state)
{}

void BuzzerDriver::begin() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    Serial.println("[BuzzerDriver] Initialized");
}

void BuzzerDriver::update() {
    uint32_t now = millis();

    if (_oneShot) {
        // One-shot: beep N times with fixed duration, then stop
        // State machine: _beepsDone counts completed beeps (on+off = 1 cycle)
        // _patternAt = time last state change happened
        // _beepCount = total beeps requested
        // We track high/low using pin state
        bool pinHigh = (digitalRead(PIN_BUZZER) == HIGH);

        if (pinHigh) {
            // Currently beeping — check if ON time elapsed
            if (now - _patternAt >= _beepDurMs) {
                digitalWrite(PIN_BUZZER, LOW);
                _patternAt = now;
                _beepsDone++;
                if (_beepsDone >= _beepCount) {
                    _oneShot = false;
                }
            }
        } else {
            // Currently silent gap between one-shot beeps
            if (_beepsDone < _beepCount && now - _patternAt >= FAULT_BEEP_OFF_MS) {
                digitalWrite(PIN_BUZZER, HIGH);
                _patternAt = now;
            }
        }
        return;
    }

    switch (_pattern) {
        case BuzzerPattern::SILENT:
            digitalWrite(PIN_BUZZER, LOW);
            break;

        case BuzzerPattern::CONTINUOUS:
            digitalWrite(PIN_BUZZER, HIGH);
            break;

        case BuzzerPattern::FAULT_INTERMITTENT: {
            // 3 beeps (200ms on / 200ms off) then 2s pause, repeat
            // _beepsDone = how many full on+off cycles done in current group
            bool pinHigh2 = (digitalRead(PIN_BUZZER) == HIGH);

            if (_beepsDone < FAULT_BEEP_COUNT) {
                if (pinHigh2) {
                    if (now - _patternAt >= FAULT_BEEP_ON_MS) {
                        digitalWrite(PIN_BUZZER, LOW);
                        _patternAt = now;
                        _beepsDone++;
                    }
                } else {
                    if (now - _patternAt >= FAULT_BEEP_OFF_MS) {
                        if (_beepsDone < FAULT_BEEP_COUNT) {
                            digitalWrite(PIN_BUZZER, HIGH);
                            _patternAt = now;
                        }
                    }
                }
            } else {
                // Pause phase
                if (now - _patternAt >= FAULT_PAUSE_MS) {
                    _beepsDone = 0;
                    _patternAt = now;
                    digitalWrite(PIN_BUZZER, HIGH); // start next group
                }
            }
            break;
        }

        case BuzzerPattern::WARNING_SINGLE: {
            // 1 long beep (800ms) then 2s pause, repeat
            bool pinHigh3 = (digitalRead(PIN_BUZZER) == HIGH);

            if (pinHigh3) {
                if (now - _patternAt >= WARN_BEEP_ON_MS) {
                    digitalWrite(PIN_BUZZER, LOW);
                    _patternAt = now;
                }
            } else {
                if (now - _patternAt >= WARN_PAUSE_MS) {
                    digitalWrite(PIN_BUZZER, HIGH);
                    _patternAt = now;
                }
            }
            break;
        }
    }
}

void BuzzerDriver::setPattern(BuzzerPattern pattern) {
    if (_pattern == pattern && !_oneShot) return;

    _oneShot  = false;
    _pattern  = pattern;
    _patternAt = millis();
    _beepsDone = 0;

    // Apply immediate state for simple patterns
    if (pattern == BuzzerPattern::SILENT) {
        digitalWrite(PIN_BUZZER, LOW);
    } else if (pattern == BuzzerPattern::CONTINUOUS) {
        digitalWrite(PIN_BUZZER, HIGH);
    } else if (pattern == BuzzerPattern::FAULT_INTERMITTENT) {
        // Start first beep immediately
        digitalWrite(PIN_BUZZER, HIGH);
    } else if (pattern == BuzzerPattern::WARNING_SINGLE) {
        digitalWrite(PIN_BUZZER, HIGH);
    }
}

void BuzzerDriver::beep(uint8_t count, uint16_t durationMs) {
    _oneShot    = true;
    _beepCount  = count;
    _beepsDone  = 0;
    _beepDurMs  = durationMs;
    _patternAt  = millis();
    digitalWrite(PIN_BUZZER, HIGH);
}

void BuzzerDriver::stop() {
    _oneShot  = false;
    _pattern  = BuzzerPattern::SILENT;
    _beepsDone = 0;
    digitalWrite(PIN_BUZZER, LOW);
}

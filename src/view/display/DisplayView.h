#pragma once
#include <U8g2lib.h>
#include "../../model/AppState.h"
#include "../../hardware/ButtonReader.h"

enum class DisplayScreen : uint8_t {
    MAIN,       // flow (big), return/room/outside (small), icons
    SETPOINTS,  // active control values
    LOG,        // event log scrollable
    BOOT,
    SETUP_REQUIRED,
};

class DisplayView {
public:
    explicit DisplayView(AppState& state);
    void begin();
    void update();   // call from loop — redraws at ~2 Hz

    void showBoot();
    void showSetupRequired();
    void handleButton(ButtonEvent btn);  // called by main loop

    void nextScreen();

private:
    void drawMain();
    void drawSetpoints();
    void drawLog();
    void drawStatusIcons(uint8_t x, uint8_t y);
    void drawTempLarge(float temp, uint8_t x, uint8_t y);
    void drawTempSmall(const char* label, float temp, uint8_t x, uint8_t y);
    const char* formatReason(ChangeReason r) const;

    AppState&      _state;
    U8G2_SSD1309_128X64_NONAME0_F_HW_I2C _u8g2;

    DisplayScreen  _screen       = DisplayScreen::BOOT;
    uint32_t       _lastDraw     = 0;
    uint16_t       _logScrollIdx = 0;

    static constexpr uint32_t DRAW_INTERVAL_MS = 500;  // 2 Hz refresh
};

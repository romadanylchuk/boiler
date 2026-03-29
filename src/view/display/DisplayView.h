#pragma once
#include <U8g2lib.h>
#include "../../model/AppState.h"
#include "../../hardware/ButtonReader.h"
#include "../../service/TimeService.h"

enum class DisplayScreen : uint8_t {
    MAIN,       // flow (big), return/room/outside (small), icons
    SETPOINTS,  // active control values
    LOG,        // event log scrollable
    SETTINGS,   // editable setpoints (flow/return + hysteresis)
    BOOT,
    SETUP_REQUIRED,
    IP_INFO,    // transient: shows IP after STA connect, then auto-advances to MAIN
};

class DisplayView {
public:
    DisplayView(AppState& state, TimeService& timeService);
    void begin();
    void update();   // call from loop — redraws at ~2 Hz

    void showBoot();
    void showSetupRequired();
    void showMain();
    void showIp(const char* ip);         // transient IP screen after STA connect
    void handleButton(ButtonEvent btn);  // called by main loop

    void nextScreen();

private:
    void drawMain();
    void drawSetpoints();
    void drawLog();
    void drawSettings();
    void drawTopBar();                   // status icons + time — drawn on every screen
    void drawButtons();                  // 5 button hint icons — drawn on every screen
    void drawTempLarge(float temp, uint8_t x, uint8_t y);
    void drawIcon(uint8_t x, uint8_t y, const uint8_t* icon8x8);
    void drawTempSmall(const uint8_t* icon8x8, float temp, uint8_t x, uint8_t y);
    const char* formatReason(ChangeReason r) const;

    AppState&    _state;
    TimeService& _timeService;
    U8G2_SSD1309_128X64_NONAME0_F_HW_I2C _u8g2;

    DisplayScreen  _screen       = DisplayScreen::BOOT;
    uint32_t       _lastDraw     = 0;
    uint16_t       _logScrollIdx = 0;
    uint32_t       _ipShowStart  = 0;
    char           _ipBuf[20]    = {};

    // Settings screen state
    uint8_t        _settingsCursor  = 0;       // 0-3: selected param
    uint8_t        _settingsTemp[4] = {};       // working copy: [flowSP, flowHyst, returnSP, returnHyst]

    static constexpr uint32_t DRAW_INTERVAL_MS = 500;   // 2 Hz refresh
    static constexpr uint32_t IP_SHOW_MS       = 8000;  // 8 s IP screen
};

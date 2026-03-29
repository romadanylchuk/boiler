#include "DisplayView.h"
#include <Arduino.h>
#include <WiFi.h>
#include "../../service/NvsConfig.h"

extern NvsConfig nvsConfig;

// ── 8×8 pixel icons (MSB first: bit7 = left column) ─────────────────────────
// Each row is one byte. Visualise: 1=lit, 0=dark, left→right = bit7→bit0.
//
//  ICO_WIFI_ON   ICO_WIFI_OFF  ICO_AP        ICO_LINK_ON   ICO_LINK_OFF
//  .XXXXXX.      X.....X.      ...X....      ........      .XX.....
//  X......X      .X...X..      ..X.X...      .XX.XX..      X..X....
//  ..XXXX..      ..X.X...      .X...X..      X..X..X.      X..X....
//  ..X..X..      ...X....      ...X....      X..X..X.      .XX.....
//  ...XX...      ..X.X...      ...X....      .XX.XX..      ....XX..
//  ...XX...      .X...X..      ..XXX...      ........      ...X..X.
//  ........      X.....X.      ...X....      ........      ...X..X.
//  ........      ........      ........      ........      ....XX..
static const uint8_t ICO_WIFI_ON[]  = {0x7E,0x81,0x3C,0x24,0x18,0x18,0x00,0x00};
static const uint8_t ICO_WIFI_OFF[] = {0x82,0x44,0x28,0x10,0x28,0x44,0x82,0x00};
static const uint8_t ICO_AP[]       = {0x10,0x28,0x44,0x10,0x10,0x38,0x10,0x00};
static const uint8_t ICO_LINK_ON[]  = {0x00,0x6C,0x92,0x92,0x6C,0x00,0x00,0x00};
static const uint8_t ICO_LINK_OFF[] = {0x60,0x90,0x90,0x60,0x0C,0x12,0x12,0x0C};

//  ICO_FLAME     ICO_POWER     ICO_SNOW      ICO_WARNING   ICO_PUMP
//  ...X....      ...X....      ...X....      ...X....      ..XXX...
//  ..XXX...      ...X....      .X.X.X..      ..X.X...      .X...X..
//  .XXXXX..      .X...X..      ..XXX...      .X.X.X..      X.....X.
//  XXXXXXX.      X.....X.      XXXXXXXX      X..X..X.      XXXX..X.
//  XXXXXXX.      X.....X.      ..XXX...      X..X..X.      X...XX..
//  XXXXXXX.      .X...X..      .X.X.X..      X.....X.      X.....X.
//  .XXXXX..      ..XXX...      ...X....      XXXXXXX.      .X...X..
//  ..XXX...      ........      ........      ........      ..XXX...
static const uint8_t ICO_FLAME[]    = {0x10,0x38,0x7C,0xFE,0xFE,0xFE,0x7C,0x38};
static const uint8_t ICO_POWER[]    = {0x10,0x10,0x44,0x82,0x82,0x44,0x38,0x00};
static const uint8_t ICO_SNOW[]     = {0x10,0x54,0x38,0xFF,0x38,0x54,0x10,0x00};
static const uint8_t ICO_WARNING[]  = {0x10,0x28,0x54,0x92,0x92,0x82,0xFE,0x00};
static const uint8_t ICO_PUMP[]     = {0x38,0x44,0x82,0xF2,0x8C,0x82,0x44,0x38};

//  ICO_PIPE      ICO_HOUSE     ICO_SUN
//  .X...X..      ...X....      ...X....
//  ..X..X..      ..X.X...      .X.X.X..
//  ...X.X..      .X...X..      ..XXX...
//  ....XX..      X.....X.      XX.X.XX.
//  ...X.X..      .XXXXX..      XX.X.XX.
//  ..X..X..      .XX.XX..      ..XXX...
//  .X...X..      .XX.XX..      .X.X.X..
//  ........      .XXXXX..      ...X....
static const uint8_t ICO_PIPE[]     = {0x44,0x24,0x14,0x0C,0x14,0x24,0x44,0x00};
static const uint8_t ICO_HOUSE[]    = {0x10,0x28,0x44,0x82,0x7C,0x6C,0x6C,0x7C};
static const uint8_t ICO_SUN[]      = {0x10,0x54,0x38,0xD6,0xD6,0x38,0x54,0x10};

//  Button hint icons (bottom bar, left→right: UP DOWN ENTER BACK SETTINGS)
//
//  ICO_BTN_UP    ICO_BTN_DN    ICO_BTN_ENT   ICO_BTN_BCK   ICO_BTN_SET
//  ...X....      ...X....      X.......      ...X....      .X.X.X..
//  ..XXX...      ...X....      XX......      ..X.....      .XXXXX..
//  .XXXXX..      ...X....      XXXX....      .XXXXXX.      XX.X.XX.
//  XXXXXXX.      XXXXXXX.      XXXXXXXX      XXXXXXX.      XXXXXXX.
//  ...X....      .XXXXX..      XXXXXXXX      XXXXXXX.      XXXXXXX.
//  ...X....      ..XXX...      XXXX....      .XXXXXX.      XX.X.XX.
//  ...X....      ...X....      XX......      ..X.....      .XXXXX..
//  ...X....      ...X....      X.......      ...X....      .X.X.X..
static const uint8_t ICO_BTN_UP[]   = {0x10,0x38,0x7C,0xFE,0x10,0x10,0x10,0x10};
static const uint8_t ICO_BTN_DN[]   = {0x10,0x10,0x10,0xFE,0x7C,0x38,0x10,0x10};
static const uint8_t ICO_BTN_ENT[]  = {0x80,0xC0,0xF0,0xFF,0xFF,0xF0,0xC0,0x80};
static const uint8_t ICO_BTN_BCK[]  = {0x10,0x20,0x7E,0xFE,0xFE,0x7E,0x20,0x10};
static const uint8_t ICO_BTN_SET[]  = {0x54,0x7C,0xD6,0xFE,0xFE,0xD6,0x7C,0x54};

// ─────────────────────────────────────────────────────────────────────────────

DisplayView::DisplayView(AppState& state, TimeService& timeService)
    : _state(state)
    , _timeService(timeService)
    , _u8g2(U8G2_R0, /* reset= */ U8X8_PIN_NONE)
{}

void DisplayView::begin() {
    _u8g2.begin();
    _u8g2.setContrast(200);
    Serial.println("[DisplayView] OLED initialized");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Persistent chrome — drawn on every screen
// ─────────────────────────────────────────────────────────────────────────────

// Top status bar: icons left, time right. Separator line below at y=8.
void DisplayView::drawTopBar() {
    uint8_t xpos = 0;

    // WiFi
    const uint8_t* wifiIcon = _state.status.apMode        ? ICO_AP
                             : _state.status.wifiConnected ? ICO_WIFI_ON
                             :                               ICO_WIFI_OFF;
    drawIcon(xpos, 7, wifiIcon);  xpos += 10;

    // MQTT
    drawIcon(xpos, 7, _state.status.mqttConnected ? ICO_LINK_ON : ICO_LINK_OFF);
    xpos += 10;

    // Mode
    const uint8_t* modeIcon = ICO_POWER;
    if (_state.status.mode == SystemMode::ON)         modeIcon = ICO_FLAME;
    if (_state.status.mode == SystemMode::ANTIFREEZE) modeIcon = ICO_SNOW;
    drawIcon(xpos, 7, modeIcon);  xpos += 10;

    // Alarm
    if (_state.alarms.any()) {
        drawIcon(xpos, 7, ICO_WARNING);
    }

    // Time (right-aligned, 5x8 font, "HH:MM")
    char timeBuf[24];
    _timeService.formatTime(timeBuf, sizeof(timeBuf));
    char hmBuf[6];
    if (timeBuf[0] == '+') {
        // Uptime format: "+HH:MM:SS" — extract HH:MM at pos 1
        strncpy(hmBuf, timeBuf + 1, 5);
    } else {
        // NTP format: "YYYY-MM-DD HH:MM:SS" — extract HH:MM at pos 11
        strncpy(hmBuf, timeBuf + 11, 5);
    }
    hmBuf[5] = '\0';
    _u8g2.setFont(u8g2_font_5x8_tr);
    _u8g2.drawStr(97, 6, hmBuf);

    // Separator
    _u8g2.drawLine(0, 8, 127, 8);
}

// Bottom button bar: separator line above, then 5 hint icons.
void DisplayView::drawButtons() {
    _u8g2.drawLine(0, 53, 127, 53);
    // Order left→right: UP DOWN ENTER BACK SETTINGS  (physical button order)
    static const uint8_t* const btnIcons[5] = {
        ICO_BTN_UP, ICO_BTN_DN, ICO_BTN_ENT, ICO_BTN_BCK, ICO_BTN_SET
    };
    static const uint8_t btnX[5] = {9, 34, 60, 86, 111};
    for (uint8_t i = 0; i < 5; i++) {
        drawIcon(btnX[i], 63, btnIcons[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public screen setters
// ─────────────────────────────────────────────────────────────────────────────

void DisplayView::showBoot() {
    _screen = DisplayScreen::BOOT;
    _u8g2.clearBuffer();
    drawTopBar();
    _u8g2.setFont(u8g2_font_logisoso16_tr);
    _u8g2.drawStr(8, 36, "Boiler FW");
    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(18, 50, "Starting...");
    drawButtons();
    _u8g2.sendBuffer();
}

void DisplayView::showMain() {
    _screen = DisplayScreen::MAIN;
    _logScrollIdx = 0;
    drawMain();
}

void DisplayView::showIp(const char* ip) {
    strncpy(_ipBuf, ip, sizeof(_ipBuf) - 1);
    _ipBuf[sizeof(_ipBuf) - 1] = '\0';
    _ipShowStart = millis();
    _screen = DisplayScreen::IP_INFO;

    _u8g2.clearBuffer();
    drawTopBar();
    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(0, 22, "WiFi connected!");
    _u8g2.drawStr(0, 36, "IP address:");
    _u8g2.drawStr(0, 50, _ipBuf);
    drawButtons();
    _u8g2.sendBuffer();
}

void DisplayView::showSetupRequired() {
    _screen = DisplayScreen::SETUP_REQUIRED;
    _u8g2.clearBuffer();
    drawTopBar();
    _u8g2.setFont(u8g2_font_6x10_tr);

    if (_state.status.apMode) {
        char ipStr[24];
        snprintf(ipStr, sizeof(ipStr), "http://%s",
                 WiFi.softAPIP().toString().c_str());
        _u8g2.drawStr(0, 20, "Connect to WiFi:");
        _u8g2.drawStr(0, 30, _state.status.apSsid);
        _u8g2.drawStr(0, 40, "Pass: boilersetup");
        _u8g2.drawStr(0, 50, ipStr);
    } else {
        _u8g2.drawStr(0, 20, "Setup required!");
        _u8g2.drawStr(0, 33, "Open browser:");
        char ipStr[32];
        snprintf(ipStr, sizeof(ipStr), "http://%s",
                 WiFi.localIP().toString().c_str());
        _u8g2.drawStr(0, 46, ipStr);
    }

    drawButtons();
    _u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Update loop
// ─────────────────────────────────────────────────────────────────────────────

void DisplayView::update() {
    // Consume any button event injected by the web interface
    uint8_t webBtn = _state.pendingWebButton;
    if (webBtn != 0) {
        _state.pendingWebButton = 0;
        handleButton(static_cast<ButtonEvent>(webBtn));
    }

    uint32_t now = millis();
    if (now - _lastDraw < DRAW_INTERVAL_MS) return;
    _lastDraw = now;

    switch (_screen) {
        case DisplayScreen::MAIN:       drawMain();       break;
        case DisplayScreen::SETPOINTS:  drawSetpoints();  break;
        case DisplayScreen::LOG:        drawLog();        break;
        case DisplayScreen::SETTINGS:   drawSettings();   break;
        case DisplayScreen::IP_INFO:
            if (now - _ipShowStart >= IP_SHOW_MS) {
                showMain();
            } else {
                // Redraw to refresh time in top bar
                _u8g2.clearBuffer();
                drawTopBar();
                _u8g2.setFont(u8g2_font_6x10_tr);
                _u8g2.drawStr(0, 22, "WiFi connected!");
                _u8g2.drawStr(0, 36, "IP address:");
                _u8g2.drawStr(0, 50, _ipBuf);
                drawButtons();
                _u8g2.sendBuffer();
            }
            break;
        case DisplayScreen::BOOT:
        case DisplayScreen::SETUP_REQUIRED:
            // Static — only redraw top bar for time update
            _u8g2.clearBuffer();
            if (_screen == DisplayScreen::BOOT) {
                drawTopBar();
                _u8g2.setFont(u8g2_font_logisoso16_tr);
                _u8g2.drawStr(8, 36, "Boiler FW");
                _u8g2.setFont(u8g2_font_6x10_tr);
                _u8g2.drawStr(18, 50, "Starting...");
            } else {
                showSetupRequired();
                return;  // showSetupRequired calls sendBuffer itself
            }
            drawButtons();
            _u8g2.sendBuffer();
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Button handling
// ─────────────────────────────────────────────────────────────────────────────

void DisplayView::handleButton(ButtonEvent btn) {
    if (_screen == DisplayScreen::BOOT || _screen == DisplayScreen::SETUP_REQUIRED) return;

    // ── Settings screen has its own navigation ────────────────────────────────
    if (_screen == DisplayScreen::SETTINGS) {
        static const struct { uint8_t min; uint8_t max; } LIMITS[4] = {
            {20, 65},   // Flow SP
            { 1, 10},   // Flow Hyst
            {20, 65},   // Return SP
            { 1, 10},   // Return Hyst
        };
        switch (btn) {
            case ButtonEvent::UP:
                if (_settingsTemp[_settingsCursor] < LIMITS[_settingsCursor].max)
                    _settingsTemp[_settingsCursor]++;
                break;
            case ButtonEvent::DOWN:
                if (_settingsTemp[_settingsCursor] > LIMITS[_settingsCursor].min)
                    _settingsTemp[_settingsCursor]--;
                break;
            case ButtonEvent::ENTER:
                _settingsCursor = (_settingsCursor + 1) % 4;
                break;
            case ButtonEvent::BACK:
                // Discard — return to MAIN without saving
                _screen = DisplayScreen::MAIN;
                break;
            case ButtonEvent::SETTINGS:
                // Save to config + NVS, then return to MAIN
                _state.config.flowSetpoint     = _settingsTemp[0];
                _state.config.flowHysteresis   = _settingsTemp[1];
                _state.config.returnSetpoint   = _settingsTemp[2];
                _state.config.returnHysteresis = _settingsTemp[3];
                nvsConfig.saveSetpoints(_state.config);
                _screen = DisplayScreen::MAIN;
                break;
            default: break;
        }
        return;
    }

    // ── All other screens ─────────────────────────────────────────────────────
    switch (btn) {
        case ButtonEvent::UP:
            if (_screen == DisplayScreen::LOG && _logScrollIdx > 0)
                _logScrollIdx--;
            break;
        case ButtonEvent::DOWN:
            if (_screen == DisplayScreen::LOG) {
                uint16_t maxIdx = (_state.log.count > 5) ? _state.log.count - 5 : 0;
                if (_logScrollIdx < maxIdx) _logScrollIdx++;
            }
            break;
        case ButtonEvent::BACK:
            _screen = DisplayScreen::MAIN;
            _logScrollIdx = 0;
            break;
        case ButtonEvent::ENTER:
            nextScreen();
            break;
        case ButtonEvent::SETTINGS:
            // Enter settings — initialise working copy from live config
            _settingsCursor  = 0;
            _settingsTemp[0] = _state.config.flowSetpoint;
            _settingsTemp[1] = _state.config.flowHysteresis;
            _settingsTemp[2] = _state.config.returnSetpoint;
            _settingsTemp[3] = _state.config.returnHysteresis;
            _screen = DisplayScreen::SETTINGS;
            break;
        default: break;
    }
}

void DisplayView::nextScreen() {
    switch (_screen) {
        case DisplayScreen::MAIN:      _screen = DisplayScreen::SETPOINTS; break;
        case DisplayScreen::SETPOINTS: _screen = DisplayScreen::LOG;       break;
        case DisplayScreen::LOG:       _screen = DisplayScreen::MAIN;      break;
        default:                       _screen = DisplayScreen::MAIN;      break;
    }
    _logScrollIdx = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw helpers
// ─────────────────────────────────────────────────────────────────────────────

static void tempToStr(float t, char* buf, size_t len) {
    if (isnan(t)) {
        snprintf(buf, len, "---");
    } else {
        snprintf(buf, len, "%.1f", t);
    }
}

void DisplayView::drawTempLarge(float temp, uint8_t x, uint8_t y) {
    char buf[10];
    tempToStr(temp, buf, sizeof(buf));
    strncat(buf, "\xb0", sizeof(buf) - strlen(buf) - 1);  // ° only
    _u8g2.setFont(u8g2_font_logisoso24_tr);
    _u8g2.drawStr(x, y, buf);
}

// Draw an 8×8 bitmap with its bottom edge at y (matches text baseline).
void DisplayView::drawIcon(uint8_t x, uint8_t y, const uint8_t* icon8x8) {
    _u8g2.drawBitmap(x, y - 7, 1, 8, icon8x8);
}

void DisplayView::drawTempSmall(const uint8_t* icon8x8, float temp, uint8_t x, uint8_t y) {
    drawIcon(x, y, icon8x8);
    char buf[8];
    tempToStr(temp, buf, sizeof(buf));
    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(x + 9, y, buf);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen renderers
// ─────────────────────────────────────────────────────────────────────────────

void DisplayView::drawMain() {
    const SensorData&   s = _state.sensors;
    const RelayState&   r = _state.relays;

    _u8g2.clearBuffer();
    drawTopBar();

    // Content area: y=9 to y=53
    // Large flow temp, baseline y=38
    drawTempLarge(s.flowTemp, 0, 38);

    // Right column temps, x=80
    drawTempSmall(ICO_PIPE,  s.returnTemp,  80, 20);
    drawTempSmall(ICO_HOUSE, s.roomTemp,    80, 32);
    drawTempSmall(ICO_SUN,   s.outsideTemp, 80, 44);

    // Relay / alarm icons (left side, below large temp)
    uint8_t iconX = 0;
    if (r.heaterOn)          { drawIcon(iconX, 52, ICO_FLAME);   iconX += 10; }
    if (r.pumpOn)            { drawIcon(iconX, 52, ICO_PUMP);    iconX += 10; }
    if (_state.alarms.any()) { drawIcon(iconX, 52, ICO_WARNING);              }

    drawButtons();
    _u8g2.sendBuffer();
}

void DisplayView::drawSetpoints() {
    const Config&       cfg = _state.config;
    const SystemStatus& st  = _state.status;

    _u8g2.clearBuffer();
    drawTopBar();

    // Content area: y=9 to y=53
    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(0, 17, "Setpoints");
    _u8g2.drawLine(0, 19, 127, 19);

    char buf[32];
    snprintf(buf, sizeof(buf), "Flow: %d (act:%d)",
             cfg.flowSetpoint, (int)st.activeFlowSetpoint);
    _u8g2.drawStr(0, 29, buf);

    snprintf(buf, sizeof(buf), "Retn: %d (act:%d)",
             cfg.returnSetpoint, (int)st.activeReturnSetpoint);
    _u8g2.drawStr(0, 39, buf);

    snprintf(buf, sizeof(buf), "Room: %d  Hyst:%d",
             cfg.roomSetpoint, cfg.roomHysteresis);
    _u8g2.drawStr(0, 49, buf);

    if (cfg.curveCount >= 3) {
        snprintf(buf, sizeof(buf), "Curve: %d pts", cfg.curveCount);
        _u8g2.drawStr(0, 53, buf);
    }

    drawButtons();
    _u8g2.sendBuffer();
}

void DisplayView::drawLog() {
    _u8g2.clearBuffer();
    drawTopBar();

    // Content area: y=9 to y=53
    _u8g2.setFont(u8g2_font_5x8_tr);

    uint16_t count = _state.log.count;

    // Header + scroll indicator
    _u8g2.drawStr(0, 15, "Log");
    if (count > 5) {
        char sc[8];
        snprintf(sc, sizeof(sc), "%d/%d", _logScrollIdx + 1, count);
        _u8g2.drawStr(95, 15, sc);
    }
    _u8g2.drawLine(0, 16, 127, 16);

    if (count == 0) {
        _u8g2.drawStr(0, 28, "No events");
        drawButtons();
        _u8g2.sendBuffer();
        return;
    }

    // 5 entries, 8 px line height, starting at y=24
    for (uint8_t i = 0; i < 5; i++) {
        uint16_t idx = (uint16_t)(_logScrollIdx + i);
        if (idx >= count) break;

        const LogEvent* ev = _state.log.get(idx);
        if (!ev) break;

        const char* relayStr = "?";
        switch (ev->relay) {
            case RelayChange::HEATER_ON:  relayStr = "H+"; break;
            case RelayChange::HEATER_OFF: relayStr = "H-"; break;
            case RelayChange::PUMP_ON:    relayStr = "P+"; break;
            case RelayChange::PUMP_OFF:   relayStr = "P-"; break;
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "%s %s", relayStr, formatReason(ev->reason));
        _u8g2.drawStr(0, (uint8_t)(24 + i * 8), buf);
    }

    drawButtons();
    _u8g2.sendBuffer();
}

void DisplayView::drawSettings() {
    static const char* const LABELS[4] = {
        "Flow SP", "Flow Hyst", "Retn SP", "Retn Hyst"
    };

    _u8g2.clearBuffer();
    drawTopBar();

    // Header
    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(0, 17, "Settings");
    _u8g2.drawLine(0, 19, 127, 19);

    // 4 parameter rows: y = 26, 34, 42, 50  (5x8 font, 8 px line pitch)
    _u8g2.setFont(u8g2_font_5x8_tr);
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t y = 26 + i * 8;
        _u8g2.drawStr(0,  y, i == _settingsCursor ? ">" : " ");
        _u8g2.drawStr(6,  y, LABELS[i]);
        char buf[6];
        snprintf(buf, sizeof(buf), "%3d\xb0", _settingsTemp[i]);
        _u8g2.drawStr(95, y, buf);
    }

    // Bottom hint (override normal button icons with context labels)
    _u8g2.drawLine(0, 53, 127, 53);
    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.drawStr(  0, 62, "val+");
    _u8g2.drawStr( 26, 62, "val-");
    _u8g2.drawStr( 52, 62, "next");
    _u8g2.drawStr( 78, 62, "back");
    _u8g2.drawStr(104, 62, "save");

    _u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────────────────────────

const char* DisplayView::formatReason(ChangeReason r) const {
    switch (r) {
        case ChangeReason::START_CONDITIONS_MET:      return "StartOK";
        case ChangeReason::ANTIFREEZE_RETURN_COLD:    return "AFrz";
        case ChangeReason::FLOW_SETPOINT_REACHED:     return "FlowSP";
        case ChangeReason::ROOM_SETPOINT_REACHED:     return "RoomSP";
        case ChangeReason::EXTERNAL_THERMOSTAT:       return "Therm";
        case ChangeReason::HA_REMOTE_DISABLE:         return "HA-Dis";
        case ChangeReason::OVERHEAT:                  return "OvrHeat";
        case ChangeReason::MODE_TURNED_OFF:           return "ModeOff";
        case ChangeReason::ANTIFREEZE_RETURN_WARM:    return "AFWarm";
        case ChangeReason::ANTIFREEZE_ROOM_WARM:      return "RmWarm";
        case ChangeReason::PREDELAY_FLOW_WARMED:      return "Pre:Flw";
        case ChangeReason::PREDELAY_RETURN_BLOCKED:   return "Pre:Ret";
        case ChangeReason::PREDELAY_ROOM_WARM:        return "Pre:Rm";
        case ChangeReason::PREDELAY_THERMOSTAT:       return "Pre:Thr";
        case ChangeReason::PREDELAY_HA_DISABLE:       return "Pre:HA";
        case ChangeReason::PUMP_PREDELAY_START:       return "PmpPre";
        case ChangeReason::PUMP_STANDBY_RUN:          return "Standby";
        case ChangeReason::PUMP_ANTIFREEZE_MODE:      return "AFrzPmp";
        case ChangeReason::PUMP_POSTDELAY_COMPLETE:   return "PmpPost";
        case ChangeReason::PUMP_STANDBY_RUN_COMPLETE: return "StbyDone";
        case ChangeReason::PUMP_ANTIFREEZE_MODE_OFF:  return "AFrzOff";
        case ChangeReason::PUMP_MODE_TURNED_OFF:      return "ModeOff";
        case ChangeReason::PUMP_MIN_TIME_WAIT:        return "MinTime";
        default:                                      return "?";
    }
}

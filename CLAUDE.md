# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Electro Boiler Firmware

Firmware for an electric boiler controller built on the **HKL-EA2** board (ESP32-WROOM-32N4) by HANKERILA.

## Build & Flash

```bash
# Build release firmware
pio run -e release

# Build with verbose debug logs (CORE_DEBUG_LEVEL=4)
pio run -e debug

# Flash to device
pio run -e release -t upload

# Upload filesystem (web SPA from data/)
pio run -e release -t uploadfs

# Open serial monitor (115200 baud)
pio device monitor

# Run native unit tests (no hardware needed)
pio test -e native
```

## Hardware: HKL-EA2

See full board reference: `.claude/hkl-ea2-board.md`

**MCU:** ESP32-WROOM-32N4

### Key GPIO assignments

| Function         | GPIO |
|------------------|------|
| DS18B20 TEM1 (flow)    | 33   |
| DS18B20 TEM2 (return)  | 14   |
| Relay 1 (heater) | 2    |
| Relay 2 (pump)   | 15   |
| Buzzer           | 12   |
| IIC SDA          | 4    |
| IIC SCL          | 16   |
| Digital IN1      | 36   |
| Digital IN2      | 39   |
| RS485 RX/TX      | 35/32 |
| GSM RX/TX        | 13/5 |

### Ethernet (LAN8720)
ETH_MDC=23, ETH_MDIO=18, ETH_CLK=GPIO17_OUT, ETH_ADDR=0

### I2C Bus (GPIO4=SDA, GPIO16=SCL)

| Address | Device              |
|---------|---------------------|
| 0x3C    | SSD1309 OLED 128×64 |
| 0x20    | PCF8574 (5 buttons) |

Full references: `.claude/ssd1309-oled.md`, `.claude/pcf8574-buttons.md`

## Libraries (platformio.ini)

| Library | Purpose |
|---------|---------|
| paulstoffregen/OneWire + milesburton/DallasTemperature | DS18B20 sensors |
| olikraus/U8g2 | SSD1309 OLED display |
| h2zero/NimBLE-Arduino | BLE passive scanning |
| marvinroger/async-mqtt-client | MQTT |
| me-no-dev/ESP Async WebServer + AsyncTCP | REST API + SPA |
| bblanchon/ArduinoJson | JSON |
| ayushsharma82/ElegantOTA | OTA via web UI |
| lorol/LittleFS_esp32 | Filesystem for web files |

## Architecture

### MVC + Service Layer

```
AppState (single source of truth)
├── Controller: BoilerLogic   — all heating decisions, state machine
├── Views (read AppState only):
│   ├── DisplayView           — OLED (U8g2)
│   ├── WebView               — ESPAsyncWebServer (REST + SPA)
│   └── MqttView              — Home Assistant integration + autodiscovery
└── Hardware/Services:
    ├── DS18B20Driver, RelayDriver, BuzzerDriver, ButtonReader (PCF8574)
    ├── BleScanner (NimBLE), ApiClient (HTTP temp poll)
    ├── MqttService, OtaService (ElegantOTA)
    ├── TimeService (NTP + uptime fallback)
    ├── EnergyMeter (heater kWh estimate, NVS-persisted)
    └── NvsConfig (load/save Config to NVS)
```

Full spec: `.claude/architecture-firmware.md`

### AppState — single header, instantiated once in main.cpp

`src/model/AppState.h` defines all structs:
- `SensorData` — flow/return/room/outside temps (NaN = unavailable), fault flags, lastSeen timestamps
- `RelayState` — heater/pump on/off + timestamps
- `SystemStatus` — mode (OFF/ON/ANTIFREEZE), phase (IDLE/PUMP_PRE/HEATING/PUMP_POST/PUMP_STANDBY), active setpoints, alarm flags
- `AlarmState` — uint16_t bitfield, 7 alarm flags (see below)
- `Config` — all NVS-persisted settings
- `EventLog` — circular buffer of 200 `LogEvent` entries (~3.2 KB RAM)

### Concurrency

AppState is shared across FreeRTOS tasks. Main loop (controller + display + buttons) runs in Arduino task. BLE callbacks and web handlers run in separate tasks — use `portENTER_CRITICAL` / mutex when writing sensor fields from BLE or web tasks.

## Control Logic

Full spec: `.claude/control-logic.md`

**Modes:** OFF (power-on default) / ON (auto heating) / ANTIFREEZE (frost protection)
Mode changed via buttons, web UI, or HA MQTT.

**ON mode heater START** — all must be true:
- `flow_temp < (flow_setpoint - flow_hysteresis)`
- `return_temp < return_setpoint` ← start gate only, not checked while running
- `room_temp < (room_setpoint - room_hysteresis)` ← only if room sensor available
- external thermostat == ALLOW (if configured)
- `ha_remote_disable == false`
- `flow_temp < 80°C`
- heater off for ≥ `minHeaterOffSec` (60–180 s, default 180)

**ON mode heater STOP** — any is true:
- `flow_temp >= flow_setpoint`
- `room_temp >= room_setpoint` (if available)
- external thermostat == BLOCK
- `ha_remote_disable == true`
- `flow_temp >= 80°C` → OVERHEAT alarm

**Pump sequence:** pre-delay → heater → post-delay (pre and post delay = same value, 30–120 s). All start conditions re-checked continuously during pre-delay; if any fails, heater is cancelled but pump finishes minimum run time. During post-delay, start conditions are also re-checked: if heat demand returns, the pump stays on until min-off time elapses, then goes back to pre-delay (no pump off/on).

**ANTIFREEZE mode:** pump always on; heater ON when `return_temp < 8°C` (+ `room_temp < 10°C` if available), OFF at `return_temp >= 10°C`.

**Hard limits:** `flow_temp >= 80°C` → immediate stop + alarm. `flow_temp = -127°C or 85°C` (DS18B20 error) → stop + alarm. Return sensor error → warning only, continue.

**Weather compensation:** 3–5 point curve (outside_temp → flow_setpoint + return_setpoint), linear interpolation, clamp at extremes. If outside sensor unavailable → use manual setpoints.

## Temperature Sources

Full spec: `.claude/architecture-temp-sources.md`

Flow and return sensors (DS18B20) are **required**. Room and outside sensors are **optional** — each independently configured as **BLE | API | None**.

- **BLE:** passive NimBLE scanning, identify by MAC address, IBS-TH2 advertisement format (byte[0..1] = temp ×100, int16 LE)
- **API:** HTTP GET + JSON path, configurable poll interval; mark unavailable on error/timeout
- Timeout: BLE → unavailable after 5 min with no advertisement

Always call `isAvailable()` before using room/outside temp. Never make boiler logic depend on optional sensors.

## MQTT Alarms

Full spec: `.claude/mqtt-alarms.md`

7 alarm types tracked in `AlarmState` bitfield:

| Alarm | Severity | Heater | Buzzer |
|-------|----------|--------|--------|
| OVERHEAT | CRITICAL | Stop | Continuous |
| FLOW_SENSOR_FAULT | CRITICAL | Stop | 3-beep repeat |
| RETURN_SENSOR_FAULT | WARNING | Continue | Silent |
| ROOM_SENSOR_LOST | INFO | Continue | Silent |
| OUTSIDE_SENSOR_LOST | INFO | Continue | Silent |
| BLE_ROOM_LOW_BATTERY | WARNING | — | Silent |
| BLE_OUTSIDE_LOW_BATTERY | WARNING | — | Silent |

Two-layer design: retained state topics (`boiler/alarm/<name>` → ON/OFF) for HA binary sensors + non-retained `boiler/alarm/event` (JSON) published on alarm activation to trigger HA automations. MqttView publishes only on state changes.

## Security

Full spec: `.claude/security.md`

- **Web:** session-based auth (cookie, 32-byte random token), 2 roles: Admin / Operator
- **First boot:** redirect to `/setup` to set passwords; no default credentials exist
- **REST API:** Bearer token (32-byte hex, generated on first boot, stored in NVS). Wrong token → 401.
- **MQTT:** username/password in NVS
- **OTA:** Admin only + password re-confirm + heater stops before flash
- HTTP only (no TLS) — acceptable for LAN; use reverse proxy if exposed externally
- Passwords stored as SHA-256 hex in NVS

## Time Service

Full spec: `.claude/time-service.md`

No RTC. Two layers: uptime (`esp_timer_get_time()`, always available) + NTP real time (after WiFi connect). `TimeService::now()` returns unix timestamp if synced, else seconds-since-boot. Re-sync every 24 h; retry every 5 min on failure. Last time NOT persisted to NVS (wear). LogEvent timestamps use unix time if synced, uptime otherwise (detectable: `timestamp < 1700000000` → uptime).

## Web API Endpoints

| Method | Path | Auth | Purpose |
|--------|------|------|---------|
| GET | `/api/state` | Token | Full AppState JSON |
| GET | `/api/log` | Token | Event log JSON |
| POST | `/api/command` | Token | Operator commands |
| GET | `/api/config` | Admin session | Full config |
| POST | `/api/config` | Admin session | Update config |
| POST | `/api/ota` | Admin + password | Firmware upload |
| GET | `/` | — | SPA (LittleFS) |

## Project File Structure

```
boiler/
├── platformio.ini
├── src/
│   ├── main.cpp                  ← setup(), loop(), AppState instance
│   ├── model/
│   │   ├── AppState.h            ← ALL data model structs + EventLog
│   │   └── pins.h                ← GPIO pin constants
│   ├── controller/
│   │   └── BoilerLogic.cpp/.h
│   ├── hardware/
│   │   ├── DS18B20Driver.cpp/.h
│   │   ├── RelayDriver.cpp/.h
│   │   ├── BuzzerDriver.cpp/.h
│   │   └── ButtonReader.cpp/.h   (PCF8574, I2C polling)
│   ├── service/
│   │   ├── BleScanner.cpp/.h     (NimBLE, IBS-TH2 parser)
│   │   ├── ApiClient.cpp/.h      (HTTP poll for temp)
│   │   ├── MqttService.cpp/.h
│   │   ├── OtaService.cpp/.h
│   │   ├── TimeService.cpp/.h    (NTP + uptime fallback)
│   │   ├── EnergyMeter.cpp/.h    (heater kWh = power × ON time, NVS-persisted)
│   │   └── NvsConfig.cpp/.h      (load/save Config)
│   ├── view/
│   │   ├── display/DisplayView.cpp/.h  (SSD1309 via U8g2)
│   │   ├── web/WebView.cpp/.h          (ESPAsyncWebServer)
│   │   └── mqtt/MqttView.cpp/.h        (HA autodiscovery + alarms)
│   └── util/
│       ├── EventLog.cpp          (no separate .h — types in AppState.h)
│       └── CurveInterp.cpp/.h    (weather compensation)
├── data/                         ← LittleFS: index.html, app.js, style.css
└── test/
    └── test_curve_interp.cpp     (native env, no hardware)
```

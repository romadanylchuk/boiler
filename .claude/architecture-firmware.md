# Firmware Architecture

## Pattern: MVC + Service Layer

```
┌─────────────────────────────────────────────────────┐
│                    AppState                         │
│              (single source of truth)               │
│  SensorData  │  RelayState  │  SystemStatus         │
│  Config      │  EventLog    │  Alarms               │
└──────┬────────────────────────────┬─────────────────┘
       │ reads                      │ reads
       │                            │
┌──────▼──────────┐    ┌────────────▼──────────────────┐
│   Controller    │    │           Views               │
│  (BoilerLogic)  │    │  DisplayView  (OLED)          │
│                 │    │  WebView      (AsyncWebServer) │
│  - state machine│    │  MqttView     (HA integration) │
│  - conditions   │    │                               │
│  - curve interp │    │  All views: READ AppState     │
│  - event log    │    │  Commands → Controller        │
└──────┬──────────┘    └───────────────────────────────┘
       │ commands
┌──────▼──────────────────────────────────────────────┐
│                  Hardware / Services                │
│  DS18B20Driver  │  RelayDriver  │  BuzzerDriver     │
│  BleScanner     │  ApiClient    │  ButtonReader      │
│  WifiManager    │  MqttClient   │  OtaService        │
└─────────────────────────────────────────────────────┘
```

---

## Model — AppState (single header, all state here)

```cpp
// AppState.h — include everywhere, instantiated once in main.cpp

// --- Sensor readings ---
struct SensorData {
    float  flowTemp;          // NaN = unavailable
    float  returnTemp;        // NaN = unavailable
    float  roomTemp;          // NaN = unavailable (optional sensor)
    float  outsideTemp;       // NaN = unavailable (optional sensor)
    bool   flowSensorFault;
    bool   returnSensorFault;
    bool   roomSensorAvail;
    bool   outsideSensorAvail;
    uint32_t flowLastSeen;    // millis
    uint32_t returnLastSeen;
    uint32_t roomLastSeen;
    uint32_t outsideLastSeen;
};

// --- Hardware output state ---
struct RelayState {
    bool     heaterOn;
    bool     pumpOn;
    uint32_t heaterOnSince;   // millis, 0 if off
    uint32_t heaterOffSince;  // millis
    uint32_t pumpOnSince;     // millis, 0 if off
};

// --- Heating state machine ---
enum class SystemMode  { OFF, ON, ANTIFREEZE };
enum class HeaterPhase {
    IDLE,           // nothing running
    PUMP_PRE,       // pump on, waiting pre-delay, checking conditions
    HEATING,        // heater on
    PUMP_POST,      // heater off, pump cooling down
    PUMP_STANDBY    // periodic anti-freeze pump run
};

struct SystemStatus {
    SystemMode   mode;
    HeaterPhase  phase;
    float  activeFlowSetpoint;    // effective value (curve or manual)
    float  activeReturnSetpoint;
    bool   overheatAlarm;
    bool   haRemoteDisable;       // flag from HA
    bool   externalThermostatBlock;
    bool   returnGateClosed;      // return temp blocking re-start
    bool   otaInProgress;
};

// --- Configuration (NVS-persisted) ---
enum class TempSourceMode { NONE, BLE, API };
enum class ThermostatContact { NORMAL_OPEN, NORMAL_CLOSED };

struct TempSourceConfig {
    TempSourceMode mode;
    char bleMac[18];          // "AA:BB:CC:DD:EE:FF"
    char apiUrl[128];
    char apiJsonPath[64];
    uint32_t apiPollSec;
};

struct CurvePoint {
    int8_t  outsideTemp;      // °C  (e.g. -20, 0, +10, +15)
    uint8_t flowSetpoint;     // °C
    uint8_t returnSetpoint;   // °C
};

struct Config {
    // Setpoints (stored int, displayed ×10 for 1 decimal)
    uint8_t flowSetpoint;         // 20–65
    uint8_t returnSetpoint;       // 20–65
    uint8_t flowHysteresis;       // 1–10
    uint8_t returnHysteresis;     // 1–10
    uint8_t roomSetpoint;         // 5–30
    uint8_t roomHysteresis;       // 1–5

    // Timing (seconds)
    uint16_t pumpPrePostDelay;    // 30–120
    uint16_t standbyPumpPeriodMin;// 30–180 (minutes)
    uint8_t  standbyPumpDurationMin; // 1–5
    // min on/off times are fixed constants (3 min), not stored

    // Hardware
    ThermostatContact thermostatMode;

    // Optional sensors
    TempSourceConfig roomSensor;
    TempSourceConfig outsideSensor;

    // Weather compensation curve
    CurvePoint curve[5];
    uint8_t    curveCount;        // 0 = disabled, 3–5 = active

    // Credentials (hashed or plain in NVS)
    char adminUser[32];
    char adminPassHash[65];       // SHA-256 hex
    char operatorUser[32];
    char operatorPassHash[65];
    char apiToken[65];            // 32-byte random hex
    char mqttUser[32];
    char mqttPass[64];
    char mqttBroker[64];
    uint16_t mqttPort;

    // WiFi
    char wifiSsid[64];
    char wifiPass[64];
};

// --- Event log ---
// Events = relay state changes only (heater on/off, pump on/off)
// Each event records WHAT changed, WHY, and a temperature snapshot at that moment.

enum class RelayChange : uint8_t {
    HEATER_ON,
    HEATER_OFF,
    PUMP_ON,
    PUMP_OFF
};

enum class ChangeReason : uint8_t {
    // --- Heater ON ---
    START_CONDITIONS_MET,          // normal auto start
    ANTIFREEZE_RETURN_COLD,        // antifreeze: return < 8°C

    // --- Heater OFF ---
    FLOW_SETPOINT_REACHED,         // flow_temp >= flow_setpoint
    ROOM_SETPOINT_REACHED,         // room_temp >= room_setpoint
    EXTERNAL_THERMOSTAT,           // dry contact blocked
    HA_REMOTE_DISABLE,             // HA flag set
    OVERHEAT,                      // flow_temp >= 80°C
    MODE_TURNED_OFF,               // user set mode to OFF
    ANTIFREEZE_RETURN_WARM,        // antifreeze: return >= 10°C
    ANTIFREEZE_ROOM_WARM,          // antifreeze: room >= 10°C
    // Cancelled during pre-delay (heater never turned on, pump winding down):
    PREDEALY_FLOW_WARMED,          // flow rose above threshold during pre-delay
    PREDELAY_RETURN_BLOCKED,       // return blocked during pre-delay
    PREDELAY_ROOM_WARM,            // room reached setpoint during pre-delay
    PREDELAY_THERMOSTAT,           // thermostat blocked during pre-delay
    PREDELAY_HA_DISABLE,           // HA disabled during pre-delay

    // --- Pump ON ---
    PUMP_PREDELAY_START,           // heating requested, starting pre-delay
    PUMP_STANDBY_RUN,              // periodic standby circulation
    PUMP_ANTIFREEZE_MODE,          // antifreeze mode activated (pump always on)

    // --- Pump OFF ---
    PUMP_POSTDELAY_COMPLETE,       // normal post-delay finished
    PUMP_STANDBY_RUN_COMPLETE,     // periodic run finished
    PUMP_ANTIFREEZE_MODE_OFF,      // antifreeze mode deactivated
    PUMP_MODE_TURNED_OFF,          // mode set to OFF during pump run
};

// Temperature snapshot at the moment of the relay change
// Values stored as int16 × 10 to avoid float (e.g. 623 = 62.3°C)
// INT16_MIN (-32768) = sensor unavailable
struct TempSnapshot {
    int16_t flowTemp;
    int16_t returnTemp;
    int16_t roomTemp;       // INT16_MIN if unavailable
    int16_t outsideTemp;    // INT16_MIN if unavailable
};

struct LogEvent {
    time_t       timestamp;           // unix time if NTP synced, else seconds since boot
    RelayChange  relay;               // what changed
    ChangeReason reason;              // why it changed
    TempSnapshot temps;               // temperatures at the moment
    uint8_t      activeFlowSetpoint;  // effective setpoint (from curve or manual)
    uint8_t      activeReturnSetpoint;
};
// sizeof(LogEvent) ≈ 16 bytes

// Circular buffer — 200 events × 16 bytes = 3.2 KB RAM
// At ~15 relay events/hour → ~13 hours coverage
constexpr uint16_t EVENT_LOG_SIZE = 200;

struct EventLog {
    LogEvent  events[EVENT_LOG_SIZE];
    uint16_t  head;    // next write index (wraps around)
    uint16_t  count;   // total stored, max EVENT_LOG_SIZE

    void     push(RelayChange relay, ChangeReason reason,
                  const TempSnapshot& temps,
                  uint8_t flowSp, uint8_t returnSp);
    LogEvent get(uint16_t indexFromNewest) const;  // 0 = most recent
};

// --- Root state object ---
struct AppState {
    SensorData   sensors;
    RelayState   relays;
    SystemStatus status;
    AlarmState   alarms;   // bitfield — see mqtt-alarms.md
    Config       config;
    EventLog     log;
};
```

---

## Controller — BoilerLogic

All heating decisions live here. Reads AppState, issues relay commands.

```cpp
class BoilerLogic {
public:
    explicit BoilerLogic(AppState& state);
    void update();   // call every loop iteration

private:
    // State machine handlers
    void handleOff();
    void handleOn();
    void handleAntifreeze();
    void handlePumpPre();
    void handleHeating();
    void handlePumpPost();
    void handleStandbyPump();

    // Condition evaluators — each returns bool, reads only AppState
    bool canStartHeater() const;    // all start conditions
    bool shouldStopHeater() const;  // any stop condition
    bool isOverheat() const;
    bool isReturnGateClosed() const;
    bool isExternalThermostatBlocking() const;
    bool isRoomOverTemp() const;

    // Utilities
    void computeActiveSetpoints();  // curve interpolation → AppState.status
    void transitionPhase(HeaterPhase next);
    void setHeater(bool on);
    void setPump(bool on);
    void logEvent(EventType type, int16_t value = 0);

    AppState&  _state;
    uint32_t   _phaseEnteredAt;   // millis when current phase started
    uint32_t   _lastStandbyPump;  // millis of last standby pump run
};
```

---

## Views

### DisplayView (OLED)
- Reads AppState only, never writes
- Screen 1 (main): flow temp (large), return/room/outside (small), heater/pump/alarm icons
- Screen 2 (setpoints): active control values
- Screen 3 (log): last N events scrollable
- Button navigation handled by ButtonController → triggers view change

### WebView (ESPAsyncWebServer)
- GET  `/api/state`     → full AppState JSON (token required)
- GET  `/api/log`       → event log JSON (token required)
- POST `/api/command`   → operator writes (token required)
- GET  `/api/config`    → full config JSON (admin session required)
- POST `/api/config`    → update config (admin session required)
- POST `/api/ota`       → firmware upload (admin session + password confirm)
- GET  `/`             → serve SPA (single page app, static HTML/JS)

### MqttView
- Publishes AppState fields to HA MQTT topics (autodiscovery)
- Subscribes to command topics → writes to AppState.config or calls BoilerLogic

---

## Project File Structure

```
boiler/
├── platformio.ini
├── src/
│   ├── main.cpp              ← setup(), loop(), AppState instance
│   ├── AppState.h            ← ALL data model structs (single include)
│   ├── controller/
│   │   └── BoilerLogic.cpp/.h
│   ├── drivers/
│   │   ├── DS18B20Driver.cpp/.h
│   │   ├── RelayDriver.cpp/.h
│   │   ├── BuzzerDriver.cpp/.h
│   │   └── ButtonReader.cpp/.h   (PCF8574 + INT)
│   ├── services/
│   │   ├── BleScanner.cpp/.h     (NimBLE, IBS-TH2 parser)
│   │   ├── ApiClient.cpp/.h      (HTTP poll for temp)
│   │   ├── MqttClient.cpp/.h
│   │   ├── OtaService.cpp/.h
│   │   ├── TimeService.cpp/.h    (NTP sync + uptime fallback)
│   │   └── NvsConfig.cpp/.h      (load/save Config to NVS)
│   ├── views/
│   │   ├── DisplayView.cpp/.h    (OLED SSD1309)
│   │   ├── WebView.cpp/.h        (ESPAsyncWebServer)
│   │   └── MqttView.cpp/.h
│   └── util/
│       ├── EventLog.cpp/.h
│       └── CurveInterp.cpp/.h    (weather compensation)
├── data/                     ← LittleFS web files
│   ├── index.html
│   ├── app.js
│   └── style.css
└── test/                     ← unit tests (native env)
    ├── test_curve_interp.cpp
    └── test_boiler_logic.cpp
```

---

## Concurrency Notes (ESP32 FreeRTOS)

| Task | Runs in |
|------|---------|
| Main loop (controller, display, buttons) | Arduino loop task |
| BLE scanning | NimBLE internal task |
| Web server | ESPAsyncWebServer internal task |
| MQTT keep-alive | background |

**AppState is shared across tasks.**
Use `portENTER_CRITICAL` / `portEXIT_CRITICAL` or a single `SemaphoreHandle_t` mutex
when BLE callback or web handler writes to AppState sensor fields.
Controller and display run in the same task — no mutex needed between them.

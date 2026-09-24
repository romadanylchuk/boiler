#pragma once
#include <stdint.h>
#include <math.h>   // NAN, isnan()
#include <time.h>

// ═════════════════════════════════════════════════════════════════════════════
//  AppState.h  —  Single source of truth for all firmware state
//  Include this file wherever state needs to be read or written.
//  One instance created in main.cpp and passed by reference everywhere.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
//  Enumerations
// ─────────────────────────────────────────────────────────────────────────────

enum class SystemMode : uint8_t {
    OFF,
    ON,
    ANTIFREEZE
};

enum class HeaterPhase : uint8_t {
    IDLE,           // nothing running
    PUMP_PRE,       // pump on, pre-delay, conditions still checked
    HEATING,        // heater on
    PUMP_POST,      // heater off, pump cooling down
    PUMP_STANDBY    // periodic anti-freeze circulation run
};

enum class TempSourceMode : uint8_t {
    NONE,
    BLE,
    API
};

enum class ThermostatContact : uint8_t {
    NORMAL_OPEN,    // contact open = allow heating
    NORMAL_CLOSED   // contact closed = allow heating
};

// ─────────────────────────────────────────────────────────────────────────────
//  Sensor Data
// ─────────────────────────────────────────────────────────────────────────────

struct SensorData {
    float    flowTemp;          // NAN if unavailable
    float    returnTemp;        // NAN if unavailable
    float    roomTemp;          // NAN if unavailable (optional)
    float    outsideTemp;       // NAN if unavailable (optional)

    bool     flowSensorFault;
    bool     returnSensorFault;

    uint32_t flowLastSeen;      // millis of last valid reading
    uint32_t returnLastSeen;
    uint32_t roomLastSeen;
    uint32_t outsideLastSeen;

    // Helpers
    bool hasFlow()    const { return !isnan(flowTemp)    && !flowSensorFault; }
    bool hasReturn()  const { return !isnan(returnTemp)  && !returnSensorFault; }
    bool hasRoom()    const { return !isnan(roomTemp); }
    bool hasOutside() const { return !isnan(outsideTemp); }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Relay / Hardware Output State
// ─────────────────────────────────────────────────────────────────────────────

struct RelayState {
    bool     heaterOn;
    bool     pumpOn;
    uint32_t heaterOnSince;     // millis when heater turned on (0 if off)
    uint32_t heaterOffSince;    // millis when heater turned off
    uint32_t pumpOnSince;       // millis when pump turned on (0 if off)
    uint32_t pumpOffSince;      // millis when pump turned off
};

// ─────────────────────────────────────────────────────────────────────────────
//  System Status (runtime, not persisted)
// ─────────────────────────────────────────────────────────────────────────────

struct SystemStatus {
    SystemMode   mode;
    HeaterPhase  phase;

    float  activeFlowSetpoint;      // effective value — from curve or manual config
    float  activeReturnSetpoint;

    bool   haRemoteDisable;         // HA flag — disables heating when true
    bool   externalThermostatBlock; // dry contact signal (processed per config)
    bool   returnGateClosed;        // return temp blocking re-start
    bool   otaInProgress;
    bool   mqttConnected;
    bool   wifiConnected;
    bool   ntpSynced;
    bool   apMode;                  // true when running as WiFi Access Point (no STA)
    char   apSsid[24];              // AP SSID shown on display ("Boiler-XXXXXX")
};

// ─────────────────────────────────────────────────────────────────────────────
//  Alarm State
// ─────────────────────────────────────────────────────────────────────────────

enum class AlarmFlag : uint16_t {
    NONE                    = 0,
    OVERHEAT                = (1 << 0),   // flow >= 80°C  → stop heater + buzzer
    FLOW_SENSOR_FAULT       = (1 << 1),   // DS18B20 error → stop heater + buzzer
    RETURN_SENSOR_FAULT     = (1 << 2),   // DS18B20 error → warn only
    ROOM_SENSOR_LOST        = (1 << 3),   // BLE/API unavailable > timeout
    OUTSIDE_SENSOR_LOST     = (1 << 4),   // BLE/API unavailable > timeout
    BLE_ROOM_LOW_BATTERY    = (1 << 5),
    BLE_OUTSIDE_LOW_BATTERY = (1 << 6),
};

struct AlarmState {
    uint16_t active = 0;

    void set(AlarmFlag f)         { active |=  static_cast<uint16_t>(f); }
    void clear(AlarmFlag f)       { active &= ~static_cast<uint16_t>(f); }
    bool isSet(AlarmFlag f) const { return active & static_cast<uint16_t>(f); }
    bool any()             const  { return active != 0; }

    // Critical alarms require heater stop
    bool isCritical() const {
        return isSet(AlarmFlag::OVERHEAT) ||
               isSet(AlarmFlag::FLOW_SENSOR_FAULT);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Event Log
// ─────────────────────────────────────────────────────────────────────────────

enum class RelayChange : uint8_t {
    HEATER_ON,
    HEATER_OFF,
    PUMP_ON,
    PUMP_OFF
};

enum class ChangeReason : uint8_t {
    // Heater ON
    START_CONDITIONS_MET,
    ANTIFREEZE_RETURN_COLD,

    // Heater OFF
    FLOW_SETPOINT_REACHED,
    ROOM_SETPOINT_REACHED,
    EXTERNAL_THERMOSTAT,
    HA_REMOTE_DISABLE,
    OVERHEAT,
    MODE_TURNED_OFF,
    ANTIFREEZE_RETURN_WARM,
    ANTIFREEZE_ROOM_WARM,

    // Cancelled during pre-delay (heater never turned on)
    PREDELAY_FLOW_WARMED,
    PREDELAY_RETURN_BLOCKED,
    PREDELAY_ROOM_WARM,
    PREDELAY_THERMOSTAT,
    PREDELAY_HA_DISABLE,

    // Pump ON
    PUMP_PREDELAY_START,
    PUMP_STANDBY_RUN,
    PUMP_ANTIFREEZE_MODE,

    // Pump OFF
    PUMP_POSTDELAY_COMPLETE,
    PUMP_STANDBY_RUN_COMPLETE,
    PUMP_ANTIFREEZE_MODE_OFF,
    PUMP_MODE_TURNED_OFF,
    PUMP_MIN_TIME_WAIT,         // cancelled pre-delay, waiting min pump time
};

// Temperature snapshot — stored as int16 × 10 (e.g. 623 = 62.3°C)
// INT16_MIN = sensor unavailable at this moment
struct TempSnapshot {
    int16_t flowTemp;
    int16_t returnTemp;
    int16_t roomTemp;
    int16_t outsideTemp;

    static constexpr int16_t UNAVAILABLE = INT16_MIN;

    static int16_t encode(float v) {
        return isnan(v) ? UNAVAILABLE : static_cast<int16_t>(v * 10.0f);
    }
    static float decode(int16_t v) {
        return v == UNAVAILABLE ? NAN : v / 10.0f;
    }
};

struct LogEvent {
    time_t       timestamp;            // unix time if NTP synced, else seconds since boot
    RelayChange  relay;
    ChangeReason reason;
    TempSnapshot temps;
    uint8_t      activeFlowSetpoint;   // effective setpoint at event time
    uint8_t      activeReturnSetpoint;
};
// sizeof(LogEvent) ≈ 16 bytes

constexpr uint16_t EVENT_LOG_SIZE = 200;  // 200 × 16 bytes = 3.2 KB

struct EventLog {
    LogEvent  events[EVENT_LOG_SIZE];
    uint16_t  head  = 0;
    uint16_t  count = 0;

    void push(RelayChange relay, ChangeReason reason,
              const TempSnapshot& temps,
              uint8_t flowSp, uint8_t returnSp,
              time_t ts);

    // 0 = most recent, 1 = second most recent, etc.
    const LogEvent* get(uint16_t indexFromNewest) const;

    void clear();
};

// ─────────────────────────────────────────────────────────────────────────────
//  Configuration (NVS-persisted)
// ─────────────────────────────────────────────────────────────────────────────

struct TempSourceConfig {
    TempSourceMode mode = TempSourceMode::NONE;
    char  bleMac[18]       = {};   // "AA:BB:CC:DD:EE:FF"
    char  apiUrl[128]      = {};
    char  apiJsonPath[64]  = {};
    uint32_t apiPollSec    = 60;
};

struct CurvePoint {
    int8_t  outsideTemp;      // °C outside
    uint8_t flowSetpoint;     // °C flow target
    uint8_t returnSetpoint;   // °C return target
};

struct Config {
    // ── Setpoints (°C, stored as integer) ──
    uint8_t flowSetpoint       = 55;   // 20–65
    uint8_t returnSetpoint     = 45;   // 20–65
    uint8_t flowHysteresis     =  3;   // 1–10
    uint8_t returnHysteresis   =  3;   // 1–10
    uint8_t roomSetpoint       = 21;   // 5–30
    uint8_t roomHysteresis     =  1;   // 1–5

    // ── Timing ──
    uint16_t pumpPrePostDelaySec  =  60;  // 30–120 s
    uint16_t standbyPumpPeriodMin = 120;  // 30–180 min
    uint8_t  standbyPumpDurationMin = 3;  // 1–5 min
    uint16_t minHeaterOffSec      = 180;  // 60–180 s
    // min heater ON time is a fixed constant — not stored

    // ── Energy meter ──
    uint16_t heaterPowerDeciKw    =  75;  // 5–300 (0.5–30.0 kW, 0.1 kW units)
                                          // default: 3× 3 kW/230 V elements — 2 in series
                                          // phase-phase (400 V → 4.54 kW) + 1 phase-N (3 kW)

    // ── Hardware ──
    ThermostatContact thermostatMode = ThermostatContact::NORMAL_OPEN;

    // ── Optional sensors ──
    TempSourceConfig roomSensor;
    TempSourceConfig outsideSensor;

    // ── Weather compensation curve ──
    CurvePoint curve[5]  = {};
    uint8_t    curveCount = 0;    // 0 = disabled, 3–5 = active

    // ── WiFi ──
    char wifiSsid[64] = {};
    char wifiPass[64] = {};

    // ── MQTT ──
    char     mqttBroker[64] = {};
    uint16_t mqttPort       = 1883;
    char     mqttUser[32]   = {};
    char     mqttPass[64]   = {};
    char     mqttClientId[32] = "boiler";

    // ── NTP ──
    char ntpServer1[64] = "pool.ntp.org";
    char ntpServer2[64] = "time.google.com";
    char timezone[48]   = "EET-2EEST,M3.5.0,M10.5.0/3";  // Ukraine UTC+2/+3

    // ── Credentials (SHA-256 hex of password) ──
    char adminUser[32]        = "admin";
    char adminPassHash[65]    = {};
    char operatorUser[32]     = "operator";
    char operatorPassHash[65] = {};
    char apiToken[65]         = {};   // 32-byte random hex, generated on first boot
    char resetPhrase[32]      = "boiler123";  // secret phrase for admin password reset

    // ── First-boot flag ──
    bool setupComplete = false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Test Mode State (RAM only, not persisted)
// ─────────────────────────────────────────────────────────────────────────────

struct TestState {
    bool  active             = false;

    // Temperature injection — replaces DS18B20 + ApiClient readings while active
    float flowTemp           = NAN;   // NAN = no injection (sensor unavailable)
    float returnTemp         = NAN;
    float roomTemp           = NAN;   // NAN = optional sensor not present
    float outsideTemp        = NAN;

    // Fault injection — direct flags replacing DS18B20 fault detection
    bool  flowFault          = false;
    bool  returnFault        = false;

    // Sensor lost simulation — BoilerLogic applies these immediately
    bool  roomSensorLost     = false;
    bool  outsideSensorLost  = false;

    // External thermostat override — replaces digitalRead(PIN_DIN_THERMOSTAT)
    bool  thermostatOverride = false;
    bool  thermostatAllow    = true;  // effective value when override is active
};

// ─────────────────────────────────────────────────────────────────────────────
//  Energy Meter (persisted by EnergyMeter, not part of Config)
// ─────────────────────────────────────────────────────────────────────────────

struct EnergyState {
    uint32_t totalWh = 0;   // heater energy since last reset (Wh)
    uint32_t resetTs = 0;   // unix time of last reset, 0 = unknown / never
};

// ─────────────────────────────────────────────────────────────────────────────
//  Root State — single instance in main.cpp
// ─────────────────────────────────────────────────────────────────────────────

struct AppState {
    SensorData   sensors;
    RelayState   relays;
    SystemStatus status;
    AlarmState   alarms;
    Config       config;
    EventLog     log;
    TestState    test;
    EnergyState  energy;

    // Set by web task, consumed by EnergyMeter::update() on the main loop task.
    volatile bool energyResetRequested = false;

    // Set by web task, consumed by DisplayView::update() on the main loop task.
    // Values map to ButtonEvent enum: 0=none, 1=UP, 2=DOWN, 3=ENTER, 4=BACK, 5=SETTINGS.
    volatile uint8_t pendingWebButton = 0;

    // Set by ButtonReader on any hardware button press, consumed by /api/state.
    // Same encoding as pendingWebButton.
    volatile uint8_t lastHwButton = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────────────────────────────────────

namespace Limits {
    constexpr float  OVERHEAT_TEMP        = 80.0f;   // °C — hard stop
    constexpr float  ANTIFREEZE_HEATER_ON = 8.0f;    // °C return — start in antifreeze
    constexpr float  ANTIFREEZE_HEATER_OFF= 10.0f;   // °C return — stop in antifreeze
    constexpr float  ANTIFREEZE_ROOM_MAX  = 10.0f;   // °C room — guard in antifreeze
    constexpr uint32_t MIN_HEATER_ON_MS   = 3 * 60 * 1000;  // 3 min
    constexpr uint32_t SENSOR_TIMEOUT_MS  = 5 * 60 * 1000;  // 5 min → alarm
    constexpr uint8_t  BLE_LOW_BATTERY_PCT = 20;
}

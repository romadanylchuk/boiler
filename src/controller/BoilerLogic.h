#pragma once
#include "../model/AppState.h"
#include "../hardware/RelayDriver.h"
#include "../hardware/BuzzerDriver.h"
#include "../service/TimeService.h"

// ─────────────────────────────────────────────────────────────────────────────
//  BoilerLogic — all heating decisions live here
//
//  Reads AppState, issues relay/buzzer commands.
//  Never accesses hardware directly — only through drivers.
//  All condition methods are const — they only read state.
// ─────────────────────────────────────────────────────────────────────────────

class BoilerLogic {
public:
    BoilerLogic(AppState& state, RelayDriver& relays,
                BuzzerDriver& buzzer, TimeService& time);

    void begin();
    void update();   // call every loop iteration

    // External commands (from web, MQTT, buttons)
    void setMode(SystemMode mode);
    void setHaRemoteDisable(bool disable);

private:
    // ── Phase handlers ────────────────────────────────────────────────────────
    void handleOff();
    void handleOn();
    void handleAntifreeze();
    void handlePumpPre();
    void handleHeating();
    void handlePumpPost();
    void handlePumpStandby();

    // ── Condition evaluators (read-only, all const) ───────────────────────────

    // All start conditions — returns true if heater may start
    bool canStartHeater()  const;
    bool startConditionsMet() const;  // canStartHeater() minus min-off-time

    // Any stop condition — returns true if heater must stop
    bool shouldStopHeater() const;

    // Individual conditions (used in both start check and pre-delay monitoring)
    bool isFlowBelowThreshold()        const;  // flow < setpoint - hysteresis
    bool isReturnGateOpen()            const;  // return < setpoint (start gate)
    bool isRoomBelowThreshold()        const;  // room < setpoint - hysteresis (if available)
    bool isExternalThermostatAllowing()const;
    bool isHaAllowing()                const;
    bool isOverheat()                  const;  // flow >= OVERHEAT_TEMP
    bool isMinOffTimeElapsed()         const;

    // Antifreeze-specific
    bool isAntifreezeHeatNeeded()  const;  // return < ANTIFREEZE_HEATER_ON && room < ANTIFREEZE_ROOM_MAX
    bool isAntifreezeHeatDone()    const;  // return >= ANTIFREEZE_HEATER_OFF || room >= ANTIFREEZE_ROOM_MAX

    // Pre-delay: check which condition was lost (for log reason)
    ChangeReason findPredelayFailReason() const;

    // ── Setpoint calculation ──────────────────────────────────────────────────
    void computeActiveSetpoints();   // curve interpolation → status.activeFlow/ReturnSetpoint

    // ── Hardware commands ─────────────────────────────────────────────────────
    void setHeater(bool on, ChangeReason reason);
    void setPump(bool on, ChangeReason reason);
    void transitionPhase(HeaterPhase next);

    // ── Alarm management ──────────────────────────────────────────────────────
    void checkSensorAlarms();
    void checkOverheatAlarm();
    void updateBuzzer();

    // ── Logging ───────────────────────────────────────────────────────────────
    void logRelayChange(RelayChange relay, ChangeReason reason);
    TempSnapshot captureTemps() const;

    // ── References ───────────────────────────────────────────────────────────
    AppState&     _state;
    RelayDriver&  _relays;
    BuzzerDriver& _buzzer;
    TimeService&  _time;

    uint32_t _phaseEnteredAt    = 0;  // millis when current phase started
    uint32_t _lastStandbyPump   = 0;  // millis of last standby pump run
    uint32_t _heaterOnAt        = 0;  // millis when heater turned on
    uint32_t _heaterOffAt       = 0;  // millis when heater turned off
    uint32_t _pumpOnAt          = 0;  // millis when pump turned on
    bool     _heaterRanThisCycle = false;  // heater was on since pump started
};

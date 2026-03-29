#include "BoilerLogic.h"
#include "../util/CurveInterp.h"
#include "../model/pins.h"
// model/AppState.h included via BoilerLogic.h
#include <Arduino.h>

BoilerLogic::BoilerLogic(AppState& state, RelayDriver& relays,
                         BuzzerDriver& buzzer, TimeService& time)
    : _state(state), _relays(relays), _buzzer(buzzer), _time(time) {}

void BoilerLogic::begin() {
    _state.status.mode  = SystemMode::OFF;
    _state.status.phase = HeaterPhase::IDLE;
    computeActiveSetpoints();
}

void BoilerLogic::update() {
    if (_state.status.otaInProgress) return;  // freeze logic during OTA

    computeActiveSetpoints();
    checkSensorAlarms();
    checkOverheatAlarm();

    // Critical alarm — force stop regardless of mode
    if (_state.alarms.isCritical()) {
        if (_state.relays.heaterOn) setHeater(false, ChangeReason::OVERHEAT);
        if (_state.status.phase == HeaterPhase::HEATING ||
            _state.status.phase == HeaterPhase::PUMP_PRE) {
            transitionPhase(HeaterPhase::PUMP_POST);
        }
        updateBuzzer();
        return;
    }

    switch (_state.status.mode) {
        case SystemMode::OFF:        handleOff();        break;
        case SystemMode::ON:
            switch (_state.status.phase) {
                case HeaterPhase::IDLE:       handleOn();          break;
                case HeaterPhase::PUMP_PRE:   handlePumpPre();     break;
                case HeaterPhase::HEATING:    handleHeating();     break;
                case HeaterPhase::PUMP_POST:  handlePumpPost();    break;
                case HeaterPhase::PUMP_STANDBY: handlePumpStandby(); break;
                default: break;
            }
            break;
        case SystemMode::ANTIFREEZE: handleAntifreeze();  break;
    }

    updateBuzzer();
}

void BoilerLogic::setMode(SystemMode mode) {
    if (mode == _state.status.mode) return;

    // Safe transition — always stop hardware first
    if (_state.relays.heaterOn)  setHeater(false, ChangeReason::MODE_TURNED_OFF);
    if (_state.status.phase != HeaterPhase::IDLE) {
        if (_state.relays.pumpOn) {
            // Let pump finish minimum time, then stop
            transitionPhase(HeaterPhase::PUMP_POST);
        }
    }
    _state.status.mode = mode;

    // Antifreeze keeps pump on
    if (mode == SystemMode::ANTIFREEZE && !_state.relays.pumpOn) {
        setPump(true, ChangeReason::PUMP_ANTIFREEZE_MODE);
    }
}

void BoilerLogic::setHaRemoteDisable(bool disable) {
    _state.status.haRemoteDisable = disable;
}

// ── Phase handlers ────────────────────────────────────────────────────────────

void BoilerLogic::handleOff() {
    if (_state.relays.heaterOn) setHeater(false, ChangeReason::MODE_TURNED_OFF);
    if (_state.relays.pumpOn)   setPump(false,   ChangeReason::PUMP_MODE_TURNED_OFF);
    _state.status.phase = HeaterPhase::IDLE;
}

void BoilerLogic::handleOn() {
    // Check standby pump run
    uint32_t periodMs = (uint32_t)_state.config.standbyPumpPeriodMin * 60 * 1000;
    if (millis() - _lastStandbyPump > periodMs) {
        transitionPhase(HeaterPhase::PUMP_STANDBY);
        setPump(true, ChangeReason::PUMP_STANDBY_RUN);
        return;
    }

    // Normal start check
    if (canStartHeater()) {
        transitionPhase(HeaterPhase::PUMP_PRE);
        setPump(true, ChangeReason::PUMP_PREDELAY_START);
    }
}

void BoilerLogic::handlePumpPre() {
    // Re-check all start conditions every loop during pre-delay
    if (!canStartHeater()) {
        ChangeReason reason = findPredelayFailReason();
        setHeater(false, reason);  // log the cancel (heater was never on but log is informational)
        transitionPhase(HeaterPhase::PUMP_POST);
        return;
    }

    uint32_t preDelayMs = (uint32_t)_state.config.pumpPrePostDelaySec * 1000;
    if (millis() - _phaseEnteredAt >= preDelayMs) {
        // Pre-delay done, all conditions still met — start heater
        transitionPhase(HeaterPhase::HEATING);
        setHeater(true, ChangeReason::START_CONDITIONS_MET);
    }
}

void BoilerLogic::handleHeating() {
    if (shouldStopHeater()) {
        ChangeReason reason = ChangeReason::FLOW_SETPOINT_REACHED;
        if (_state.status.haRemoteDisable)              reason = ChangeReason::HA_REMOTE_DISABLE;
        else if (!isExternalThermostatAllowing())        reason = ChangeReason::EXTERNAL_THERMOSTAT;
        else if (_state.sensors.hasRoom() && !isRoomBelowThreshold()) reason = ChangeReason::ROOM_SETPOINT_REACHED;

        setHeater(false, reason);
        transitionPhase(HeaterPhase::PUMP_POST);
    }
}

void BoilerLogic::handlePumpPost() {
    uint32_t postDelayMs = (uint32_t)_state.config.pumpPrePostDelaySec * 1000;
    uint32_t minPumpMs   = postDelayMs;  // reuse post_delay as min pump time

    uint32_t pumpRunMs = millis() - _pumpOnAt;
    if (pumpRunMs >= minPumpMs) {
        setPump(false, ChangeReason::PUMP_POSTDELAY_COMPLETE);
        transitionPhase(HeaterPhase::IDLE);
    }
}

void BoilerLogic::handlePumpStandby() {
    uint32_t durationMs = (uint32_t)_state.config.standbyPumpDurationMin * 60 * 1000;
    if (millis() - _phaseEnteredAt >= durationMs) {
        setPump(false, ChangeReason::PUMP_STANDBY_RUN_COMPLETE);
        _lastStandbyPump = millis();
        transitionPhase(HeaterPhase::IDLE);
    }
}

void BoilerLogic::handleAntifreeze() {
    if (!_state.relays.pumpOn) {
        setPump(true, ChangeReason::PUMP_ANTIFREEZE_MODE);
    }

    if (!_state.relays.heaterOn && isAntifreezeHeatNeeded()) {
        if (isMinOffTimeElapsed()) {
            setHeater(true, ChangeReason::ANTIFREEZE_RETURN_COLD);
        }
    } else if (_state.relays.heaterOn && isAntifreezeHeatDone()) {
        ChangeReason r = (_state.sensors.hasRoom() &&
                          _state.sensors.roomTemp >= Limits::ANTIFREEZE_ROOM_MAX)
                         ? ChangeReason::ANTIFREEZE_ROOM_WARM
                         : ChangeReason::ANTIFREEZE_RETURN_WARM;
        setHeater(false, r);
    }
}

// ── Condition evaluators ──────────────────────────────────────────────────────

bool BoilerLogic::canStartHeater() const {
    if (!_state.sensors.hasFlow())          return false;
    if (isOverheat())                       return false;
    if (!isFlowBelowThreshold())            return false;
    if (!isReturnGateOpen())                return false;
    if (!isExternalThermostatAllowing())    return false;
    if (isHaAllowing() == false)            return false;
    if (!isMinOffTimeElapsed())             return false;
    if (_state.sensors.hasRoom() && !isRoomBelowThreshold()) return false;
    return true;
}

bool BoilerLogic::shouldStopHeater() const {
    if (isOverheat())                       return true;
    if (!isExternalThermostatAllowing())    return true;
    if (_state.status.haRemoteDisable)      return true;
    if (!_state.sensors.hasFlow())          return true;  // sensor fault
    if (_state.sensors.flowTemp >= _state.status.activeFlowSetpoint) return true;
    if (_state.sensors.hasRoom() && _state.sensors.roomTemp >= _state.config.roomSetpoint) return true;
    return false;
}

bool BoilerLogic::isFlowBelowThreshold() const {
    return _state.sensors.flowTemp <
           (_state.status.activeFlowSetpoint - _state.config.flowHysteresis);
}

bool BoilerLogic::isReturnGateOpen() const {
    if (!_state.sensors.hasReturn()) return true;  // unavailable = don't block
    return _state.sensors.returnTemp < _state.status.activeReturnSetpoint;
}

bool BoilerLogic::isRoomBelowThreshold() const {
    return _state.sensors.roomTemp <
           (_state.config.roomSetpoint - _state.config.roomHysteresis);
}

bool BoilerLogic::isExternalThermostatAllowing() const {
    bool contact = digitalRead(PIN_DIN_THERMOSTAT);
    if (_state.config.thermostatMode == ThermostatContact::NORMAL_OPEN)
        return !contact;   // LOW = contact closed = allow
    else
        return contact;    // HIGH = contact closed = allow
}

bool BoilerLogic::isHaAllowing() const {
    return !_state.status.haRemoteDisable;
}

bool BoilerLogic::isOverheat() const {
    return _state.sensors.hasFlow() &&
           _state.sensors.flowTemp >= Limits::OVERHEAT_TEMP;
}

bool BoilerLogic::isMinOffTimeElapsed() const {
    return (millis() - _heaterOffAt) >= Limits::MIN_HEATER_OFF_MS;
}

bool BoilerLogic::isAntifreezeHeatNeeded() const {
    if (!_state.sensors.hasReturn()) return false;
    bool returnCold = _state.sensors.returnTemp < Limits::ANTIFREEZE_HEATER_ON;
    bool roomOk     = !_state.sensors.hasRoom() ||
                      _state.sensors.roomTemp < Limits::ANTIFREEZE_ROOM_MAX;
    return returnCold && roomOk;
}

bool BoilerLogic::isAntifreezeHeatDone() const {
    if (_state.sensors.hasReturn() &&
        _state.sensors.returnTemp >= Limits::ANTIFREEZE_HEATER_OFF) return true;
    if (_state.sensors.hasRoom() &&
        _state.sensors.roomTemp >= Limits::ANTIFREEZE_ROOM_MAX)      return true;
    return false;
}

ChangeReason BoilerLogic::findPredelayFailReason() const {
    if (_state.status.haRemoteDisable)            return ChangeReason::PREDELAY_HA_DISABLE;
    if (!isExternalThermostatAllowing())           return ChangeReason::PREDELAY_THERMOSTAT;
    if (_state.sensors.hasRoom() && !isRoomBelowThreshold()) return ChangeReason::PREDELAY_ROOM_WARM;
    if (!isReturnGateOpen())                       return ChangeReason::PREDELAY_RETURN_BLOCKED;
    return ChangeReason::PREDELAY_FLOW_WARMED;
}

// ── Setpoint calculation ──────────────────────────────────────────────────────

void BoilerLogic::computeActiveSetpoints() {
    const Config& cfg = _state.config;

    if (cfg.curveCount >= 2 && _state.sensors.hasOutside()) {
        _state.status.activeFlowSetpoint =
            CurveInterp::interpolateFlow(cfg.curve, cfg.curveCount,
                                         _state.sensors.outsideTemp,
                                         cfg.flowSetpoint);
        _state.status.activeReturnSetpoint =
            CurveInterp::interpolateReturn(cfg.curve, cfg.curveCount,
                                           _state.sensors.outsideTemp,
                                           cfg.returnSetpoint);
    } else {
        _state.status.activeFlowSetpoint   = cfg.flowSetpoint;
        _state.status.activeReturnSetpoint = cfg.returnSetpoint;
    }
}

// ── Hardware commands ─────────────────────────────────────────────────────────

void BoilerLogic::setHeater(bool on, ChangeReason reason) {
    _relays.setHeater(on);
    if (on)  _heaterOnAt  = millis();
    else     _heaterOffAt = millis();
    logRelayChange(on ? RelayChange::HEATER_ON : RelayChange::HEATER_OFF, reason);
}

void BoilerLogic::setPump(bool on, ChangeReason reason) {
    _relays.setPump(on);
    if (on) _pumpOnAt = millis();
    logRelayChange(on ? RelayChange::PUMP_ON : RelayChange::PUMP_OFF, reason);
}

void BoilerLogic::transitionPhase(HeaterPhase next) {
    _state.status.phase = next;
    _phaseEnteredAt = millis();
}

// ── Alarm management ──────────────────────────────────────────────────────────

void BoilerLogic::checkSensorAlarms() {
    // Flow sensor
    bool flowFault = _state.sensors.flowSensorFault ||
                     (millis() - _state.sensors.flowLastSeen > Limits::SENSOR_TIMEOUT_MS &&
                      _state.sensors.flowLastSeen != 0);
    if (flowFault != _state.alarms.isSet(AlarmFlag::FLOW_SENSOR_FAULT)) {
        flowFault ? _state.alarms.set(AlarmFlag::FLOW_SENSOR_FAULT)
                  : _state.alarms.clear(AlarmFlag::FLOW_SENSOR_FAULT);
    }

    // Return sensor
    bool returnFault = _state.sensors.returnSensorFault;
    if (returnFault != _state.alarms.isSet(AlarmFlag::RETURN_SENSOR_FAULT)) {
        returnFault ? _state.alarms.set(AlarmFlag::RETURN_SENSOR_FAULT)
                    : _state.alarms.clear(AlarmFlag::RETURN_SENSOR_FAULT);
    }

    // Optional sensors lost
    auto checkLost = [&](uint32_t lastSeen, AlarmFlag flag) {
        bool lost = (lastSeen != 0 &&
                     millis() - lastSeen > Limits::SENSOR_TIMEOUT_MS);
        if (lost != _state.alarms.isSet(flag)) {
            lost ? _state.alarms.set(flag) : _state.alarms.clear(flag);
        }
    };
    checkLost(_state.sensors.roomLastSeen,    AlarmFlag::ROOM_SENSOR_LOST);
    checkLost(_state.sensors.outsideLastSeen, AlarmFlag::OUTSIDE_SENSOR_LOST);
}

void BoilerLogic::checkOverheatAlarm() {
    bool overheat = isOverheat();
    if (overheat != _state.alarms.isSet(AlarmFlag::OVERHEAT)) {
        overheat ? _state.alarms.set(AlarmFlag::OVERHEAT)
                 : _state.alarms.clear(AlarmFlag::OVERHEAT);
    }
}

void BoilerLogic::updateBuzzer() {
    if (_state.alarms.isSet(AlarmFlag::OVERHEAT)) {
        _buzzer.setPattern(BuzzerPattern::CONTINUOUS);
    } else if (_state.alarms.isSet(AlarmFlag::FLOW_SENSOR_FAULT)) {
        _buzzer.setPattern(BuzzerPattern::FAULT_INTERMITTENT);
    } else {
        _buzzer.setPattern(BuzzerPattern::SILENT);
    }
}

// ── Logging ───────────────────────────────────────────────────────────────────

void BoilerLogic::logRelayChange(RelayChange relay, ChangeReason reason) {
    TempSnapshot snap = captureTemps();
    _state.log.push(relay, reason, snap,
                    (uint8_t)_state.status.activeFlowSetpoint,
                    (uint8_t)_state.status.activeReturnSetpoint,
                    _time.now());
}

TempSnapshot BoilerLogic::captureTemps() const {
    return {
        TempSnapshot::encode(_state.sensors.flowTemp),
        TempSnapshot::encode(_state.sensors.returnTemp),
        TempSnapshot::encode(_state.sensors.roomTemp),
        TempSnapshot::encode(_state.sensors.outsideTemp)
    };
}

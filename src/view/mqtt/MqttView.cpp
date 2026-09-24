#include "MqttView.h"
#include <Arduino.h>
#include <esp_timer.h>

MqttView* MqttView::_instance = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / begin
// ─────────────────────────────────────────────────────────────────────────────

MqttView::MqttView(AppState& state, MqttService& mqtt, BoilerLogic& logic)
    : _state(state)
    , _mqtt(mqtt)
    , _logic(logic)
{}

void MqttView::begin() {
    _instance = this;
    _mqtt.onMessage(onMessage);
    Serial.println("[MqttView] Initialized");
}

void MqttView::update() {
    applyPendingCommands();

    if (!_mqtt.connected()) return;

    // New broker session (first connect or reconnect): clean session drops
    // subscriptions, so discovery + subscribe must be redone every time
    uint32_t session = _mqtt.connectCount();
    if (session != _sessionSeen) {
        _sessionSeen = session;
        onNewSession();
    }

    uint32_t now = millis();
    if (_publishNow || now - _lastPublish >= PUBLISH_INTERVAL_MS) {
        _publishNow  = false;
        _lastPublish = now;
        publishState();
        publishAlarms();
    }
}

void MqttView::onNewSession() {
    publishDiscovery();
    _mqtt.subscribe("boiler/set/mode",        1);
    _mqtt.subscribe("boiler/set/ha_disable",  1);
    _mqtt.subscribe("boiler/set/flow_sp",     1);
    _mqtt.subscribe("boiler/set/return_sp",   1);
    _mqtt.subscribe("boiler/set/room_sp",     1);

    // Forget cached values so the full state is republished right away
    _lastFlowTemp = _lastReturnTemp = _lastRoomTemp = _lastOutsideTemp = -999;
    _lastMode   = 255;
    _lastHeater = !_state.relays.heaterOn;
    _lastPump   = !_state.relays.pumpOn;
    _lastAlarms = 0xFFFF;
    _publishNow = true;
}

void MqttView::applyPendingCommands() {
    portENTER_CRITICAL(&_pendingMux);
    PendingCmds cmd = _pending;
    _pending = PendingCmds{};
    portEXIT_CRITICAL(&_pendingMux);

    bool any = false;
    if (cmd.mode >= 0) {
        _logic.setMode(static_cast<SystemMode>(cmd.mode));  // safe transition (pump post-run etc.)
        any = true;
    }
    if (cmd.haDisable >= 0) {
        _logic.setHaRemoteDisable(cmd.haDisable == 1);
        any = true;
    }
    if (cmd.flowSp   >= 0) { _state.config.flowSetpoint   = (uint8_t)cmd.flowSp;   any = true; }
    if (cmd.returnSp >= 0) { _state.config.returnSetpoint = (uint8_t)cmd.returnSp; any = true; }
    if (cmd.roomSp   >= 0) { _state.config.roomSetpoint   = (uint8_t)cmd.roomSp;   any = true; }

    if (any) _publishNow = true;  // echo new state to HA without waiting 5 s
}

// ─────────────────────────────────────────────────────────────────────────────
//  Discovery
// ─────────────────────────────────────────────────────────────────────────────

void MqttView::publishDiscovery() {
    // Temperature sensors
    publishSensorDiscovery("flow_temp",    "Boiler Flow Temp",    "boiler/sensor/flow_temp",    "°C", "temperature");
    publishSensorDiscovery("return_temp",  "Boiler Return Temp",  "boiler/sensor/return_temp",  "°C", "temperature");
    publishSensorDiscovery("room_temp",    "Room Temperature",    "boiler/sensor/room_temp",    "°C", "temperature");
    publishSensorDiscovery("outside_temp", "Outside Temperature", "boiler/sensor/outside_temp", "°C", "temperature");

    // Binary sensors for relays
    publishBinarySensorDiscovery("heater", "Boiler Heater", "boiler/relay/heater", "heat");
    publishBinarySensorDiscovery("pump",   "Boiler Pump",   "boiler/relay/pump",   nullptr);

    // Binary sensors for alarms
    publishBinarySensorDiscovery("alarm_overheat",          "Boiler Overheat",          "boiler/alarm/overheat",           "problem");
    publishBinarySensorDiscovery("alarm_flow_fault",        "Flow Sensor Fault",        "boiler/alarm/flow_sensor_fault",  "problem");
    publishBinarySensorDiscovery("alarm_return_fault",      "Return Sensor Fault",      "boiler/alarm/return_sensor_fault","problem");
    publishBinarySensorDiscovery("alarm_room_lost",         "Room Sensor Lost",         "boiler/alarm/room_sensor_lost",   "problem");
    publishBinarySensorDiscovery("alarm_outside_lost",      "Outside Sensor Lost",      "boiler/alarm/outside_sensor_lost","problem");
    publishBinarySensorDiscovery("alarm_ble_room_bat",      "Room BLE Low Battery",     "boiler/alarm/ble_room_low_bat",   "battery");
    publishBinarySensorDiscovery("alarm_ble_outside_bat",   "Outside BLE Low Battery",  "boiler/alarm/ble_outside_low_bat","battery");

    // Mode select
    publishSelectDiscovery();

    // HA remote disable switch
    publishSwitchDiscovery("ha_disable", "HA Remote Disable",
                           "boiler/status/ha_disable",
                           "boiler/set/ha_disable");

    // Setpoint numbers
    publishNumberDiscovery("flow_sp",   "Flow Setpoint",   "boiler/setpoint/flow",   "boiler/set/flow_sp",   20, 65, 1, "°C");
    publishNumberDiscovery("return_sp", "Return Setpoint", "boiler/setpoint/return", "boiler/set/return_sp", 20, 65, 1, "°C");
    publishNumberDiscovery("room_sp",   "Room Setpoint",   "boiler/setpoint/room",   "boiler/set/room_sp",   5,  30, 1, "°C");

    Serial.println("[MqttView] HA discovery published");
}

void MqttView::publishSensorDiscovery(const char* id, const char* name,
                                       const char* topic, const char* unit,
                                       const char* devClass) {
    char discoveryTopic[96];
    snprintf(discoveryTopic, sizeof(discoveryTopic),
             "homeassistant/sensor/%s/%s/config", BASE, id);

    JsonDocument doc;
    doc["name"]        = name;
    doc["unique_id"]   = String("boiler_") + id;
    doc["state_topic"] = topic;
    doc["unit_of_measurement"] = unit;
    if (devClass) doc["device_class"] = devClass;
    doc["value_template"] = "{{ value }}";
    doc["availability_topic"] = "boiler/status/online";
    doc["payload_available"]  = "online";
    doc["payload_not_available"] = "offline";

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = "boiler_esp32";
    dev["name"]           = "Electric Boiler";
    dev["model"]          = "HKL-EA2";
    dev["manufacturer"]   = "HANKERILA";

    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(discoveryTopic, payload.c_str(), true);
}

void MqttView::publishBinarySensorDiscovery(const char* id, const char* name,
                                             const char* topic, const char* devClass) {
    char discoveryTopic[96];
    snprintf(discoveryTopic, sizeof(discoveryTopic),
             "homeassistant/binary_sensor/%s/%s/config", BASE, id);

    JsonDocument doc;
    doc["name"]        = name;
    doc["unique_id"]   = String("boiler_") + id;
    doc["state_topic"] = topic;
    doc["payload_on"]  = "ON";
    doc["payload_off"] = "OFF";
    if (devClass) doc["device_class"] = devClass;
    doc["availability_topic"]    = "boiler/status/online";
    doc["payload_available"]     = "online";
    doc["payload_not_available"] = "offline";

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = "boiler_esp32";
    dev["name"]           = "Electric Boiler";
    dev["model"]          = "HKL-EA2";
    dev["manufacturer"]   = "HANKERILA";

    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(discoveryTopic, payload.c_str(), true);
}

void MqttView::publishSelectDiscovery() {
    char discoveryTopic[96];
    snprintf(discoveryTopic, sizeof(discoveryTopic),
             "homeassistant/select/%s/mode/config", BASE);

    JsonDocument doc;
    doc["name"]           = "Boiler Mode";
    doc["unique_id"]      = "boiler_mode";
    doc["state_topic"]    = "boiler/status/mode";
    doc["command_topic"]  = "boiler/set/mode";
    doc["options"][0]     = "off";
    doc["options"][1]     = "on";
    doc["options"][2]     = "antifreeze";
    doc["availability_topic"]    = "boiler/status/online";
    doc["payload_available"]     = "online";
    doc["payload_not_available"] = "offline";

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = "boiler_esp32";
    dev["name"]           = "Electric Boiler";
    dev["model"]          = "HKL-EA2";
    dev["manufacturer"]   = "HANKERILA";

    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(discoveryTopic, payload.c_str(), true);
}

void MqttView::publishSwitchDiscovery(const char* id, const char* name,
                                       const char* stateTopic, const char* cmdTopic) {
    char discoveryTopic[96];
    snprintf(discoveryTopic, sizeof(discoveryTopic),
             "homeassistant/switch/%s/%s/config", BASE, id);

    JsonDocument doc;
    doc["name"]           = name;
    doc["unique_id"]      = String("boiler_") + id;
    doc["state_topic"]    = stateTopic;
    doc["command_topic"]  = cmdTopic;
    doc["payload_on"]     = "ON";
    doc["payload_off"]    = "OFF";
    doc["state_on"]       = "ON";
    doc["state_off"]      = "OFF";
    doc["availability_topic"]    = "boiler/status/online";
    doc["payload_available"]     = "online";
    doc["payload_not_available"] = "offline";

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = "boiler_esp32";
    dev["name"]           = "Electric Boiler";
    dev["model"]          = "HKL-EA2";
    dev["manufacturer"]   = "HANKERILA";

    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(discoveryTopic, payload.c_str(), true);
}

void MqttView::publishNumberDiscovery(const char* id, const char* name,
                                       const char* stateTopic, const char* cmdTopic,
                                       float min, float max, float step, const char* unit) {
    char discoveryTopic[96];
    snprintf(discoveryTopic, sizeof(discoveryTopic),
             "homeassistant/number/%s/%s/config", BASE, id);

    JsonDocument doc;
    doc["name"]          = name;
    doc["unique_id"]     = String("boiler_") + id;
    doc["state_topic"]   = stateTopic;
    doc["command_topic"] = cmdTopic;
    doc["min"]           = min;
    doc["max"]           = max;
    doc["step"]          = step;
    doc["unit_of_measurement"] = unit;
    doc["availability_topic"]    = "boiler/status/online";
    doc["payload_available"]     = "online";
    doc["payload_not_available"] = "offline";

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = "boiler_esp32";
    dev["name"]           = "Electric Boiler";
    dev["model"]          = "HKL-EA2";
    dev["manufacturer"]   = "HANKERILA";

    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(discoveryTopic, payload.c_str(), true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  State publish
// ─────────────────────────────────────────────────────────────────────────────

void MqttView::publishState() {
    const SensorData&   s   = _state.sensors;
    const SystemStatus& st  = _state.status;
    const RelayState&   r   = _state.relays;
    const Config&       cfg = _state.config;

    // Online
    _mqtt.publish("boiler/status/online", "online", true);

    // Temperatures — only publish if changed
    if (!isnan(s.flowTemp)) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%.1f", s.flowTemp);
        publishIfChanged("boiler/sensor/flow_temp", _lastFlowTemp, s.flowTemp, buf);
    }
    if (!isnan(s.returnTemp)) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%.1f", s.returnTemp);
        publishIfChanged("boiler/sensor/return_temp", _lastReturnTemp, s.returnTemp, buf);
    }
    if (!isnan(s.roomTemp)) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%.1f", s.roomTemp);
        publishIfChanged("boiler/sensor/room_temp", _lastRoomTemp, s.roomTemp, buf);
    }
    if (!isnan(s.outsideTemp)) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%.1f", s.outsideTemp);
        publishIfChanged("boiler/sensor/outside_temp", _lastOutsideTemp, s.outsideTemp, buf);
    }

    // Relays
    bool heater = r.heaterOn;
    bool pump   = r.pumpOn;
    publishIfChanged("boiler/relay/heater", _lastHeater, heater, heater ? "ON" : "OFF");
    publishIfChanged("boiler/relay/pump",   _lastPump,   pump,   pump   ? "ON" : "OFF");

    // Mode
    uint8_t mode = (uint8_t)st.mode;
    const char* modeStr = "off";
    if (st.mode == SystemMode::ON)         modeStr = "on";
    if (st.mode == SystemMode::ANTIFREEZE) modeStr = "antifreeze";
    publishIfChanged("boiler/status/mode", _lastMode, mode, modeStr);

    // HA remote disable
    _mqtt.publish("boiler/status/ha_disable",
                  st.haRemoteDisable ? "ON" : "OFF", true);

    // Active setpoints
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)st.activeFlowSetpoint);
    _mqtt.publish("boiler/setpoint/flow", buf, true);
    snprintf(buf, sizeof(buf), "%d", (int)st.activeReturnSetpoint);
    _mqtt.publish("boiler/setpoint/return", buf, true);
    snprintf(buf, sizeof(buf), "%d", (int)cfg.roomSetpoint);
    _mqtt.publish("boiler/setpoint/room", buf, true);

    // Phase
    const char* phaseStr = "idle";
    switch (st.phase) {
        case HeaterPhase::PUMP_PRE:     phaseStr = "pump_pre";     break;
        case HeaterPhase::HEATING:      phaseStr = "heating";      break;
        case HeaterPhase::PUMP_POST:    phaseStr = "pump_post";    break;
        case HeaterPhase::PUMP_STANDBY: phaseStr = "pump_standby"; break;
        default: break;
    }
    _mqtt.publish("boiler/status/phase", phaseStr, true);
}

void MqttView::publishAlarms() {
    const AlarmState& al = _state.alarms;
    uint16_t current     = al.active;

    if (current == _lastAlarms) return; // No change

    // Publish individual alarm topics (retained)
    auto pub = [&](AlarmFlag flag, const char* topic, const char* eventMsg) {
        bool isSet = al.isSet(flag);
        uint16_t bit = static_cast<uint16_t>(flag);
        bool wasSet = (_lastAlarms & bit) != 0;
        _mqtt.publish(topic, isSet ? "ON" : "OFF", true);

        // Publish non-retained event on activation
        if (isSet && !wasSet) {
            publishAlarmEvent(flag, eventMsg);
        }
    };

    pub(AlarmFlag::OVERHEAT,              "boiler/alarm/overheat",           "Overheat alarm");
    pub(AlarmFlag::FLOW_SENSOR_FAULT,     "boiler/alarm/flow_sensor_fault",  "Flow sensor fault");
    pub(AlarmFlag::RETURN_SENSOR_FAULT,   "boiler/alarm/return_sensor_fault","Return sensor fault");
    pub(AlarmFlag::ROOM_SENSOR_LOST,      "boiler/alarm/room_sensor_lost",   "Room sensor lost");
    pub(AlarmFlag::OUTSIDE_SENSOR_LOST,   "boiler/alarm/outside_sensor_lost","Outside sensor lost");
    pub(AlarmFlag::BLE_ROOM_LOW_BATTERY,  "boiler/alarm/ble_room_low_bat",   "Room BLE low battery");
    pub(AlarmFlag::BLE_OUTSIDE_LOW_BATTERY,"boiler/alarm/ble_outside_low_bat","Outside BLE low battery");

    _lastAlarms = current;
}

void MqttView::publishAlarmEvent(AlarmFlag flag, const char* message) {
    JsonDocument doc;
    doc["alarm"]   = static_cast<uint16_t>(flag);
    doc["message"] = message;
    doc["ts"]      = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    const SensorData& s = _state.sensors;
    if (!isnan(s.flowTemp))   doc["flowTemp"]   = serialized(String(s.flowTemp, 1));
    if (!isnan(s.returnTemp)) doc["returnTemp"]  = serialized(String(s.returnTemp, 1));

    String payload;
    serializeJson(doc, payload);
    // Non-retained event topic
    _mqtt.publish("boiler/alarm/event", payload.c_str(), false, 1);
}

template<typename T>
bool MqttView::publishIfChanged(const char* topic, T& lastVal, T newVal,
                                 const char* payload, bool retain) {
    if (lastVal == newVal) return false;
    lastVal = newVal;
    return _mqtt.publish(topic, payload, retain);
}

// Template explicit instantiation to avoid linker errors
template bool MqttView::publishIfChanged<float>(const char*, float&, float, const char*, bool);
template bool MqttView::publishIfChanged<bool>(const char*, bool&, bool, const char*, bool);
template bool MqttView::publishIfChanged<uint8_t>(const char*, uint8_t&, uint8_t, const char*, bool);

// ─────────────────────────────────────────────────────────────────────────────
//  Incoming MQTT command handling
// ─────────────────────────────────────────────────────────────────────────────

void MqttView::onMessage(const char* topic, const char* payload) {
    if (!_instance) return;
    MqttView& self = *_instance;

    // Parse here, apply later in the main loop (see applyPendingCommands)
    PendingCmds cmd;
    if (strcmp(topic, "boiler/set/mode") == 0) {
        if      (strcmp(payload, "off") == 0)        cmd.mode = (int8_t)SystemMode::OFF;
        else if (strcmp(payload, "on") == 0)         cmd.mode = (int8_t)SystemMode::ON;
        else if (strcmp(payload, "antifreeze") == 0) cmd.mode = (int8_t)SystemMode::ANTIFREEZE;
        else { Serial.printf("[MqttView] Unknown mode: %s\n", payload); return; }

    } else if (strcmp(topic, "boiler/set/ha_disable") == 0) {
        cmd.haDisable = (strcmp(payload, "ON") == 0) ? 1 : 0;

    } else if (strcmp(topic, "boiler/set/flow_sp") == 0) {
        int val = atoi(payload);
        if (val >= 20 && val <= 65) cmd.flowSp = val;

    } else if (strcmp(topic, "boiler/set/return_sp") == 0) {
        int val = atoi(payload);
        if (val >= 20 && val <= 65) cmd.returnSp = val;

    } else if (strcmp(topic, "boiler/set/room_sp") == 0) {
        int val = atoi(payload);
        if (val >= 5 && val <= 30) cmd.roomSp = val;
    }
    Serial.printf("[MqttView] Command %s = %s\n", topic, payload);

    portENTER_CRITICAL(&self._pendingMux);
    if (cmd.mode      >= 0) self._pending.mode      = cmd.mode;
    if (cmd.haDisable >= 0) self._pending.haDisable = cmd.haDisable;
    if (cmd.flowSp    >= 0) self._pending.flowSp    = cmd.flowSp;
    if (cmd.returnSp  >= 0) self._pending.returnSp  = cmd.returnSp;
    if (cmd.roomSp    >= 0) self._pending.roomSp    = cmd.roomSp;
    portEXIT_CRITICAL(&self._pendingMux);
}

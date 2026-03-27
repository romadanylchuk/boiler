# MQTT Alarms & Notifications

## Alarm Types

| Alarm | Severity | Heater behavior | Buzzer |
|-------|----------|-----------------|--------|
| OVERHEAT | CRITICAL | Stop immediately | Yes — continuous |
| FLOW_SENSOR_FAULT | CRITICAL | Stop immediately | Yes — intermittent |
| RETURN_SENSOR_FAULT | WARNING | Continue | No |
| ROOM_SENSOR_LOST | INFO | Continue (ignore room input) | No |
| OUTSIDE_SENSOR_LOST | INFO | Continue (use manual setpoints) | No |
| BLE_ROOM_LOW_BATTERY | WARNING | No effect | No |
| BLE_OUTSIDE_LOW_BATTERY | WARNING | No effect | No |

---

## Two-Layer MQTT Alarm Design

### Layer 1 — State topics (retained)
Current alarm state. HA creates binary sensors from these.
Persists across MQTT reconnects — HA always knows current state.

```
boiler/alarm/overheat          → "ON" / "OFF"
boiler/alarm/flow_sensor       → "ON" / "OFF"
boiler/alarm/return_sensor     → "ON" / "OFF"
boiler/alarm/room_sensor_lost  → "ON" / "OFF"
boiler/alarm/outside_sensor_lost → "ON" / "OFF"
boiler/alarm/ble_room_battery  → "ON" / "OFF"
boiler/alarm/ble_outside_battery → "ON" / "OFF"
```

### Layer 2 — Event topic (non-retained)
One-shot notification published when alarm ACTIVATES.
Used to trigger HA automations → phone notifications.
Not published on alarm clear (clear = state topic going to "OFF").

```
boiler/alarm/event
```

Payload JSON:
```json
{
  "alarm":     "OVERHEAT",
  "severity":  "CRITICAL",
  "message":   "Flow temperature exceeded 80°C",
  "flow_temp": 81.4,
  "timestamp": "2025-03-24 14:32:11"
}
```

---

## AppState — Alarm Model

```cpp
// Each bit = one alarm flag
enum class AlarmFlag : uint16_t {
    NONE                  = 0,
    OVERHEAT              = (1 << 0),
    FLOW_SENSOR_FAULT     = (1 << 1),
    RETURN_SENSOR_FAULT   = (1 << 2),
    ROOM_SENSOR_LOST      = (1 << 3),
    OUTSIDE_SENSOR_LOST   = (1 << 4),
    BLE_ROOM_LOW_BATTERY  = (1 << 5),
    BLE_OUTSIDE_LOW_BATTERY = (1 << 6),
};

struct AlarmState {
    uint16_t active;   // bitfield of AlarmFlag values

    void  set(AlarmFlag f)   { active |=  static_cast<uint16_t>(f); }
    void  clear(AlarmFlag f) { active &= ~static_cast<uint16_t>(f); }
    bool  isSet(AlarmFlag f) const { return active & static_cast<uint16_t>(f); }
    bool  any() const        { return active != 0; }

    // Critical = must stop heater
    bool  isCritical() const {
        return isSet(AlarmFlag::OVERHEAT) ||
               isSet(AlarmFlag::FLOW_SENSOR_FAULT);
    }
};
```

Add to `AppState`:
```cpp
struct AppState {
    SensorData   sensors;
    RelayState   relays;
    SystemStatus status;
    AlarmState   alarms;    // ← added
    Config       config;
    EventLog     log;
};
```

---

## MqttView — Alarm Publishing

```
On alarm SET:
  1. Publish state topic with "ON"  (retained=true)
  2. Publish event topic with JSON  (retained=false)
  3. Log entry added to EventLog

On alarm CLEAR:
  1. Publish state topic with "OFF" (retained=true)
  2. No event topic (clear is informational only)
  3. Log entry added to EventLog
```

MqttView compares current `AlarmState` to previous published state each loop.
Only publishes when state changes — no flooding.

---

## HA MQTT Autodiscovery

Each alarm registers as a binary sensor with `device_class: problem`.

Example autodiscovery config published to:
`homeassistant/binary_sensor/boiler_overheat/config`

```json
{
  "name": "Boiler Overheat",
  "device_class": "problem",
  "state_topic": "boiler/alarm/overheat",
  "payload_on": "ON",
  "payload_off": "OFF",
  "unique_id": "boiler_alarm_overheat",
  "device": {
    "identifiers": ["boiler_hkl_ea2"],
    "name": "Boiler Controller",
    "model": "HKL-EA2",
    "manufacturer": "Custom"
  }
}
```

All 7 alarms auto-register similarly on first MQTT connect.

---

## HA Automation Example (user sets up in HA)

```yaml
automation:
  - alias: "Boiler overheat notification"
    trigger:
      platform: mqtt
      topic: boiler/alarm/event
    condition:
      - condition: template
        value_template: "{{ trigger.payload_json.severity == 'CRITICAL' }}"
    action:
      - service: notify.mobile_app
        data:
          title: "🔥 Boiler Alert"
          message: "{{ trigger.payload_json.message }} at {{ trigger.payload_json.timestamp }}"
```

---

## Buzzer Behavior

| Alarm | Pattern |
|-------|---------|
| OVERHEAT | Continuous tone |
| FLOW_SENSOR_FAULT | 3 beeps, 2s pause, repeat |
| Alarm cleared | 1 short confirmation beep |
| No active alarms | Silent |

Buzzer controlled by BoilerLogic based on AlarmState — not by MqttView.

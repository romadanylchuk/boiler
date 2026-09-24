# Boiler Control Logic

## System Modes
- **OFF** — everything stopped. Power-on always starts in OFF. Requires manual or HA command to start.
- **ON** — auto heating logic (see below)
- **ANTIFREEZE** — frost protection (see below)

Mode selectable from: buttons, web UI, HA MQTT.

---

## ON Mode — Heating Algorithm

### Heater START conditions (ALL must be true):
```
flow_temp  <  (flow_setpoint - flow_hysteresis)
AND return_temp  <  return_setpoint
AND room_temp  <  (room_setpoint - room_hysteresis)   [only if room sensor available]
AND external_thermostat == ALLOW                       [if configured]
AND ha_remote_disable == false
AND flow_temp  <  80°C
AND heater_off_elapsed  >=  min_heater_off_time   [60–180 s, configurable]
```

### Heater STOP conditions (ANY is true):
```
flow_temp  >=  flow_setpoint
OR  room_temp  >=  room_setpoint                       [only if room sensor available]
OR  external_thermostat == BLOCK
OR  ha_remote_disable == true
OR  flow_temp  >=  80°C  → OVERHEAT ALARM
OR  heater_on_elapsed  >=  min ON time (after this, normal stop conditions apply)
```
NOTE: return_temp is NOT checked while heater is running — only at start.

### Return temp re-enable gate:
After heater stops, heater cannot start again until:
```
return_temp  <  (return_setpoint - return_hysteresis)
```

### Pump sequence:
```
START trigger received
  → Pump ON
  → Wait pre_delay (30–120 sec, configurable)
      ↑ ALL start conditions re-checked continuously during this wait
      ↑ If ANY condition becomes false → cancel:
            Heater stays OFF
            Pump continues running until pump_min_on_time elapsed
            Then → post_delay → Pump OFF → Standby
  → All conditions still true after pre_delay → Heater ON
  → [Heating runs — normal STOP conditions apply]
  → Heater OFF
  → Wait post_delay (= pre_delay, same value), counted from heater OFF
      ↑ Start conditions (except min_heater_off_time) re-checked continuously
      ↑ If ALL are true → heat demand returned:
            Pump stays ON (even past post_delay)
            Wait until min_heater_off_time elapsed
            → back to pre_delay (full length, pump not restarted, no new pump-ON log)
  → No heat demand when post_delay done → Pump OFF
  → [Standby]
```
NOTE: if heater never turned on (cancelled during pre_delay), post phase only
waits until pump has run pump_min_on_time (= pre_delay) since pump start.

### Standby pump run (anti-freeze circulation):
- Period: configurable 30 min – 3 h
- Duration: configurable 1–5 min
- Runs pump only (no heater) to prevent pipe freeze in long standby

---

## ANTIFREEZE Mode

Purpose: keep pipes from freezing when building is empty.
Toggle: **manual only** — via buttons, web UI, or HA MQTT. Never auto-activates.

### Behavior:
- Pump runs **continuously** (no pre/post delay — always on)
- Heater ON when:
  `return_temp < 8°C`
  AND `room_temp < 10°C`  ← only checked if room sensor is available
- Heater OFF when:
  `return_temp >= 10°C` (fixed 2°C hysteresis)
  OR `room_temp >= 10°C` (if room sensor available)
  OR `flow_temp >= flow_setpoint` (safety ceiling, same as ON mode)
- Overheat protection still active (80°C hard limit)
- External thermostat and HA remote disable still respected

---

## Safety / Hard Limits

| Condition | Action |
|-----------|--------|
| flow_temp >= 80°C | Stop heater immediately + buzzer alarm |
| flow sensor error (DS18B20 -127°C or 85°C) | Stop heater immediately + warning |
| return sensor error | Continue + warning (return treated as unavailable) |
| Power restored | Start in OFF mode, require manual confirmation |

Hardware backup: physical 85°C thermostat on heater (independent of firmware).

---

## Configurable Parameters

### Setpoints (stored as int in NVS, displayed with 1 decimal)
| Parameter | Range | Default |
|-----------|-------|---------|
| flow_setpoint | 20–65°C | 55°C |
| return_setpoint | 20–65°C | 45°C |
| flow_hysteresis | 1–10°C | 3°C |
| return_hysteresis | 1–10°C | 3°C |
| room_setpoint | 5–30°C | 21°C |
| room_hysteresis | 1–5°C | 1°C |

### Timing
| Parameter | Range | Default |
|-----------|-------|---------|
| pump_pre_post_delay | 30–120 sec | 60 sec |
| min_heater_on_time | fixed | 3 min |
| min_heater_off_time | 60–180 sec | 180 sec |
| standby_pump_period | 30 min – 3 h | 2 h |
| standby_pump_duration | 1–5 min | 3 min |

### External thermostat
| Parameter | Options | Default |
|-----------|---------|---------|
| thermostat_mode | NORMAL_OPEN, NORMAL_CLOSED | NORMAL_OPEN |

---

## Weather Compensation (Outside Temp Curve)

- User configures 3–5 points: outside_temp → flow_setpoint + return_setpoint
- Firmware linearly interpolates between points
- Beyond extreme points: clamp to nearest defined value (no extrapolation)
- If outside sensor unavailable: use manually configured flow/return setpoints
- Typical example:
  ```
  outside -20°C → flow 65°C, return 55°C
  outside   0°C → flow 55°C, return 45°C
  outside +10°C → flow 40°C, return 35°C
  outside +15°C → flow 30°C, return 25°C
  ```

---

## Home Assistant MQTT Interface

### Readable (published by boiler):
- flow_temp, return_temp
- room_temp (if available + source)
- outside_temp (if available + source)
- heater_state (ON/OFF)
- pump_state (ON/OFF)
- mode (OFF/ON/ANTIFREEZE)
- warnings (overheat, flow_sensor_error, return_sensor_error)
- active_flow_setpoint (current effective value, may be from curve)
- active_return_setpoint

### Writable (subscribed by boiler):
- mode (OFF / ON / ANTIFREEZE)
- ha_remote_disable (bool — remote thermostat flag)
- room_setpoint
- flow_setpoint
- room_sensor_api_url (if room source = API)
- outside_sensor_api_url (if outside source = API)

---

## OTA Updates
- Via WiFi (Arduino OTA / ElegantOTA)
- Protected by password
- Boiler stops heating during OTA flash for safety

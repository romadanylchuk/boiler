# Security Architecture

## General Notes
- All communication is HTTP (not HTTPS) — acceptable for local LAN use.
  TLS on ESP32 is possible but costs ~50KB RAM and slows OTA significantly.
  If exposed outside LAN, use a reverse proxy with TLS (e.g. HA + Nginx).
- All credentials stored in NVS (encrypted flash partition).
- Default credentials must be changed on first boot (forced via web UI).

---

## Web Interface — Two Access Levels

### REST API read scope (token auth):
Same as monitoring dashboard below — all sensor values, states, warnings readable.

### Both levels — first page (monitoring dashboard):
- flow_temp, return_temp, room_temp, outside_temp
- heater state (ON/OFF), pump state (ON/OFF)
- current mode (OFF/ON/ANTIFREEZE)
- active setpoints (flow, return — effective values incl. curve)
- warnings / alarms

### Level 2 — Operator (same scope as HA API):
- View monitoring dashboard
- Change: mode, ha_remote_disable, room_setpoint, flow_setpoint
- Change: room_sensor_api_url, outside_sensor_api_url (if mode=API)
- Cannot access: system settings, calibration, timing params, curve config
- Cannot access: OTA firmware update

### Level 1 — Admin:
- Everything in Level 2
- All configurable parameters (setpoints, hysteresis, timing, thermostat mode)
- Weather compensation curve editor (3–5 points)
- Sensor source configuration (BLE MAC, API URL, None — for room + outside)
- WiFi / MQTT credentials
- Access credentials management (change passwords, API token)
- OTA firmware update (password-confirmed action)
- System reboot

---

## Web Authentication
- Session-based: login form → server sets session cookie (random 32-byte token)
- Session stored in ESP32 RAM (max 4 concurrent sessions)
- Session timeout: 30 min inactivity (configurable in admin)
- Login page always accessible, all other routes require valid session
- Role encoded in session (ADMIN or OPERATOR)
- Failed login: 3 attempts → 60 sec lockout (per IP)

### Credentials stored in NVS:
```
web_admin_user     (default: "admin")
web_admin_pass     (default: — forced change on first boot)
web_operator_user  (default: "operator")
web_operator_pass  (default: — forced change on first boot)
```

### First boot:
- If credentials not set → redirect all requests to /setup page
- /setup allows setting admin + operator passwords
- After setup → normal operation

---

## REST API (for Home Assistant)
- Bearer token in Authorization header:
  `Authorization: Bearer <api_token>`
- Or API key in custom header:
  `X-API-Key: <api_token>`
- Token: 32-byte random hex, generated on first boot, stored in NVS
- Token visible + regeneratable in Admin web UI only
- All API endpoints (GET + POST) require token — no exceptions
- Wrong token → HTTP 401

### API token stored in NVS:
```
api_token   (generated on first boot, never a default)
```

---

## MQTT (Home Assistant)
- Username + password authentication
- Credentials configurable in Admin web UI
- Stored in NVS

```
mqtt_user
mqtt_pass
```

---

## OTA Firmware Update
- Accessible from Admin web UI only (Level 1)
- Requires re-entering admin password before flash starts
- Boiler stops heating during flash (safe state)
- On failed flash: automatic rollback to previous firmware (if partition scheme supports it)

---

## Security Summary Table

| Feature | Operator | Admin |
|---------|----------|-------|
| View monitoring dashboard | ✅ | ✅ |
| Change mode / remote disable | ✅ | ✅ |
| Change room/flow setpoints | ✅ | ✅ |
| Change sensor API URLs | ✅ | ✅ |
| Change all system parameters | ❌ | ✅ |
| Weather curve editor | ❌ | ✅ |
| Sensor source config (BLE/API/None) | ❌ | ✅ |
| WiFi / MQTT credentials | ❌ | ✅ |
| Manage web/API credentials | ❌ | ✅ |
| OTA update | ❌ | ✅ |
| System reboot | ❌ | ✅ |
| REST API access | ✅ (token) | ✅ (token) |

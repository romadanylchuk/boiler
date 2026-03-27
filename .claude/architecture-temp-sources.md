# Temperature Source Architecture

## Principle
Room and outside temperature sensors are OPTIONAL inputs.
Boiler logic always works using only flow/return DS18B20 sensors.
Optional sensors enhance logic when available, are silently ignored when not.

## Per-sensor modes (configured independently)
- **BLE** — passive advertisement scanning by MAC address (IBS-TH2 / IBS-TH2 Plus)
- **API** — HTTP GET + JSON path parsing, configurable poll interval
- **None** — disabled, always returns unavailable

## Abstraction Layer
Each sensor exposed as:
- `getValue() → float`  (NaN if unavailable)
- `isAvailable() → bool`
- `getLastSeen() → uint32_t` (millis timestamp)

Boiler logic only calls `isAvailable()` before using a value. Never crashes on missing sensor.

## Failure / Timeout Rules
- BLE: mark unavailable if no advertisement received for >5 minutes
- API: mark unavailable on HTTP error, timeout, or JSON parse failure; retry on next poll interval
- None: always unavailable

## Config Structure (NVS-persisted)
```cpp
enum class TempSourceMode { NONE, BLE, API };

struct TempSourceConfig {
    TempSourceMode mode;
    char bleMac[18];        // BLE mode: "AA:BB:CC:DD:EE:FF"
    char apiUrl[128];       // API mode: full URL
    char apiJsonPath[64];   // API mode: JSON key or path
    uint32_t apiPollSec;    // API mode: poll interval in seconds
};
```

## Sensors
| Sensor | Variable | Source options |
|--------|----------|---------------|
| Room temperature | room_temp | BLE (IBS-TH2), API, None |
| Outside temperature | outside_temp | BLE (IBS-TH2 Plus / Ruuvi), API, None |
| Flow temperature | flow_temp | DS18B20 GPIO33 — REQUIRED, always |
| Return temperature | return_temp | DS18B20 GPIO14 — REQUIRED, always |

## BLE sensor details
- IBS-TH2 / IBS-TH2 Plus advertisement format:
  - byte[0..1] LE int16 / 100.0 = temperature °C
  - byte[2..3] LE int16 / 100.0 = humidity %
  - byte[4]    uint8              = battery %
- Identify sensor by MAC address (hardcoded in config)
- Library: NimBLE-Arduino (h2zero/NimBLE-Arduino)

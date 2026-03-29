#include "ApiClient.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <Arduino.h>

ApiClient::ApiClient(AppState& state, TimeService& time)
    : _state(state)
    , _time(time)
{}

void ApiClient::update() {
    if (!_state.status.wifiConnected) return;

    uint32_t now = millis();
    Config& cfg  = _state.config;

    // Poll room sensor
    if (cfg.roomSensor.mode == TempSourceMode::API &&
        cfg.roomSensor.apiUrl[0] != '\0') {
        uint32_t pollMs = (uint32_t)cfg.roomSensor.apiPollSec * 1000;
        if (now - _lastRoomPoll >= pollMs) {
            _lastRoomPoll = now;
            pollSensor(cfg.roomSensor,
                       _state.sensors.roomTemp,
                       _state.sensors.roomLastSeen);

            if (_state.sensors.hasRoom()) {
                _state.alarms.clear(AlarmFlag::ROOM_SENSOR_LOST);
            } else {
                _state.alarms.set(AlarmFlag::ROOM_SENSOR_LOST);
            }
        }
    }

    // Poll outside sensor
    if (cfg.outsideSensor.mode == TempSourceMode::API &&
        cfg.outsideSensor.apiUrl[0] != '\0') {
        uint32_t pollMs = (uint32_t)cfg.outsideSensor.apiPollSec * 1000;
        if (now - _lastOutsidePoll >= pollMs) {
            _lastOutsidePoll = now;
            pollSensor(cfg.outsideSensor,
                       _state.sensors.outsideTemp,
                       _state.sensors.outsideLastSeen);

            if (_state.sensors.hasOutside()) {
                _state.alarms.clear(AlarmFlag::OUTSIDE_SENSOR_LOST);
            } else {
                _state.alarms.set(AlarmFlag::OUTSIDE_SENSOR_LOST);
            }
        }
    }

    // Check timeout for sensors already polling
    if (cfg.roomSensor.mode == TempSourceMode::API) {
        uint32_t ls = _state.sensors.roomLastSeen;
        if (ls > 0 && now - ls > Limits::SENSOR_TIMEOUT_MS) {
            _state.sensors.roomTemp = NAN;
            _state.alarms.set(AlarmFlag::ROOM_SENSOR_LOST);
        }
    }
    if (cfg.outsideSensor.mode == TempSourceMode::API) {
        uint32_t ls = _state.sensors.outsideLastSeen;
        if (ls > 0 && now - ls > Limits::SENSOR_TIMEOUT_MS) {
            _state.sensors.outsideTemp = NAN;
            _state.alarms.set(AlarmFlag::OUTSIDE_SENSOR_LOST);
        }
    }
}

void ApiClient::pollSensor(TempSourceConfig& cfg, float& outTemp, uint32_t& lastSeen) {
    JsonDocument doc;
    if (!fetchJson(cfg.apiUrl, doc)) {
        Serial.printf("[ApiClient] Failed to fetch: %s\n", cfg.apiUrl);
        outTemp = NAN;
        return;
    }

    float val = extractValue(doc, cfg.apiJsonPath);
    if (isnan(val)) {
        Serial.printf("[ApiClient] Failed to extract path '%s' from response\n", cfg.apiJsonPath);
        outTemp = NAN;
        return;
    }

    outTemp  = val;
    lastSeen = millis();
    Serial.printf("[ApiClient] Polled %s -> %.1f°C\n", cfg.apiUrl, val);
}

bool ApiClient::fetchJson(const char* url, JsonDocument& doc) {
    HTTPClient http;
    http.setTimeout(5000);
    http.begin(url);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[ApiClient] HTTP %d for %s\n", code, url);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[ApiClient] JSON parse error: %s\n", err.c_str());
        return false;
    }
    return true;
}

float ApiClient::extractValue(const JsonDocument& doc, const char* path) {
    if (!path || path[0] == '\0') return NAN;

    // Support dot-separated JSON path, e.g. "temperature" or "data.sensors.0.temp"
    // Parse by splitting on '.'
    char pathBuf[64];
    strncpy(pathBuf, path, sizeof(pathBuf) - 1);
    pathBuf[sizeof(pathBuf) - 1] = '\0';

    JsonVariantConst node = doc.as<JsonVariantConst>();

    char* token = strtok(pathBuf, ".");
    while (token != nullptr) {
        if (node.is<JsonObjectConst>()) {
            node = node.as<JsonObjectConst>()[token];
        } else if (node.is<JsonArrayConst>()) {
            // Try numeric index
            char* end;
            long idx = strtol(token, &end, 10);
            if (*end == '\0') {
                node = node.as<JsonArrayConst>()[(size_t)idx];
            } else {
                return NAN;
            }
        } else {
            return NAN;
        }
        token = strtok(nullptr, ".");
    }

    if (node.is<float>() || node.is<double>() || node.is<int>()) {
        return node.as<float>();
    }
    if (node.is<const char*>()) {
        return atof(node.as<const char*>());
    }
    return NAN;
}

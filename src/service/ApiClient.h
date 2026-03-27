#pragma once
#include <ArduinoJson.h>
#include "../model/AppState.h"
#include "TimeService.h"

// Polls HTTP API endpoints to get room / outside temperature.
// Used when sensor source mode = API.

class ApiClient {
public:
    ApiClient(AppState& state, TimeService& time);
    void update();  // call from loop — polls on schedule

private:
    void pollSensor(TempSourceConfig& cfg, float& outTemp, uint32_t& lastSeen);
    bool fetchJson(const char* url, JsonDocument& doc);
    float extractValue(const JsonDocument& doc, const char* path);

    AppState&    _state;
    TimeService& _time;

    uint32_t _lastRoomPoll    = 0;
    uint32_t _lastOutsidePoll = 0;
};

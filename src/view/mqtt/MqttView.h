#pragma once
#include <ArduinoJson.h>
#include "../../model/AppState.h"
#include "../../service/MqttService.h"
#include "../../controller/BoilerLogic.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MqttView — Home Assistant MQTT integration
//
//  On connect:  publishes HA autodiscovery configs
//  Each loop:   publishes changed state (sensors, relays, alarms)
//  Subscribes:  command topics → BoilerLogic / Config
// ─────────────────────────────────────────────────────────────────────────────

class MqttView {
public:
    MqttView(AppState& state, MqttService& mqtt);
    void begin();
    void update();  // call from loop

private:
    // ── Autodiscovery ─────────────────────────────────────────────────────────
    void publishDiscovery();
    void publishSensorDiscovery(const char* id, const char* name,
                                const char* topic, const char* unit,
                                const char* devClass = nullptr);
    void publishBinarySensorDiscovery(const char* id, const char* name,
                                      const char* topic,
                                      const char* devClass = nullptr);
    void publishSelectDiscovery();   // mode selector
    void publishSwitchDiscovery(const char* id, const char* name,
                                const char* stateTopic,
                                const char* cmdTopic);
    void publishNumberDiscovery(const char* id, const char* name,
                                const char* stateTopic, const char* cmdTopic,
                                float min, float max, float step,
                                const char* unit);

    // ── State publishing ──────────────────────────────────────────────────────
    void publishState();
    void publishAlarms();

    // Publishes only if value changed since last publish
    template<typename T>
    bool publishIfChanged(const char* topic, T& lastVal, T newVal,
                          const char* payload, bool retain = true);

    // ── Alarm publishing ──────────────────────────────────────────────────────
    void publishAlarmEvent(AlarmFlag flag, const char* message);

    // ── Command handler ───────────────────────────────────────────────────────
    static void onMessage(const char* topic, const char* payload);

    AppState&    _state;
    MqttService& _mqtt;

    bool     _discoveryPublished = false;
    uint32_t _lastPublish        = 0;
    uint16_t _lastAlarms         = 0xFFFF;  // force publish on start

    // Cached last-published values (detect changes)
    float    _lastFlowTemp    = -999;
    float    _lastReturnTemp  = -999;
    float    _lastRoomTemp    = -999;
    float    _lastOutsideTemp = -999;
    bool     _lastHeater      = false;
    bool     _lastPump        = false;
    uint8_t  _lastMode        = 255;

    static constexpr uint32_t PUBLISH_INTERVAL_MS = 5000;  // publish state every 5s
    static constexpr const char* BASE = "boiler";           // MQTT topic prefix

    static MqttView* _instance;  // for static callback
};

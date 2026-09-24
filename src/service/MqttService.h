#pragma once
#include <AsyncMqttClient.h>
#include "../model/AppState.h"

// Low-level MQTT connection wrapper.
// MqttView uses this to publish and subscribe.

class MqttService {
public:
    explicit MqttService(AppState& state);
    void begin();
    void update();
    void reconnect();

    bool connected() const;
    // Incremented on every successful (re)connect — lets MqttView detect a new session
    uint32_t connectCount() const { return _connectCount; }

    bool publish(const char* topic, const char* payload,
                 bool retain = false, uint8_t qos = 0);

    void subscribe(const char* topic, uint8_t qos = 0);

    // Callback registration — MqttView sets this on begin()
    using MessageCb = void(*)(const char* topic, const char* payload);
    void onMessage(MessageCb cb);

private:
    void onConnect(bool sessionPresent);
    void onDisconnect(AsyncMqttClientDisconnectReason reason);
    void onMessage_(char* topic, char* payload,
                    AsyncMqttClientMessageProperties props,
                    size_t len, size_t index, size_t total);

    AppState&       _state;
    AsyncMqttClient _client;
    MessageCb       _msgCb  = nullptr;
    uint32_t        _lastReconnect = 0;
    volatile uint32_t _connectCount = 0;
};

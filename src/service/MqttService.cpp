#include "MqttService.h"
#include <WiFi.h>
#include <Arduino.h>

static constexpr uint32_t RECONNECT_INTERVAL_MS = 10000;

MqttService::MqttService(AppState& state)
    : _state(state)
{}

void MqttService::begin() {
    const Config& cfg = _state.config;

    if (cfg.mqttBroker[0] == '\0') {
        Serial.println("[MqttService] No broker configured, skipping");
        return;
    }

    _client.setServer(cfg.mqttBroker, cfg.mqttPort);
    _client.setClientId(cfg.mqttClientId);

    if (cfg.mqttUser[0] != '\0') {
        _client.setCredentials(cfg.mqttUser, cfg.mqttPass);
    }

    _client.setKeepAlive(60);
    _client.setCleanSession(true);
    // Broker publishes this if the board drops off (power loss, crash) → HA marks entities unavailable
    _client.setWill("boiler/status/online", 1, true, "offline");

    _client.onConnect([this](bool sessionPresent) {
        onConnect(sessionPresent);
    });

    _client.onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
        onDisconnect(reason);
    });

    _client.onMessage([this](char* topic, char* payload,
                              AsyncMqttClientMessageProperties props,
                              size_t len, size_t index, size_t total) {
        onMessage_(topic, payload, props, len, index, total);
    });

    reconnect();
    Serial.printf("[MqttService] Connecting to %s:%d\n", cfg.mqttBroker, cfg.mqttPort);
}

void MqttService::update() {
    if (!_client.connected()) {
        uint32_t now = millis();
        if (now - _lastReconnect >= RECONNECT_INTERVAL_MS) {
            _lastReconnect = now;
            reconnect();
        }
    }
}

void MqttService::reconnect() {
    const Config& cfg = _state.config;
    if (cfg.mqttBroker[0] == '\0') return;
    if (_client.connected()) return;

    _client.connect();
}

bool MqttService::connected() const {
    return _client.connected();
}

bool MqttService::publish(const char* topic, const char* payload, bool retain, uint8_t qos) {
    if (!_client.connected()) return false;
    uint16_t id = _client.publish(topic, qos, retain, payload);
    return id != 0;
}

void MqttService::subscribe(const char* topic, uint8_t qos) {
    if (!_client.connected()) return;
    _client.subscribe(topic, qos);
}

void MqttService::onMessage(MessageCb cb) {
    _msgCb = cb;
}

void MqttService::onConnect(bool sessionPresent) {
    _state.status.mqttConnected = true;
    _connectCount++;
    Serial.printf("[MqttService] Connected (session=%d)\n", (int)sessionPresent);
}

void MqttService::onDisconnect(AsyncMqttClientDisconnectReason reason) {
    _state.status.mqttConnected = false;
    Serial.printf("[MqttService] Disconnected (reason=%d)\n", (int)reason);
}

void MqttService::onMessage_(char* topic, char* payload,
                               AsyncMqttClientMessageProperties props,
                               size_t len, size_t index, size_t total) {
    if (index == 0 && len == total) {
        // Single-packet message — safe to null-terminate
        char buf[256];
        size_t copyLen = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
        memcpy(buf, payload, copyLen);
        buf[copyLen] = '\0';

        if (_msgCb) {
            _msgCb(topic, buf);
        }
    }
    // Multi-packet messages not supported in this implementation
}

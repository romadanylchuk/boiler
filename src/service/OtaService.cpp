#include "OtaService.h"
#include "../view/web/WebView.h"
#include <Arduino.h>

// WebView is declared as a global in main.cpp
extern WebView webView;

OtaService::OtaService(AppState& state)
    : _state(state)
{}

void OtaService::begin() {
    ElegantOTA.begin(&webView.server());

    ElegantOTA.onStart([this]() {
        onStart();
    });

    ElegantOTA.onEnd([this](bool success) {
        onEnd(success);
    });

    Serial.println("[OtaService] ElegantOTA initialized");
}

void OtaService::update() {
    ElegantOTA.loop();
}

void OtaService::onStart() {
    Serial.println("[OtaService] OTA update starting — stopping heater");
    _state.status.otaInProgress = true;
    // BoilerLogic checks otaInProgress and will stop the heater on next update()
}

void OtaService::onEnd(bool success) {
    _state.status.otaInProgress = false;
    if (success) {
        Serial.println("[OtaService] OTA succeeded — rebooting");
    } else {
        Serial.println("[OtaService] OTA failed");
    }
}

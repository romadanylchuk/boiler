#include <Arduino.h>
#include <Wire.h>

// Model
#include "model/AppState.h"

// Controller
#include "controller/BoilerLogic.h"

// Hardware drivers
#include "hardware/DS18B20Driver.h"
#include "hardware/RelayDriver.h"
#include "hardware/BuzzerDriver.h"
#include "hardware/ButtonReader.h"

// Services
#include "service/BleScanner.h"
#include "service/ApiClient.h"
#include "service/MqttService.h"
#include "service/OtaService.h"
#include "service/TimeService.h"
#include "service/NvsConfig.h"

// Views
#include "view/display/DisplayView.h"
#include "view/web/WebView.h"
#include "view/mqtt/MqttView.h"

// ─── Single state instance ────────────────────────────────────────────────────
AppState state;

// ─── Components ───────────────────────────────────────────────────────────────
NvsConfig    nvsConfig;
TimeService  timeService;

DS18B20Driver ds18b20(state);
RelayDriver   relays(state);
BuzzerDriver  buzzer(state);
ButtonReader  buttons(state);

BleScanner  bleScanner(state);
ApiClient   apiClient(state, timeService);
MqttService mqttService(state);
OtaService  otaService(state);

BoilerLogic boilerLogic(state, relays, buzzer, timeService);

DisplayView displayView(state);
WebView     webView(state, boilerLogic);
MqttView    mqttView(state, mqttService);

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("[boot] Boiler firmware starting...");

    // I2C bus (shared: OLED + PCF8574)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // Load config from NVS
    nvsConfig.load(state.config);

    // Hardware init
    relays.begin();
    buzzer.begin();
    buttons.begin();
    ds18b20.begin();

    // Display early — show boot screen
    displayView.begin();
    displayView.showBoot();

    // WiFi
    if (state.config.setupComplete) {
        Serial.println("[wifi] Connecting...");
        WiFi.begin(state.config.wifiSsid, state.config.wifiPass);
        uint32_t t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
    }

    state.status.wifiConnected = (WiFi.status() == WL_CONNECTED);

    if (state.status.wifiConnected) {
        timeService.begin(state.config.ntpServer1,
                          state.config.ntpServer2,
                          state.config.timezone);
        mqttService.begin();
        mqttView.begin();
        webView.begin();
        otaService.begin();
        Serial.printf("[wifi] IP: %s\n", WiFi.localIP().toString().c_str());
    }

    // BLE scanner (runs regardless of WiFi)
    bleScanner.begin();

    // Boiler controller — always starts in OFF mode
    state.status.mode  = SystemMode::OFF;
    state.status.phase = HeaterPhase::IDLE;
    boilerLogic.begin();

    // First-boot setup redirect handled by WebView
    if (!state.config.setupComplete) {
        Serial.println("[boot] First boot — setup required via web UI");
        displayView.showSetupRequired();
    }

    Serial.println("[boot] Ready.");
    buzzer.beep(1, 100);  // 1 short beep = ready
}

void loop() {
    // ── Time ──────────────────────────────────────────────────────────────────
    timeService.update();

    // ── Sensor readings ───────────────────────────────────────────────────────
    ds18b20.update();

    // ── Optional sensor sources ───────────────────────────────────────────────
    bleScanner.update();
    apiClient.update();

    // ── Button input ──────────────────────────────────────────────────────────
    buttons.update();

    // ── Heating controller ────────────────────────────────────────────────────
    boilerLogic.update();

    // ── Display ───────────────────────────────────────────────────────────────
    displayView.update();

    // ── Network services ──────────────────────────────────────────────────────
    if (state.status.wifiConnected) {
        mqttService.update();
        mqttView.update();
        otaService.update();
    }

    // ── WiFi reconnect watchdog ───────────────────────────────────────────────
    static uint32_t lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 30000) {
        lastWifiCheck = millis();
        bool connected = (WiFi.status() == WL_CONNECTED);
        if (!connected && state.status.wifiConnected) {
            Serial.println("[wifi] Connection lost, reconnecting...");
            WiFi.reconnect();
        }
        if (connected && !state.status.wifiConnected) {
            Serial.println("[wifi] Reconnected.");
            timeService.triggerResync();
            mqttService.reconnect();
        }
        state.status.wifiConnected = connected;
        state.status.mqttConnected = mqttService.connected();
    }
}

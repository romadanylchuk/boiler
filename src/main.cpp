#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>

// Model
#include "model/AppState.h"
#include "model/pins.h"

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
#include "service/EnergyMeter.h"

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
EnergyMeter energyMeter(state, timeService);

DisplayView displayView(state, timeService);
WebView     webView(state, boilerLogic);
MqttView    mqttView(state, mqttService, boilerLogic);

// ─────────────────────────────────────────────────────────────────────────────

static bool webStarted = false;
static bool netStarted = false;
static bool bleStarted = false;

static void startWeb() {
    if (webStarted) return;
    webView.begin();
    webStarted = true;
}

static void startBle() {
    if (bleStarted) return;
    bleScanner.begin();
    bleStarted = true;
}

// Services that need the STA link. Called once — at boot if WiFi is up,
// otherwise from the loop watchdog when STA connects later.
static void startNetworkServices() {
    if (netStarted) return;
    netStarted = true;
    timeService.begin(state.config.ntpServer1,
                      state.config.ntpServer2,
                      state.config.timezone);
    mqttService.begin();
    mqttView.begin();
    startWeb();
    otaService.begin();
    if (MDNS.begin("boiler")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[mdns] boiler.local ready");
    }
    Serial.printf("[wifi] STA IP: %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
    Serial.begin(115200);
    Serial.println("[boot] Boiler firmware starting...");

    // I2C bus (shared: OLED + PCF8574)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // Load config from NVS
    nvsConfig.load(state.config);
    energyMeter.begin();

    // Hardware init
    relays.begin();
    buzzer.begin();
    buttons.begin();
    ds18b20.begin();

    // Display early — show boot screen
    displayView.begin();
    displayView.showBoot();

    // WiFi — try STA if credentials are configured
    if (state.config.wifiSsid[0] != '\0') {
        Serial.println("[wifi] Connecting to STA...");
        WiFi.mode(WIFI_STA);
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
        startNetworkServices();
    } else {
        // No STA link — start Access Point so the web UI is always reachable.
        // If STA is configured, keep it retrying in the background (AP+STA):
        // after a power cut the router often boots slower than the ESP32.
        bool staConfigured = state.config.wifiSsid[0] != '\0';
        uint8_t mac[6];
        WiFi.mode(staConfigured ? WIFI_AP_STA : WIFI_AP);
        WiFi.macAddress(mac);
        snprintf(state.status.apSsid, sizeof(state.status.apSsid),
                 "Boiler-%02X%02X%02X", mac[3], mac[4], mac[5]);
        WiFi.softAP(state.status.apSsid, "boilersetup");
        state.status.apMode = true;
        startWeb();
        Serial.printf("[wifi] AP started: SSID=%s  pass=boilersetup  IP=%s%s\n",
                      state.status.apSsid,
                      WiFi.softAPIP().toString().c_str(),
                      staConfigured ? "  (STA still retrying)" : "");
    }

    // BLE scanner — skip in AP mode (NimBLE init disrupts the AP radio)
    if (!state.status.apMode) {
        startBle();
    }

    // Boiler controller — always starts in OFF mode
    state.status.mode  = SystemMode::OFF;
    state.status.phase = HeaterPhase::IDLE;
    boilerLogic.begin();

    // First-boot setup redirect handled by WebView
    if (!state.config.setupComplete) {
        Serial.println("[boot] First boot — setup required via web UI");
        displayView.showSetupRequired();
    } else if (state.status.wifiConnected) {
        displayView.showIp(WiFi.localIP().toString().c_str());
    } else {
        displayView.showMain();
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
    if (!state.status.apMode) bleScanner.update();
    apiClient.update();

    // ── Button input ──────────────────────────────────────────────────────────
    buttons.update();
    if (buttons.hasEvent()) displayView.handleButton(buttons.consume());

    // ── Heating controller ────────────────────────────────────────────────────
    boilerLogic.update();
    energyMeter.update();

    // ── Display ───────────────────────────────────────────────────────────────
    displayView.update();

    // ── Network services ──────────────────────────────────────────────────────
    if (state.status.wifiConnected) {
        mqttService.update();
        mqttView.update();
        otaService.update();
    }

    // ── WiFi reconnect watchdog (whenever STA is configured) ─────────────────
    static uint32_t lastWifiCheck = 0;
    if (state.config.wifiSsid[0] != '\0' && millis() - lastWifiCheck > 30000) {
        lastWifiCheck = millis();
        bool connected = (WiFi.status() == WL_CONNECTED);
        if (!connected) {
            if (state.status.wifiConnected) Serial.println("[wifi] Connection lost, reconnecting...");
            WiFi.reconnect();
        }
        if (connected && !state.status.wifiConnected) {
            Serial.println("[wifi] Connected.");
            if (!netStarted) {
                // First STA connect after a failed boot attempt
                startNetworkServices();
            } else {
                timeService.triggerResync();
                mqttService.reconnect();
            }
            if (state.status.apMode) {
                // Fallback AP no longer needed — drop it and start BLE
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                state.status.apMode = false;
                Serial.println("[wifi] Fallback AP stopped");
                startBle();
                if (state.config.setupComplete) {
                    displayView.showIp(WiFi.localIP().toString().c_str());
                }
            }
        }
        state.status.wifiConnected = connected;
        state.status.mqttConnected = mqttService.connected();
    }
}

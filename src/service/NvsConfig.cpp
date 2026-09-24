#include "NvsConfig.h"
#include <Arduino.h>

void NvsConfig::load(Config& cfg) {
    _prefs.begin(NS, true); // read-only

    // ── Setpoints ──────────────────────────────────────────────────────────────
    cfg.flowSetpoint     = _prefs.getUChar("flowSp",     cfg.flowSetpoint);
    cfg.returnSetpoint   = _prefs.getUChar("returnSp",   cfg.returnSetpoint);
    cfg.flowHysteresis   = _prefs.getUChar("flowHyst",   cfg.flowHysteresis);
    cfg.returnHysteresis = _prefs.getUChar("returnHyst", cfg.returnHysteresis);
    cfg.roomSetpoint     = _prefs.getUChar("roomSp",     cfg.roomSetpoint);
    cfg.roomHysteresis   = _prefs.getUChar("roomHyst",   cfg.roomHysteresis);

    // ── Timing ─────────────────────────────────────────────────────────────────
    cfg.pumpPrePostDelaySec    = _prefs.getUShort("pumpDelay",    cfg.pumpPrePostDelaySec);
    cfg.standbyPumpPeriodMin   = _prefs.getUShort("stbyPeriod",   cfg.standbyPumpPeriodMin);
    cfg.standbyPumpDurationMin = _prefs.getUChar("stbyDur",      cfg.standbyPumpDurationMin);
    cfg.minHeaterOffSec        = _prefs.getUShort("minOffSec",    cfg.minHeaterOffSec);

    // ── Energy meter ───────────────────────────────────────────────────────────
    cfg.heaterPowerDeciKw      = _prefs.getUShort("heaterPwr",    cfg.heaterPowerDeciKw);

    // ── Hardware ───────────────────────────────────────────────────────────────
    cfg.thermostatMode = (ThermostatContact)_prefs.getUChar("tstatMode", (uint8_t)cfg.thermostatMode);

    // ── Room sensor ────────────────────────────────────────────────────────────
    cfg.roomSensor.mode       = (TempSourceMode)_prefs.getUChar("roomSrcMode", (uint8_t)cfg.roomSensor.mode);
    _prefs.getString("roomBleMac",  cfg.roomSensor.bleMac,    sizeof(cfg.roomSensor.bleMac));
    _prefs.getString("roomApiUrl",  cfg.roomSensor.apiUrl,    sizeof(cfg.roomSensor.apiUrl));
    _prefs.getString("roomApiPath", cfg.roomSensor.apiJsonPath, sizeof(cfg.roomSensor.apiJsonPath));
    cfg.roomSensor.apiPollSec = _prefs.getUInt("roomApiPoll", cfg.roomSensor.apiPollSec);

    // ── Outside sensor ─────────────────────────────────────────────────────────
    cfg.outsideSensor.mode       = (TempSourceMode)_prefs.getUChar("outSrcMode", (uint8_t)cfg.outsideSensor.mode);
    _prefs.getString("outBleMac",  cfg.outsideSensor.bleMac,    sizeof(cfg.outsideSensor.bleMac));
    _prefs.getString("outApiUrl",  cfg.outsideSensor.apiUrl,    sizeof(cfg.outsideSensor.apiUrl));
    _prefs.getString("outApiPath", cfg.outsideSensor.apiJsonPath, sizeof(cfg.outsideSensor.apiJsonPath));
    cfg.outsideSensor.apiPollSec = _prefs.getUInt("outApiPoll", cfg.outsideSensor.apiPollSec);

    // ── Weather curve ──────────────────────────────────────────────────────────
    cfg.curveCount = _prefs.getUChar("curveCount", cfg.curveCount);
    if (cfg.curveCount > 5) cfg.curveCount = 0;
    for (uint8_t i = 0; i < cfg.curveCount && i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "curveOut%d", i);
        cfg.curve[i].outsideTemp  = (int8_t)_prefs.getChar(key, cfg.curve[i].outsideTemp);
        snprintf(key, sizeof(key), "curveFlow%d", i);
        cfg.curve[i].flowSetpoint = _prefs.getUChar(key, cfg.curve[i].flowSetpoint);
        snprintf(key, sizeof(key), "curveRet%d", i);
        cfg.curve[i].returnSetpoint = _prefs.getUChar(key, cfg.curve[i].returnSetpoint);
    }

    // ── WiFi ───────────────────────────────────────────────────────────────────
    _prefs.getString("wifiSsid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
    _prefs.getString("wifiPass", cfg.wifiPass, sizeof(cfg.wifiPass));

    // ── MQTT ───────────────────────────────────────────────────────────────────
    _prefs.getString("mqttBroker",   cfg.mqttBroker,   sizeof(cfg.mqttBroker));
    cfg.mqttPort = _prefs.getUShort("mqttPort", cfg.mqttPort);
    _prefs.getString("mqttUser",     cfg.mqttUser,     sizeof(cfg.mqttUser));
    _prefs.getString("mqttPass",     cfg.mqttPass,     sizeof(cfg.mqttPass));
    _prefs.getString("mqttClientId", cfg.mqttClientId, sizeof(cfg.mqttClientId));

    // ── NTP / Timezone ─────────────────────────────────────────────────────────
    _prefs.getString("ntpServer1", cfg.ntpServer1, sizeof(cfg.ntpServer1));
    _prefs.getString("ntpServer2", cfg.ntpServer2, sizeof(cfg.ntpServer2));
    _prefs.getString("timezone",   cfg.timezone,   sizeof(cfg.timezone));

    // ── Credentials ────────────────────────────────────────────────────────────
    _prefs.getString("adminUser",    cfg.adminUser,       sizeof(cfg.adminUser));
    _prefs.getString("adminPassH",   cfg.adminPassHash,   sizeof(cfg.adminPassHash));
    _prefs.getString("operUser",     cfg.operatorUser,    sizeof(cfg.operatorUser));
    _prefs.getString("operPassH",    cfg.operatorPassHash, sizeof(cfg.operatorPassHash));
    _prefs.getString("apiToken",     cfg.apiToken,        sizeof(cfg.apiToken));
    _prefs.getString("resetPhrase",  cfg.resetPhrase,     sizeof(cfg.resetPhrase));

    // ── First-boot ─────────────────────────────────────────────────────────────
    cfg.setupComplete = _prefs.getBool("setupDone", false);

    _prefs.end();

    Serial.println("[NvsConfig] Config loaded from NVS");
}

void NvsConfig::save(const Config& cfg) {
    _prefs.begin(NS, false);

    // Setpoints
    _prefs.putUChar("flowSp",     cfg.flowSetpoint);
    _prefs.putUChar("returnSp",   cfg.returnSetpoint);
    _prefs.putUChar("flowHyst",   cfg.flowHysteresis);
    _prefs.putUChar("returnHyst", cfg.returnHysteresis);
    _prefs.putUChar("roomSp",     cfg.roomSetpoint);
    _prefs.putUChar("roomHyst",   cfg.roomHysteresis);

    // Timing
    _prefs.putUShort("pumpDelay",  cfg.pumpPrePostDelaySec);
    _prefs.putUShort("stbyPeriod", cfg.standbyPumpPeriodMin);
    _prefs.putUChar("stbyDur",     cfg.standbyPumpDurationMin);
    _prefs.putUShort("minOffSec",  cfg.minHeaterOffSec);

    // Energy meter
    _prefs.putUShort("heaterPwr",  cfg.heaterPowerDeciKw);

    // Hardware
    _prefs.putUChar("tstatMode", (uint8_t)cfg.thermostatMode);

    // Room sensor
    _prefs.putUChar("roomSrcMode", (uint8_t)cfg.roomSensor.mode);
    _prefs.putString("roomBleMac",  cfg.roomSensor.bleMac);
    _prefs.putString("roomApiUrl",  cfg.roomSensor.apiUrl);
    _prefs.putString("roomApiPath", cfg.roomSensor.apiJsonPath);
    _prefs.putUInt("roomApiPoll",   cfg.roomSensor.apiPollSec);

    // Outside sensor
    _prefs.putUChar("outSrcMode", (uint8_t)cfg.outsideSensor.mode);
    _prefs.putString("outBleMac",  cfg.outsideSensor.bleMac);
    _prefs.putString("outApiUrl",  cfg.outsideSensor.apiUrl);
    _prefs.putString("outApiPath", cfg.outsideSensor.apiJsonPath);
    _prefs.putUInt("outApiPoll",   cfg.outsideSensor.apiPollSec);

    // Curve
    _prefs.putUChar("curveCount", cfg.curveCount);
    for (uint8_t i = 0; i < cfg.curveCount && i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "curveOut%d", i);
        _prefs.putChar(key, cfg.curve[i].outsideTemp);
        snprintf(key, sizeof(key), "curveFlow%d", i);
        _prefs.putUChar(key, cfg.curve[i].flowSetpoint);
        snprintf(key, sizeof(key), "curveRet%d", i);
        _prefs.putUChar(key, cfg.curve[i].returnSetpoint);
    }

    // WiFi
    _prefs.putString("wifiSsid", cfg.wifiSsid);
    _prefs.putString("wifiPass", cfg.wifiPass);

    // MQTT
    _prefs.putString("mqttBroker",   cfg.mqttBroker);
    _prefs.putUShort("mqttPort",     cfg.mqttPort);
    _prefs.putString("mqttUser",     cfg.mqttUser);
    _prefs.putString("mqttPass",     cfg.mqttPass);
    _prefs.putString("mqttClientId", cfg.mqttClientId);

    // NTP
    _prefs.putString("ntpServer1", cfg.ntpServer1);
    _prefs.putString("ntpServer2", cfg.ntpServer2);
    _prefs.putString("timezone",   cfg.timezone);

    // Credentials
    _prefs.putString("adminUser",   cfg.adminUser);
    _prefs.putString("adminPassH",  cfg.adminPassHash);
    _prefs.putString("operUser",    cfg.operatorUser);
    _prefs.putString("operPassH",   cfg.operatorPassHash);
    _prefs.putString("apiToken",    cfg.apiToken);
    _prefs.putString("resetPhrase", cfg.resetPhrase);

    // Setup flag
    _prefs.putBool("setupDone", cfg.setupComplete);

    _prefs.end();
    Serial.println("[NvsConfig] Config saved to NVS");
}

void NvsConfig::reset() {
    _prefs.begin(NS, false);
    _prefs.clear();
    _prefs.end();
    Serial.println("[NvsConfig] NVS namespace cleared");
}

void NvsConfig::saveSetpoints(const Config& cfg) {
    _prefs.begin(NS, false);
    _prefs.putUChar("flowSp",     cfg.flowSetpoint);
    _prefs.putUChar("returnSp",   cfg.returnSetpoint);
    _prefs.putUChar("flowHyst",   cfg.flowHysteresis);
    _prefs.putUChar("returnHyst", cfg.returnHysteresis);
    _prefs.putUChar("roomSp",     cfg.roomSetpoint);
    _prefs.putUChar("roomHyst",   cfg.roomHysteresis);
    _prefs.end();
}

void NvsConfig::saveTiming(const Config& cfg) {
    _prefs.begin(NS, false);
    _prefs.putUShort("pumpDelay",  cfg.pumpPrePostDelaySec);
    _prefs.putUShort("stbyPeriod", cfg.standbyPumpPeriodMin);
    _prefs.putUChar("stbyDur",     cfg.standbyPumpDurationMin);
    _prefs.putUShort("minOffSec",  cfg.minHeaterOffSec);
    _prefs.end();
}

void NvsConfig::saveCredentials(const Config& cfg) {
    _prefs.begin(NS, false);
    _prefs.putString("adminUser",   cfg.adminUser);
    _prefs.putString("adminPassH",  cfg.adminPassHash);
    _prefs.putString("operUser",    cfg.operatorUser);
    _prefs.putString("operPassH",   cfg.operatorPassHash);
    _prefs.putString("apiToken",    cfg.apiToken);
    _prefs.putString("resetPhrase", cfg.resetPhrase);
    _prefs.putBool("setupDone",     cfg.setupComplete);
    _prefs.end();
}

void NvsConfig::saveWifi(const Config& cfg) {
    _prefs.begin(NS, false);
    _prefs.putString("wifiSsid", cfg.wifiSsid);
    _prefs.putString("wifiPass", cfg.wifiPass);
    _prefs.end();
}

void NvsConfig::saveMqtt(const Config& cfg) {
    _prefs.begin(NS, false);
    _prefs.putString("mqttBroker",   cfg.mqttBroker);
    _prefs.putUShort("mqttPort",     cfg.mqttPort);
    _prefs.putString("mqttUser",     cfg.mqttUser);
    _prefs.putString("mqttPass",     cfg.mqttPass);
    _prefs.putString("mqttClientId", cfg.mqttClientId);
    _prefs.end();
}

void NvsConfig::saveSensorSources(const Config& cfg) {
    _prefs.begin(NS, false);
    _prefs.putUChar("roomSrcMode",  (uint8_t)cfg.roomSensor.mode);
    _prefs.putString("roomBleMac",  cfg.roomSensor.bleMac);
    _prefs.putString("roomApiUrl",  cfg.roomSensor.apiUrl);
    _prefs.putString("roomApiPath", cfg.roomSensor.apiJsonPath);
    _prefs.putUInt("roomApiPoll",   cfg.roomSensor.apiPollSec);

    _prefs.putUChar("outSrcMode",  (uint8_t)cfg.outsideSensor.mode);
    _prefs.putString("outBleMac",  cfg.outsideSensor.bleMac);
    _prefs.putString("outApiUrl",  cfg.outsideSensor.apiUrl);
    _prefs.putString("outApiPath", cfg.outsideSensor.apiJsonPath);
    _prefs.putUInt("outApiPoll",   cfg.outsideSensor.apiPollSec);
    _prefs.end();
}

void NvsConfig::saveCurve(const Config& cfg) {
    _prefs.begin(NS, false);
    _prefs.putUChar("curveCount", cfg.curveCount);
    for (uint8_t i = 0; i < cfg.curveCount && i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "curveOut%d", i);
        _prefs.putChar(key, cfg.curve[i].outsideTemp);
        snprintf(key, sizeof(key), "curveFlow%d", i);
        _prefs.putUChar(key, cfg.curve[i].flowSetpoint);
        snprintf(key, sizeof(key), "curveRet%d", i);
        _prefs.putUChar(key, cfg.curve[i].returnSetpoint);
    }
    _prefs.end();
}

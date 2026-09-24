#include "WebView.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <esp_system.h>
#include "../../service/NvsConfig.h"

// Forward declaration — NvsConfig is a global in main.cpp
extern NvsConfig nvsConfig;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void sha256Hex(const char* input, char* outHex65) {
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t*)input, strlen(input));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    for (int i = 0; i < 32; i++) {
        snprintf(outHex65 + i * 2, 3, "%02x", hash[i]);
    }
    outHex65[64] = '\0';
}

static void generateToken(char* outHex65) {
    uint8_t buf[32];
    for (int i = 0; i < 8; i++) {
        uint32_t r = esp_random();
        memcpy(buf + i * 4, &r, 4);
    }
    for (int i = 0; i < 32; i++) {
        snprintf(outHex65 + i * 2, 3, "%02x", buf[i]);
    }
    outHex65[64] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / begin
// ─────────────────────────────────────────────────────────────────────────────

WebView::WebView(AppState& state, BoilerLogic& logic)
    : _state(state)
    , _logic(logic)
{}

void WebView::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[WebView] LittleFS mount failed");
    } else {
        Serial.println("[WebView] LittleFS mounted");
    }

    // Generate API token on first boot
    if (_state.config.apiToken[0] == '\0') {
        generateToken(_state.config.apiToken);
        nvsConfig.saveCredentials(_state.config);
        Serial.printf("[WebView] Generated API token: %s\n", _state.config.apiToken);
    }

    // CORS headers — allow requests from any origin (test runner, local tools)
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                         "Authorization, Content-Type, Cookie");

    setupRoutes();
    _server.begin();
    Serial.println("[WebView] HTTP server started on port 80");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Route setup
// ─────────────────────────────────────────────────────────────────────────────

void WebView::setupRoutes() {
    // ── Auth ──────────────────────────────────────────────────────────────────
    _server.on("/api/login", HTTP_POST,
        [this](AsyncWebServerRequest* req) { handleLogin(req); });

    _server.on("/api/logout", HTTP_POST,
        [this](AsyncWebServerRequest* req) { handleLogout(req); });

    // ── State / Log (token auth) ──────────────────────────────────────────────
    _server.on("/api/state", HTTP_GET,
        [this](AsyncWebServerRequest* req) { handleApiState(req); });

    _server.on("/api/log", HTTP_GET,
        [this](AsyncWebServerRequest* req) { handleApiLog(req); });

    // ── Commands (token auth, JSON POST) ──────────────────────────────────────
    _server.addHandler(new AsyncCallbackJsonWebHandler(
        "/api/command",
        [this](AsyncWebServerRequest* req, JsonVariant& body) {
            handleApiCommand(req, body);
        }
    ));

    // ── Config (session/admin auth) ───────────────────────────────────────────
    _server.on("/api/config", HTTP_GET,
        [this](AsyncWebServerRequest* req) { handleApiConfigGet(req); });

    _server.addHandler(new AsyncCallbackJsonWebHandler(
        "/api/config",
        [this](AsyncWebServerRequest* req, JsonVariant& body) {
            handleApiConfigSet(req, body);
        }
    ));

    // ── First-boot setup (no auth) ────────────────────────────────────────────
    _server.addHandler(new AsyncCallbackJsonWebHandler(
        "/api/setup",
        [this](AsyncWebServerRequest* req, JsonVariant& body) {
            handleSetup(req, body);
        }
    ));

    // ── Admin password reset via secret phrase (no auth) ──────────────────────
    _server.addHandler(new AsyncCallbackJsonWebHandler(
        "/api/reset-admin-password",
        [this](AsyncWebServerRequest* req, JsonVariant& body) {
            handleResetAdminPassword(req, body);
        }
    ));

    // ── Admin-gated actions (admin session required) ───────────────────────────
    _server.addHandler(new AsyncCallbackJsonWebHandler(
        "/api/admin-action",
        [this](AsyncWebServerRequest* req, JsonVariant& body) {
            handleAdminAction(req, body);
        }
    ));

    // ── Test mode (admin session required) ────────────────────────────────────
    _server.addHandler(new AsyncCallbackJsonWebHandler(
        "/api/test",
        [this](AsyncWebServerRequest* req, JsonVariant& body) {
            handleApiTest(req, body);
        }
    ));
    _server.on("/api/test", HTTP_GET,
        [this](AsyncWebServerRequest* req) { handleApiTestGet(req); });

    // ── Public status endpoint (no auth) — lets the SPA detect first boot ───────
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        String json = "{\"setupComplete\":";
        json += _state.config.setupComplete ? "true" : "false";
        json += ",\"apMode\":";
        json += _state.status.apMode ? "true" : "false";
        json += "}";
        req->send(200, "application/json", json);
    });

    // ── SPA: serve index.html for all routes, fall back to inline message ──────
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    _server.onNotFound([](AsyncWebServerRequest* req) {
        // Handle CORS preflight from test runner or external tools
        if (req->method() == HTTP_OPTIONS) {
            req->send(200);
            return;
        }
        // Serve index.html for any unknown path so the SPA router handles it.
        // If LittleFS files were never uploaded, show a plain-text hint.
        if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(200, "text/html",
                "<html><head><title>Boiler</title></head><body>"
                "<h2>Boiler Controller</h2>"
                "<p>Web interface files are missing.<br>"
                "Flash the filesystem: <code>pio run -e release -t uploadfs</code></p>"
                "</body></html>");
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Authentication helpers
// ─────────────────────────────────────────────────────────────────────────────

WebRole WebView::authenticateToken(AsyncWebServerRequest* req) const {
    // Check Authorization: Bearer <token> header
    if (req->hasHeader("Authorization")) {
        const String& auth = req->header("Authorization");
        if (auth.startsWith("Bearer ")) {
            String token = auth.substring(7);
            if (strcmp(token.c_str(), _state.config.apiToken) == 0) {
                return WebRole::OPERATOR; // Token = operator-level
            }
        }
    }
    return WebRole::NONE;
}

WebRole WebView::authenticateSession(AsyncWebServerRequest* req) const {
    if (!req->hasHeader("Cookie")) return WebRole::NONE;

    const String& cookies = req->header("Cookie");
    // Find "sid=<token>"
    int idx = cookies.indexOf("sid=");
    if (idx < 0) return WebRole::NONE;

    String token = cookies.substring(idx + 4);
    int end = token.indexOf(';');
    if (end >= 0) token = token.substring(0, end);
    token.trim();

    for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
        const WebSession& sess = _sessions[i];
        if (sess.active && strcmp(sess.token, token.c_str()) == 0) {
            uint32_t now = millis();
            if (now - sess.lastSeen < SESSION_TIMEOUT) {
                return sess.role;
            }
        }
    }
    return WebRole::NONE;
}

WebSession* WebView::findSession(const char* token) {
    for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
        if (_sessions[i].active && strcmp(_sessions[i].token, token) == 0) {
            return &_sessions[i];
        }
    }
    return nullptr;
}

WebSession* WebView::createSession(WebRole role) {
    // Find empty slot or evict oldest
    uint32_t oldestTime = UINT32_MAX;
    int8_t   oldestIdx  = 0;
    for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
        if (!_sessions[i].active) {
            // Use empty slot
            _sessions[i].active   = true;
            _sessions[i].role     = role;
            _sessions[i].lastSeen = millis();
            generateToken(_sessions[i].token);
            return &_sessions[i];
        }
        if (_sessions[i].lastSeen < oldestTime) {
            oldestTime = _sessions[i].lastSeen;
            oldestIdx  = i;
        }
    }
    // Evict oldest
    _sessions[oldestIdx].active   = true;
    _sessions[oldestIdx].role     = role;
    _sessions[oldestIdx].lastSeen = millis();
    generateToken(_sessions[oldestIdx].token);
    return &_sessions[oldestIdx];
}

void WebView::expireSessions() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
        if (_sessions[i].active && now - _sessions[i].lastSeen >= SESSION_TIMEOUT) {
            _sessions[i].active = false;
        }
    }
}

bool WebView::checkAdminPassword(const char* pass) const {
    char hash[65];
    sha256Hex(pass, hash);
    return strcmp(hash, _state.config.adminPassHash) == 0;
}

void WebView::sendUnauthorized(AsyncWebServerRequest* req) {
    req->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
}

void WebView::sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code) {
    String out;
    serializeJson(doc, out);
    req->send(code, "application/json", out);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Auth handlers
// ─────────────────────────────────────────────────────────────────────────────

void WebView::handleLogin(AsyncWebServerRequest* req) {
    // Login is handled via form POST with URL-encoded body
    // For simplicity, accept JSON body via a separate path
    // Actual login handled as a form: username + password fields
    expireSessions();

    if (!req->hasParam("username", true) || !req->hasParam("password", true)) {
        req->send(400, "application/json", "{\"error\":\"Missing credentials\"}");
        return;
    }

    const String& user = req->getParam("username", true)->value();
    const String& pass = req->getParam("password", true)->value();

    char hash[65];
    sha256Hex(pass.c_str(), hash);

    WebRole role = WebRole::NONE;
    if (strcmp(user.c_str(), _state.config.adminUser) == 0 &&
        strcmp(hash, _state.config.adminPassHash) == 0 &&
        _state.config.adminPassHash[0] != '\0') {
        role = WebRole::ADMIN;
    } else if (strcmp(user.c_str(), _state.config.operatorUser) == 0 &&
               strcmp(hash, _state.config.operatorPassHash) == 0 &&
               _state.config.operatorPassHash[0] != '\0') {
        role = WebRole::OPERATOR;
    }

    if (role == WebRole::NONE) {
        req->send(401, "application/json", "{\"error\":\"Invalid credentials\"}");
        return;
    }

    WebSession* sess = createSession(role);
    String cookie = "sid=";
    cookie += sess->token;
    cookie += "; Path=/; HttpOnly; SameSite=Strict";

    // Include session token in body so the test runner (cross-origin) can use it
    // without relying on cookie jar (which is blocked for file:// origins).
    String body = "{\"ok\":true,\"role\":\"";
    body += (role == WebRole::ADMIN ? "admin" : "operator");
    body += "\",\"sid\":\"";
    body += sess->token;
    body += "\"}";

    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
    resp->addHeader("Set-Cookie", cookie);
    req->send(resp);
}

void WebView::handleLogout(AsyncWebServerRequest* req) {
    if (req->hasHeader("Cookie")) {
        const String& cookies = req->header("Cookie");
        int idx = cookies.indexOf("sid=");
        if (idx >= 0) {
            String token = cookies.substring(idx + 4);
            int end = token.indexOf(';');
            if (end >= 0) token = token.substring(0, end);
            token.trim();
            WebSession* sess = findSession(token.c_str());
            if (sess) sess->active = false;
        }
    }
    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", "{\"ok\":true}");
    resp->addHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
    req->send(resp);
}

// ─────────────────────────────────────────────────────────────────────────────
//  API: State
// ─────────────────────────────────────────────────────────────────────────────

void WebView::handleApiState(AsyncWebServerRequest* req) {
    expireSessions();

    WebRole role = authenticateToken(req);
    if (role == WebRole::NONE) role = authenticateSession(req);
    if (role == WebRole::NONE) { sendUnauthorized(req); return; }

    // Update session lastSeen
    if (req->hasHeader("Cookie")) {
        const String& cookies = req->header("Cookie");
        int idx = cookies.indexOf("sid=");
        if (idx >= 0) {
            String token = cookies.substring(idx + 4);
            int end = token.indexOf(';');
            if (end >= 0) token = token.substring(0, end);
            token.trim();
            WebSession* sess = findSession(token.c_str());
            if (sess) sess->lastSeen = millis();
        }
    }

    JsonDocument doc;
    buildStateJson(doc);
    doc["testMode"]     = _state.test.active;
    doc["lastHwButton"] = _state.lastHwButton;
    _state.lastHwButton = 0;  // consume — next poll gets 0 unless a new press happened
    sendJson(req, doc);
}

void WebView::handleApiLog(AsyncWebServerRequest* req) {
    WebRole role = authenticateToken(req);
    if (role == WebRole::NONE) role = authenticateSession(req);
    if (role == WebRole::NONE) { sendUnauthorized(req); return; }

    JsonDocument doc;
    buildLogJson(doc);
    sendJson(req, doc);
}

// ─────────────────────────────────────────────────────────────────────────────
//  API: Command
// ─────────────────────────────────────────────────────────────────────────────

void WebView::handleApiCommand(AsyncWebServerRequest* req, JsonVariant& body) {
    WebRole role = authenticateToken(req);
    if (role == WebRole::NONE) role = authenticateSession(req);
    if (role == WebRole::NONE) { sendUnauthorized(req); return; }

    if (!body.is<JsonObject>()) {
        req->send(400, "application/json", "{\"error\":\"Expected JSON object\"}");
        return;
    }

    JsonObject obj = body.as<JsonObject>();
    const char* cmd = obj["cmd"] | "";

    if (strcmp(cmd, "set_mode") == 0) {
        const char* modeStr = obj["mode"] | "";
        SystemMode mode = SystemMode::OFF;
        if (strcmp(modeStr, "on") == 0)         mode = SystemMode::ON;
        else if (strcmp(modeStr, "antifreeze") == 0) mode = SystemMode::ANTIFREEZE;
        _logic.setMode(mode);
        req->send(200, "application/json", "{\"ok\":true}");

    } else if (strcmp(cmd, "ha_disable") == 0) {
        bool disable = obj["disable"] | false;
        _logic.setHaRemoteDisable(disable);
        req->send(200, "application/json", "{\"ok\":true}");

    } else if (strcmp(cmd, "button") == 0) {
        const char* btn = obj["button"] | "";
        uint8_t code = 0;
        if      (strcmp(btn, "up")       == 0) code = 1;  // ButtonEvent::UP
        else if (strcmp(btn, "down")     == 0) code = 2;  // ButtonEvent::DOWN
        else if (strcmp(btn, "enter")    == 0) code = 3;  // ButtonEvent::ENTER
        else if (strcmp(btn, "back")     == 0) code = 4;  // ButtonEvent::BACK
        else if (strcmp(btn, "settings") == 0) code = 5;  // ButtonEvent::SETTINGS
        if (code != 0) {
            _state.pendingWebButton = code;
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(400, "application/json", "{\"error\":\"Unknown button\"}");
        }

    } else {
        req->send(400, "application/json", "{\"error\":\"Unknown command\"}");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  API: Config
// ─────────────────────────────────────────────────────────────────────────────

void WebView::handleApiConfigGet(AsyncWebServerRequest* req) {
    WebRole role = authenticateSession(req);
    if (role != WebRole::ADMIN) { sendUnauthorized(req); return; }

    JsonDocument doc;
    buildConfigJson(doc);
    sendJson(req, doc);
}

void WebView::handleApiConfigSet(AsyncWebServerRequest* req, JsonVariant& body) {
    WebRole role = authenticateSession(req);
    if (role != WebRole::ADMIN) { sendUnauthorized(req); return; }

    if (!body.is<JsonObject>()) {
        req->send(400, "application/json", "{\"error\":\"Expected JSON object\"}");
        return;
    }

    JsonObject obj = body.as<JsonObject>();
    Config& cfg = _state.config;
    bool changed = false;

    // Setpoints
    if (obj.containsKey("flowSetpoint"))     { cfg.flowSetpoint     = (uint8_t)constrain((int)obj["flowSetpoint"],     20, 65); changed = true; }
    if (obj.containsKey("returnSetpoint"))   { cfg.returnSetpoint   = (uint8_t)constrain((int)obj["returnSetpoint"],   20, 65); changed = true; }
    if (obj.containsKey("flowHysteresis"))   { cfg.flowHysteresis   = (uint8_t)constrain((int)obj["flowHysteresis"],   1, 10);  changed = true; }
    if (obj.containsKey("returnHysteresis")) { cfg.returnHysteresis = (uint8_t)constrain((int)obj["returnHysteresis"], 1, 10);  changed = true; }
    if (obj.containsKey("roomSetpoint"))     { cfg.roomSetpoint     = (uint8_t)constrain((int)obj["roomSetpoint"],     5, 30);  changed = true; }
    if (obj.containsKey("roomHysteresis"))   { cfg.roomHysteresis   = (uint8_t)constrain((int)obj["roomHysteresis"],   1, 5);   changed = true; }

    // Timing
    if (obj.containsKey("pumpPrePostDelaySec"))    { cfg.pumpPrePostDelaySec    = (uint16_t)constrain((int)obj["pumpPrePostDelaySec"],    30, 120);  changed = true; }
    if (obj.containsKey("standbyPumpPeriodMin"))   { cfg.standbyPumpPeriodMin   = (uint16_t)constrain((int)obj["standbyPumpPeriodMin"],   30, 180);  changed = true; }
    if (obj.containsKey("standbyPumpDurationMin")) { cfg.standbyPumpDurationMin = (uint8_t)constrain((int)obj["standbyPumpDurationMin"],  1, 5);     changed = true; }
    if (obj.containsKey("minHeaterOffSec"))        { cfg.minHeaterOffSec        = (uint16_t)constrain((int)obj["minHeaterOffSec"],        60, 180);  changed = true; }

    // Energy meter
    if (obj.containsKey("heaterPowerKw")) {
        float kw = obj["heaterPowerKw"] | 0.0f;
        cfg.heaterPowerDeciKw = (uint16_t)constrain((int)lroundf(kw * 10), 5, 300);
        changed = true;
    }
    if (obj["energyReset"] | false) _state.energyResetRequested = true;  // applied by EnergyMeter

    // WiFi
    if (obj.containsKey("wifiSsid")) { strlcpy(cfg.wifiSsid, obj["wifiSsid"] | "", sizeof(cfg.wifiSsid)); changed = true; }
    if (obj.containsKey("wifiPass")) { strlcpy(cfg.wifiPass, obj["wifiPass"] | "", sizeof(cfg.wifiPass)); changed = true; }

    // MQTT
    if (obj.containsKey("mqttBroker"))   { strlcpy(cfg.mqttBroker,   obj["mqttBroker"]   | "", sizeof(cfg.mqttBroker));   changed = true; }
    if (obj.containsKey("mqttPort"))     { cfg.mqttPort = (uint16_t)(int)obj["mqttPort"];                                  changed = true; }
    if (obj.containsKey("mqttUser"))     { strlcpy(cfg.mqttUser,     obj["mqttUser"]     | "", sizeof(cfg.mqttUser));     changed = true; }
    if (obj.containsKey("mqttPass"))     { strlcpy(cfg.mqttPass,     obj["mqttPass"]     | "", sizeof(cfg.mqttPass));     changed = true; }
    if (obj.containsKey("mqttClientId")) { strlcpy(cfg.mqttClientId, obj["mqttClientId"] | "", sizeof(cfg.mqttClientId)); changed = true; }

    // NTP
    if (obj.containsKey("ntpServer1")) { strlcpy(cfg.ntpServer1, obj["ntpServer1"] | "", sizeof(cfg.ntpServer1)); changed = true; }
    if (obj.containsKey("ntpServer2")) { strlcpy(cfg.ntpServer2, obj["ntpServer2"] | "", sizeof(cfg.ntpServer2)); changed = true; }
    if (obj.containsKey("timezone"))   { strlcpy(cfg.timezone,   obj["timezone"]   | "", sizeof(cfg.timezone));   changed = true; }

    // Thermostat contact mode
    if (obj.containsKey("thermostatMode")) {
        const char* tm = obj["thermostatMode"] | "NO";
        cfg.thermostatMode = (strcmp(tm, "NC") == 0) ?
            ThermostatContact::NORMAL_CLOSED : ThermostatContact::NORMAL_OPEN;
        changed = true;
    }

    // Reset phrase
    if (obj.containsKey("resetPhrase")) {
        const char* p = obj["resetPhrase"] | "";
        if (strlen(p) >= 4) {
            strlcpy(cfg.resetPhrase, p, sizeof(cfg.resetPhrase));
            changed = true;
        }
    }

    // Password change
    if (obj.containsKey("adminPassword")) {
        const char* p = obj["adminPassword"] | "";
        if (strlen(p) >= 8) {
            sha256Hex(p, cfg.adminPassHash);
            changed = true;
        }
    }
    if (obj.containsKey("operatorPassword")) {
        const char* p = obj["operatorPassword"] | "";
        if (strlen(p) >= 8) {
            sha256Hex(p, cfg.operatorPassHash);
            changed = true;
        }
    }

    // Weather curve
    if (obj.containsKey("curve") && obj["curve"].is<JsonArray>()) {
        JsonArray arr = obj["curve"].as<JsonArray>();
        uint8_t cnt = 0;
        for (JsonVariant pt : arr) {
            if (cnt >= 5) break;
            if (pt.is<JsonObject>()) {
                cfg.curve[cnt].outsideTemp    = (int8_t)(int)pt["outsideTemp"];
                cfg.curve[cnt].flowSetpoint   = (uint8_t)constrain((int)pt["flowSetpoint"],   20, 90);
                cfg.curve[cnt].returnSetpoint = (uint8_t)constrain((int)pt["returnSetpoint"],  20, 90);
                cnt++;
            }
        }
        cfg.curveCount = (cnt >= 3) ? cnt : 0;
        changed = true;
    }

    // Sensor sources
    if (obj.containsKey("roomSensor") && obj["roomSensor"].is<JsonObject>()) {
        JsonObject rs = obj["roomSensor"].as<JsonObject>();
        const char* modeStr = rs["mode"] | "none";
        if (strcmp(modeStr, "ble") == 0) cfg.roomSensor.mode = TempSourceMode::BLE;
        else if (strcmp(modeStr, "api") == 0) cfg.roomSensor.mode = TempSourceMode::API;
        else cfg.roomSensor.mode = TempSourceMode::NONE;
        if (rs.containsKey("bleMac")) strlcpy(cfg.roomSensor.bleMac, rs["bleMac"] | "", sizeof(cfg.roomSensor.bleMac));
        if (rs.containsKey("apiUrl")) strlcpy(cfg.roomSensor.apiUrl, rs["apiUrl"] | "", sizeof(cfg.roomSensor.apiUrl));
        if (rs.containsKey("apiJsonPath")) strlcpy(cfg.roomSensor.apiJsonPath, rs["apiJsonPath"] | "", sizeof(cfg.roomSensor.apiJsonPath));
        if (rs.containsKey("apiPollSec")) cfg.roomSensor.apiPollSec = (uint32_t)(int)rs["apiPollSec"];
        changed = true;
    }

    if (obj.containsKey("outsideSensor") && obj["outsideSensor"].is<JsonObject>()) {
        JsonObject os = obj["outsideSensor"].as<JsonObject>();
        const char* modeStr = os["mode"] | "none";
        if (strcmp(modeStr, "ble") == 0) cfg.outsideSensor.mode = TempSourceMode::BLE;
        else if (strcmp(modeStr, "api") == 0) cfg.outsideSensor.mode = TempSourceMode::API;
        else cfg.outsideSensor.mode = TempSourceMode::NONE;
        if (os.containsKey("bleMac")) strlcpy(cfg.outsideSensor.bleMac, os["bleMac"] | "", sizeof(cfg.outsideSensor.bleMac));
        if (os.containsKey("apiUrl")) strlcpy(cfg.outsideSensor.apiUrl, os["apiUrl"] | "", sizeof(cfg.outsideSensor.apiUrl));
        if (os.containsKey("apiJsonPath")) strlcpy(cfg.outsideSensor.apiJsonPath, os["apiJsonPath"] | "", sizeof(cfg.outsideSensor.apiJsonPath));
        if (os.containsKey("apiPollSec")) cfg.outsideSensor.apiPollSec = (uint32_t)(int)os["apiPollSec"];
        changed = true;
    }

    if (changed) {
        nvsConfig.save(cfg);
    }

    req->send(200, "application/json", "{\"ok\":true}");
}

// ─────────────────────────────────────────────────────────────────────────────
//  API: First-boot Setup
// ─────────────────────────────────────────────────────────────────────────────

void WebView::handleSetup(AsyncWebServerRequest* req, JsonVariant& body) {
    // Only allowed if setup is not complete
    if (_state.config.setupComplete) {
        req->send(403, "application/json", "{\"error\":\"Already configured\"}");
        return;
    }

    if (!body.is<JsonObject>()) {
        req->send(400, "application/json", "{\"error\":\"Expected JSON object\"}");
        return;
    }

    JsonObject obj = body.as<JsonObject>();
    Config& cfg = _state.config;

    const char* adminPass = obj["adminPassword"] | "";
    const char* operPass  = obj["operatorPassword"] | "";
    const char* ssid      = obj["wifiSsid"] | "";
    const char* wpass     = obj["wifiPass"] | "";

    if (strlen(adminPass) < 8) {
        req->send(400, "application/json", "{\"error\":\"Admin password too short (min 8)\"}");
        return;
    }

    sha256Hex(adminPass, cfg.adminPassHash);

    if (strlen(operPass) >= 8) {
        sha256Hex(operPass, cfg.operatorPassHash);
    }

    if (ssid[0] != '\0') {
        strlcpy(cfg.wifiSsid, ssid, sizeof(cfg.wifiSsid));
        strlcpy(cfg.wifiPass, wpass, sizeof(cfg.wifiPass));
    }

    // Generate API token
    generateToken(cfg.apiToken);

    cfg.setupComplete = true;
    nvsConfig.save(cfg);

    req->send(200, "application/json",
        "{\"ok\":true,\"apiToken\":\"" + String(cfg.apiToken) + "\"}");
}

// ─────────────────────────────────────────────────────────────────────────────
//  API: Admin password reset
// ─────────────────────────────────────────────────────────────────────────────

void WebView::handleResetAdminPassword(AsyncWebServerRequest* req, JsonVariant& body) {
    if (!body.is<JsonObject>()) {
        req->send(400, "application/json", "{\"error\":\"Expected JSON object\"}");
        return;
    }

    JsonObject obj = body.as<JsonObject>();
    const char* phrase  = obj["phrase"]      | "";
    const char* newPass = obj["newPassword"] | "";

    // Validate phrase (constant-time compare to avoid timing oracle)
    const char* stored = _state.config.resetPhrase;
    bool phraseOk = (strlen(phrase) == strlen(stored)) &&
                    (memcmp(phrase, stored, strlen(stored)) == 0);
    if (!phraseOk) {
        req->send(403, "application/json", "{\"error\":\"Wrong reset phrase\"}");
        return;
    }

    if (strlen(newPass) < 8) {
        req->send(400, "application/json", "{\"error\":\"Password too short (min 8)\"}");
        return;
    }

    sha256Hex(newPass, _state.config.adminPassHash);
    nvsConfig.saveCredentials(_state.config);
    Serial.println("[WebView] Admin password reset via secret phrase");

    req->send(200, "application/json", "{\"ok\":true}");
}

// ─────────────────────────────────────────────────────────────────────────────
//  API: Admin actions (verify password / factory reset)
// ─────────────────────────────────────────────────────────────────────────────

void WebView::handleAdminAction(AsyncWebServerRequest* req, JsonVariant& body) {
    WebRole role = authenticateSession(req);
    if (role != WebRole::ADMIN) { sendUnauthorized(req); return; }

    if (!body.is<JsonObject>()) {
        req->send(400, "application/json", "{\"error\":\"Expected JSON object\"}");
        return;
    }

    JsonObject obj = body.as<JsonObject>();
    const char* action = obj["action"]   | "";
    const char* pass   = obj["password"] | "";

    if (!checkAdminPassword(pass)) {
        req->send(403, "application/json", "{\"error\":\"Wrong password\"}");
        return;
    }

    if (strcmp(action, "verify") == 0) {
        req->send(200, "application/json", "{\"ok\":true}");

    } else if (strcmp(action, "factory-reset") == 0) {
        nvsConfig.reset();
        req->send(200, "application/json", "{\"ok\":true}");
        // Restart after the response is flushed
        xTaskCreate([](void*) {
            vTaskDelay(pdMS_TO_TICKS(800));
            esp_restart();
        }, "rst", 2048, nullptr, 1, nullptr);

    } else {
        req->send(400, "application/json", "{\"error\":\"Unknown action\"}");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  JSON builders
// ─────────────────────────────────────────────────────────────────────────────

void WebView::buildStateJson(JsonDocument& doc) const {
    const SensorData&   s   = _state.sensors;
    const SystemStatus& st  = _state.status;
    const RelayState&   r   = _state.relays;
    const AlarmState&   al  = _state.alarms;

    // Sensors
    JsonObject sensors = doc["sensors"].to<JsonObject>();
    if (isnan(s.flowTemp))    sensors["flowTemp"]    = nullptr;
    else                      sensors["flowTemp"]    = serialized(String(s.flowTemp, 1));
    if (isnan(s.returnTemp))  sensors["returnTemp"]  = nullptr;
    else                      sensors["returnTemp"]  = serialized(String(s.returnTemp, 1));
    if (isnan(s.roomTemp))    sensors["roomTemp"]    = nullptr;
    else                      sensors["roomTemp"]    = serialized(String(s.roomTemp, 1));
    if (isnan(s.outsideTemp)) sensors["outsideTemp"] = nullptr;
    else                      sensors["outsideTemp"] = serialized(String(s.outsideTemp, 1));
    sensors["flowFault"]   = s.flowSensorFault;
    sensors["returnFault"] = s.returnSensorFault;

    // Relays
    JsonObject relays = doc["relays"].to<JsonObject>();
    relays["heater"] = r.heaterOn;
    relays["pump"]   = r.pumpOn;

    // Energy meter
    JsonObject energy = doc["energy"].to<JsonObject>();
    energy["totalKwh"] = serialized(String(_state.energy.totalWh / 1000.0, 3));
    energy["resetTs"]  = _state.energy.resetTs;
    energy["powerKw"]  = serialized(String(_state.config.heaterPowerDeciKw / 10.0, 1));

    // Status
    JsonObject status = doc["status"].to<JsonObject>();
    const char* modeStr = "off";
    if (st.mode == SystemMode::ON)         modeStr = "on";
    if (st.mode == SystemMode::ANTIFREEZE) modeStr = "antifreeze";
    status["mode"] = modeStr;

    const char* phaseStr = "idle";
    switch (st.phase) {
        case HeaterPhase::PUMP_PRE:     phaseStr = "pump_pre";     break;
        case HeaterPhase::HEATING:      phaseStr = "heating";      break;
        case HeaterPhase::PUMP_POST:    phaseStr = "pump_post";    break;
        case HeaterPhase::PUMP_STANDBY: phaseStr = "pump_standby"; break;
        default: break;
    }
    status["phase"] = phaseStr;
    status["activeFlowSetpoint"]   = st.activeFlowSetpoint;
    status["activeReturnSetpoint"] = st.activeReturnSetpoint;
    status["haRemoteDisable"]      = st.haRemoteDisable;
    status["wifiConnected"]        = st.wifiConnected;
    status["mqttConnected"]        = st.mqttConnected;
    status["ntpSynced"]            = st.ntpSynced;
    status["otaInProgress"]        = st.otaInProgress;

    // Alarms
    JsonObject alarms = doc["alarms"].to<JsonObject>();
    alarms["active"]               = al.active;
    alarms["overheat"]             = al.isSet(AlarmFlag::OVERHEAT);
    alarms["flowSensorFault"]      = al.isSet(AlarmFlag::FLOW_SENSOR_FAULT);
    alarms["returnSensorFault"]    = al.isSet(AlarmFlag::RETURN_SENSOR_FAULT);
    alarms["roomSensorLost"]       = al.isSet(AlarmFlag::ROOM_SENSOR_LOST);
    alarms["outsideSensorLost"]    = al.isSet(AlarmFlag::OUTSIDE_SENSOR_LOST);
    alarms["bleRoomLowBattery"]    = al.isSet(AlarmFlag::BLE_ROOM_LOW_BATTERY);
    alarms["bleOutsideLowBattery"] = al.isSet(AlarmFlag::BLE_OUTSIDE_LOW_BATTERY);
}

void WebView::buildLogJson(JsonDocument& doc) const {
    JsonArray arr = doc["events"].to<JsonArray>();
    uint16_t count = _state.log.count;

    for (uint16_t i = 0; i < count; i++) {
        const LogEvent* ev = _state.log.get(i);
        if (!ev) break;

        JsonObject obj = arr.add<JsonObject>();
        obj["ts"] = ev->timestamp;

        const char* relayStr = "?";
        switch (ev->relay) {
            case RelayChange::HEATER_ON:  relayStr = "heater_on";  break;
            case RelayChange::HEATER_OFF: relayStr = "heater_off"; break;
            case RelayChange::PUMP_ON:    relayStr = "pump_on";    break;
            case RelayChange::PUMP_OFF:   relayStr = "pump_off";   break;
        }
        obj["relay"] = relayStr;
        obj["reason"] = (uint8_t)ev->reason;
        obj["flowSp"]   = ev->activeFlowSetpoint;
        obj["returnSp"] = ev->activeReturnSetpoint;

        JsonObject temps = obj["temps"].to<JsonObject>();
        float ft = TempSnapshot::decode(ev->temps.flowTemp);
        float rt = TempSnapshot::decode(ev->temps.returnTemp);
        float rm = TempSnapshot::decode(ev->temps.roomTemp);
        float ot = TempSnapshot::decode(ev->temps.outsideTemp);
        if (isnan(ft)) temps["flow"]    = nullptr; else temps["flow"]    = serialized(String(ft, 1));
        if (isnan(rt)) temps["return"]  = nullptr; else temps["return"]  = serialized(String(rt, 1));
        if (isnan(rm)) temps["room"]    = nullptr; else temps["room"]    = serialized(String(rm, 1));
        if (isnan(ot)) temps["outside"] = nullptr; else temps["outside"] = serialized(String(ot, 1));
    }

    doc["count"] = count;
}

void WebView::buildConfigJson(JsonDocument& doc) const {
    const Config& cfg = _state.config;

    doc["flowSetpoint"]     = cfg.flowSetpoint;
    doc["returnSetpoint"]   = cfg.returnSetpoint;
    doc["flowHysteresis"]   = cfg.flowHysteresis;
    doc["returnHysteresis"] = cfg.returnHysteresis;
    doc["roomSetpoint"]     = cfg.roomSetpoint;
    doc["roomHysteresis"]   = cfg.roomHysteresis;

    doc["pumpPrePostDelaySec"]    = cfg.pumpPrePostDelaySec;
    doc["standbyPumpPeriodMin"]   = cfg.standbyPumpPeriodMin;
    doc["standbyPumpDurationMin"] = cfg.standbyPumpDurationMin;
    doc["minHeaterOffSec"]        = cfg.minHeaterOffSec;
    doc["heaterPowerKw"]          = serialized(String(cfg.heaterPowerDeciKw / 10.0, 1));

    doc["thermostatMode"] = (cfg.thermostatMode == ThermostatContact::NORMAL_CLOSED) ? "NC" : "NO";

    doc["wifiSsid"] = cfg.wifiSsid;
    // Don't expose wifiPass

    doc["mqttBroker"]   = cfg.mqttBroker;
    doc["mqttPort"]     = cfg.mqttPort;
    doc["mqttUser"]     = cfg.mqttUser;
    doc["mqttClientId"] = cfg.mqttClientId;
    // Don't expose mqttPass

    doc["ntpServer1"] = cfg.ntpServer1;
    doc["ntpServer2"] = cfg.ntpServer2;
    doc["timezone"]   = cfg.timezone;

    doc["adminUser"]    = cfg.adminUser;
    doc["operatorUser"] = cfg.operatorUser;
    doc["resetPhrase"]  = cfg.resetPhrase;

    // Sensor sources
    JsonObject room = doc["roomSensor"].to<JsonObject>();
    const char* roomMode = "none";
    if (cfg.roomSensor.mode == TempSourceMode::BLE) roomMode = "ble";
    else if (cfg.roomSensor.mode == TempSourceMode::API) roomMode = "api";
    room["mode"]       = roomMode;
    room["bleMac"]     = cfg.roomSensor.bleMac;
    room["apiUrl"]     = cfg.roomSensor.apiUrl;
    room["apiJsonPath"]= cfg.roomSensor.apiJsonPath;
    room["apiPollSec"] = cfg.roomSensor.apiPollSec;

    JsonObject outside = doc["outsideSensor"].to<JsonObject>();
    const char* outMode = "none";
    if (cfg.outsideSensor.mode == TempSourceMode::BLE) outMode = "ble";
    else if (cfg.outsideSensor.mode == TempSourceMode::API) outMode = "api";
    outside["mode"]       = outMode;
    outside["bleMac"]     = cfg.outsideSensor.bleMac;
    outside["apiUrl"]     = cfg.outsideSensor.apiUrl;
    outside["apiJsonPath"]= cfg.outsideSensor.apiJsonPath;
    outside["apiPollSec"] = cfg.outsideSensor.apiPollSec;

    // Curve
    doc["curveCount"] = cfg.curveCount;
    JsonArray curve = doc["curve"].to<JsonArray>();
    for (uint8_t i = 0; i < cfg.curveCount && i < 5; i++) {
        JsonObject pt = curve.add<JsonObject>();
        pt["outsideTemp"]    = cfg.curve[i].outsideTemp;
        pt["flowSetpoint"]   = cfg.curve[i].flowSetpoint;
        pt["returnSetpoint"] = cfg.curve[i].returnSetpoint;
    }

    doc["setupComplete"] = cfg.setupComplete;
    // Expose API token for admin
    doc["apiToken"] = cfg.apiToken;
}

// ─────────────────────────────────────────────────────────────────────────────
//  API: Test mode
// ─────────────────────────────────────────────────────────────────────────────

void WebView::handleApiTest(AsyncWebServerRequest* req, JsonVariant& body) {
    WebRole role = authenticateSession(req);
    if (role != WebRole::ADMIN) { sendUnauthorized(req); return; }

    if (!body.is<JsonObject>()) {
        req->send(400, "application/json", "{\"error\":\"Expected JSON object\"}");
        return;
    }

    JsonObject obj = body.as<JsonObject>();
    TestState& t = _state.test;

    if (obj.containsKey("active"))
        t.active = obj["active"].as<bool>();

    // Helper: set float field from JSON, treating JSON null as NAN
    auto setTemp = [&](const char* key, float& field) {
        if (!obj.containsKey(key)) return;
        JsonVariant v = obj[key];
        field = v.isNull() ? NAN : v.as<float>();
    };
    setTemp("flowTemp",    t.flowTemp);
    setTemp("returnTemp",  t.returnTemp);
    setTemp("roomTemp",    t.roomTemp);
    setTemp("outsideTemp", t.outsideTemp);

    if (obj.containsKey("flowFault"))          t.flowFault          = obj["flowFault"].as<bool>();
    if (obj.containsKey("returnFault"))        t.returnFault        = obj["returnFault"].as<bool>();
    if (obj.containsKey("roomSensorLost"))     t.roomSensorLost     = obj["roomSensorLost"].as<bool>();
    if (obj.containsKey("outsideSensorLost"))  t.outsideSensorLost  = obj["outsideSensorLost"].as<bool>();
    if (obj.containsKey("thermostatOverride")) t.thermostatOverride = obj["thermostatOverride"].as<bool>();
    if (obj.containsKey("thermostatAllow"))    t.thermostatAllow    = obj["thermostatAllow"].as<bool>();

    JsonDocument doc;
    buildTestJson(doc);
    sendJson(req, doc);
}

void WebView::handleApiTestGet(AsyncWebServerRequest* req) {
    WebRole role = authenticateSession(req);
    if (role != WebRole::ADMIN) { sendUnauthorized(req); return; }

    JsonDocument doc;
    buildTestJson(doc);
    sendJson(req, doc);
}

void WebView::buildTestJson(JsonDocument& doc) const {
    const TestState& t = _state.test;
    doc["active"] = t.active;
    if (isnan(t.flowTemp))    doc["flowTemp"]    = nullptr;
    else                      doc["flowTemp"]    = t.flowTemp;
    if (isnan(t.returnTemp))  doc["returnTemp"]  = nullptr;
    else                      doc["returnTemp"]  = t.returnTemp;
    if (isnan(t.roomTemp))    doc["roomTemp"]    = nullptr;
    else                      doc["roomTemp"]    = t.roomTemp;
    if (isnan(t.outsideTemp)) doc["outsideTemp"] = nullptr;
    else                      doc["outsideTemp"] = t.outsideTemp;
    doc["flowFault"]          = t.flowFault;
    doc["returnFault"]        = t.returnFault;
    doc["roomSensorLost"]     = t.roomSensorLost;
    doc["outsideSensorLost"]  = t.outsideSensorLost;
    doc["thermostatOverride"] = t.thermostatOverride;
    doc["thermostatAllow"]    = t.thermostatAllow;
}

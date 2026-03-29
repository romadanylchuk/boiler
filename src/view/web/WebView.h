#pragma once
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "../../model/AppState.h"
#include "../../controller/BoilerLogic.h"

enum class WebRole : uint8_t { NONE, OPERATOR, ADMIN };

struct WebSession {
    char     token[65] = {};  // 64-char hex (32 random bytes → 64 hex chars + null)
    WebRole  role      = WebRole::NONE;
    uint32_t lastSeen  = 0;   // millis
    bool     active    = false;
};

class WebView {
public:
    WebView(AppState& state, BoilerLogic& logic);
    void begin();
    AsyncWebServer& server() { return _server; }  // OtaService uses this

private:
    // ── Route handlers ────────────────────────────────────────────────────────
    void setupRoutes();

    // Auth
    void handleLogin(AsyncWebServerRequest* req);
    void handleLogout(AsyncWebServerRequest* req);

    // API — read (token auth)
    void handleApiState(AsyncWebServerRequest* req);
    void handleApiLog(AsyncWebServerRequest* req);

    // API — write operator (token auth)
    void handleApiCommand(AsyncWebServerRequest* req, JsonVariant& body);

    // API — config (admin session)
    void handleApiConfigGet(AsyncWebServerRequest* req);
    void handleApiConfigSet(AsyncWebServerRequest* req, JsonVariant& body);

    // Setup (first boot)
    void handleSetup(AsyncWebServerRequest* req, JsonVariant& body);

    // Password reset via secret phrase (no auth)
    void handleResetAdminPassword(AsyncWebServerRequest* req, JsonVariant& body);

    // Admin-gated actions: verify password, factory-reset
    void handleAdminAction(AsyncWebServerRequest* req, JsonVariant& body);

    // ── Auth helpers ──────────────────────────────────────────────────────────
    WebRole  authenticateToken(AsyncWebServerRequest* req) const;
    WebRole  authenticateSession(AsyncWebServerRequest* req) const;
    WebSession* findSession(const char* token);
    WebSession* createSession(WebRole role);
    void     expireSessions();

    // ── JSON builders ─────────────────────────────────────────────────────────
    void buildStateJson(JsonDocument& doc) const;
    void buildLogJson(JsonDocument& doc) const;
    void buildConfigJson(JsonDocument& doc) const;

    // ── Helpers ───────────────────────────────────────────────────────────────
    bool checkAdminPassword(const char* pass) const;
    void sendUnauthorized(AsyncWebServerRequest* req);
    void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200);

    AppState&       _state;
    BoilerLogic&    _logic;
    AsyncWebServer  _server{80};

    static constexpr uint8_t  MAX_SESSIONS    = 4;
    static constexpr uint32_t SESSION_TIMEOUT = 30 * 60 * 1000;  // 30 min
    static constexpr uint8_t  MAX_LOGIN_FAILS = 3;
    static constexpr uint32_t LOCKOUT_MS      = 60 * 1000;

    WebSession _sessions[MAX_SESSIONS];
};

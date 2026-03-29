#include "TimeService.h"
#include <esp_sntp.h>

TimeService* TimeService::_instance = nullptr;

void TimeService::_syncCallback(struct timeval* tv) {
    if (_instance) {
        _instance->_synced       = true;
        _instance->_unixAtSync   = (time_t)tv->tv_sec;
        _instance->_uptimeAtSync = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        _instance->_lastSync     = (time_t)tv->tv_sec;
        Serial.printf("[TimeService] NTP synced: %ld\n", (long)tv->tv_sec);
    }
}

void TimeService::begin(const char* server1, const char* server2, const char* tz) {
    _instance = this;

    sntp_set_time_sync_notification_cb(_syncCallback);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    configTzTime(tz, server1, server2);

    _lastSyncAttempt = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    Serial.printf("[TimeService] NTP configured: %s / %s, tz=%s\n", server1, server2, tz);
}

void TimeService::update() {
    uint32_t uptimeSec = uptimeSeconds();

    if (!_synced) {
        // Retry every RETRY_SEC
        if (uptimeSec - _lastSyncAttempt >= RETRY_SEC) {
            _lastSyncAttempt = uptimeSec;
            sntp_restart();
            Serial.println("[TimeService] NTP retry...");
        }
    } else {
        // Re-sync every RESYNC_SEC
        if (uptimeSec - _uptimeAtSync >= RESYNC_SEC) {
            _lastSyncAttempt = uptimeSec;
            sntp_restart();
            Serial.println("[TimeService] NTP periodic resync...");
        }
    }
}

void TimeService::triggerResync() {
    _lastSyncAttempt = 0;
    sntp_restart();
    Serial.println("[TimeService] NTP resync triggered");
}

time_t TimeService::now() const {
    if (_synced) {
        time_t t;
        time(&t);
        return t;
    }
    // Fallback: seconds since boot
    return (time_t)uptimeSeconds();
}

uint32_t TimeService::uptimeSeconds() const {
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

void TimeService::formatTimestamp(time_t ts, char* buf, size_t len) const {
    if (ts < 1700000000L) {
        // Treat as uptime
        uint32_t secs = (uint32_t)ts;
        uint32_t h = secs / 3600;
        uint32_t m = (secs % 3600) / 60;
        uint32_t s = secs % 60;
        snprintf(buf, len, "+%02u:%02u:%02u", h, m, s);
    } else {
        struct tm tm_info;
        localtime_r(&ts, &tm_info);
        strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
    }
}

void TimeService::formatTime(char* buf, size_t len) const {
    time_t t = now();
    formatTimestamp(t, buf, len);
}

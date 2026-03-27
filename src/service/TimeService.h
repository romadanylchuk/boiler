#pragma once
#include <Arduino.h>
#include <time.h>

class TimeService {
public:
    void begin(const char* server1, const char* server2, const char* tz);
    void update();          // call from loop — handles re-sync schedule
    void triggerResync();   // call after WiFi reconnect

    time_t   now()          const;   // unix time if synced, else seconds since boot
    uint32_t uptimeSeconds()const;   // always available
    bool     isSynced()     const { return _synced; }
    time_t   lastSyncAt()   const { return _lastSync; }

    // Format a timestamp for display
    // Synced:   "2025-03-24 14:32:11"
    // Unsynced: "T+02:14:33"
    void formatTimestamp(time_t ts, char* buf, size_t len) const;
    void formatTime(char* buf, size_t len) const;  // current time only (HH:MM:SS)

private:
    static void _syncCallback(struct timeval* tv);

    bool     _synced          = false;
    time_t   _unixAtSync      = 0;
    uint32_t _uptimeAtSync    = 0;
    time_t   _lastSync        = 0;
    uint32_t _lastSyncAttempt = 0;

    static constexpr uint32_t RESYNC_SEC  = 24 * 3600;
    static constexpr uint32_t RETRY_SEC   =  5 * 60;

    static TimeService* _instance;
};

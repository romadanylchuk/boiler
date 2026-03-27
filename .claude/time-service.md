# Time Service

## Strategy

No RTC on board. Time is managed in two layers:

```
Layer 1 — Uptime (always available, starts at 0 on boot)
  esp_timer_get_time() → microseconds (int64, no 49-day overflow like millis)
  uptimeSeconds() = esp_timer_get_time() / 1000000

Layer 2 — Real time (available after first NTP sync)
  Sync via NTP on boot + every 24h
  After sync: unixBase + uptimeSeconds = current unix time
  If sync broken: keep calculating from last known unixBase (drift ~5 sec/day, acceptable)
```

## TimeService Class

```cpp
class TimeService {
public:
    void begin();                        // configTime() + register sync callback
    void update();                       // call from loop, handles re-sync schedule

    time_t  now() const;                 // unix timestamp if synced, else seconds since boot
    uint32_t uptimeSeconds() const;      // always available, seconds since boot
    bool    isSynced() const;            // true after first successful NTP sync
    time_t  lastSyncAt() const;          // unix time of last successful sync (0 = never)

    // For display — returns formatted string
    // If synced:   "2025-03-24 14:32:11"
    // If not yet:  "T+02:14:33" (uptime)
    void    formatTimestamp(time_t ts, char* buf, size_t len) const;

private:
    bool     _synced         = false;
    time_t   _unixAtBoot     = 0;     // unix time captured at first sync moment
    uint32_t _uptimeAtSync   = 0;     // uptimeSeconds() at first sync moment
    time_t   _lastSync       = 0;
    uint32_t _lastSyncAttempt = 0;    // uptime of last attempt

    static constexpr uint32_t RESYNC_INTERVAL_SEC = 24 * 3600;  // 24 hours
    static constexpr uint32_t RETRY_INTERVAL_SEC  = 5 * 60;     // 5 min retry if failed

    static void _ntpSyncCallback(struct timeval* tv);  // SNTP callback
};
```

## Boot Behavior

```
Boot
  ├── WiFi connected?
  │     YES → configTime("pool.ntp.org", "time.google.com")
  │            wait up to 10s for sync
  │            if synced → _synced=true, capture unixAtBoot + uptimeAtSync
  │            if timeout → continue without sync, retry every 5 min
  │
  └── WiFi not connected → continue without sync, retry when WiFi connects
```

## Re-sync Schedule

- Every 24 hours (from last successful sync)
- If sync fails: retry every 5 minutes
- On reconnect after WiFi loss: trigger immediate re-sync attempt

## LogEvent Timestamp

```cpp
// In LogEvent:
time_t timestamp;   // unix time if synced, seconds-since-boot if not

// How to store:
logEvent.timestamp = timeService.now();

// How to interpret on display / web:
// If timestamp < 1700000000 (year ~2023) → it's uptime, display as T+HH:MM:SS
// If timestamp >= 1700000000            → it's real time, display formatted
```

## NVS Persistence

The last known unix time is NOT saved to NVS.
Reason: NVS has limited write cycles; time changes constantly.
On each power cycle: start as unsynced, re-sync from NTP on first WiFi connection.
Log events before first sync show relative uptime — clearly identifiable.

## Config (stored in NVS)

```cpp
char ntpServer1[64];    // default: "pool.ntp.org"
char ntpServer2[64];    // default: "time.google.com"
char timezone[48];      // POSIX TZ string, e.g. "EET-2EEST,M3.5.0,M10.5.0/3"
                        // (Ukraine: Eastern European Time UTC+2/+3)
```

## Display Formatting

| Sync state | Log entry display | Current time display |
|-----------|------------------|---------------------|
| Synced | `2025-03-24 14:32:11` | `14:32:11` (time only, date in status) |
| Not synced | `T+02:14:33` | `T+02:14:33` |
| Re-sync after gap | Real time continues correctly from last known base | |

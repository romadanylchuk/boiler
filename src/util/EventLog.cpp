#include "../model/AppState.h"

void EventLog::push(RelayChange relay, ChangeReason reason,
                    const TempSnapshot& temps,
                    uint8_t flowSp, uint8_t returnSp,
                    time_t ts) {
    LogEvent& e    = events[head];
    e.timestamp    = ts;
    e.relay        = relay;
    e.reason       = reason;
    e.temps        = temps;
    e.activeFlowSetpoint   = flowSp;
    e.activeReturnSetpoint = returnSp;

    head = (head + 1) % EVENT_LOG_SIZE;
    if (count < EVENT_LOG_SIZE) count++;
}

const LogEvent* EventLog::get(uint16_t indexFromNewest) const {
    if (indexFromNewest >= count) return nullptr;
    uint16_t idx = (head + EVENT_LOG_SIZE - 1 - indexFromNewest) % EVENT_LOG_SIZE;
    return &events[idx];
}

void EventLog::clear() {
    head  = 0;
    count = 0;
}

#pragma once
// Alarms: SetAlarm / ReleaseAlarm (and the i-variants) in the TICK domain.
//
// Kernel rule: "SetAlarm(time, handler, arg) arms a one-shot alarm that
// fires `time` H-sync units later, calling handler(id, time, arg) in
// interrupt context; ReleaseAlarm(id) cancels it (KE_ERROR when unknown)."
// Time in this runtime is the emulated tick domain (the frame boundary is the
// only clock edge the guest observes): an alarm's deadline is a tick count and
// it FIRES ONLY AT A BOUNDARY STEP -- FireDue(now_tick) invoked by the boundary
// sequencer -- never between boundaries, never on a wall timer. The game arms
// no alarm before the Phase-3 park; the table exists so the boundary owner has
// its hook and later phases never touch this file.
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

class Alarms {
public:
    // handler(id, time, arg) -- the guest-side call is made by the installer
    // (syscall layer) through this host callback; Phase 3 tests use a lambda.
    using Handler = std::function<void(int id, uint32_t time, uint32_t arg)>;

    static Alarms& Instance();

    // Arms an alarm due at now_tick + ticks (>= 1); returns the alarm id (> 0).
    int SetAlarm(uint64_t now_tick, uint32_t ticks, Handler handler, uint32_t arg);
    // Cancels; KE_OK, or KE_ERROR when no such alarm is armed.
    int ReleaseAlarm(int id);
    // Boundary step: fire (in id order) every alarm whose deadline <= now_tick.
    // Returns the number fired. The only place a handler ever runs.
    int FireDue(uint64_t now_tick);

    int ArmedCount();
    void ResetForTest();

private:
    Alarms() = default;
    struct Entry {
        int id;
        uint64_t due_tick;
        uint32_t time;
        uint32_t arg;
        Handler handler;
    };
    std::mutex mu_;
    std::vector<Entry> armed_;
    int next_id_ = 1;
};

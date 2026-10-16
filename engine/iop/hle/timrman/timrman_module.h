// engine/iop/hle/timrman/timrman_module.h -- TimrmanModule: the IOP timrman
// library as STREAM.IRX sees it: AllocHardTimer (idx 4), FreeHardTimer (6),
// SetTimerHandler (20), SetupHardTimer (22), StartHardTimer (23). Timer ids
// are 1..4. StartHardTimer turns the compare value (36.864 MHz sysclock
// ticks) into a kernel periodic timer whose beat runs the recompiled handler
// on a fresh IopContext ($a0 = arg; $sp/$gp from the title's TimerLayout) and
// rearms while the handler returns non-zero in $v0 -- STREAM.IRX's heartbeat.
#pragma once

#include <cstdint>
#include <functional>

#include "engine/core/config/config.h"
#include "engine/iop/iop_context.h"

namespace ps2r::iop {
class IopModuleTable;
}

namespace ps2r::iop::hle {

class TimrmanModule {
public:
    using GuestFunction = std::function<void(IopContext&)>;
    using FunctionResolver = std::function<GuestFunction(uint32_t addr)>;

    // The handler's scratch stack and the module $gp (title data; gp 0 = the
    // $gp of the thread that called SetTimerHandler, for several hosted modules).
    struct TimerLayout {
        uint32_t handler_sp = 0;
        uint32_t gp = 0;
    };

    static constexpr int kMaxTimers = 4;
    static constexpr uint64_t kSysClockHz = 36864000;   // the IOP hard-timer clock
    static constexpr uint64_t kMinPeriodUsec = 1000;
    static constexpr uint64_t kMaxPeriodUsec = 100000;

    TimrmanModule(IopModuleTable& table, FunctionResolver resolve, TimerLayout layout);

    void AllocHardTimer(IopContext& ctx);    // int (source, size, prescale) -> id 1..4 or -1
    void FreeHardTimer(IopContext& ctx);     // int (id) -> 0; stops a running beat
    void SetTimerHandler(IopContext& ctx);   // int (id, compare, handler, arg) -> 0
    void SetupHardTimer(IopContext& ctx);    // int (id, source, mode, prescale) -> 0
    void StartHardTimer(IopContext& ctx);    // int (id) -> 0; arms the periodic beat
    void StopHardTimer(IopContext& ctx);     // int (id) -> 0; stops a running beat (the timer stays allocated)

    // compare sysclock ticks -> the beat period in microseconds (clamped).
    static uint64_t PeriodUsec(uint32_t compare);

    // Savestate restore: a hard timer the image records as allocated, set up and
    // started (the module's descriptor words) is running after the restore --
    // the same end state AllocHardTimer + SetTimerHandler + StartHardTimer reach.
    // Idempotent on a timer already beating; the id counter advances past `id`
    // so a later AllocHardTimer never hands the live id out again. The beat's
    // first due follows the restored-timer rule (SetRestoredTimerDue): one period
    // after the kernel clock's now (the restored anchor's time), or at the
    // restore itself -- due = now, so the handler runs in the first Dispatch
    // after the restore, before any RPC-woken thread of that pass. Neither image
    // format carries the timer's remaining time; a timer mid-period at the save
    // has at most one period left, so "at the restore" is the earliest admissible
    // phase and "one period" the latest. Returns true when a beat was armed.
    bool RestoreHardTimer(int id, uint32_t compare, uint32_t handler, uint32_t arg);
    // Config iop.restored_timer_due (default OnePeriod).
    void SetRestoredTimerDue(ps2r::IopRestoredTimerDue rule) { restored_due_ = rule; }
    ps2r::IopRestoredTimerDue RestoredTimerDue() const { return restored_due_; }
    // Savestate restore: a hard timer the image records as freed is stopped.
    void ReleaseHardTimer(int id);

    bool Running(int id) const;
    int KernelTimerId(int id) const;

private:
    struct HardTimer {
        bool allocated = false;
        bool running = false;
        uint32_t handler = 0;
        uint32_t arg = 0;
        uint32_t compare = 0;
        uint32_t gp = 0;          // the registering thread's $gp when the layout's gp is 0
        int kernel_timer_id = -1;
    };
    HardTimer* Timer(uint32_t id);
    const HardTimer* Timer(uint32_t id) const;
    bool Arm(HardTimer* t, bool due_now);   // the periodic beat StartHardTimer arms (first due now, or now + period)

    IopModuleTable& table_;
    FunctionResolver resolve_;
    TimerLayout layout_;
    HardTimer timers_[kMaxTimers];
    int next_id_ = 1;
    ps2r::IopRestoredTimerDue restored_due_ = ps2r::IopRestoredTimerDue::OnePeriod;
};

}  // namespace ps2r::iop::hle

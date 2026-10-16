// engine/iop/hle/timrman/timrman_module.cpp -- TimrmanModule: hard timers as kernel periodic timers.
#include "engine/iop/hle/timrman/timrman_module.h"

#include <utility>

#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_module_table.h"

namespace ps2r::iop::hle {

TimrmanModule::TimrmanModule(IopModuleTable& table, FunctionResolver resolve, TimerLayout layout)
    : table_(table), resolve_(std::move(resolve)), layout_(layout) {}

TimrmanModule::HardTimer* TimrmanModule::Timer(uint32_t id) {
    if (id < 1 || id > static_cast<uint32_t>(kMaxTimers)) return nullptr;
    return &timers_[id - 1];
}

const TimrmanModule::HardTimer* TimrmanModule::Timer(uint32_t id) const {
    if (id < 1 || id > static_cast<uint32_t>(kMaxTimers)) return nullptr;
    return &timers_[id - 1];
}

uint64_t TimrmanModule::PeriodUsec(uint32_t compare) {
    // compare ticks at 36.864 MHz -> microseconds: compare * 1000 / 36864.
    uint64_t usec = static_cast<uint64_t>(compare) * 1000000ull / kSysClockHz;
    if (usec < kMinPeriodUsec) usec = kMinPeriodUsec;
    if (usec > kMaxPeriodUsec) usec = kMaxPeriodUsec;
    return usec;
}

void TimrmanModule::AllocHardTimer(IopContext& ctx) {
    if (next_id_ > kMaxTimers) {
        ctx.gpr[2] = 0xFFFFFFFFu;
        return;
    }
    const int id = next_id_++;
    timers_[id - 1].allocated = true;
    ctx.gpr[2] = static_cast<uint32_t>(id);
}

void TimrmanModule::FreeHardTimer(IopContext& ctx) {
    if (HardTimer* t = Timer(ctx.gpr[4])) {
        if (t->kernel_timer_id >= 0) table_.Kernel().StopTimer(t->kernel_timer_id);
        *t = HardTimer{};
    }
    ctx.gpr[2] = 0;
}

void TimrmanModule::SetTimerHandler(IopContext& ctx) {
    if (HardTimer* t = Timer(ctx.gpr[4])) {
        t->compare = ctx.gpr[5];
        t->handler = ctx.gpr[6];
        t->arg = ctx.gpr[7];
        t->gp = ctx.gpr[28];
    }
    ctx.gpr[2] = 0;
}

void TimrmanModule::SetupHardTimer(IopContext& ctx) { ctx.gpr[2] = 0; }

void TimrmanModule::StartHardTimer(IopContext& ctx) {
    ctx.gpr[2] = 0;
    Arm(Timer(ctx.gpr[4]), false);
}

void TimrmanModule::StopHardTimer(IopContext& ctx) {
    if (HardTimer* t = Timer(ctx.gpr[4])) {
        if (t->running && t->kernel_timer_id >= 0) table_.Kernel().StopTimer(t->kernel_timer_id);
        t->running = false;
        t->kernel_timer_id = -1;
    }
    ctx.gpr[2] = 0;
}

bool TimrmanModule::Arm(HardTimer* t, bool due_now) {
    if (!t || t->handler == 0 || t->running) return false;
    GuestFunction handler = resolve_ ? resolve_(t->handler) : GuestFunction{};
    if (!handler) return false;   // no recompiled handler: the timer stays inert
    t->running = true;
    const uint32_t arg = t->arg;
    TimerLayout layout = layout_;
    if (layout.gp == 0) layout.gp = t->gp;
    const uint64_t period = PeriodUsec(t->compare);
    const uint64_t now = table_.Kernel().Clock().NowUs();
    t->kernel_timer_id = table_.Kernel().StartPeriodicTimerAtUs(due_now ? now : now + period, period, [t, handler, arg, layout] {
        if (!t->running) return false;
        IopContext cb = IopContext::ForEntry(layout.handler_sp, layout.gp);
        cb.gpr[4] = arg;
        handler(cb);
        const bool rearm = cb.gpr[2] != 0;
        if (!rearm) t->running = false;
        return rearm;
    });
    table_.NotifyHost();
    return true;
}

bool TimrmanModule::RestoreHardTimer(int id, uint32_t compare, uint32_t handler, uint32_t arg) {
    HardTimer* t = Timer(static_cast<uint32_t>(id));
    if (!t || compare == 0 || handler == 0) return false;
    if (next_id_ <= id) next_id_ = id + 1;
    if (t->running) return false;   // already beating: the live state is the image's
    t->allocated = true;
    t->compare = compare;
    t->handler = handler;
    t->arg = arg;
    return Arm(t, restored_due_ == ps2r::IopRestoredTimerDue::AtRestore);
}

void TimrmanModule::ReleaseHardTimer(int id) {
    HardTimer* t = Timer(static_cast<uint32_t>(id));
    if (!t || !t->allocated) return;
    if (t->kernel_timer_id >= 0) table_.Kernel().StopTimer(t->kernel_timer_id);
    *t = HardTimer{};
}

bool TimrmanModule::Running(int id) const {
    const HardTimer* t = Timer(static_cast<uint32_t>(id));
    return t && t->running;
}

int TimrmanModule::KernelTimerId(int id) const {
    const HardTimer* t = Timer(static_cast<uint32_t>(id));
    return t ? t->kernel_timer_id : -1;
}

}  // namespace ps2r::iop::hle

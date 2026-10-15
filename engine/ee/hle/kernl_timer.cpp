#include "engine/ee/hle/kernl_timer.h"

#include <cstring>

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/recomp_abi/host_app/memory.h"

KernlTimer::Clock KernlTimer::clock_;
KernlTimer::WaitHook KernlTimer::wait_;
uint64_t KernlTimer::last_deadline_ = 0;

namespace {
void Ret0(CpuContext& ctx) { ctx.cpuRegs.GPR.r[2].SD[0] = 0; }
}  // namespace

void KernlTimer::Register(SdkBindingManifest& m) {
    m.Register("libkernl.a", "InitTimer", &Ret0);
    m.Register("libkernl.a", "StartTimerSystemTime", &Ret0);
}

void KernlTimer::SetClock(Clock clock, WaitHook wait) {
    clock_ = std::move(clock);
    wait_ = std::move(wait);
}

bool KernlTimer::HasClock() { return static_cast<bool>(clock_); }

uint32_t KernlTimer::TargetTicks(float seconds, uint32_t tick_rate) {
    const float t = seconds * static_cast<float>(tick_rate);
    if (t <= 0.0f) return 0;
    if (t >= 4294967040.0f) return 0xFFFFFFFFu;
    return static_cast<uint32_t>(t);
}

void KernlTimer::TimerWaitMicroseconds(CpuContext& ctx) {
    if (!clock_) GuestPark::Forever("tick_clock_absent", ctx.cpuRegs.pc);
    const uint32_t obj = ctx.cpuRegs.GPR.r[4].UL[0];
    float seconds;
    std::memcpy(&seconds, &ctx.fpuRegs.fpr[12].UL, sizeof(seconds));
    const uint32_t tick_rate = memory::read<uint32_t>(obj + 0x70);
    const uint64_t deadline = clock_() + TargetTicks(seconds, tick_rate);
    last_deadline_ = deadline;
    while (clock_() < deadline) {
        if (wait_) wait_(deadline);
        else GuestPark::Forever("tick_clock_stalled", ctx.cpuRegs.pc);
    }
    ctx.cpuRegs.GPR.r[2].UL[0] = 1;  // wait completed
}

void KernlTimer::ResetForTest() {
    clock_ = nullptr;
    wait_ = nullptr;
    last_deadline_ = 0;
}

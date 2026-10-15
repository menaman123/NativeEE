#pragma once
// KernlTimer: the timer-family bindings and the TimerWaitMicroseconds seam.
//   hle_TimerWaitMicroseconds(obj, f12): the game's busy-wait on T0_COUNT for
//   f12 * tick_rate ticks. In NEW it lives in the TICK domain: the wait ends
//   when the emu clock has advanced past the deadline. Until the timing layer
//   (3D1 DerivedClocks) installs a clock, the body PARKS with
//   `park=tick_clock_absent` -- a boot leg that reaches it before 3D1 is a
//   finding, never a sleep. With a clock installed: deadline = now + ticks;
//   the body returns 1 (wait completed) once the clock reads >= deadline,
//   yielding through the installed wait hook between reads.
#include <cstdint>
#include <functional>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

class KernlTimer {
public:
    using Clock = std::function<uint64_t()>;      // T0-domain count (3D1 installs)
    using WaitHook = std::function<void(uint64_t deadline)>;  // block until the clock may advance

    static void Register(SdkBindingManifest& m);
    static void SetClock(Clock clock, WaitHook wait);
    static bool HasClock();

    // The seam body (row 18).
    static void TimerWaitMicroseconds(CpuContext& ctx);
    // Pure: ticks the SDK body would wait for (int)(f12 * tick_rate).
    static uint32_t TargetTicks(float seconds, uint32_t tick_rate);
    static uint64_t LastDeadline() { return last_deadline_; }
    static void ResetForTest();

private:
    static Clock clock_;
    static WaitHook wait_;
    static uint64_t last_deadline_;
};

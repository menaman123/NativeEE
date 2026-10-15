#pragma once
// DerivedClocks: every game-observable clock as a PURE FUNCTION of the
// EmuDomain tick (plus fixed configuration). No accessor reads a host clock;
// two runs with the same boundary/tick sequence read identical values.
//
// The nine clocks (ARCH A4 5.1), each with its OLD source and the domain it
// now derives from:
//  (1) EE timers T0-T3 COUNT   -- emu ticks x counts-per-tick[CLKS]
//      (BUSCLK 147.456 MHz: /1 = 2457600, /16 = 153600, /256 = 9600 counts per
//      60 Hz field; CLKS=3 HBLANK = 262 lines/field). OLD ee_timers.cpp
//      synthesised the count from the SyncV count (9600/tick) plus a WALL
//      sub-tick interpolation; the wall term does not exist here (the
//      profile's t0_tickpaced pin is the only shape). Register file, 16-bit
//      window, compare/overflow are 3D2's EeTimers, reading TimerCounts().
//  (2) VBlank tick             -- the emu tick itself (EE-synchronous; OLD
//      ps2_vblank.cpp's wall worker is the play pacer of TickSource, never a
//      value source).
//  (3) frame boundary / SyncV field parity -- boundary count from EmuDomain
//      (OLD gs_hle.cpp g_sceGsSyncV_count); FIELD = parity of the emu tick.
//  (4) frame-budget anchor     -- emu-domain T0 counts at the last boundary
//      (OLD main.cpp 0x191bd0 hook's frame-top snapshot; the title supplies
//      the anchor VALUE the game stored, this clock supplies the domain).
//  (5) COP0 Count              -- emu ticks x EE core counts per field
//      (EE core 294.912 MHz = 2 x BUSCLK: 4915200 per 60 Hz field); the mtc0
//      reseat offset belongs to SyscallTable::HandleMtc0 (OLD syscalls.cpp).
//  (6) RTC                     -- fixed 2005-06-15 12:00:00 UTC when
//      game.rtc_fixed (deterministic profile); otherwise that epoch advanced
//      by emu ticks / 60 s -- still the emu domain, never the host date
//      (OLD sce_hle sceCdReadClock + PS2_RTC_FIXED).
//  (7) IOP heartbeat           -- one beat per round(usec / 16667) emu ticks
//      (OLD turbo.h PS2_HB_TICKCLOCK re-clocking of STREAM.IRX's wall beat).
//  (8) CDVD latency / refill epoch -- the emu tick, metered at 256 KB per tick
//      (OLD ps2_vblank.h delivery meter + PS2_CDVD_BUDGET_KB=256).
//  (9) audio                   -- the DEVICE clock is the rate master and is
//      NOT derived here: it never enters the emu domain and no emu value is
//      stretched to it (OLD mixer rule; Phase 7 owns the device). Recorded by
//      AudioRateMasterIsDevice() so the rule is visible where the other eight
//      live.
#include <cstdint>
#include <functional>

#include "engine/ee/timing/emu_domain.h"

class SyscallTable;

class DerivedClocks {
public:
    // BUSCLK-derived counts per 60 Hz field by T*_MODE.CLKS (hardware facts).
    static constexpr uint64_t kCountsPerTick[4] = {2457600ull, 153600ull, 9600ull, 262ull};
    static constexpr uint64_t kCop0CountsPerTick = 4915200ull;  // EE core 294.912 MHz / 60
    static constexpr uint32_t kFieldUs = 16667;                  // NTSC field period, us
    static constexpr uint32_t kCdvdKbPerTick = 256;
    // 2005-06-15 12:00:00 UTC as seconds since 1970-01-01 (the fixed RTC).
    static constexpr uint64_t kRtcFixedEpochS = 1118836800ull;

    struct RtcTime {
        int year, month, day, hour, minute, second;
    };

    explicit DerivedClocks(const EmuDomain& domain) : domain_(domain) {}

    void SetRtcFixed(bool fixed) { rtc_fixed_ = fixed; }
    bool RtcFixed() const { return rtc_fixed_; }

    // (1) 64-bit unwrapped timer count for a CLKS selector. Domain: emu ticks.
    uint64_t TimerCounts(uint32_t clks) const;
    uint64_t T0Counts() const { return TimerCounts(2); }
    // (2) Domain: emu ticks.
    uint64_t VblankCount() const { return domain_.DomainTick(); }
    // (3) Domain: boundary count / emu tick parity.
    uint64_t FrameBoundaryCount() const { return domain_.BoundaryCount(); }
    uint32_t SyncVFieldParity() const { return static_cast<uint32_t>(domain_.DomainTick() & 1u); }
    // (4) Domain: emu T0 counts at the last boundary.
    uint64_t BudgetAnchorCounts() const { return domain_.LastBoundaryTick() * kCountsPerTick[2]; }
    // (5) Domain: emu ticks x EE core counts per field (free-run model).
    uint64_t Cop0Count() const { return domain_.DomainTick() * kCop0CountsPerTick; }
    // (6) Domain: fixed epoch (+ emu seconds when not pinned).
    uint64_t RtcEpochSeconds() const;
    RtcTime Rtc() const;
    static RtcTime EpochToCivil(uint64_t seconds);
    // (7) Domain: emu ticks / round(usec / 16667).
    static uint32_t HeartbeatTicksFor(uint32_t usec);
    uint64_t IopHeartbeatBeats(uint32_t usec) const;
    // (8) Domain: emu ticks (256 KB refill budget per tick).
    uint64_t CdvdEpochTick() const { return domain_.DomainTick(); }
    uint64_t CdvdBudgetKbSince(uint64_t epoch_tick) const;
    // (9) Not derived: the device clock is the rate master.
    static constexpr bool AudioRateMasterIsDevice() { return true; }

    // Installs the kernel consumers: KernlTimer::SetClock (T0 counts + the
    // boundary wait hook the caller supplies) and SyscallTable::SetTickClock
    // (alarms count emu ticks).
    // LIFETIME: the installed closures capture `this` (and this object holds a
    // reference to its EmuDomain). Both must outlive every consumer call --
    // the composition root owns them for the whole run; a test that installs
    // from a stack object resets the consumers (KernlTimer::ResetForTest(),
    // SetTickClock(nullptr)) before the object dies.
    void InstallKernelClocks(SyscallTable& table, std::function<void(uint64_t)> wait) const;

private:
    const EmuDomain& domain_;
    bool rtc_fixed_ = true;
};

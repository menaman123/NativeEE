#pragma once
// EeTimers: the four EE hardware timers T0-T3 as MMIO registers
// (0x10000000 + n*0x800 + {0x00 COUNT, 0x10 MODE, 0x20 COMP, 0x30 HOLD}) on
// the MmioRouter, counting in the EMU TICK DOMAIN: every COUNT is a pure
// function of DerivedClocks (emu ticks x counts-per-tick[CLKS]; no wall term
// -- BUG-014/031 pinned the sub-tick wall interpolation as a defect source).
//
// Model: a timer settles lazily -- baseline_tick/count_base plus dticks x
// counts-per-tick -- at every register access and at the boundary pump:
//  * CUE clear: the count holds (baseline follows time, count does not);
//  * ZRET: the count returns to 0 on reaching COMP (period = COMP, or 0x10000
//    when COMP is 0); each period latches EQUF and, with CMPE, one event;
//  * otherwise a free-running 16-bit counter: each wrap latches OVFF and, with
//    OVFE, one overflow event; each pass over COMP latches EQUF and, with
//    CMPE, one compare event;
//  * COUNT write rebases; MODE write settles first (a divisor/CUE change never
//    re-scales elapsed time), then applies TimerCodec::ApplyModeWrite.
// Events dispatch on a guest EE thread only: PumpAtBoundary (boundary step 2,
// after the tick advance) and PumpAtCountRead (a COUNT read with events
// pending runs the ISR first so a reader's (acc << 16) | COUNT clock never
// regresses across a wrap -- on hardware the ISR fires within microseconds of
// the wrap). Each event: re-latch the flag, Intc::Raise(TIMn), Intc::Dispatch
// (STAT & MASK gated), at most kMaxEventsPerPump per timer per pump.
#include <cstdint>
#include <functional>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/intc/timer_codec.h"
#include "engine/ee/mem/mmio_handler.h"

class DerivedClocks;
class Intc;
class MmioRouter;

class EeTimers : public MmioHandler {
public:
    static constexpr uint32_t kBase = 0x10000000u;
    static constexpr uint32_t kSize = 0x2000u;
    static constexpr int kMaxEventsPerPump = 128;
    // The calling guest thread's context for the COUNT-read pump (nullptr =
    // not a guest thread: the read returns without running the ISR).
    using CtxProvider = std::function<CpuContext*()>;

    EeTimers(const DerivedClocks& clocks, Intc& intc);

    void Register(MmioRouter& router);
    void SetCtxProvider(CtxProvider fn) { ctx_provider_ = std::move(fn); }

    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    // Settle all four timers to the current emu tick (latches flags/events).
    void Advance();
    bool HasDispatchable() const { return dispatchable_ != 0; }
    // Boundary step 2: settle, then dispatch every pending enabled event.
    int PumpAtBoundary(CpuContext& ctx);
    int PumpAtCountRead();

    const TimerCodec::Regs& Regs(int i) const { return t_[i & 3]; }
    void Snapshot(TimerCodec::Regs out[4]);
    // Restore rebases every baseline to the current tick (deltas from now).
    void Restore(const TimerCodec::Regs in[4]);
    // A state file without TIMR: all four timers reset, T0_MODE = the title's
    // boot-time configuration (its EQUF/OVFF latches cleared), every baseline
    // at the current tick -- the counters restart from 0 as after the game's
    // own boot init; the game-visible accumulator is guest RAM (rides RAM0).
    void SeedBootDefault(uint16_t t0_mode);
    uint64_t DispatchCount() const { return dispatch_count_; }
    void ResetForTest();

private:
    static int IndexOf(uint32_t addr) { return static_cast<int>((addr >> 11) & 3u); }
    uint64_t NowTick() const;
    void AdvanceTo(uint64_t now_tick);
    void RecomputeDispatchable();
    int Pump(CpuContext& ctx);

    const DerivedClocks& clocks_;
    Intc& intc_;
    CtxProvider ctx_provider_;
    TimerCodec::Regs t_[4];
    uint32_t dispatchable_ = 0;
    bool in_dispatch_ = false;
    uint64_t dispatch_count_ = 0;
};

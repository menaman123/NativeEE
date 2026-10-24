#include "engine/ee/intc/ee_timers.h"

#include "engine/ee/intc/intc.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/timing/derived_clocks.h"

EeTimers::EeTimers(const DerivedClocks& clocks, Intc& intc) : clocks_(clocks), intc_(intc) {}

void EeTimers::Register(MmioRouter& router) { router.Register(kBase, kSize, *this, "EE timers"); }

uint64_t EeTimers::NowTick() const { return clocks_.VblankCount(); }

void EeTimers::AdvanceTo(uint64_t now_tick) {
    for (auto& t : t_) {
        if (now_tick <= t.baseline_tick) continue;
        if (!(t.mode & TimerCodec::kModeCue)) {  // counting disabled: hold the value
            t.baseline_tick = now_tick;
            continue;
        }
        const uint64_t dticks = now_tick - t.baseline_tick;
        const uint64_t raw = t.count_base + dticks * DerivedClocks::kCountsPerTick[TimerCodec::Clks(t.mode)];
        if (t.mode & TimerCodec::kModeZret) {
            const uint64_t period = t.comp ? t.comp : 0x10000ull;
            const uint64_t cycles = raw / period;
            if (cycles) {
                t.mode |= TimerCodec::kModeEquf;
                if (t.mode & TimerCodec::kModeCmpe) t.pending_cmp += cycles;
            }
            t.count_base = static_cast<uint32_t>(raw % period);
        } else {
            const uint64_t wraps = raw >> 16;
            if (wraps) {
                t.mode |= TimerCodec::kModeOvff;
                if (t.mode & TimerCodec::kModeOvfe) t.pending_ovf += wraps;
            }
            if (t.mode & (TimerCodec::kModeCmpe | TimerCodec::kModeEquf)) {
                // Compare crossings: the first needs the count to pass COMP
                // ahead of count_base; every further 0x10000 is one more.
                const uint64_t c = t.comp;
                const uint64_t first = c > t.count_base ? c - t.count_base : c + 0x10000ull - t.count_base;
                if (raw >= t.count_base + first) {
                    t.mode |= TimerCodec::kModeEquf;
                    if (t.mode & TimerCodec::kModeCmpe) t.pending_cmp += 1 + ((raw - t.count_base - first) >> 16);
                }
            }
            t.count_base = static_cast<uint32_t>(raw & 0xFFFFu);
        }
        t.baseline_tick = now_tick;
    }
    RecomputeDispatchable();
}

void EeTimers::RecomputeDispatchable() {
    uint32_t d = 0;
    for (int i = 0; i < 4; ++i) {
        const auto& t = t_[i];
        if ((t.pending_ovf && (t.mode & TimerCodec::kModeOvfe)) || (t.pending_cmp && (t.mode & TimerCodec::kModeCmpe)))
            d |= 1u << i;
    }
    dispatchable_ = d;
}

void EeTimers::Advance() { AdvanceTo(NowTick()); }

uint64_t EeTimers::Read(uint32_t addr, unsigned) {
    const int i = IndexOf(addr);
    if ((addr & 0x30u) == 0x00u) PumpAtCountRead();  // ISR before the reader sees the count
    AdvanceTo(NowTick());
    switch (addr & 0x30u) {
        case 0x00: return t_[i].count_base & 0xFFFFu;
        case 0x10: return t_[i].mode;
        case 0x20: return t_[i].comp;
        case 0x30: return t_[i].hold;
        default: return 0;
    }
}

void EeTimers::Write(uint32_t addr, unsigned, uint64_t value) {
    const int i = IndexOf(addr);
    const uint64_t now = NowTick();
    AdvanceTo(now);  // settle BEFORE the change: elapsed time is never re-scaled
    auto& t = t_[i];
    const uint32_t v = static_cast<uint32_t>(value);
    switch (addr & 0x30u) {
        case 0x00:
            t.count_base = v & 0xFFFFu;
            t.baseline_tick = now;
            break;
        case 0x10: t.mode = TimerCodec::ApplyModeWrite(t.mode, v); break;
        case 0x20: t.comp = static_cast<uint16_t>(v); break;
        case 0x30: t.hold = static_cast<uint16_t>(v); break;
        default: break;
    }
    RecomputeDispatchable();
}

int EeTimers::Pump(CpuContext& ctx) {
    int fired = 0;
    for (int i = 0; i < 4; ++i) {
        for (int guard = 0; guard < kMaxEventsPerPump; ++guard) {
            auto& t = t_[i];
            if (t.pending_ovf && (t.mode & TimerCodec::kModeOvfe)) {
                --t.pending_ovf;
                t.mode |= TimerCodec::kModeOvff;  // the ISR tests the flag
            } else if (t.pending_cmp && (t.mode & TimerCodec::kModeCmpe)) {
                --t.pending_cmp;
                t.mode |= TimerCodec::kModeEquf;
            } else {
                break;
            }
            RecomputeDispatchable();
            intc_.Raise(Intc::kTim0 + i);
            in_dispatch_ = true;
            intc_.Dispatch(ctx, Intc::kTim0 + i, Intc::kSiteTimerPump);  // STAT & MASK gated
            in_dispatch_ = false;
            ++dispatch_count_;
            ++fired;
        }
    }
    return fired;
}

int EeTimers::PumpAtBoundary(CpuContext& ctx) {
    if (in_dispatch_) return 0;
    AdvanceTo(NowTick());
    if (!HasDispatchable()) return 0;
    return Pump(ctx);
}

int EeTimers::PumpAtCountRead() {
    if (in_dispatch_ || !ctx_provider_) return 0;  // the ISR's own reads; non-guest threads
    AdvanceTo(NowTick());
    if (!HasDispatchable()) return 0;
    CpuContext* ctx = ctx_provider_();
    if (!ctx) return 0;
    return Pump(*ctx);
}

void EeTimers::Snapshot(TimerCodec::Regs out[4]) {
    AdvanceTo(NowTick());
    for (int i = 0; i < 4; ++i) out[i] = t_[i];
}

void EeTimers::Restore(const TimerCodec::Regs in[4]) {
    const uint64_t now = NowTick();
    for (int i = 0; i < 4; ++i) {
        t_[i] = in[i];
        t_[i].baseline_tick = now;  // Snapshot settled at its baseline: lossless rebase
    }
    RecomputeDispatchable();
}

void EeTimers::SeedBootDefault(uint16_t t0_mode) {
    const uint64_t now = NowTick();
    for (auto& t : t_) {
        t = TimerCodec::Regs{};
        t.baseline_tick = now;
    }
    t_[0].mode = static_cast<uint16_t>(t0_mode & ~TimerCodec::kModeFlags);
    RecomputeDispatchable();
}

void EeTimers::ResetForTest() {
    for (auto& t : t_) t = TimerCodec::Regs{};
    dispatchable_ = 0;
    in_dispatch_ = false;
    dispatch_count_ = 0;
}

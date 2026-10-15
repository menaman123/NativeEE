#include "engine/ee/hle/dyn_interp.h"

#include <atomic>

#include "engine/core/log/ring_log.h"
#include "engine/ee/cpu/guest_park.h"

namespace {
std::atomic<uint32_t> g_trips{0};
std::atomic<uint32_t> g_last_pc{0};
}  // namespace

void DynInterp::Trip(uint32_t pc, CpuContext& ctx) {
    g_trips.fetch_add(1, std::memory_order_relaxed);
    g_last_pc.store(pc, std::memory_order_relaxed);
    ps2r::ringlog::RingLog::EmitV(ps2r::ringlog::CH_EE, ps2r::ringlog::TAG_INDIRECT_JUMP, pc,
                                  ctx.cpuRegs.GPR.r[31].UL[0]);
    ctx.cpuRegs.pc = pc;
    GuestPark::Forever("unrecompiled_pc", pc);
}

uint32_t DynInterp::TripCount() { return g_trips.load(std::memory_order_relaxed); }
uint32_t DynInterp::LastPc() { return g_last_pc.load(std::memory_order_relaxed); }

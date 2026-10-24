// engine/iop/hle/intrman/intrman_module.cpp -- IntrmanModule bodies.
#include "engine/iop/hle/intrman/intrman_module.h"

#include "engine/iop/iop_error.h"

namespace ps2r::iop::hle {

void IntrmanModule::CpuSuspendIntr(IopContext& ctx) {
    const bool previous = enabled_.exchange(false, std::memory_order_relaxed);
    if (ctx.gpr[4] != 0) mem_.Write<uint32_t>(ctx.gpr[4], previous ? 1 : 0);
    suspends_.fetch_add(1, std::memory_order_relaxed);
    if (!previous) nested_.fetch_add(1, std::memory_order_relaxed);
    ctx.gpr[2] = static_cast<uint32_t>(previous ? IopError::Ok : IopError::CpuDisabled);
}

void IntrmanModule::CpuResumeIntr(IopContext& ctx) {
    enabled_.store(ctx.gpr[4] != 0, std::memory_order_relaxed);
    resumes_.fetch_add(1, std::memory_order_relaxed);
    ctx.gpr[2] = 0;
}

void IntrmanModule::CpuDisableIntr(IopContext& ctx) { ctx.gpr[2] = 0; }

void IntrmanModule::CpuEnableIntr(IopContext& ctx) { ctx.gpr[2] = 0; }

}  // namespace ps2r::iop::hle

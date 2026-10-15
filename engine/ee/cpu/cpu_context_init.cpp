#include "engine/ee/cpu/cpu_context_init.h"

#include <cstring>

namespace {
constexpr int kRegSp = 29;
}

void CpuContextInit::Reset(CpuContext& ctx, u32 entry_pc, u32 stack_top) {
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.cpuRegs.pc = entry_pc;
    ctx.cpuRegs.GPR.r[kRegSp].UD[0] = stack_top;
    ctx.vuRegs.Reset();
}

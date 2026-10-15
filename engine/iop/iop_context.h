// engine/iop/iop_context.h -- the R3000A (MIPS I) register file the recompiled
// IOP module code runs on: 32 general registers ($0 hardwired to zero), HI/LO,
// PC. No FPU, no COP2, 32-bit only. Results travel in $v0 (gpr[2]); arguments
// in $a0..$a3 (gpr[4..7]); the stack pointer is $sp (gpr[29]), $gp is gpr[28].
#pragma once

#include <cstdint>

namespace ps2r::iop {

struct IopContext {
    uint32_t gpr[32] = {};
    uint32_t hi = 0;
    uint32_t lo = 0;
    uint32_t pc = 0;

    // Register $0 is hardwired to zero: called after any GPR write.
    void FixZero() { gpr[0] = 0; }
    // Fresh context for a thread/callback entry: $sp and $gp set, everything else 0.
    static IopContext ForEntry(uint32_t sp, uint32_t gp);
};

}  // namespace ps2r::iop

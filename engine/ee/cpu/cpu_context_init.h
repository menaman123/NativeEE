#pragma once
// Reset of a CpuContext to the state a freshly started guest thread sees:
// all registers zero, pc = entry, $sp = stack top, VU0 macro block at its
// power-on state (VF0.w = 1, Q = 1).
#include "engine/ee/cpu/cpu_state.h"

class CpuContextInit {
public:
    static void Reset(CpuContext& ctx, u32 entry_pc, u32 stack_top);
};

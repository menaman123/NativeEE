#pragma once
// DynInterp: the tripwire for a jump into code the recompiler did not emit.
// OLD fell back to a rabbitizer-driven interpreter (100k-instruction cap); in
// NEW a boot leg that needs it is a construction bug, so the Phase-3 form
// records the target, reports through the crash ring and PARKS
// (`park=unrecompiled_pc`). There is no interpreter body here.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class DynInterp {
public:
    [[noreturn]] static void Trip(uint32_t pc, CpuContext& ctx);
    static uint32_t TripCount();
    static uint32_t LastPc();
};

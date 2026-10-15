#pragma once
// Generated-code ABI forwarder (row 27): the two vector-unit objects the
// VCALLMS/VCALLMSR sites and the VU1 window drive. Phase-3 form: both park
// (Phase 5 replaces the definitions with the real units).
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class Vu0Macro {
public:
    void Execute(CpuContext& ctx, uint32_t start_addr);
    void ResumeIfWaiting(CpuContext& ctx);
};

class Vu1Window {
public:
    void Execute(CpuContext& ctx, uint32_t start_addr);
};

extern Vu0Macro g_vu0;
extern Vu1Window g_vu1;

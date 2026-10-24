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

// Macro-mode FMAC flag commit (the generated COP2 arithmetic calls it after
// every flag-setting op: VADD/VSUB/VMUL/VMADD/VMSUB/VOPMULA/VOPMSUB and their
// broadcast / accumulator / I / Q forms). PS2 rule: the MAC flag register
// holds the Z/S/U/O nibbles of the lanes the dest mask selects (the other
// lanes read 0); STATUS Z/S/U/O (bits 0-3) are rewritten from them and the
// sticky twins (bits 6-9) OR in; D/I (the FDIV unit's) are untouched. MAX /
// MINI / MOVE / MR32 / ABS / FTOI / ITOF leave both registers alone.
// `result` = the four computed lanes (before the mask), `mask` = dest xyzw
// as bits 8/4/2/1. Defined in engine/vu/macro/vu_macro_bridge.cpp.
void VuMacroCommitFlags(VUregs& regs, const VR_reg& result, uint8_t mask);

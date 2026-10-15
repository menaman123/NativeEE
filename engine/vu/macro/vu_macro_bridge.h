#pragma once
// VuMacroBridge: the hand-over between the EE's COP2 register block and the
// two VuUnit objects behind the generated code's VCALLMS / VCALLMSR / CMSAR1
// sites (the Vu0Macro / Vu1Window seam classes of the recomp ABI).
//
// PS2 rule: in macro mode the EE's COP2 registers ARE VU0's register file.
// VCALLMS imm15 / VCALLMSR (CMSAR0) start the VU0 microprogram at imm15 * 8
// synchronously with the EE; the program runs to its E bit (or parks in a
// CTC2-gated self-spin until the EE releases it) and the EE then reads the
// results from its COP2 registers. The unit owns the persistent VU0 state
// the register block does not carry -- the flag pipeline, the clock, the Q/P
// latches, both memories -- so the bridge copies the 608-byte `VUregs` block
// in before a run and back out after it, nothing more. VU1 owns its own
// register file (the EE only starts it); no copy on that path.
//
// Process-wide binding by the composition root (one EE, two units). Unbound,
// the seam bodies park exactly as the Phase-3 seams did (`vu_micro_absent`),
// so an unwired binary still attributes its leg from run.meta alone.
//
// Cross-thread contract: called from the guest thread that executes the EE
// instruction; the bound units are the EE-side (inline) units.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class VuUnit;
class Vu0Registry;
class Vu0Trace;

class VuMacroBridge {
public:
    struct Census {
        uint64_t vu0_calls = 0;         // VCALLMS / VCALLMSR entries run
        uint64_t vu0_park_resumes = 0;  // ResumeIfWaiting calls that found a parked VU0
        uint64_t vu1_kicks = 0;         // CMSAR1 starts run
    };

    // `census` (optional) receives one NoteEntry per VU0 entry; its notes are logged.
    static void Bind(VuUnit* vu0, VuUnit* vu1, Vu0Registry* census = nullptr);
    // `trace` (optional, diagnostics) records the COP2 block before and after
    // every VU0 run of an armed micro pc; null = off (the default).
    static void SetTrace(Vu0Trace* trace);
    static void Unbind();
    static bool Bound();

    // The three seam bodies forward here.
    static void ExecuteVu0(CpuContext& ctx, uint32_t start_addr);
    static void ResumeVu0IfWaiting(CpuContext& ctx);
    static void ExecuteVu1(CpuContext& ctx, uint32_t start_addr);

    static const Census& Stats();
    static void ResetStats();

private:
    static void NoteVu0Entry(uint32_t start_addr);
};

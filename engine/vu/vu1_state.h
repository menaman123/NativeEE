#pragma once
// The register file + memories of one vector unit, as a plain record.
//
// PS2 rule: a VU owns a micro-instruction memory and a data memory (VU1: 16 KB
// each; VU0: 4 KB each) addressed modulo their size, plus the COP2-visible
// register block (VF0..31, VI0..15, ACC, Q, P, I, R, the flag and control
// registers). The register block is the frozen `VUregs` POD of the EE
// context, used verbatim so a VU1 trace case and the savestate sections copy
// it raw. Zero primary types (PODs only).
#include <cstddef>
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

struct Vu1State {
    static constexpr uint32_t kMemSize = 16384;     // VU1 micro / data memory bytes
    static constexpr uint32_t kVu1MemMask = 0x3FFF;  // VU1: 16 KB, byte addresses wrap here
    static constexpr uint32_t kVu0MemMask = 0x0FFF;  // VU0: 4 KB, the low quarter of the arrays

    VUregs regs;                       // 608 bytes, 16-aligned
    alignas(16) uint8_t micro[kMemSize];  // LQ/SQ move whole quadwords: keep both arrays 16-aligned
    alignas(16) uint8_t data[kMemSize];

    // Every micro/data address the unit forms is masked with this before use
    // (VU1 0x3FFF, VU0 0x0FFF); the VU0 instance simply never reaches the
    // upper 12 KB of the arrays.
    uint32_t mem_mask = kVu1MemMask;

    // Per-run scratch (reset at the start of every Execute).
    bool suspended = false;           // VU0: parked in a CTC2-gated spin until the EE releases it
    uint32_t suspended_tpc = 0;       // the bundle the parked VU0 resumes at
    int xtop_read_count = 0;          // XTOP reads since the run started / the last XGKICK (yield rule)
    bool xtop_exit_requested = false; // the XTOP yield fired: end the run at the next bundle boundary
    bool xgkick_has_fired = false;    // an XGKICK was issued during this run
    int xgkick_count = 0;             // XGKICKs issued during this run
    uint32_t cur_bundle_pc = 0;       // byte address of the bundle being executed

    // Bumped on every micro-memory store (VIF MPG, the EE window); the cached
    // tier and the HLE runtime compare it against their decoded image epoch.
    uint64_t micro_generation = 0;
};

// VU0 shares the record: the unit constructs it with `mem_mask = kVu0MemMask`
// and only the low 4 KB of `micro` / `data` are reachable.
using Vu0State = Vu1State;

static_assert(sizeof(VUregs) == 608, "VUregs is the frozen EE register block");
static_assert(offsetof(Vu1State, micro) % 16 == 0 && offsetof(Vu1State, data) % 16 == 0,
              "VU memories are quadword-aligned");

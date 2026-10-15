#pragma once
//
// functions.h — RECOMP v2 discovery: multi-source function discovery +
// extents (P1 STUB), and the whole-pipeline entry point.
//
// [RECOMP-P1] Spec (RECOMP_P0_DESIGN.md §7.3): union of seeds
//   1. ELF entry point.
//   2. All jal targets.
//   3. Prologue patterns after function ends — SEED-ONLY, never authoritative
//      (PCSX2 issue #4024 lesson, concept only; fixture ST-06).
//   4. Data-section pointer scan: aligned 32-bit words in .data/.rodata whose
//      value lands 4-aligned inside .text (vtable/callback candidates;
//      fixture ST-04).
//   5. External jump-table targets (fixture ST-05).
// Extents: sort starts; a function runs to the next start; recursive descent
// trims/extends to the reachable-code closure. A "function" reached only by
// fallthrough from a predecessor that flows past its start is a shared-tail
// label emitted as both (folds_successor; fixtures ST-02/ST-03,
// FUN_001bb478/FUN_001cc8e0). Unreached gaps = padding/data; never
// translated, always logged (fixture ST-07).
//
// Delay-slot rule: an address that is the delay slot of the preceding
// branch/jump is never promoted from a weak seed (3/4/6) — the slot executes
// with its transfer on both paths, so a function cannot begin there (a
// prologue `addiu sp,sp,-N` in the slot of an entry's guard branch would run
// twice on the fall-through path). Strong seeds there are kept and reported.
// Every address flow can enter from outside a function (branch fall-through,
// jal/jalr return site, external jump-table target) is a leader of the
// function that CONTAINS it, so the emitted `switch (addr)` has its case.
//
// The four-source seed union follows the angr CFGFast documentation model
// (ideas only). Iterated with leaders + tables to a fixed point.
//
// P1 stub behavior: discover_program returns an empty DiscoveryResult with
// engine_ran == false. Every discovery test in tests/recompiler_v2/ is RED
// against this stub by construction.
//

#include "discovery_types.h"

namespace recomp_v2 {

// Run the full v2 discovery pipeline (seeds -> leaders -> tables -> extents,
// iterated to fixed point) over the inputs.
//
// P1: STUB — returns {} with engine_ran=false.
DiscoveryResult discover_program(const DiscoveryInputs& in,
                                 const DiscoveryOptions& opts = {});

} // namespace recomp_v2

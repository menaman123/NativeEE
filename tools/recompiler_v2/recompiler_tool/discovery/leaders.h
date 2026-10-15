#pragma once
//
// leaders.h — RECOMP v2 discovery: complete block-leader analysis (P1 STUB).
//
// [RECOMP-P1] Spec (RECOMP_P0_DESIGN.md §7.1): fixed-point worklist over
// .text. Leaders are: function entries; targets of every branch/jump
// (beq/bne/blez/bgtz/likely/REGIMM/j/jal-return-sites); the fall-through
// successor of every branch delay slot; every recovered jump-table target.
// Every leader inside a function becomes an emitted label AND an entry in the
// function's `switch (addr)` mid-entry dispatch, which makes the class-A
// failure (find_containing_function re-entry on an internal target,
// CONVENTIONS D2) structurally impossible.
//
// Delay-slot overlap (a branch target that IS another branch's delay slot)
// is legal MIPS and must produce a leader "between" the fused pair
// (fixture DS-05; the FUN_00201428 loop archetype).
//
// Concept provenance: the multi-pass/fixed-point recovery approach follows
// the angr CFGFast documentation (ideas only; BSD-2-Clause project docs).
//
// P1 stub behavior: returns an empty result (engine_ran == false upstream).
//

#include "discovery_types.h"

namespace recomp_v2 {

struct LeaderAnalysis {
    std::set<uint32_t> leaders;              // program-wide leader set
    std::multimap<uint32_t, uint32_t> edges; // site -> target static edges
};

// Compute the program-wide leader set + static edge relation over `text`,
// given the current function-entry seed set (iterated to fixed point by
// discover_program as new entries/tables appear).
//
// P1: STUB — returns empty.
LeaderAnalysis analyze_leaders(const SectionImage& text,
                               const std::set<uint32_t>& function_entries);

} // namespace recomp_v2

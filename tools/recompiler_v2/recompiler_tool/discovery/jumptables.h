#pragma once
//
// jumptables.h — RECOMP v2 discovery: jump-table pattern library (P1 STUB).
//
// [RECOMP-P1] Spec (RECOMP_P0_DESIGN.md §7.2): a two-engine cross-check whose
// disagreement on any site is a hard build error with a dossier.
//
//   Engine 1 (forward): per-function register-state dataflow tracking
//     lui/addiu/addu/daddu/or-move/sll/dsll, WITH stack spill/reload tracking
//     (sw/lw to sp slots) so spilled table bases survive. A `jr` on a
//     "loaded from table T" register = table site.
//     Adapted (concept + algorithm shape) from N64Recomp, MIT license:
//     https://github.com/N64Recomp/N64Recomp (src/analysis.cpp).
//     Where code is adapted in P2 the MIT copyright notice is retained.
//
//   Engine 2 (backward): from each `jr rs` (rs != $ra), backward slice to the
//     feeding lw, the address sum, the scale, the base construction; bound
//     from the dominating sltiu/sltu guard searched across preceding blocks
//     (not a linear window); evaluate the address computation per index and
//     read each entry from the ELF data image.
//     Adapted from Ghidra (Apache-2.0), National Security Agency;
//     https://github.com/NationalSecurityAgency/ghidra
//     (Ghidra/Features/Decompiler/src/decompile/cpp/jumptable.cc — the
//     JumpBasic model: PathMeld slice, GuardRecord value ranges,
//     EmulateFunction per-index evaluation, sanityCheck). This attribution
//     comment satisfies CONVENTIONS.md rule P3 for the adaptation site.
//
//   Sanity (Ghidra concept): every target 4-aligned, inside .text, and (for
//     local tables) inside the candidate function extent; entries failing
//     >= half the table => reject; bound cap configurable
//     (DiscoveryOptions::table_bound_cap, default 1024; ELF max observed 663).
//
//   Classification per table: all-internal (local switch over real targets),
//     all-external (tail-call dispatch), MIXED (internal gotos + external
//     dispatcher tail calls) — fixture SW-05 covers the mixed case even
//     though this ELF measures zero of them.
//
// In-repo feasibility proof: enumerate_switch_goto_functions.py (:139-251),
// whose ELF-recovered tables the TODO doc calls ground truth.
//
// P1 stub behavior: returns no tables.
//

#include "discovery_types.h"

namespace recomp_v2 {

// Outcome of the two-engine cross-check for one jr site.
struct TableRecovery {
    bool is_table = false;      // false => genuine computed jump (fixture SW-11):
                                // the site keeps ONLY the runtime-dispatcher path.
    JumpTable table;            // valid iff is_table
    bool engines_agree = true;  // false => hard build error upstream (dossier)
    std::string dossier;        // human-readable disagreement report
    // [RECOMP-P2] The switch's default path (Ghidra GuardRecord concept): the
    // guard branch's out-of-range destination. Function-local flow by the
    // switch construction, so discover_program walks it as own-flow — this is
    // what brackets internal case blocks (SW-01) apart from tail-call
    // dispatch targets (ST-05/SW-04), whose default precedes the targets.
    bool has_guard_default = false;
    uint32_t guard_default = 0;
};

// Recover the jump table (if any) feeding the `jr` at site_pc inside the
// candidate function extent [func_entry, func_end).
//
// P1: STUB — always returns {is_table=false}.
TableRecovery recover_jump_table(const SectionImage& text,
                                 const std::vector<SectionImage>& data,
                                 uint32_t site_pc,
                                 uint32_t func_entry,
                                 uint32_t func_end,
                                 const DiscoveryOptions& opts);

// [RECOMP-P2b] Weak seed source 6 (RECOMP_P2_REVIEW.md required change #3):
// program-wide linear sweep for lui + addiu/daddiu constant pairs whose
// resolved value is a 4-aligned address inside .text (address-taken
// functions / callback registration — the FUN_002d3218 class). Reuses
// Engine 1's register-state constant-resolution rules (lui sets a base,
// addiu/daddiu derives base+simm, canonical moves copy, any other GPR def
// kills; control-flow instructions are passed over linearly, exactly as
// engine1_forward does, so the canonical `lui rX; jal F; addiu rX,rX,lo`
// delay-slot form is caught). Returns target -> constructing addiu site VAs.
// SEED-ONLY, never authoritative: functions.cpp absorbs candidates that any
// flow reaches and promotes only the unclaimed remainder (design §7.3).
std::map<uint32_t, std::vector<uint32_t>> scan_code_constructed_pointers(
    const SectionImage& text);

} // namespace recomp_v2

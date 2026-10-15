#pragma once
//
// discovery_types.h — RECOMP v2 engine, discovery data model (P1 STUB API).
//
// [RECOMP-P1] This header is the SPEC SURFACE the test-first suite under
// tests/recompiler_v2/ is written against (RECOMP_P0_DESIGN.md §6-§7,
// RECOMP_P0_FIXTURES.md classes (a)/(b)). In P1 the implementations behind it
// are STUBS that return empty results, so every discovery test runs RED by
// construction. P2 fills in the engine; the tests do not change.
//
// GAME-AGNOSTIC RULE (design §6): nothing in discovery/ may hardcode a
// the title address. Game specifics live in data/config only.
//
// Licensing note (CONVENTIONS.md P3): the algorithm design this API models
// adapts, at the concept level:
//   * N64Recomp (MIT) — forward register-state jump-table dataflow
//     (https://github.com/N64Recomp/N64Recomp, src/analysis.cpp). Where actual
//     code is adapted in P2, the MIT copyright notice is retained at the site.
//   * Ghidra (Apache-2.0) — backward-slice + guard-range + per-index
//     evaluation jump-table recovery model (JumpBasic; jumptable.cc).
//     Adapted algorithm sites in P2 carry this attribution comment:
//     "Adapted from Ghidra (Apache-2.0), NSA;
//      https://github.com/NationalSecurityAgency/ghidra".
//   * PCSX2 (GPL-3.0) — CONCEPTS ONLY from public docs/behavior; no GPL code
//     or text is present in this tree, in P1 or ever.
//

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace recomp_v2 {

// A contiguous byte image of one ELF section, addressed by EE virtual address.
// The discovery engine reads code from `.text` images and jump-table entries /
// pointer candidates from data images (.data/.rodata/.sdata).
struct SectionImage {
    std::string name;           // ".text", ".rodata", ...
    uint32_t base_va = 0;       // EE virtual address of byte 0
    std::vector<uint8_t> bytes; // section contents (little-endian words)

    bool contains(uint32_t va) const {
        return va >= base_va && va < base_va + static_cast<uint32_t>(bytes.size());
    }
    // Little-endian 32-bit word read; caller must ensure contains(va..va+3).
    uint32_t read_u32(uint32_t va) const {
        size_t off = va - base_va;
        return static_cast<uint32_t>(bytes[off]) |
               (static_cast<uint32_t>(bytes[off + 1]) << 8) |
               (static_cast<uint32_t>(bytes[off + 2]) << 16) |
               (static_cast<uint32_t>(bytes[off + 3]) << 24);
    }
};

// Everything discovery is allowed to look at (design §6 rule: discovery/ may
// read only elf/ outputs — in tests, fixtures build these directly).
struct DiscoveryInputs {
    SectionImage text;                     // executable EE code
    std::vector<SectionImage> data;        // .data/.rodata/.sdata for tables + pointer scan
    uint32_t elf_entry = 0;                // ELF entry point (seed 1)
};

struct DiscoveryOptions {
    uint32_t table_bound_cap = 1024;       // design §7.2 sanity cap (ELF max observed 663)
    bool enable_prologue_seeds = true;     // seed source 3 (seed-only, never authoritative)
    bool enable_data_pointer_seeds = true; // seed source 4 (vtables/callbacks)
};

// One recovered jump table at a `jr rs` (rs != $ra) site (design §7.2).
// Recovered by BOTH engines (forward N64Recomp-style dataflow + backward
// Ghidra-style slice) which must agree; targets are read from the ELF data
// image, never inferred from block metadata.
struct JumpTable {
    uint32_t site_pc = 0;                // VA of the jr instruction
    uint32_t table_base = 0;             // VA of entry 0 in the data image
    uint32_t bound = 0;                  // guard bound (entry count incl. default-fill)
    std::vector<uint32_t> targets;       // all entries, table order (size == bound)
    std::set<uint32_t> internal_targets; // distinct targets inside the owning function
    std::set<uint32_t> external_targets; // distinct targets that are function entries
};

// How a function entry was discovered (design §7.3 multi-source union).
enum class SeedSource : uint8_t {
    kElfEntry = 0,   // seed 1: ELF entry point
    kJalTarget,      // seed 2: jal target
    kProloguePattern,// seed 3: prologue scan (seed-only)
    kDataPointer,    // seed 4: aligned code pointer in a data section
    kTableExternal,  // seed 5: external jump-table target
    kCodePointer,    // seed 6 [RECOMP-P2b]: code-constructed pointer — a
                     // lui/addiu(/daddiu) constant pair in .text whose
                     // resolved value is a 4-aligned .text address (callback
                     // registration / address-taken functions). Weak seed,
                     // same absorb-or-promote rules as 3/4. Added per
                     // RECOMP_P2_REVIEW.md required change #3 (finding F1:
                     // the five largest true-gap misses, e.g. FUN_002d3218,
                     // are referenced ONLY this way).
};

struct DiscoveredFunction {
    uint32_t entry = 0;                  // first instruction VA
    uint32_t end = 0;                    // half-open extent [entry, end)
    std::set<uint32_t> leaders;          // every block leader in extent (incl. entry).
                                         // Spec (design §7.1): function entry, every
                                         // branch/jump target, the fall-through
                                         // successor of every branch delay slot,
                                         // every jump-table target.
    std::vector<JumpTable> tables;       // recovered jr tables owned by this function
    std::set<SeedSource> seeds;          // how the entry was found (>= 1 source)
    bool folds_successor = false;        // extent absorbs a fallthrough-only neighbor
};

// Full-program discovery result. All containers ordered (determinism rule P2).
struct DiscoveryResult {
    std::map<uint32_t, DiscoveredFunction> functions; // keyed by entry VA
    // Every static control-flow edge (site VA -> target VA) for branches,
    // jumps, and branch-fallthroughs. Jump-table edges are in tables[].
    std::multimap<uint32_t, uint32_t> edges;
    // Diagnostics: addresses in .text not reached by any function (padding/data).
    std::set<uint32_t> unreached_gaps;
    // Diagnostics [RECOMP-P2b]: every recovered jump table's data range
    // [base, end) — including pre-pass recoveries at never-reached sites.
    // Data-section words inside these ranges are TABLE ENTRIES, not
    // independent vtable pointers, and are excluded from the weak
    // data-pointer seed (the 0x21bc1c lesson). Exported so the comparison
    // report can state per-miss reasons definitively.
    std::map<uint32_t, uint32_t> table_data_ranges;
    // Diagnostics: weak seeds (prologue / data pointer / code pointer) whose
    // address is the delay slot of the preceding transfer instruction. A delay
    // slot executes WITH its branch/jump on both paths, so a function can never
    // begin there; these seeds are rejected (the code is reached through the
    // real entry's own seed, or stays a reported gap).
    std::set<uint32_t> delay_slot_weak_rejected;
    // Diagnostics: STRONG seeds (ELF entry / jal target / jump-table target)
    // that land in a delay slot. The hardware does start executing there when
    // called, so the entry is KEPT — but the shape is pathological (the
    // preceding function's branch would fuse another function's first word)
    // and is reported for a human.
    std::set<uint32_t> delay_slot_strong_entries;
    // True once a real engine ran (the P1 stub leaves this false).
    bool engine_ran = false;
};

} // namespace recomp_v2

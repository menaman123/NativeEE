#pragma once
//
// v2_emission.h — [RECOMP-P3a] v2 emission configuration consumed by the
// legacy Recompiler class (RECOMP_P0_DESIGN.md §6 emit/, §8 overrides).
//
// DESIGN (P3a decision, RECOMP_P3A_IMPL.md): translation semantics are reused
// by running the SAME Recompiler emission code for both engines. This struct
// carries the four v2-only deltas:
//   * pinned    — addresses forced to their LEGACY body (recomp_overrides.ini
//                 channel, design §8). A pinned function's Function object in
//                 m_functions is the Ghidra-derived one and the body-time
//                 special-case chain stays enabled for it.
//   * aliases   — legacy function entries ABSORBED inside a v2 extent
//                 (P2 comparison "absorbed" class). Emitted as forwarder
//                 bodies `FUN_X(ctx,addr){FUN_C(ctx, addr?addr:X);}` so the
//                 function-set contract (design §10: nothing lost) holds and
//                 inbound dispatch lands on the container's mid-entry label
//                 (X is injected as a leader of C — additive, label+case).
//   * tables    — per jr-site ELF-recovered target sets (CONVENTIONS D3):
//                 internal targets emit `case: goto`, external targets emit
//                 dense-id tail dispatch, default stays the runtime-dispatcher
//                 fallback (D4).
//   * extents   — v2 function extents for the D2 generation-time hard error
//                 (an internal literal reaching the fcf path aborts the
//                 build instead of emitting the class-A shape).
//
#include <cstdint>
#include <map>
#include <set>
#include <string>

struct V2JumpTableEmission {
    // Distinct table targets inside the owning function (goto cases; every
    // one is a discovery leader => a block label by construction).
    std::set<uint32_t> internal_targets;
    // Distinct table targets that are function entries (tail-call cases).
    std::set<uint32_t> external_targets;
};

struct V2EmissionConfig {
    bool enabled = false;
    // recomp_overrides.ini pins: emit the LEGACY body for these addresses.
    std::set<uint32_t> pinned;
    // Alias entries (absorbed legacy functions) -> container entry/name.
    std::set<uint32_t> aliases;
    std::map<uint32_t, uint32_t> alias_container;
    std::map<uint32_t, std::string> alias_container_name;
    // jr-site pc -> ELF-recovered emission target sets (CONVENTIONS D3).
    std::map<uint32_t, V2JumpTableEmission> tables;
    // v2 function entry -> extent end (half-open) for the D2 hard error.
    std::map<uint32_t, uint32_t> extents;
};

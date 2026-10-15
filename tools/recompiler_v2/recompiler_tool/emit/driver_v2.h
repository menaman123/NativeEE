#pragma once
//
// driver_v2.h — [RECOMP-P3a] builds the emission Function map + V2EmissionConfig
// from the approved discovery engine's result (RECOMP_P2B_IMPL.md: 8,819
// functions, 177 table sites, leader-complete).
//
#include "EEAnalyze/Function.h"
#include "discovery/discovery_types.h"
#include "emit/v2_emission.h"

#include <cstdint>
#include <map>
#include <set>

namespace recomp_v2_emit {

struct BuildResult {
    std::map<uint32_t, Function> functions; // what the Recompiler emits
    V2EmissionConfig config;                // the four v2 deltas
};

// Build the v2 emission inputs.
//   result / inputs      — the discovery engine's output over the ELF.
//   ghidra_functions     — OPTIONAL legacy-compat input (P3a: present, P4:
//                          empty). Supplies (a) alias/compat entries so the
//                          function-set gate loses nothing vs legacy, and
//                          (b) the Function objects for PINNED addresses so a
//                          pin reproduces the legacy body.
//   pinned               — resolved override pins (recomp_overrides.ini).
//   text_data/size/base  — the raw .text image (instruction decode).
BuildResult build_v2_functions(const recomp_v2::DiscoveryResult& result,
                               const recomp_v2::DiscoveryInputs& inputs,
                               const std::map<uint32_t, Function>& ghidra_functions,
                               const std::set<uint32_t>& pinned,
                               const uint8_t* text_data, uint32_t text_size,
                               uint32_t text_base);

} // namespace recomp_v2_emit

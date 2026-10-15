#pragma once
//
// report.h — RECOMP v2 discovery: report emission + Ghidra-diff mode (P1 STUB).
//
// [RECOMP-P1] Spec (RECOMP_P0_DESIGN.md §6, §7.3 seed 6, §10):
//   * write_discovery_report: deterministic JSON of the DiscoveryResult
//     (functions, extents, leaders count, tables, seeds). Consumed by the
//     G-FSET whole-program gate (function-set diff vs legacy: nothing lost)
//     and by the P2 comparison report.
//   * The Ghidra JSON is an OPTIONAL comparison input only (P2 emits
//     found/missed/extent-diff/table-diff vs Ghidra; P4 builds with the JSON
//     absent).
//
// Determinism (CONVENTIONS P2): ordered iteration only, no timestamps, no
// host paths, no pointer values.
//
// P1 stub behavior: writes a valid empty report ({"engine":"v2-stub",
// "engine_ran":false,"functions":[]}) so gate plumbing can run RED.
//

#include "discovery_types.h"

#include <string>

namespace recomp_v2 {

// Serialize `result` as deterministic JSON to `path`. Returns false on I/O
// failure. P1: writes the empty stub report.
bool write_discovery_report(const DiscoveryResult& result,
                            const std::string& path);

} // namespace recomp_v2

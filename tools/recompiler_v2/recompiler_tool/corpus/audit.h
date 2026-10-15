#pragma once
//
// audit.h — [RECOMP-P4] corpus-vs-static-discovery differ
// (RECOMP_P0_DESIGN.md §9: "for each observed (site, target): site must be a
// discovered jr/jalr; if site has a recovered table, target ∈ table targets;
// if tail-call, target must be a discovered function entry or leader. Any
// miss = a precise discovery bug report").
//
// Predicted classes, checked in order:
//   table         site has a recovered jump table and target ∈ its targets
//   entry         target is a discovered function entry
//   leader        target ∈ leaders of the discovered function containing it
//                 (mid-entry dispatch: every leader is an entry-switch case)
//   continuation  target == some jal/jalr site + 8 inside a discovered
//                 function (the [E2-SAVE] SaveResume re-entry set — computed
//                 returns; statically derivable from the decode)
//   GAP           none of the above — a discovery bug report
// A site outside every discovered extent is its own gap class (site-unknown).
//

#include "../discovery/discovery_types.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace recomp_corpus {

struct CorpusGap {
    uint32_t site = 0;
    uint32_t target = 0;
    uint32_t site_fn = 0;    // containing discovered fn entry (0 = none)
    uint32_t target_fn = 0;  // discovered fn whose extent contains target (0 = none)
    std::string reason;      // "site-not-discovered" | "target-unpredicted"
};

struct CorpusAuditResult {
    size_t pairs = 0;
    size_t sites = 0;
    size_t targets = 0;
    size_t predicted_table = 0;
    size_t predicted_entry = 0;
    size_t predicted_leader = 0;
    size_t predicted_continuation = 0;
    std::vector<CorpusGap> gaps; // deterministic (site, target) order
};

// Load one or more corpus files ("0xSITE 0xTARGET" lines), dedup + sort.
// Returns false if any path cannot be opened or parsed.
bool load_corpus_files(const std::vector<std::string>& paths,
                       std::vector<std::pair<uint32_t, uint32_t>>& out);

// Classify every observed pair against the discovery result.
CorpusAuditResult run_corpus_audit(
    const recomp_v2::DiscoveryResult& result,
    const recomp_v2::DiscoveryInputs& inputs,
    const std::vector<std::pair<uint32_t, uint32_t>>& pairs);

// Deterministic markdown report (audit table + per-gap detail lines).
bool write_corpus_audit_report(const CorpusAuditResult& audit,
                               const std::vector<std::string>& corpus_paths,
                               const std::string& out_path);

} // namespace recomp_corpus

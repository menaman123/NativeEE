//
// audit.cpp — [RECOMP-P4] corpus-vs-static-discovery differ (design §9).
// See audit.h for the predicted-class definitions.
//

#include "audit.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace recomp_corpus {

bool load_corpus_files(const std::vector<std::string>& paths,
                       std::vector<std::pair<uint32_t, uint32_t>>& out) {
    std::set<std::pair<uint32_t, uint32_t>> dedup;
    for (const std::string& p : paths) {
        std::ifstream f(p);
        if (!f) {
            std::fprintf(stderr, "[corpus-audit] cannot open %s\n", p.c_str());
            return false;
        }
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            uint32_t site = 0, target = 0;
            if (std::sscanf(line.c_str(), "%" SCNx32 " %" SCNx32, &site,
                            &target) != 2) {
                std::fprintf(stderr, "[corpus-audit] bad line in %s: %s\n",
                             p.c_str(), line.c_str());
                return false;
            }
            dedup.emplace(site, target);
        }
    }
    out.assign(dedup.begin(), dedup.end());
    return true;
}

namespace {

// Find the discovered function whose extent contains `va` (nullptr if none).
const recomp_v2::DiscoveredFunction* containing(
    const recomp_v2::DiscoveryResult& r, uint32_t va) {
    auto it = r.functions.upper_bound(va);
    if (it == r.functions.begin()) return nullptr;
    const recomp_v2::DiscoveredFunction& fn = std::prev(it)->second;
    return (va >= fn.entry && va < fn.end) ? &fn : nullptr;
}

// The [E2-SAVE] SaveResume re-entry set: every jal/jalr instruction VA + 8
// inside a discovered extent. Two fixed R5900 encodings decode this without
// a disassembler: jal = opcode 3 (top 6 bits), jalr = SPECIAL (opcode 0)
// with funct 9.
std::set<uint32_t> continuation_points(const recomp_v2::DiscoveryResult& r,
                                       const recomp_v2::DiscoveryInputs& in) {
    std::set<uint32_t> pts;
    for (const auto& kv : r.functions) {
        const recomp_v2::DiscoveredFunction& fn = kv.second;
        for (uint32_t pc = fn.entry; pc + 4 <= fn.end; pc += 4) {
            if (!in.text.contains(pc)) break;
            const uint32_t w = in.text.read_u32(pc);
            const uint32_t op = w >> 26;
            const bool is_jal = (op == 3u);
            const bool is_jalr = (op == 0u) && ((w & 0x3Fu) == 9u);
            if (is_jal || is_jalr) pts.insert(pc + 8u);
        }
    }
    return pts;
}

} // namespace

CorpusAuditResult run_corpus_audit(
    const recomp_v2::DiscoveryResult& result,
    const recomp_v2::DiscoveryInputs& inputs,
    const std::vector<std::pair<uint32_t, uint32_t>>& pairs) {
    CorpusAuditResult a;
    a.pairs = pairs.size();

    // site -> table targets (all recovered tables, keyed by site pc).
    std::map<uint32_t, const recomp_v2::JumpTable*> tables;
    for (const auto& kv : result.functions)
        for (const recomp_v2::JumpTable& t : kv.second.tables)
            tables[t.site_pc] = &t;

    const std::set<uint32_t> continuations =
        continuation_points(result, inputs);

    std::set<uint32_t> sites, targets;
    for (const auto& pr : pairs) {
        const uint32_t site = pr.first, target = pr.second;
        sites.insert(site);
        targets.insert(target);

        const recomp_v2::DiscoveredFunction* site_fn = containing(result, site);
        const recomp_v2::DiscoveredFunction* tgt_fn = containing(result, target);

        if (!site_fn) {
            a.gaps.push_back({site, target, 0, tgt_fn ? tgt_fn->entry : 0,
                              "site-not-discovered"});
            continue;
        }

        // (1) table target at this site.
        auto tb = tables.find(site);
        if (tb != tables.end()) {
            const recomp_v2::JumpTable& t = *tb->second;
            if (std::find(t.targets.begin(), t.targets.end(), target) !=
                    t.targets.end() ||
                t.internal_targets.count(target) ||
                t.external_targets.count(target)) {
                a.predicted_table++;
                continue;
            }
        }
        // (2) discovered function entry.
        if (result.functions.count(target)) {
            a.predicted_entry++;
            continue;
        }
        // (3) leader (mid-entry) of the containing discovered function.
        if (tgt_fn && tgt_fn->leaders.count(target)) {
            a.predicted_leader++;
            continue;
        }
        // (4) post-call continuation (SaveResume-dispatchable).
        if (continuations.count(target)) {
            a.predicted_continuation++;
            continue;
        }
        a.gaps.push_back({site, target, site_fn->entry,
                          tgt_fn ? tgt_fn->entry : 0, "target-unpredicted"});
    }
    a.sites = sites.size();
    a.targets = targets.size();
    return a;
}

bool write_corpus_audit_report(const CorpusAuditResult& a,
                               const std::vector<std::string>& corpus_paths,
                               const std::string& out_path) {
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    char b[512];
    f << "# RECOMP P4 — corpus audit (observed jr/jalr transfers vs v2 static "
         "discovery)\n\n";
    f << "Corpus inputs:\n";
    for (const std::string& p : corpus_paths) f << "- " << p << "\n";
    const size_t predicted = a.predicted_table + a.predicted_entry +
                             a.predicted_leader + a.predicted_continuation;
    f << "\n| metric | count |\n|---|---|\n";
    std::snprintf(b, sizeof b,
                  "| observed (site,target) pairs | %zu |\n"
                  "| distinct sites | %zu |\n"
                  "| distinct targets | %zu |\n"
                  "| predicted: table target | %zu |\n"
                  "| predicted: function entry | %zu |\n"
                  "| predicted: leader (mid-entry) | %zu |\n"
                  "| predicted: call continuation | %zu |\n"
                  "| predicted total | %zu |\n"
                  "| GAPS | %zu |\n",
                  a.pairs, a.sites, a.targets, a.predicted_table,
                  a.predicted_entry, a.predicted_leader,
                  a.predicted_continuation, predicted, a.gaps.size());
    f << b << "\n";
    if (a.gaps.empty()) {
        f << "Every observed indirect transfer is predicted by static "
             "discovery.\n";
    } else {
        f << "## Gaps (discovery bug reports)\n\n"
             "| site | site fn | target | target-containing fn | reason |\n"
             "|---|---|---|---|---|\n";
        for (const CorpusGap& g : a.gaps) {
            std::snprintf(b, sizeof b,
                          "| 0x%08x | 0x%08x | 0x%08x | 0x%08x | %s |\n",
                          g.site, g.site_fn, g.target, g.target_fn,
                          g.reason.c_str());
            f << b;
        }
    }
    return f.good();
}

} // namespace recomp_corpus

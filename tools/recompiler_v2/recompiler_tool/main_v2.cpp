//
// main_v2.cpp — RECOMP v2 engine CLI (P2: discovery; P3a: + THE EMISSION CORE).
//
// [RECOMP-P2] Runs the REAL discovery pipeline (ELF -> leaders -> two-engine
// jump tables -> multi-source functions), writes the deterministic discovery
// report, and offers the OPTIONAL Ghidra comparison mode used to produce
// docs/loop_state/RECOMP_P2_COMPARISON.md.
//
// [RECOMP-P3a] When --output-header/--output-cpp are given, the tool EMITS the
// full artifact tree (recompiled.h + common TU + shards) through the REUSED
// legacy Recompiler (no new translation semantics), fed by the discovery
// result via recomp_v2_emit::build_v2_functions:
//
//   recompiler_tool_v2 --input SLUS_123.45
//       --ghidra-input extracted_functions.json     (P3a compat: aliases,
//                                                    true-gap fns, pin bodies;
//                                                    NEVER a discovery input)
//       --output-header recompiled.h --output-cpp recompiled.cpp --shards 32
//       [--direct-calls] [--overrides recomp_overrides.ini]
//       [--manual-json switch_manual_enumeration.json]   (ALL_MANUAL expansion)
//
// The DISCOVERY engine itself never consumes the Ghidra JSON (design §7.3);
// under P3a the EMISSION layer consumes it as the legacy-compat data input so
// the function-set gate loses nothing (design §10). P4 drops it.
//
// The legacy tool (main.cpp) is untouched by this file.
//

#include "discovery/functions.h"
#include "discovery/jumptables.h"
#include "discovery/leaders.h"
#include "discovery/report.h"
#include "corpus/audit.h" // [RECOMP-P4] corpus audit mode
#include "elf/elf_image.h"
#include "emit/driver_v2.h"
#include "legacy/recomp_overrides_config.h"
#include "recompiler.h"
#include "EEAnalyze/analyze.h"

#include <include/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

uint32_t parse_hex(const std::string& s) {
    return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

std::string hex32(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

// ---------------------------------------------------------------------------
// The P2 comparison report (design §7.3 seed 6 + master brief P2 deliverable).
// ---------------------------------------------------------------------------
int write_comparison(const recomp_v2::DiscoveryResult& r,
                     const recomp_v2::DiscoveryInputs& in,
                     const std::string& ghidra_path,
                     const std::string& enum_path,
                     const std::string& out_path) {
    using nlohmann::json;

    json gh;
    {
        std::ifstream f(ghidra_path);
        if (!f) { std::fprintf(stderr, "[v2] cannot open %s\n", ghidra_path.c_str()); return 1; }
        f >> gh;
    }
    const json& gfuncs = gh.is_object() && gh.contains("functions") ? gh["functions"] : gh;

    struct GFn { uint32_t addr = 0, size = 0; std::string name; };
    std::map<uint32_t, GFn> ghidra;
    for (const auto& f : gfuncs) {
        GFn g;
        g.addr = f["address"].is_string() ? parse_hex(f["address"].get<std::string>())
                                          : f["address"].get<uint32_t>();
        g.size = f["size"].get<uint32_t>();
        g.name = f.value("name", "");
        ghidra[g.addr] = g;
    }

    json en;
    bool have_enum = false;
    if (!enum_path.empty()) {
        std::ifstream f(enum_path);
        if (f) { f >> en; have_enum = true; }
    }

    std::ofstream md(out_path, std::ios::binary | std::ios::trunc);
    if (!md) { std::fprintf(stderr, "[v2] cannot write %s\n", out_path.c_str()); return 1; }

    // ---- function-set diff -------------------------------------------------
    std::vector<uint32_t> missed, extra;
    size_t matched = 0;
    for (const auto& kv : ghidra)
        if (r.functions.count(kv.first)) ++matched; else missed.push_back(kv.first);
    for (const auto& kv : r.functions)
        if (!ghidra.count(kv.first)) extra.push_back(kv.first);

    // [RECOMP-P2b] Honest miss classification (RECOMP_P2_REVIEW.md required
    // change #2): every miss is either ABSORBED (its bytes live inside a v2
    // extent — an attribution difference, no code lost) or a TRUE GAP
    // (genuinely undiscovered). True gaps are listed INDIVIDUALLY with a
    // reference analysis re-derived from the ELF bytes here — never
    // summarized away.
    auto absorbed_by = [&](uint32_t a) -> const recomp_v2::DiscoveredFunction* {
        auto it = r.functions.upper_bound(a);
        if (it == r.functions.begin()) return nullptr;
        const auto& fn = std::prev(it)->second;
        return (a >= fn.entry && a < fn.end) ? &fn : nullptr;
    };
    std::vector<uint32_t> absorbed, true_gaps;
    for (uint32_t a : missed)
        (absorbed_by(a) ? absorbed : true_gaps).push_back(a);

    // Reference analysis inputs for the true-gap rows.
    const uint32_t tbase = in.text.base_va;
    const uint32_t tend = tbase + static_cast<uint32_t>(in.text.bytes.size());
    std::set<uint32_t> jal_targets;
    for (uint32_t pc = tbase; pc + 4 <= tend; pc += 4) {
        const uint32_t word = in.text.read_u32(pc);
        if ((word >> 26) == 3)
            jal_targets.insert((pc & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2));
    }
    std::map<uint32_t, std::vector<uint32_t>> data_ptr_srcs;
    for (const auto& sec : in.data) {
        const uint32_t n = static_cast<uint32_t>(sec.bytes.size());
        for (uint32_t off = 0; off + 4 <= n; off += 4) {
            const uint32_t v = sec.read_u32(sec.base_va + off);
            if ((v & 3u) == 0 && v >= tbase && v + 4 <= tend)
                data_ptr_srcs[v].push_back(sec.base_va + off);
        }
    }
    const auto code_ptr_srcs = recomp_v2::scan_code_constructed_pointers(in.text);
    auto in_table_range = [&](uint32_t va) {
        auto it = r.table_data_ranges.upper_bound(va);
        if (it == r.table_data_ranges.begin()) return false;
        return va < std::prev(it)->second;
    };
    auto gap_reason = [&](uint32_t a) {
        std::string why;
        auto cp = code_ptr_srcs.find(a);
        if (cp != code_ptr_srcs.end()) {
            why += "lui/addiu-constructed at";
            for (size_t i = 0; i < cp->second.size() && i < 3; ++i)
                why += " " + hex32(cp->second[i]);
        }
        auto dp = data_ptr_srcs.find(a);
        if (dp != data_ptr_srcs.end()) {
            size_t in_tbl = 0;
            for (uint32_t s : dp->second)
                if (in_table_range(s)) ++in_tbl;
            if (!why.empty()) why += "; ";
            why += "data-pointed from";
            for (size_t i = 0; i < dp->second.size() && i < 3; ++i)
                why += " " + hex32(dp->second[i]);
            if (in_tbl == dp->second.size())
                why += " — ALL sources inside recovered jump-table data "
                       "ranges (slots of never-reached table sites; the "
                       "weak-pointer seed excludes table entries by design "
                       "[the 0x21bc1c lesson], and the owning jr sites were "
                       "never reached by any flow)";
        }
        if (jal_targets.count(a)) {
            if (!why.empty()) why += "; ";
            why += "jal'd";
        }
        const uint32_t w0 = in.text.contains(a) ? in.text.read_u32(a) : 0;
        const bool prologue0 =
            ((w0 >> 16) == 0x27BD || (w0 >> 16) == 0x67BD) && (w0 & 0x8000u);
        if (why.empty())
            why = std::string("no static reference (not jal'd, not data-pointed, "
                              "not lui/addiu-constructed); ") +
                  (prologue0 ? "prologue at entry" : "no prologue at entry");
        return why;
    };

    // ---- extent diff over matched ------------------------------------------
    size_t ext_eq = 0, ext_v2_longer = 0, ext_v2_shorter = 0;
    std::vector<std::pair<uint32_t, int64_t>> shorter_rows;
    for (const auto& kv : ghidra) {
        auto it = r.functions.find(kv.first);
        if (it == r.functions.end()) continue;
        const int64_t d = static_cast<int64_t>(it->second.end) -
                          static_cast<int64_t>(kv.first + kv.second.size);
        if (d == 0) ++ext_eq;
        else if (d > 0) ++ext_v2_longer;
        else { ++ext_v2_shorter; shorter_rows.emplace_back(kv.first, d); }
    }
    std::sort(shorter_rows.begin(), shorter_rows.end(),
              [](auto& a, auto& b) { return a.second < b.second; });

    // ---- table sites vs enumeration ground truth ---------------------------
    size_t truth_sites = 0, sites_resolved = 0, sites_base_ok = 0,
           sites_bound_ok = 0, sites_sets_ok = 0;
    std::vector<std::string> site_misses;
    // v2 site index
    std::map<uint32_t, const recomp_v2::JumpTable*> v2_sites;
    for (const auto& kv : r.functions)
        for (const auto& t : kv.second.tables) v2_sites[t.site_pc] = &t;
    size_t v2_total_sites = v2_sites.size();
    if (have_enum) {
        for (const auto& e : en) {
            for (const auto& jt : e["jump_tables"]) {
                if (!jt.contains("site") || jt["site"].is_null()) continue;
                if (jt.contains("error") && !jt.contains("bound")) continue;
                if (!jt.contains("table_base")) continue;
                ++truth_sites;
                const uint32_t site = parse_hex(jt["site"].get<std::string>());
                const uint32_t base = parse_hex(jt["table_base"].get<std::string>());
                const uint32_t bound = jt["bound"].get<uint32_t>();
                auto it = v2_sites.find(site);
                if (it == v2_sites.end()) {
                    site_misses.push_back("site " + hex32(site) + " (fn " +
                                          e.value("address", "?") + "): not recovered");
                    continue;
                }
                ++sites_resolved;
                const auto* t = it->second;
                if (t->table_base == base) ++sites_base_ok;
                else site_misses.push_back("site " + hex32(site) + ": base " +
                                           hex32(t->table_base) + " != truth " + hex32(base));
                if (t->bound == bound) ++sites_bound_ok;
                else site_misses.push_back("site " + hex32(site) + ": bound " +
                                           std::to_string(t->bound) + " != truth " +
                                           std::to_string(bound));
                std::set<uint32_t> want_int, want_ext;
                for (const auto& s : jt["internal_targets"]) want_int.insert(parse_hex(s.get<std::string>()));
                for (const auto& s : jt["external_targets"]) want_ext.insert(parse_hex(s.get<std::string>()));
                if (t->internal_targets == want_int && t->external_targets == want_ext)
                    ++sites_sets_ok;
                else
                    site_misses.push_back("site " + hex32(site) + ": int/ext split differs (v2 " +
                                          std::to_string(t->internal_targets.size()) + "/" +
                                          std::to_string(t->external_targets.size()) + " vs truth " +
                                          std::to_string(want_int.size()) + "/" +
                                          std::to_string(want_ext.size()) + ")");
            }
        }
    }

    // ---- leader stats -------------------------------------------------------
    size_t total_leaders = 0;
    for (const auto& kv : r.functions) total_leaders += kv.second.leaders.size();
    const recomp_v2::LeaderAnalysis la =
        recomp_v2::analyze_leaders(in.text, {});
    size_t sweep_covered = 0, sweep_total = 0;
    for (uint32_t t : la.leaders) {
        auto it = r.functions.upper_bound(t);
        if (it == r.functions.begin()) continue;
        const auto& fn = std::prev(it)->second;
        if (t < fn.entry || t >= fn.end) continue;
        ++sweep_total;
        if (fn.leaders.count(t)) ++sweep_covered;
    }

    // ---- emit the markdown --------------------------------------------------
    md << "# RECOMP P2 — DISCOVERY COMPARISON REPORT (v2 engine vs Ghidra JSON)\n\n"
       << "Generated by `recompiler_tool_v2 --compare-ghidra` over the full ELF.\n"
       << "The v2 engine ran WITHOUT the Ghidra JSON (it is a comparison input\n"
       << "only). All counts are from this run; misses are listed, not hidden.\n\n"
       << "## Function set\n\n"
       << "| metric | count |\n|---|---|\n"
       << "| Ghidra functions | " << ghidra.size() << " |\n"
       << "| v2 functions | " << r.functions.size() << " |\n"
       << "| matched (same entry) | " << matched << " |\n"
       << "| missed by v2 | " << missed.size() << " |\n"
       << "| — absorbed inside a v2 extent (attribution diff, bytes covered) | "
       << absorbed.size() << " |\n"
       << "| — TRUE GAPS (genuinely undiscovered) | " << true_gaps.size() << " |\n"
       << "| extra in v2 (not in Ghidra) | " << extra.size() << " |\n\n";
    if (!true_gaps.empty()) {
        size_t gap_bytes = 0;
        for (uint32_t a : true_gaps) gap_bytes += ghidra[a].size;
        md << "### TRUE-GAP misses — every row, with the re-derived reason ("
           << true_gaps.size() << " rows, " << gap_bytes << " bytes)\n\n";
        for (uint32_t a : true_gaps) {
            const auto& g = ghidra[a];
            md << "- " << hex32(a) << " " << g.name << " (size " << g.size
               << ") — " << gap_reason(a) << "\n";
        }
        md << "\n";
    }
    if (!absorbed.empty()) {
        md << "### Absorbed misses (first 40 of " << absorbed.size()
           << " — bytes live inside a v2 extent; attribution only)\n\n";
        for (size_t i = 0; i < absorbed.size() && i < 40; ++i) {
            const auto& g = ghidra[absorbed[i]];
            const auto* fn = absorbed_by(absorbed[i]);
            md << "- " << hex32(absorbed[i]) << " " << g.name << " (size " << g.size
               << ") inside v2 fn " << hex32(fn->entry) << " [.." << hex32(fn->end)
               << ")\n";
        }
        if (absorbed.size() > 40) md << "- ... and " << (absorbed.size() - 40) << " more\n";
        md << "\n";
    }
    md << "## Extents (matched functions, v2 end vs Ghidra entry+size)\n\n"
       << "| relation | count |\n|---|---|\n"
       << "| identical | " << ext_eq << " |\n"
       << "| v2 longer | " << ext_v2_longer << " |\n"
       << "| v2 shorter | " << ext_v2_shorter << " |\n\n";
    if (!shorter_rows.empty()) {
        md << "### v2-shorter extents (worst 25 — each a potential missed tail)\n\n";
        for (size_t i = 0; i < shorter_rows.size() && i < 25; ++i)
            md << "- " << hex32(shorter_rows[i].first) << ": " << shorter_rows[i].second
               << " bytes vs Ghidra\n";
        md << "\n";
    }
    md << "## Jump tables vs enumeration ground truth (the 160 ELF-recovered sites)\n\n"
       << "| metric | count |\n|---|---|\n"
       << "| ground-truth sites | " << truth_sites << " |\n"
       << "| resolved by v2 (site match) | " << sites_resolved << " |\n"
       << "| base match | " << sites_base_ok << " |\n"
       << "| bound match | " << sites_bound_ok << " |\n"
       << "| internal/external split match | " << sites_sets_ok << " |\n"
       << "| v2 total table sites (whole ELF) | " << v2_total_sites << " |\n\n";
    if (!site_misses.empty()) {
        md << "### Site-level differences (every one)\n\n";
        for (const auto& s : site_misses) md << "- " << s << "\n";
        md << "\n";
    }
    md << "## Leaders\n\n"
       << "| metric | count |\n|---|---|\n"
       << "| per-function leaders (total) | " << total_leaders << " |\n"
       << "| sweep-derived branch/jump targets inside v2 extents | " << sweep_total << " |\n"
       << "| covered by per-function leader sets | " << sweep_covered << " |\n"
       << "| unreached .text words (gaps/padding diagnostics) | "
       << r.unreached_gaps.size() << " |\n";
    return md.good() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::string elf_path, report_path;
    std::string out_header, out_cpp;
    std::string cmp_ghidra, cmp_enum, cmp_out;
    std::string ghidra_path, overrides_path, manual_json_path;
    std::vector<std::string> audit_corpus_paths; // [RECOMP-P4]
    std::string audit_out;                       // [RECOMP-P4]
    int shard_count = 0;
    int init_parts = 0;   // [INIT-PARTS] 0/1 = classic single init body
    bool direct_calls = true;
    bool legacy_title_stubs = false;   // --legacy-title-stubs: the first title's hardcoded stub blocks
    bool yield_backedges = false;      // --yield-backedges: the engine's in-line yield check at loop back-edges
    for (int i = 1; i < argc; ++i) {
        auto need = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[v2] missing value for %s\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (!std::strcmp(argv[i], "--input")) elf_path = need("--input");
        else if (!std::strcmp(argv[i], "--output-header")) out_header = need("--output-header");
        else if (!std::strcmp(argv[i], "--output-cpp")) out_cpp = need("--output-cpp");
        else if (!std::strcmp(argv[i], "--shards")) shard_count = std::atoi(need("--shards"));
        else if (!std::strcmp(argv[i], "--init-parts")) init_parts = std::atoi(need("--init-parts"));
        else if (!std::strcmp(argv[i], "--report")) report_path = need("--report");
        else if (!std::strcmp(argv[i], "--compare-ghidra")) cmp_ghidra = need("--compare-ghidra");
        else if (!std::strcmp(argv[i], "--compare-enum")) cmp_enum = need("--compare-enum");
        else if (!std::strcmp(argv[i], "--compare-out")) cmp_out = need("--compare-out");
        else if (!std::strcmp(argv[i], "--ghidra-input")) ghidra_path = need("--ghidra-input");
        // NOTE: --ghidra-input is NEVER a discovery input (design §7.3). Under
        // P3a the EMISSION layer consumes it as the legacy-compat data source
        // (aliases for absorbed entries, true-gap compat functions, pin
        // bodies). P4 builds without it.
        else if (!std::strcmp(argv[i], "--overrides")) overrides_path = need("--overrides");
        else if (!std::strcmp(argv[i], "--manual-json")) manual_json_path = need("--manual-json");
        else if (!std::strcmp(argv[i], "--direct-calls")) direct_calls = true;
        else if (!std::strcmp(argv[i], "--no-direct-calls")) direct_calls = false;
        else if (!std::strcmp(argv[i], "--legacy-title-stubs")) legacy_title_stubs = true;
        else if (!std::strcmp(argv[i], "--no-legacy-title-stubs")) legacy_title_stubs = false;
        else if (!std::strcmp(argv[i], "--yield-backedges")) yield_backedges = true;
        else if (!std::strcmp(argv[i], "--no-yield-backedges")) yield_backedges = false;
        // [RECOMP-P4] corpus audit mode (design §9): --audit-corpus is
        // repeatable (one merged run over all files); --audit-out is the
        // deterministic markdown report. Exit 4 when gaps exist (gate-able).
        else if (!std::strcmp(argv[i], "--audit-corpus")) audit_corpus_paths.push_back(need("--audit-corpus"));
        else if (!std::strcmp(argv[i], "--audit-out")) audit_out = need("--audit-out");
    }

    if (elf_path.empty()) {
        std::fprintf(stderr, "[v2] usage: recompiler_tool_v2 --input <elf> [--report out.json]\n"
                             "        [--ghidra-input g.json --output-header h --output-cpp c --shards N --init-parts P\n"
                             "         --overrides recomp_overrides.ini --manual-json enum.json]\n"
                             "        [--compare-ghidra g.json --compare-enum e.json --compare-out out.md]\n"
                             "        [--audit-corpus pairs.txt ... --audit-out audit.md]\n");
        return 2;
    }

    auto elf = recomp_v2::ElfImage::load(elf_path);
    if (!elf) {
        std::fprintf(stderr, "[v2] failed to load ELF: %s\n", elf_path.c_str());
        return 1;
    }
    recomp_v2::DiscoveryInputs in;
    in.text = elf->text();
    in.data = elf->data_sections();
    in.elf_entry = elf->entry_point();

    std::printf("[v2] discovery over %s (.text %zu bytes, %zu data sections, entry %s)\n",
                elf_path.c_str(), in.text.bytes.size(), in.data.size(),
                hex32(in.elf_entry).c_str());

    recomp_v2::DiscoveryResult result;
    try {
        result = recomp_v2::discover_program(in);
    } catch (const std::exception& e) {
        // Cross-check disagreement dossier (design §7.2): a hard build error.
        std::fprintf(stderr, "[v2] DISCOVERY HARD ERROR: %s\n", e.what());
        return 3;
    }

    size_t tables = 0;
    for (const auto& kv : result.functions) tables += kv.second.tables.size();
    std::printf("[v2] discovered %zu functions, %zu jump-table sites, "
                "%zu unreached gap words\n",
                result.functions.size(), tables, result.unreached_gaps.size());
    // Delay-slot rule (a delay slot is never a function start): weak seeds
    // rejected there, and strong entries that nevertheless sit in one (kept,
    // pathological — a human should look).
    std::printf("[v2] delay-slot rule: %zu weak seed(s) rejected, %zu strong "
                "entr%s in a delay slot\n",
                result.delay_slot_weak_rejected.size(),
                result.delay_slot_strong_entries.size(),
                result.delay_slot_strong_entries.size() == 1 ? "y" : "ies");
    for (uint32_t e : result.delay_slot_strong_entries)
        std::printf("[v2]   STRONG ENTRY IN DELAY SLOT: %s (preceding transfer at %s)\n",
                    hex32(e).c_str(), hex32(e - 4).c_str());

    if (!report_path.empty()) {
        if (!recomp_v2::write_discovery_report(result, report_path)) {
            std::fprintf(stderr, "[v2] failed to write report %s\n", report_path.c_str());
            return 1;
        }
        std::printf("[v2] wrote discovery report: %s\n", report_path.c_str());
    }

    if (!cmp_ghidra.empty() && !cmp_out.empty()) {
        const int rc = write_comparison(result, in, cmp_ghidra, cmp_enum, cmp_out);
        if (rc) return rc;
        std::printf("[v2] wrote comparison report: %s\n", cmp_out.c_str());
    }

    // ------------------------------------------------------------------------
    // [RECOMP-P4] corpus audit (design §9): every runtime-observed jr/jalr
    // (site -> target) pair must be predicted by static discovery. Gaps are
    // precise discovery bug reports; exit 4 makes this a gate.
    // ------------------------------------------------------------------------
    if (!audit_corpus_paths.empty()) {
        std::vector<std::pair<uint32_t, uint32_t>> pairs;
        if (!recomp_corpus::load_corpus_files(audit_corpus_paths, pairs))
            return 2;
        const recomp_corpus::CorpusAuditResult audit =
            recomp_corpus::run_corpus_audit(result, in, pairs);
        std::printf(
            "[v2] corpus audit: %zu pairs (%zu sites, %zu targets) -> "
            "table=%zu entry=%zu leader=%zu continuation=%zu GAPS=%zu\n",
            audit.pairs, audit.sites, audit.targets, audit.predicted_table,
            audit.predicted_entry, audit.predicted_leader,
            audit.predicted_continuation, audit.gaps.size());
        for (const recomp_corpus::CorpusGap& g : audit.gaps)
            std::printf("[v2]   GAP site=0x%08x (fn 0x%08x) target=0x%08x "
                        "(in fn 0x%08x) %s\n",
                        g.site, g.site_fn, g.target, g.target_fn,
                        g.reason.c_str());
        if (!audit_out.empty() &&
            !recomp_corpus::write_corpus_audit_report(audit, audit_corpus_paths,
                                                      audit_out)) {
            std::fprintf(stderr, "[v2] failed to write audit report %s\n",
                         audit_out.c_str());
            return 1;
        }
        if (!audit_out.empty())
            std::printf("[v2] wrote corpus audit report: %s\n",
                        audit_out.c_str());
        if (!audit.gaps.empty()) return 4;
    }

    // ------------------------------------------------------------------------
    // [RECOMP-P3a] THE EMISSION CORE.
    // ------------------------------------------------------------------------
    if (!out_header.empty() && !out_cpp.empty()) {
        // The reused legacy emitter logs through the global `log_file`
        // (recompiler.cpp). Point it at the same place the legacy tool uses.
        try {
            std::filesystem::path dir =
                std::filesystem::path("logs/recompiler_tool_v2");
            if (!std::filesystem::exists(dir))
                std::filesystem::create_directories(dir);
            log_file.open((dir / "runtime_log.txt").string());
        } catch (...) { /* logging is best-effort; emission must not depend on it */ }

        const uint8_t* text_data = in.text.bytes.data();
        const uint32_t text_size = static_cast<uint32_t>(in.text.bytes.size());
        const uint32_t text_base = in.text.base_va;

        // Legacy-compat input (optional; absent under the P4 no-Ghidra build).
        std::map<uint32_t, Function> ghidra_functions;
        if (!ghidra_path.empty()) {
            ghidra_functions = parse_ghidra_function_file(
                ghidra_path, text_data, text_size, text_base, log_file);
            std::printf("[v2] legacy-compat input: %zu functions from %s\n",
                        ghidra_functions.size(), ghidra_path.c_str());
        } else {
            std::printf("[v2] no --ghidra-input: emitting the pure discovery "
                        "set (no aliases/compat/pins)\n");
        }

        // Override pins (recomp_overrides.ini channel, design §8).
        std::set<uint32_t> pinned;
        if (!overrides_path.empty()) {
            recompovr::Config ocfg;
            std::vector<std::string> warnings;
            if (recompovr::LoadConfigFile(overrides_path, ocfg, &warnings)) {
                for (const auto& w : warnings)
                    std::fprintf(stderr, "[v2] overrides: %s\n", w.c_str());
                std::set<uint32_t> manual_addrs;
                if (ocfg.pin_all_manual) {
                    if (manual_json_path.empty()) {
                        std::fprintf(stderr,
                                     "[v2] overrides: pin=ALL_MANUAL requires "
                                     "--manual-json; sentinel ignored\n");
                    } else {
                        std::ifstream mf(manual_json_path);
                        nlohmann::json mj;
                        if (mf && (mf >> mj, mj.is_array())) {
                            for (const auto& e : mj)
                                if (e.contains("address"))
                                    manual_addrs.insert(parse_hex(
                                        e["address"].get<std::string>()));
                        }
                        std::printf("[v2] overrides: ALL_MANUAL expands over "
                                    "%zu manual addresses\n",
                                    manual_addrs.size());
                    }
                }
                pinned = ocfg.pins;
                if (ocfg.pin_all_manual)
                    pinned.insert(manual_addrs.begin(), manual_addrs.end());
                std::printf("[v2] overrides: %zu pinned address(es)\n",
                            pinned.size());
            } else {
                std::fprintf(stderr, "[v2] overrides file not readable: %s "
                             "(no pins)\n", overrides_path.c_str());
            }
        }

        recomp_v2_emit::BuildResult build = recomp_v2_emit::build_v2_functions(
            result, in, ghidra_functions, pinned, text_data, text_size,
            text_base);
        std::printf("[v2] emission model: %zu functions (%zu aliases, %zu "
                    "pinned), %zu jr table sites\n",
                    build.functions.size(), build.config.aliases.size(),
                    build.config.pinned.size(), build.config.tables.size());

        Recompiler recompiler(build.functions, text_data, text_size, text_base);
        recompiler.set_direct_calls(direct_calls);
        recompiler.set_init_parts(init_parts);
        recompiler.set_legacy_title_stubs(legacy_title_stubs);
        recompiler.set_yield_backedges(yield_backedges);
        recompiler.set_v2_emission(build.config);
        try {
            if (!recompiler.recompile_to_files(out_header, out_cpp,
                                               shard_count)) {
                std::fprintf(stderr, "[v2] emission failed\n");
                return 1;
            }
        } catch (const std::exception& e) {
            // D1/D2 generation-time hard errors (CONVENTIONS): never emit the
            // class-A shape — abort the build with the dossier.
            std::fprintf(stderr, "[v2] EMISSION HARD ERROR: %s\n", e.what());
            return 3;
        }
        std::printf("[v2] emitted %s + %s (%d shard(s))\n", out_header.c_str(),
                    out_cpp.c_str(), shard_count);
        return 0;
    }

    std::printf("[v2] discovery-only run (no --output-header/--output-cpp).\n");
    return 0;
}

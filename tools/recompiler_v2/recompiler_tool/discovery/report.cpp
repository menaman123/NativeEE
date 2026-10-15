//
// report.cpp — RECOMP v2 discovery: deterministic report emission.
//
// [RECOMP-P2] Implements write_discovery_report (design §6): ordered
// iteration only, no timestamps, no host paths, no pointer values
// (CONVENTIONS P2). Consumed by the G-FSET whole-program gate and the P2
// Ghidra comparison driver (recompiler_tool/main_v2.cpp — the Ghidra JSON is
// an OPTIONAL comparison input there; the engine itself never reads it).
//

#include "report.h"

#include <cinttypes>
#include <cstdio>
#include <fstream>

namespace recomp_v2 {

namespace {
const char* seed_name(SeedSource s) {
    switch (s) {
        case SeedSource::kElfEntry: return "elf_entry";
        case SeedSource::kJalTarget: return "jal";
        case SeedSource::kProloguePattern: return "prologue";
        case SeedSource::kDataPointer: return "data_pointer";
        case SeedSource::kTableExternal: return "table_external";
        case SeedSource::kCodePointer: return "code_pointer";
    }
    return "unknown";
}
} // namespace

bool write_discovery_report(const DiscoveryResult& result,
                            const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    char buf[128];
    f << "{\n"
      << "  \"engine\": \"v2\",\n"
      << "  \"engine_ran\": " << (result.engine_ran ? "true" : "false") << ",\n"
      << "  \"function_count\": " << result.functions.size() << ",\n"
      << "  \"unreached_gap_words\": " << result.unreached_gaps.size() << ",\n"
      << "  \"functions\": [\n";
    bool first = true;
    for (const auto& kv : result.functions) {
        const DiscoveredFunction& fn = kv.second;
        if (!first) f << ",\n";
        first = false;
        std::snprintf(buf, sizeof buf,
                      "    {\"entry\": \"0x%08x\", \"end\": \"0x%08x\", "
                      "\"leaders\": %zu, \"folds\": %s, \"seeds\": [",
                      fn.entry, fn.end, fn.leaders.size(),
                      fn.folds_successor ? "true" : "false");
        f << buf;
        bool s1 = true;
        for (SeedSource s : fn.seeds) {
            if (!s1) f << ", ";
            s1 = false;
            f << "\"" << seed_name(s) << "\"";
        }
        f << "], \"tables\": [";
        bool t1 = true;
        for (const JumpTable& t : fn.tables) {
            if (!t1) f << ", ";
            t1 = false;
            std::snprintf(buf, sizeof buf,
                          "{\"site\": \"0x%08x\", \"base\": \"0x%08x\", "
                          "\"bound\": %u, \"internal\": %zu, \"external\": %zu}",
                          t.site_pc, t.table_base, t.bound,
                          t.internal_targets.size(), t.external_targets.size());
            f << buf;
        }
        f << "]}";
    }
    f << "\n  ]\n}\n";
    return f.good();
}

} // namespace recomp_v2

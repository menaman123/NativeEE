//
// recorder_gen.cpp — [RECOMP-P4] corpus-as-audit recorder emission
// (design §9). See recorder_gen.h for the site-probe-vs-dispatcher-seam
// rationale. Everything here EMITS TEXT ONLY.
//

#include "recorder_gen.h"

#include <iomanip>

namespace recomp_corpus {

void emit_header_decls(std::ofstream& file) {
    file << "// [RECOMP-P4] corpus-as-audit recorder seam (RECOMP_P0_DESIGN.md §9):\n";
    file << "// env-gated PS2_RECOMP_CORPUS=<file>. Defined in the common TU; the\n";
    file << "// generated jr/jalr sites in every shard call it behind the flag.\n";
    file << "// recomp_corpus_flush_now(): idempotent durable write (host_app calls it\n";
    file << "// around the shutdown joins — the std::_Exit wedged-join path skips\n";
    file << "// atexit, same rider pattern as the bench/ring artifacts).\n";
    file << "extern bool g_recomp_corpus_enabled;\n";
    file << "void recomp_corpus_record(uint32_t site_pc, uint32_t target);\n";
    file << "void recomp_corpus_flush_now();\n\n";
}

void emit_runtime(std::ofstream& file) {
    file << "// ==========================================================================\n";
    file << "// [RECOMP-P4] corpus-as-audit recorder (RECOMP_P0_DESIGN.md §9).\n";
    file << "// PS2_RECOMP_CORPUS=<file> arms it; unset = one predicted-not-taken branch\n";
    file << "// per indirect transfer. Records every taken jr/jalr (site_pc -> target)\n";
    file << "// pair, deduped in an ordered set. Durable write: host_app calls\n";
    file << "// recomp_corpus_flush_now() around its shutdown joins (the wedged-join\n";
    file << "// path hard-exits via std::_Exit, skipping atexit — same rider pattern as\n";
    file << "// the bench/ring artifacts); the atexit registration below covers plain\n";
    file << "// return-from-main consumers. The write is idempotent (wb truncate).\n";
    file << "// Crash paths use _exit(1) and intentionally skip the flush (a crash is a\n";
    file << "// louder audit signal than a corpus line). Single-threaded by\n";
    file << "// architecture: all recompiled EE code runs on the CPU thread (PS2 threads\n";
    file << "// are fibers on it), so the set needs no lock. The set outlives main on\n";
    file << "// purpose (alive for the atexit flush).\n";
    file << "// ==========================================================================\n";
    file << "bool g_recomp_corpus_enabled = std::getenv(\"PS2_RECOMP_CORPUS\") != nullptr;\n";
    file << "static std::set<uint64_t>* g_recomp_corpus_pairs = nullptr;\n";
    file << "void recomp_corpus_flush_now() {\n";
    file << "    const char* path = std::getenv(\"PS2_RECOMP_CORPUS\");\n";
    file << "    if (!path || !g_recomp_corpus_pairs) return;\n";
    file << "    FILE* f = std::fopen(path, \"wb\");\n";
    file << "    if (!f) return;\n";
    file << "    for (uint64_t p : *g_recomp_corpus_pairs)\n";
    file << "        std::fprintf(f, \"0x%08x 0x%08x\\n\",\n";
    file << "                     static_cast<uint32_t>(p >> 32), static_cast<uint32_t>(p));\n";
    file << "    std::fclose(f);\n";
    file << "}\n";
    file << "void recomp_corpus_record(uint32_t site_pc, uint32_t target) {\n";
    file << "    if (!g_recomp_corpus_pairs) {\n";
    file << "        g_recomp_corpus_pairs = new std::set<uint64_t>();\n";
    file << "        std::atexit(recomp_corpus_flush_now);\n";
    file << "    }\n";
    file << "    g_recomp_corpus_pairs->insert((static_cast<uint64_t>(site_pc) << 32) |\n";
    file << "                                  target);\n";
    file << "}\n\n";
}

void emit_site_probe(std::ofstream& file, uint32_t site_pc, const char* indent) {
    file << indent << "if (g_recomp_corpus_enabled) recomp_corpus_record(0x"
         << std::hex << site_pc << "u, target); // [RECOMP-P4] corpus\n";
    file << std::dec;
}

} // namespace recomp_corpus

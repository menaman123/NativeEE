#pragma once
//
// recorder_gen.h — [RECOMP-P4] corpus-as-audit recorder emission
// (RECOMP_P0_DESIGN.md §9: "emitted by corpus/recorder_gen.cpp into the
// jr/jalr emission sites, env-gated PS2_RECOMP_CORPUS=<path>, default off").
//
// WHY generator-emitted probes and not a runtime hook at the dispatcher seam:
// an indirect site has FOUR transfer paths and only one touches a runtime
// seam —
//   (1) jump-table internal targets transfer via `goto Label_NNNN` (no seam),
//   (2) jump-table external targets dispatch via recomp_call (bypasses fcf),
//   (3) the [W16R1][FIX-E3] per-site monomorphic cache satisfies repeat
//       targets without calling find_containing_function,
//   (4) only cache-miss transfers reach find_containing_function.
// A dispatcher-seam recorder would systematically miss (1)-(2) — exactly the
// table targets the audit must verify. One probe at the target latch covers
// all four paths uniformly. Emitted by the SHARED jr/jalr emission (both
// engines carry it — house probe convention, contract surface #6); under
// legacy the 143 manual raw-string bodies bypass the general emission and are
// unprobed, which is why the P4 audit sweep runs on the v2 binary (every body
// auto-emitted, full site coverage).
//
// Cost when PS2_RECOMP_CORPUS is unset: one predicted-not-taken global-bool
// branch per indirect transfer, beneath the crashring_enabled check already
// on the same paths.
//

#include <cstdint>
#include <fstream>

namespace recomp_corpus {

// Emit the extern declarations into recompiled.h (shard TUs reference them).
void emit_header_decls(std::ofstream& file);

// Emit the recorder runtime (flag + record + atexit flush) into the common TU.
// Requires <set>/<cstdio>/<cstdlib> in the TU's include set (recompiler.cpp's
// write_cpp_includes provides them).
void emit_runtime(std::ofstream& file);

// Emit the per-site guarded record call. `indent` is the leading whitespace;
// the site's latched `target` variable must be in scope.
void emit_site_probe(std::ofstream& file, uint32_t site_pc, const char* indent);

} // namespace recomp_corpus

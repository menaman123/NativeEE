# FILEMAP — Shard 8: Toolchain + Tests + Tools + Build

Shard f8 of the nine-shard file-by-file mapping (session s116, 2026-09-23).
Scope: `recompiler_tool/` (28 code files, 31,381 ln), `iop_recompiler/` (4 files,
1,741 ln — task brief said 3; the `.h` was uncounted), `EEAnalyze/` (10 files,
2,106 ln), `tools/` (74 code files, 9,019 ln), `tests/` (158 files, 55,340 ln),
the 222 `host_app/fun_*_body.inc` as one grouped entry (159,811 ln), the 4-file
CMake set + `CMakePresets.json` + `tests/CMakeLists.txt` + the dead swizzle
CMakeLists, `scripts/` (5 files) + root `inject_logging.sh`, and
`vu_aot.ini` / `recomp_overrides.ini` / `display.ini`.

Authorities used: ARCHITECTURE.md §3 tree (destinations), MIGRATION_PLAN.md
§1.1–§1.6 (verbs), HOOK_RECLASSIFICATION.md §3.2 (manual bodies), A2 §1/§7
(flag dispositions), A6 §7 (engine/game split), TRIAGE_CAPTURE.md §12
(bug_report/slowspot retargets). Every file below was opened and read
(structure skim + every gate/hack region); line counts are `wc -l` verified
2026-09-23.

Disposition vocabulary: NORM / SETTING / DIAG / TEST-ONLY / DELETE per the
shard instructions; verbs verbatim/split/rewrite/delete per MIGRATION §1.
The PEAK configuration (run_play.sh CANON incl. PS2_GS_SCALE=2 PS2_GS_INTERP=1
PS2_FPS_CAP=60 PS2_MTVU=1 PS2_GL_FAST=1 PS2_GL_FAST2=1 PS2_VU1_HLE=1
PS2_VU1_HLE_DIRECT=1) is the NORM baseline; SCALE/INTERP/FPS_CAP are SETTINGs
with those defaults.

A structural note that frames every entry in §1–§2: the recompiler tools
consume **no env vars themselves** (pure CLI flags). Every `getenv` string in
them is *emitted text* — a gate injected into the GENERATED code. Those are
"generator-injected gates" (A2 §7.4 / MIGRATION §3 step 6): they purge at the
generator, then the code is regenerated. Phase 8's GenProbe manifest channel is
the sanctioned replacement for any probe that must survive.

---

## 1. recompiler_tool/ — 28 code files, 31,381 lines

Per D1 (MIGRATION Phase 2) the **legacy engine is retired**: legacy CLI, the
body-time special-case chain, the 84 embedded `R"code(` raw-string manual
bodies, the FUN_00177168 hand emitter, the Ghidra JSON input, the
`recomp_overrides.ini` pin machinery, and `inject_fun_1017c8_helpers` all
delete. The **v2 engine** (discovery/ + emit/ + translate/ + corpus/ + elf/)
is THE engine and moves to `engine/tools/recompiler/`. The user's play binary
is already the v2 pure-discovery build (`run_play.sh` runs
`./build-v2-nog/host_app/host_app`), so Phase 2 formalizes the shipped truth.

### recompiler_tool/recompiler.cpp (25,813 lines)
- **Purpose**: The shared MIPS R5900 → C++ emitter used by BOTH engines. One
  `Recompiler` class: builds dense address→id tables, writes `recompiled.h`,
  the bodies-free common TU (file-scope infra, `find_containing_function`,
  `initialize_recompiled_functions`, W16 hook-list dispatch armor), N shard
  TUs of function bodies, and per-instruction translation
  (`translate_instruction`, `translate_control_flow`, `generate_block_code`)
  with delay-slot/branch-likely/64-bit-condition rules (CONVENTIONS DS1–DS6).
  Also carries the legacy engine's per-address freight: a ~265-site
  `if (func.base_address == 0x...)` chain and 84 `R"code(` raw-string manual
  bodies plus the ~1,490-line FUN_00177168 hand emitter.
- **Public surface**: class `Recompiler` (recompile_to_files,
  set_direct_calls, set_v2_emission, emit_semantics_snippets,
  rewrite_manual_dispatch_file, inject_gen_hook_list); global
  `std::ofstream log_file`. Compiled into `recompiler_tool` (legacy CLI) and
  `recompiler_tool_v2`.
- **Classification**: MIXED (A6 §7 row J). The general emitter is
  GENERIC-PS2 mechanism; the special-case chain, raw-string bodies, injected
  probe addresses (e.g. the 0x1cd768 probe the snippet mode dodges), and the
  emission-time sce-seam bindings (:903–:2156, e.g. SyncV at 0x002c1d78,
  hle_SetupDisplayRegisters at 0x0019aff0) are GAME-SPECIFIC.
- **Refactor destination** (MIGRATION §1.3 recompiler.cpp row + §1.6 row J):
  split into `engine/tools/recompiler/ir/` + `engine/tools/recompiler/emit/`:
  - `ir/mips_lifter.{h,cpp}` — class `MipsLifter`: the translate_instruction /
    translate_control_flow switch rebuilt as MIPS→IR lifting tables (ARCH §5;
    Phase 8 introduces the IR + `ir/ir_verifier.{h,cpp}` class `IrVerifier`).
  - `emit/cpp_emitter.{h,cpp}` — class `CppEmitter`: IR→C++ body emission,
    delay-slot shapes preserved (golden-emission snapshots pin content).
  - `emit/shard_writer.{h,cpp}` — class `ShardWriter`: header/common-TU/shard
    partitioning (write_header_file / write_cpp_includes / write_cpp_infra /
    write_cpp_tail / write_sharded_cpp_files).
  - `emit/dispatch_tables.{h,cpp}` — class `DispatchTables`: dense-id tables,
    entry tables, flat dispatch (ARCH §5.2 replaces std::map dispatch; the
    W16 inject_gen_hook_list post-pass and hooked[] armor dissolve into it).
  - `emit/snippet_emitter.{h,cpp}` — class `SnippetEmitter`: the
    --emit-snippets differential-semantics channel (TEST-ONLY consumer:
    tests/recomp_semantics).
  - **Deleted in Phase 2** (§1.1 D1 rows): the :903–:2156 emission-time
    special-case chain (265 `base_address ==` sites incl. commented-out ones),
    the 84 `R"code(` manual bodies (:8938–:21770 region), the FUN_00177168
    hand emitter (:10464–:11901), `rewrite_manual_dispatch_file` (:25582,
    the W16R1 FIX-E2 post-pass that rewrites manual bodies' map dispatch —
    nothing left to rewrite), and every injected env-gated probe (below).
- **Flags consumed here** (all are *emitted* getenv gates, not read by the tool):

  | var | default today | what it selects | disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_SHADOW_DIAG (emitted :2412/:3198/:3247/:3312) | off | shadow-chain diagnostics blocks injected into 4 generated bodies | generator-injected gate (A2 §7.4) | DIAG — purge at generator Phase 4; GenProbe manifest is the Phase-8 replacement |
  | PS2_EE_RAWDISP (emitted :25786) | off | raw-dispatch trace injected into generated dispatch | generator-injected gate (A2 §7.4) | DIAG — purge at generator Phase 4 |
  | PS2_CRASHRING (emitted reference) | off | crash-ring branch consulted on indirect-dispatch paths in generated code | A2: move-to-diagnostics-build | DIAG — the ring itself moves to core/log; the generated-code consult becomes a core/triage-owned hook |
  | PS2_CALL_CENSUS / PS2_CALL_CENSUS_ROOT (emitted) | off | per-function call-census counters in generated dispatch | A2: move-to-diagnostics-build | DIAG (core/diag census; PS2R_DIAGNOSTICS builds) |
  | PS2_HAVE_BACKTRACE (emitted `#ifdef`) | build define | backtrace dump in generated FATAL paths | compile-time platform capability | DIAG — becomes a platform/ capability define consumed by core/triage crash capture |
- **Hacks / workarounds** (each with the correct implementation):
  - :903–:2156 — 265-site `base_address ==` emission chain (skip-body /
    seam-bind / probe-wrap per address, e.g. :903 0x2d1a50, :1344 0x173ce8
    CASE6-DIAG, :1702–:1825 libc addresses). Correct: v2 general emission +
    SdkBindingManifest rows (ARCH §4.9) for the sce seams; libc rows per
    HOOK §3.4. DELETE Phase 2.
  - :10464–:11901 — FUN_00177168 hand emitter (game's scene-dispatcher,
    23-target table @0x1772c0). Correct: v2 jump-table discovery emits the
    general-path body (RECOMP_P3A proved it). DELETE Phase 2.
  - 84 × `R"code(` raw-string manual bodies (e.g. :13233 FUN_002a0250,
    :18043 FUN_00255188) — hand-translated legacy bodies pasted as text.
    Correct: v2 auto-emission; per-address adjudication ledger (HOOK §3.2).
    DELETE Phase 2.
  - :25582 rewrite_manual_dispatch_file + :25696 inject_gen_hook_list — W16
    textual POST-PASSES over the generated file (regex-rewriting emitted C++
    after the fact). Correct: emit the right form the first time —
    `DispatchTables` owns hook/dispatch emission; post-passes die.
  - Emitted probes referencing fixed game addresses (0x1cd768 probe noted in
    recompiler.h snippet contract; comment narration of sessions throughout,
    e.g. :3613 "SINGLE dispatcher FUN_00177168" analysis, :20753 theory
    notes). Correct: probes via the GenProbe manifest channel; narration to
    docs/loop_state, one-line invariant comments only (ARCH §11).
  - `#include "host_app/cpu_state.h"` / `"host_app/memory.h"` — the TOOL
    includes RUNTIME headers (layering violation: tools/ must not depend on
    the runtime). Correct: emitted code includes runtime headers; the tool
    itself needs only the emission string templates — the split removes the
    includes.
- **Risks**: the split is the highest-token-count move in the shard; the
  golden-emission snapshots (Phase 8), RecompRegenDeterminismV2, the oracle
  family (selftest / planted-bugs / 600 s sweep), RecompCorpusAudit GAPS=0,
  and the full Phase-0 harness are the proving gates. The Phase-2 deletion is
  proof-gated by the per-address adjudication ledger (243 addresses) and the
  P4-F1/P4FIX-F1 root fixes.

### recompiler_tool/recompiler.h (155 lines)
- **Purpose**: Declaration of the `Recompiler` class: both constructors (with
  optional raw .text image for out-of-block delay-slot fetch), sharding API,
  direct-call gate, v2-emission config setter, snippet mode, W16 post-pass
  entry points, and the m_body_skipped / m_addr_to_id bookkeeping.
- **Public surface**: `class Recompiler`; `extern std::ofstream log_file`;
  `Recompiler::SnippetCase`.
- **Classification**: GENERIC-PS2 (tooling; the game specificity lives in the
  .cpp chain).
- **Refactor destination**: dissolves with the recompiler.cpp split — each new
  class gets its own header (`ir/mips_lifter.h`, `emit/cpp_emitter.h`,
  `emit/shard_writer.h`, `emit/dispatch_tables.h`, `emit/snippet_emitter.h`).
  The `set_v2_emission`/pin plumbing deletes in Phase 2 (pins retired);
  `set_direct_calls` deletes (direct-call NORM, classic map dispatch arm
  removed — Phase 8 flat tables supersede both).
- **Flags consumed here**: none.
- **Hacks / workarounds**: documents the [W16R1][FIX-E1/E2] post-pass
  contract (textual rewrite of generated files) — dies with the post-passes;
  the "0x1cd768 probe" avoidance note in the snippet contract is a
  game-address leak into a tool header — gone when probes go manifest-driven.
- **Risks**: none beyond the parent split.

### recompiler_tool/main.cpp (325 lines)
- **Purpose**: The LEGACY CLI. Parses --input/--ghidra-input/--shards/
  --direct-calls/--emit-snippets, loads the ELF via ELFIO, parses the Ghidra
  function JSON (`parse_ghidra_function_file`), injects the FUN_001017c8
  helper blocks, runs the Recompiler. Tail is a ~150-line commented-out
  ancient host-app main (dead text).
- **Public surface**: `main()` of target `recompiler_tool`.
- **Classification**: GAME-SPECIFIC glue over generic tooling (Ghidra JSON
  path, 1017c8 injection).
- **Refactor destination**: **DELETE in Phase 2** (MIGRATION §1.1 D1 row:
  "legacy recompiler engine: recompiler_tool legacy CLI (main.cpp)"). The
  --emit-snippets mode it hosts moves to the v2 CLI (`SnippetEmitter` entry
  point) so tests/recomp_semantics keeps its generator.
- **Flags consumed here**: none (CLI flags only).
- **Hacks / workarounds**: the giant commented-out legacy host main at the
  tail (dead scaffold narration — delete); `inject_fun_1017c8_helpers` call
  (Ghidra-split repair — retired with Ghidra input).
- **Risks**: the snippet-mode relocation must land before the file dies or
  RecompSemantics loses its generator — same-commit move, gate green.

### recompiler_tool/main_v2.cpp (572 lines)
- **Purpose**: THE v2 CLI. Runs discovery (`discover_program`), writes the
  deterministic discovery report, offers the P2 Ghidra comparison mode
  (absorbed-vs-true-gap classification with per-miss re-derived reasons),
  the P4 corpus-audit mode (--audit-corpus, exit 4 on gaps = gate-able), and
  the P3a emission path: optional Ghidra legacy-compat input, overrides-ini
  pins with ALL_MANUAL expansion, `build_v2_functions`, then the reused
  Recompiler with `set_v2_emission`.
- **Public surface**: `main()` of target `recompiler_tool_v2`.
- **Classification**: GENERIC-PS2 (the tool); game inputs (ELF path, corpus
  pairs) are data per TitleProfile.recomp.
- **Refactor destination**: `engine/tools/recompiler/main.cpp` — split:
  discovery+report+audit paths move verbatim; the `write_comparison` Ghidra
  diff mode, the --ghidra-input emission-compat branch, and the whole
  --overrides/--manual-json pin block **DELETE in Phase 2** (their inputs are
  §1.1 deletion rows). Corpus-pair file loading stays and its fixtures home
  in `engine/tools/recompiler/config/` (the ONLY per-game metadata input that
  survives, §1.2).
- **Flags consumed here**: none (CLI flags only).
- **Hacks / workarounds**: none beyond the compat channels scheduled to die;
  the comparison-report narration ("the 0x21bc1c lesson") is history that
  belongs in docs — the report generator deletes with its Ghidra input.
- **Risks**: gates that consume the deleted modes (RecompV2OverridePin*,
  RecompFunctionSetDiffV2, RecompV2OracleDiffLegacy) delete in the same
  commit (MIGRATION §3 step 7); the audit + report modes must stay
  byte-deterministic (CONVENTIONS P2) — RecompRegenDeterminismV2 proves it.

### recompiler_tool/discovery/discovery_types.h (133 lines)
- **Purpose**: The v2 discovery data model: `SectionImage` (VA-addressed
  section bytes), `DiscoveryInputs`/`DiscoveryOptions`, `JumpTable`,
  `SeedSource` (6 seed classes), `DiscoveredFunction` (entry/end/leaders/
  tables/seeds), `DiscoveryResult` (functions, edges, unreached gaps,
  table_data_ranges). Carries the licensing-attribution header (N64Recomp
  MIT / Ghidra Apache-2.0 concepts / PCSX2 concepts-only).
- **Public surface**: namespace `recomp_v2` types above; included by every
  discovery/emit/corpus TU and the tests.
- **Classification**: GENERIC-PS2 ("GAME-AGNOSTIC RULE (design §6): nothing
  in discovery/ may hardcode a the title address" — verified, none do).
- **Refactor destination**: `engine/tools/recompiler/discovery/discovery_types.h`
  — verbatim. Multiple PODs in one header is D4-legal (plain-data records
  adjacent to their owner module); if the lint objects, split JumpTable and
  DiscoveredFunction into sibling headers.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none.
- **Risks**: none; tests/recompiler_v2 fixture suite pins the API.

### recompiler_tool/discovery/leaders.h (41) + leaders.cpp (81)
- **Purpose**: Program-wide block-leader analysis (design §7.1): linear sweep
  over .text collecting every branch/jump target, delay-slot fall-throughs,
  jal/jalr return sites; the fixed-point half combined by discover_program.
  Delay-slot-overlap leaders (fixture DS-05, the FUN_00201428 archetype)
  handled.
- **Public surface**: `recomp_v2::analyze_leaders`, `LeaderAnalysis`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/recompiler/discovery/leaders.{h,cpp}`
  — verbatim (already one-concept-per-file).
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: none; RecompV2DiscoverySynthetic/RealElf pin behavior.

### recompiler_tool/discovery/jumptables.h (89) + jumptables.cpp (521)
- **Purpose**: The two-engine jump-table recovery cross-check: Engine 1
  forward register-state dataflow with stack spill/reload tracking (adapted
  from N64Recomp, MIT notice retained), Engine 2 backward slice + dominating
  sltiu/sltu guard + per-index evaluation + sanity check (adapted from
  Ghidra JumpBasic, Apache-2.0 attribution present). Disagreement = hard
  build error with dossier. Also `scan_code_constructed_pointers` (seed 6:
  lui/addiu pairs resolving into .text).
- **Public surface**: `recover_jump_table`, `TableRecovery`,
  `scan_code_constructed_pointers`.
- **Classification**: GENERIC-PS2 (bound cap 1024 configurable; ELF max
  observed 663 is a comment, not a constant).
- **Refactor destination**: `engine/tools/recompiler/discovery/jumptables.{h,cpp}`
  — verbatim. License attributions move with the code (binding).
- **Flags consumed here**: none. **Hacks**: none — this file is the D1
  root-fix for the entire manual-body population.
- **Risks**: the corpus audit (GAPS=0 over 1,642 observed pairs) is the
  standing oracle; any refactor touch re-runs RecompCorpusAudit.

### recompiler_tool/discovery/functions.h (41) + functions.cpp (589)
- **Purpose**: Multi-source function discovery + extents + the whole-pipeline
  entry `discover_program`: 6 seed sources unioned, global recursive descent
  (delay slots execute with their transfer; jr $ra/eret stop; jr rs runs
  table recovery), extent trimming to the reachable closure, shared-tail
  folding (folds_successor), unreached-gap diagnostics. Fixed-point iterated
  with leaders + tables.
- **Public surface**: `recomp_v2::discover_program`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/recompiler/discovery/functions.{h,cpp}`
  — verbatim.
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: the 8,819-function / 177-table-site result is the frozen
  baseline; RecompV2DiscoveryRealElf + the discovery report diff pin it.

### recompiler_tool/discovery/report.h (32) + report.cpp (79)
- **Purpose**: Deterministic JSON discovery-report writer (ordered iteration,
  no timestamps/paths/pointers — CONVENTIONS P2). Consumed by the G-FSET
  gate and the P2 comparison driver.
- **Public surface**: `write_discovery_report`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/recompiler/discovery/report.{h,cpp}`
  — verbatim.
- **Flags consumed here**: none. **Hacks**: none. **Risks**: none.

### recompiler_tool/discovery/rv2_decode.h (143 lines)
- **Purpose**: Minimal hand-rolled R5900 control-flow decoder (header-only):
  instruction Kind classification (branches/likely/J/Jal/Jr/JrRa/Jalr/Eret),
  branch/jump target math, prologue-shape predicate, `gpr_def` (which GPR an
  instruction defines — shared by both table engines so their kill-view is
  identical). Dependency-free by design (no rabbitizer in the engine lib);
  the test suite cross-derives targets with rabbitizer.
- **Public surface**: `recomp_v2::decode::{Kind, Insn, decode,
  is_prologue_word, gpr_def}`.
- **Classification**: GENERIC-PS2 ("no game addresses in this file, ever" —
  holds).
- **Refactor destination**: `engine/tools/recompiler/discovery/rv2_decode.h`
  — verbatim. Header-only free-function group is D4-legal (concept-named
  pair); inline defs are all trivial-ish decode switches — if the header
  lint objects, split a `rv2_decode.cpp`.
- **Flags consumed here**: none. **Hacks**: none. **Risks**: none.

### recompiler_tool/elf/elf_image.h (40) + elf_image.cpp (38)
- **Purpose**: The single ELF authority: loads the EE ELF once via ELFIO,
  exposes .text + all PROGBITS sections as SectionImages + entry point.
  Module rule: discovery/ reads only this; translate/emit never touch the
  ELF directly.
- **Public surface**: `recomp_v2::ElfImage::{load, text, data_sections,
  entry_point}`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/recompiler/loader/elf_image.{h,cpp}`
  — verbatim (the tree's `loader/` = "ELF sections, .text image, symbol
  seeds").
- **Flags consumed here**: none. **Hacks**: none. **Risks**: none.

### recompiler_tool/emit/v2_emission.h (52 lines)
- **Purpose**: `V2EmissionConfig` — the four v2 deltas the reused Recompiler
  consumes: pinned (legacy-body pins), aliases (+container maps), tables
  (per jr-site ELF-recovered target sets), extents (the D2 hard-error map).
- **Public surface**: `V2JumpTableEmission`, `V2EmissionConfig`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/recompiler/emit/emission_config.h`
  — split: `tables` + `extents` survive as the emission-model config;
  `pinned`/`aliases`/`alias_container*` **DELETE in Phase 2** (the pure
  discovery build has 0 aliases/compat/pins — proven by the P4 no-Ghidra
  build).
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: field deletion must ride the same commit as the main_v2 pin
  block deletion.

### recompiler_tool/emit/driver_v2.h (38) + driver_v2.cpp (273)
- **Purpose**: Builds the emission `Function` map + `V2EmissionConfig` from
  the DiscoveryResult. DATA ONLY — no instruction translated here; blocks
  decoded with rabbitizer byte-identically to EEAnalyze/analyze.cpp so
  translate_instruction sees identical inputs. Handles alias/compat/pin
  synthesis when the (optional) Ghidra map is present.
- **Public surface**: `recomp_v2_emit::build_v2_functions`, `BuildResult`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**:
  `engine/tools/recompiler/emit/emission_model_builder.{h,cpp}` — class
  `EmissionModelBuilder`; the ghidra_functions/pinned parameters and
  alias/compat synthesis DELETE in Phase 2 (signature narrows to
  discovery-result → model).
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: emission-model determinism (ordered containers) is load-bearing
  for RecompRegenDeterminismV2 — keep std::map/std::set.

### recompiler_tool/translate/control_flow_v2.cpp (105 lines)
- **Purpose**: The two v2-only emission deltas as Recompiler methods in their
  own TU: (1) `emit_v2_jr_table_switch` — jr sites emit a switch over
  EXACTLY the ELF-recovered target set (internal targets goto block labels,
  external targets dense-id tail dispatch, default = runtime-dispatcher
  fallback); unclassified sites emit fallback only (never the legacy
  all-leaders switch). (2) `emit_v2_alias_body` — forwarder bodies for
  absorbed legacy entries.
- **Public surface**: the two Recompiler methods (declared in recompiler.h).
- **Classification**: GENERIC-PS2.
- **Refactor destination**: jr-table emission →
  `engine/tools/recompiler/emit/indirect_dispatch.{h,cpp}` (class
  `IndirectDispatchEmitter`, absorbed into the CppEmitter family);
  `emit_v2_alias_body` **DELETE in Phase 2** (0 aliases under pure
  discovery).
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: RecompEnfSwitchTableV2 + corpus audit pin the switch shape.

### recompiler_tool/corpus/audit.h (65) + audit.cpp (188)
- **Purpose**: The corpus-vs-static-discovery differ (design §9): every
  runtime-observed jr/jalr (site→target) pair must be predicted as
  table/entry/leader/continuation; anything else is a precise GAP report.
  Deterministic markdown report; exit-4 gating.
- **Public surface**: `recomp_corpus::{load_corpus_files, run_corpus_audit,
  write_corpus_audit_report, CorpusGap, CorpusAuditResult}`.
- **Classification**: GENERIC-PS2 (corpus FILES are game data →
  TitleProfile.recomp / config/).
- **Refactor destination**: `engine/tools/recompiler/corpus/audit.{h,cpp}` —
  verbatim.
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: none; RecompCorpusAudit is itself a phase gate.

### recompiler_tool/corpus/recorder_gen.h (45) + recorder_gen.cpp (69)
- **Purpose**: Emits the corpus-as-audit recorder INTO the generated code:
  extern decls in recompiled.h, the flag+record+atexit-flush runtime in the
  common TU, and a guarded per-site probe at every jr/jalr target latch
  (covers all four indirect transfer paths uniformly — a dispatcher-seam
  recorder would miss goto/table paths). Env-gated `PS2_RECOMP_CORPUS=<path>`,
  default off; cost when unset = one predicted-not-taken branch per indirect
  transfer.
- **Public surface**: `recomp_corpus::{emit_header_decls, emit_runtime,
  emit_site_probe}`.
- **Classification**: GENERIC-PS2 (house probe convention, both engines).
- **Refactor destination**: `engine/tools/recompiler/corpus/recorder_gen.{h,cpp}`
  — verbatim; Phase 8 re-homes the injection under the GenProbe manifest
  channel (same emitted shape, manifest-declared instead of hardwired).
- **Flags consumed here**:

  | var | default today | what it selects | disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_RECOMP_CORPUS (emitted gate) | unset (off) | corpus recorder output path in generated code | harness capture channel | TEST-ONLY — stays as the corpus recorder (the audit's input producer); env→manifest at Phase 8 |
- **Hacks / workarounds**: none — the header comment documenting WHY the
  probe sits at the target latch is a keeper invariant comment.
- **Risks**: removing/moving the probe changes generated bytes → regen
  determinism + snapshot re-freeze via the ledger.

### recompiler_tool/legacy/recomp_overrides_config.h (151 lines)
- **Purpose**: Pure header-only parser for `recomp_overrides.ini` (the v2
  legacy-pin channel): `pin = 0xADDR,...` repeatable, `ALL_MANUAL` sentinel,
  strict 0x-hex parse, round-trip writer, `IsPinned` predicate. Shared by
  main_v2 emit and the RecompOverridesConfig ctest.
- **Public surface**: `recompovr::{Config, ParseConfigText, LoadConfigFile,
  WriteConfigText, SaveConfigFile, IsPinned}`.
- **Classification**: GENERIC-PS2 mechanism whose only purpose is
  GAME-SPECIFIC legacy-body pinning.
- **Refactor destination**: **DELETE in Phase 2** (MIGRATION §1.1 explicit
  row: "recomp_overrides.ini + the pin/pin-closure machinery
  (recompiler_tool/legacy/recomp_overrides_config.h) + …ctest gates").
- **Flags consumed here**: none.
- **Hacks / workarounds**: the file IS the workaround channel (per-address
  rollback to hand bodies). Correct implementation: none — v2 emission is
  root-fixed; A/B bisection of a future emitter regression is a git bisect +
  golden-emission snapshot diff, not a pin.
- **Risks**: tests/test_recomp_overrides_config.cpp +
  RecompV2OverridePin/PinAlias gates delete in the same commit.

### recompiler_tool/inject_fun_1017c8_helpers.h (47) + .cpp (186)
- **Purpose**: Injects 44 synthesized Function entries for the shared
  helper/suffix blocks inside FUN_001017c8 (the 0x297-entry object-factory
  dispatcher Ghidra split into ~217 pseudo-functions, missing the interior
  labels like 0x106878/0x106ab4/0x106cb4/0x106ccc) so legacy emission
  resolves runtime branches to them.
- **Public surface**: `inject_fun_1017c8_helpers(functions, text, size, base)`.
- **Classification**: GAME-SPECIFIC (hardcoded FUN_001017c8 internals).
- **Refactor destination**: **DELETE in Phase 2** (§1.6 row J names it
  explicitly: "the recompiler.cpp:903-1008 chain + inject_fun_1017c8 retire
  under D1"). v2 discovery recovers the 663-entry table and every interior
  leader natively.
- **Flags consumed here**: none.
- **Hacks / workarounds**: the whole file is a Ghidra-defect patch. Correct:
  v2 jump-table recovery + leader completeness (already proven).
- **Risks**: none — pure legacy-input repair; dead once Ghidra input dies.

### recompiler_tool/vu_aot_gen.cpp (1,470 lines)
- **Purpose**: Build-time VU1 microprogram → C++ generator ([R152-VUAOT]):
  reads Vu1Case corpus captures, emits `vu_aot_generated.inc` + 32 chunk
  files reproducing the ExecuteCached fastpack loop statement-for-statement
  with constexpr-folded decoded slots; JR/JALR handled by seeding every
  8-aligned pc and delegating unknown targets to ExecuteCachedResume;
  registry keyed by the shared VuHashImageFastRaw.
- **Public surface**: `main()` of tool `vu_aot_gen` (built only under CMake
  option PS2_VU_AOT=ON, default OFF).
- **Classification**: GENERIC-PS2 tooling for a retired tier.
- **Refactor destination**: **DELETE in Phase 1** (MIGRATION §1.1 VU-AOT
  row: default-OFF per NOAOT; generator + 33 chunk TUs + registry consumers
  + 6 ctest gates + 233 MB inc + 351 MB lib all delete). The VU1-HLE +
  cached-tier ladder is the shipped route.
- **Flags consumed here**: none read; honors vu_aot.ini
  (corpus_compile/collection keys) as tool config — dies with it. The CMake
  options PS2_VU_AOT / PS2_VU_AOT_PROFILE are DELETE (see §7).
- **Hacks / workarounds**: none (a disciplined generator; simply concluded).
- **Risks**: none — already excluded from default builds; deletion is a
  no-op proven by bit-identical harness (Phase 1 gate).

### Non-code strays in recompiler_tool/ (outside the 28)
- `CONVENTIONS.md` (135 ln) — the testable emission-rule catalog (DS/D/P
  rules). Moves to `docs/` beside INVARIANTS.md (it is the recompiler's
  invariant list; Phase 8's enforcers implement it). Keep.
- `fun_001017c8_jumptable.txt` (384 ln) — Ghidra-dump of the 663-entry table.
  Zero consumers (grep-verified). **DEAD — delete Phase 1** (v2 recovers the
  table from the ELF).
- `legacy/README.md` (18), `emit/README.md` (14), `corpus/README.md` (14),
  `translate/README.md` (12) — directory ownership notes; the non-legacy
  ones ride their directories (per-directory README is the sanctioned
  history home, ARCH §3); `legacy/README.md` deletes with legacy/.

---

## 2. iop_recompiler/ — 4 files, 1,741 lines

R3000A (MIPS I) → C++ static recompiler for IOP modules; sole real input is
STREAM.IRX (the one custom TT module — runtime-environment steering). Target
`iop_recompiler_tool` (root CMakeLists :471). Destination per ARCH tree:
`engine/tools/iop_recompiler/` (mechanism) with per-title IRX wiring as
TitleProfile.recomp data (§1.6 row G/J).

### iop_recompiler/iop_recompiler.h (59 lines)
- **Purpose**: `IopFunction` record + `IopRecompiler` class decl (import-stub
  map, .text setter, recompile-to-files). 32-bit IopContext target; no
  FPU/COP2/MMI.
- **Public surface**: `IopFunction`, `IopRecompiler`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/iop_recompiler/iop_recompiler.h` —
  verbatim.
- **Flags consumed here**: none. **Hacks**: none. **Risks**: none.

### iop_recompiler/iop_recompiler.cpp (1,010 lines)
- **Purpose**: The R3000A emitter: per-instruction translation with delay-slot
  and condition-latch rules mirroring the EE emitter; per-instruction labels;
  import-stub JALs emit HLE stub calls; thbase-ordinal commentary documents
  the S43/S44 import-mislabel lessons.
- **Public surface**: `IopRecompiler` methods.
- **Classification**: MIXED — the emitter is GENERIC-PS2; two injections are
  GAME-SPECIFIC (below).
- **Refactor destination**: `engine/tools/iop_recompiler/iop_recompiler.cpp`
  — verbatim minus the two injections, which become per-title data:
  - the `[STREAM-RPC-HLE]` hardcoded `if (pc == 0x000128a8)` injection
    (calls `iop_stubs::hle_stream_cd_read_rpc`) → a per-title IOP seam row
    (address→HLE call) in TitleProfile.recomp consumed generically; handler
    body lives in `games/<title>/iop/stream_irx/`.
  - the `[S63 STRMQ]` probe list (5 hardcoded STREAM.IRX addresses
    0xc8f0/0x594/0xe9d4/0xf230/0xfc0c) → DELETE (concluded investigation).
- **Flags consumed here** (emitted, not read):

  | var | default today | what it selects | disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_STRMQ (emitted :334) | off | ra-stamped entry traces at 5 STREAM.IRX job-retire/prime addresses | one-off session probe (S63, LOADTIME concluded) | DELETE — purge at generator; if ever needed again, GenProbe manifest |
- **Hacks / workarounds**:
  - :373 `if (pc == 0x000128a8)` — the sceSifCallRpc import thunk is an empty
    `jr ra` on stock, so the generator hand-injects an HLE call. PS2 rule:
    an IRX *import stub* gets its behavior from the exporting module at
    link/iopload time — the correct implementation is import-table-driven
    stub HLE (the import walker in main.cpp already finds stubs by
    IOP_IMPORT_MAGIC); the per-address special case becomes a generic
    "import ordinal → HLE binding" row owned by `engine/iop/hle/` with the
    STREAM.IRX-specific reply-buffer contract (IOP 0x15310/0x14a00) as title
    data.
  - :320–:342 STRMQ probe injection — session probe in a generator hot path.
    Correct: delete; manifest channel if resurrected.
- **Risks**: STREAM.IRX recomp output feeds the sound feed-protocol contract
  (A6 §5); proving tests = IopSysclibMap/IopSysmemHeap + the sound suites +
  cutscene harness legs (the 0x128a8 seam's motivating scene).

### iop_recompiler/main.cpp (599 lines)
- **Purpose**: CLI driver: loads the IOP ELF (ELFIO), walks import tables
  (IOP_IMPORT_MAGIC 0x41E00000) to name stub entries `library_funcN`,
  optionally consumes a Ghidra CSV of function bounds, falls back to built-in
  analysis, drives IopRecompiler to `iop_recompiled.{h,cpp}`.
- **Public surface**: `main()` of `iop_recompiler_tool`.
- **Classification**: MIXED — driver generic; `IMPORT_STUB_START/END =
  0x127E0/0x12B70` is STREAM.IRX-specific.
- **Refactor destination**: `engine/tools/iop_recompiler/main.cpp` — verbatim
  minus the hardcoded stub range: derive the stub extent from the
  import-table walk itself (the magic-word scan already locates every
  table), or failing that read it from TitleProfile.recomp. Per-title input
  path wiring (STREAM.IRX location) → TitleProfile.iop_modules.custom.
- **Flags consumed here**: none.
- **Hacks / workarounds**: the hardcoded import-stub address range ("known
  from analysis") — correct implementation is the table walk (mechanism);
  range constants are title data at worst.
- **Risks**: output regen must be byte-deterministic before/after the range
  derivation change — add an iop-regen determinism check (mirror of G-DET).

### iop_recompiler/ExportIopFunctions.java (73 lines)
- **Purpose**: Ghidra headless script exporting IOP function bounds to CSV
  (start,end,name) for the driver's optional --functions input. Adapted from
  ran-j's ExportPS2Functions.java.
- **Public surface**: Ghidra script class `ExportIopFunctions`.
- **Classification**: GENERIC-PS2 tooling.
- **Refactor destination**: `engine/tools/iop_recompiler/ExportIopFunctions.java`
  — verbatim (MIGRATION §1.6 row G/J keeps it with the driver). Long-term
  candidate for retirement if IOP discovery gets the v2 treatment, but no
  D1-equivalent decision exists for IOP — keep.
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: none (offline, optional input).

---

## 3. EEAnalyze/ — 10 files, 2,106 lines

MIPS static-analysis support library (rabbitizer-backed). Only `analyze.cpp`
is compiled into the `EEAnalyze` STATIC lib (root CMakeLists :28-32); the
recompiler tools additionally compile analyze.cpp directly. host_app links
EEAnalyze (host_app/CMakeLists.txt:599) but includes ZERO EEAnalyze headers
(re-verified this shard) — the vestigial link drops at migration, restoring
the "tools/ never linked into the runtime" rule (ARCH §3). Destination:
`engine/tools/EEAnalyze/` per MIGRATION §1.4 (6 survivors, 4 deletions).

### EEAnalyze/analyze.h (38 lines)
- **Purpose**: Declares `parse_ghidra_function_file` (Ghidra JSON → Function
  map with block decode), `find_function_starts` (JAL sweep), address-file
  loader.
- **Public surface**: the three functions; included by both recompiler CLIs.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/EEAnalyze/analyze.h` — verbatim in
  Phase 1; in Phase 2 `parse_ghidra_function_file` becomes dead (Ghidra input
  deleted) and is removed, leaving the block-decode/JAL-sweep helpers that
  driver_v2-successor code shares.
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: Phase-2 trim must not disturb the block-decode path driver_v2
  mirrors — RecompRegenDeterminismV2 proves regen identity.

### EEAnalyze/analyze.cpp (294 lines)
- **Purpose**: Implements the Ghidra JSON parser: reads function
  address/size/name entries, decodes each function's instruction words with
  rabbitizer into `Block`s (branch-target block splitting), builds the
  `Function` map the legacy emitter consumes.
- **Public surface**: as analyze.h.
- **Classification**: GENERIC-PS2 (consumes a game file; no game constants).
- **Refactor destination**: `engine/tools/EEAnalyze/analyze.cpp` — verbatim
  Phase 1; Phase 2 deletes the JSON-parsing half (input retired), keeping
  decode helpers. If nothing survives the trim, fold remains into the
  emission model builder and delete the TU (flag for the Phase-2 executor).
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: as analyze.h.

### EEAnalyze/Function.h (47 lines)
- **Purpose**: The `Block` (instructions, predecessors, successor indices)
  and `Function` (base_address, blocks, callee-saves, leaders) records — the
  emission model type both engines share.
- **Public surface**: `Block`, `Function`; included by recompiler.h,
  driver_v2, tests.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/EEAnalyze/function.h` — verbatim
  (rename to snake_case; two adjacent PODs = D4-legal).
- **Flags consumed here**: none. **Hacks**: none. **Risks**: none.

### EEAnalyze/register_state.h (82 lines)
- **Purpose**: Symbolic register-state lattice (Unknown/Constant/Symbolic/
  StackOffset variants with equality) for the dataflow engine.
- **Public surface**: `RegisterState` variants; included by Function.h chain.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/EEAnalyze/register_state.h` —
  verbatim.
- **Flags consumed here**: none. **Hacks**: none. **Risks**: none.

### EEAnalyze/DataFlowEngine.h (31 lines)
- **Purpose**: `DataFlowEngine` class decl (`analyze_block`: symbolic
  execution of one block over a RegisterStateMap). Alive only as a type
  dependency of Function.h's include chain after its .cpp died.
- **Public surface**: `DataFlowEngine`, `RegisterStateMap`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/EEAnalyze/data_flow_engine.h` —
  verbatim (or trim the Function.h include and delete both header + concept
  if nothing calls analyze_block — flag for Phase 1 executor; today the
  header must survive for compilation).
- **Flags consumed here**: none. **Hacks**: header kept alive purely by an
  include chain whose consumer never uses it — an include-hygiene wart.
  Correct: prune the include, then delete.
- **Risks**: none.

### EEAnalyze/main.cpp (165 lines)
- **Purpose**: Standalone disassembler CLI (the README `disasm.exe` workflow):
  loads an ELF, dumps analyzed functions/disassembly. Built by NO current
  target — kept as the library's CLI entry.
- **Public surface**: `main()` (unbuilt).
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/tools/EEAnalyze/main.cpp` — verbatim,
  and ADD a CMake target for it (an unbuildable CLI is half-dead; MIGRATION
  keeps it as "the library's CLI entry", so make that true).
- **Flags consumed here**: none. **Hacks**: duplicate include block (file
  literally includes the same headers twice) — tidy on move. **Risks**: none.

### EEAnalyze/unused_function.cpp (921 lines) — DELETE
- **Purpose**: An older jump-table analyzer (`JumpTableAnalyzer` over
  RegisterValue tracking) — superseded by the v2 two-engine recovery.
- **Classification**: GENERIC-PS2, dead (no CMake refs — verified).
- **Refactor destination**: **DELETE Phase 1** (MIGRATION §1.1 EEAnalyze row).
- **Risks**: none; zero build refs.

### EEAnalyze/jump_table_analysis.cpp (260 lines) — DELETE
- **Purpose**: Another standalone jump-table scan experiment (raw rabbitizer
  macros, own main-ish flow). Dead (no CMake refs).
- **Refactor destination**: **DELETE Phase 1** (§1.1).

### EEAnalyze/analyze_test.cpp (163 lines) — DELETE
- **Purpose**: gtest suite for the analyzer — ENTIRELY commented out (file
  body is one `/* ... */`). Dead scaffold.
- **Refactor destination**: **DELETE Phase 1** (§1.1). Real coverage lives in
  tests/recompiler_v2 + recompiler_verify.

### EEAnalyze/DataFlowEngine.cpp (105 lines) — DELETE
- **Purpose**: The dataflow engine's implementation (per-instruction symbolic
  execution). Not compiled by any target.
- **Refactor destination**: **DELETE Phase 1** (§1.1); the header survives
  the include chain until pruned (see DataFlowEngine.h).

---

## 4. tools/ — 74 code files, 9,019 lines (grouped by purpose)

Grouping and per-file dispositions follow MIGRATION §1.4's audited cluster row
(4+8+21+5+1+35 = 74 ✓), re-verified against the tree this shard. Non-code
strays (7 worklist .txt, flicker/README.md, `__pycache__`×3, .DS_Store) delete
with their campaigns / as hygiene.

### Group 4.1 — Recompiler companions (4 files) → `engine/tools/recompiler/` — verbatim
| file | purpose |
|---|---|
| tools/recomp_lint.py | offline lint over generated C++ (probe-silence / convention greps) — pairs with the RecompEnfLint enforcer |
| tools/disasm_func.py | disassemble one EE function from the ELF at a VA (rabbitizer-free quick look) |
| tools/decode_instr.py | decode single MIPS words (debugging emission questions) |
| tools/read_jt.py | dump N words at a VA from the ELF (jump-table inspection; usage: `read_jt.py 0x307b60 5`) |

- **Classification**: GENERIC-PS2 dev utilities. **Flags**: none.
- **Hacks**: none. **Risks**: none — path updates only.
- Note: `gen_merge.py` was formerly in this group; per D1 it moved to the
  delete group (4.6) — it lifts-and-merges Ghidra-split functions for the
  hand-rewrite flow whose products (fun_*_body.inc) and input (Ghidra JSON)
  both retire in Phase 2.

### Group 4.2 — SDK signature matching (8 files) → `engine/tools/sce_lib_finder/` — verbatim
| file | purpose |
|---|---|
| sce_lib_finder/__init__.py, __main__.py | package + CLI (`build-db --lib-dir…`, `scan --db… --elf…`) |
| sce_lib_finder/ar_reader.py | `.a` archive member reader (SDK 3.0.1 prebuilt libs) |
| sce_lib_finder/elf_reader.py | relocatable-object/ELF section+symbol reader |
| sce_lib_finder/mips_mask.py | relocation-aware instruction masking for signatures |
| sce_lib_finder/sig_gen.py | signature DB generation from the SDK drop |
| sce_lib_finder/sig_match.py | ELF scan + confidence-scored matching → crash_matches_full.json |
| detect_sdk_version.py | SDK 3.0.1 fingerprinting companion (header `libXXX` strings) |

- **Classification**: GENERIC-PS2 mechanism; its OUTPUT (the 291-row match
  set) is per-title data → `games/<title>/gen/sce_hle_bindings` (Phase 7/8).
- Under D2 this tool is **the ONLY way an HLE row acquires an address**
  (ARCH §4.9) — it graduates from side-tool to the SdkBindingManifest
  producer; the generate_bindings.py consumer (host_app side, shard f-host)
  redirects output to the build dir (§1.1 hygiene row).
- **Flags**: none. **Hacks**: none. **Risks**: manifest row count == 298 is
  the Phase-7 gate; keep the ambiguous/out-of-band rejection policy verbatim
  (it is the game-address-never-bound precedent, HOOK §3.3).

### Group 4.3 — Durable offline diagnostics (21 files) → `engine/tools/diagnostics/` — verbatim
| file | purpose |
|---|---|
| gs_dump_parser.py, parse_gsdump.py, summarize_gsdump.py, gsdump_state.py | PCSX2 .gs.zst dump family: per-draw GIF/GS decode, state reconstruction, summaries (ground-truth comparisons) |
| gif_diff.py | GIF packet stream differ (ours vs PCSX2) |
| dump_transfers.py | HWREG/TRX transfer extraction from dumps |
| fmv_draws.py | FMV draw census over dumps |
| host_log_parser.py | runtime-log → DrawBatch reconstruction (pairs with gs_dump_parser) |
| vu1_trace_diff.py | VU1 trace diff (vutrace corpus vs replay) |
| snap_query.py | PS2_SNAP RAM-dump query (matrix/struct readers) |
| dump_pcsx2_memory.py, ramread.py | PINE ground-truth capture; .ps2st guest-RAM word reads |
| ppm_to_png.py | frame-dump conversion |
| pss_census.py | offline PSS demux mirroring mpeg_hle ProcessBuffer (R196-FMV4) |
| ring_decode.cpp | offline .rlog → text decoder (built as `ring_decode`, root CMakeLists :263; the core/log ring's tool) |
| flicker/{bbdiff,chromaflip,greensig,pixflip,spatsig,tdiff}.py | presented-backbuffer/FBO comparators + flicker metrics — pairs with core/diag artdet (detector side) and core/triage (FIRE side per TRIAGE §12) |

- **Classification**: GENERIC-PS2 diagnostics (A2 §4 keep-list).
- **Refactor shape**: verbatim moves; `ring_decode.cpp` keeps its build
  target under the diagnostics dir (core/log ring decode tool named in the
  §1.4 keep-list row). flicker/README.md rides along.
- **Flags**: the tools read file arguments, not env. **Hacks**: none.
- **Risks**: none; these are read-only consumers of capture formats whose
  schemas (RING_LOG_SCHEMA.md, .gsdl, Vu1Case) are frozen or ledgered.

### Group 4.4 — Verification harness inputs (5 files) → `verify/` — verbatim (Phase 0)
| file | purpose |
|---|---|
| coldboot_gate.py | [S112-BLACKBOOT] cold-boot PIXEL gate (black-boot class caught by construction — named in Phase 0) |
| compare_frames.py | frame-hash/image comparison for harness legs |
| run_bounded.sh | bounded host_app runner: wall-clock timeout + RSS>8GB kill ([E2-SAVE-V2]) |
| recomp_gates/fps_class_gate.sh | G-FPS: fps class unchanged between engines (staged for P4) |
| recomp_gates/run_leg_gate.sh | G-LEGS: scripted boot→gameplay legs, bounded, log-grep verdicts |

- **Classification**: GENERIC-PS2 harness; the LEG SCRIPTS' scene lists are
  title data → `games/<title>/verify/` anchor lists (ARCH tree), the runner
  mechanisms stay in `verify/`.
- **Flags**: the gate scripts pass PS2_* env sets to the binaries under test
  — TEST-ONLY by construction; each pinned var retires with its gate wave
  (MIGRATION §3 step 7).
- **Hacks**: run_leg_gate's `bg + sleep + pkill` bounding is crude but
  intentional (documented); replaced by the Phase-0 harness runner's
  process control.
- **Risks**: Phase 0 rebuilds these into the one-runner harness — keep the
  originals until harness self-test passes (OLD vs OLD twice identical).

### Group 4.5 — Loader oracle (1 file) → `tests/` — verbatim
| file | purpose |
|---|---|
| loader_oracle_check.py | offline check of loader_oracle captures — the "oracle check remains a tests/ tool" of the loader-HLE row (§1.4); proves the cdvd/hle/loader completion-visibility model against recorded truth |

- **Classification**: MIXED — mechanism generic, the FUN_0017d6b8-shaped
  model it validates is title data (`games/…/hle/loader_sync.cpp`).
- **Risks**: retargets when loader_sync moves in Phase 7.

### Group 4.6 — Concluded one-off sessions (35 files) — **DELETE Phase 1** (gen_merge.py: Phase 2)
Per MIGRATION §1.4's delete list, verbatim (evidence: concluded sessions per
A2 §4; campaign products retired under D1):
- Hand-rewrite campaign (Ghidra-split repair; retired outright under D1):
  `gen_merge.py` (Phase 2 — its input is the Ghidra JSON), `gen_1017c8.py`
  (the FUN_001017c8 re-stitcher whose output became fun_*_body.inc),
  `swap_to_include.py`, `check_inc.py`, `brace_check.py`,
  `check_empty_switch.py`, `dump_rewrite_targets.py`, `warning_worklist.py`.
- Phase-B integration campaign (complete):
  `phaseb_integrate.py`, `phaseb_remaining.py`, `phaseb_status.py`,
  `phaseb_triage.py`.
- Bullseye scene probes (concluded): `build_bullseye_scene.py`,
  `dump_bullseye_slots.py`, `find_bullseye_calls.py`, `correlate.py`.
- Logo/scene probes: `logo_area.py`, `logo_timeseries.py`,
  `check_skydomes.py`.
- Backbuffer-dump census: `analyze_bb_dumps.py`, `sample_frame.py`,
  `scan_rows.py`, `color_map.py`, `scan_tbps.py`.
- Tile/atlas one-offs: `extract_tiles.py`, `render_tiles.py`, `tile_raw.py`.
- CLUT probes: `tmp_clut_full.py`, `tmp_clut_replay.py`, `tmp_pcsx2_clut.py`.
- Game-asset RE one-offs: `parse_bh.py`, `dump_bh_entries.py`,
  `find_in_bh.py`, `parse_rm2.py`.
- Log slicing: `per_frame_batches.sh`.

- **Hacks note**: several of these embed game addresses and scene constants
  (bullseye slots, logo rects, tile bases) — they are the *offline* face of
  the same session probes A2 §4 deletes on the runtime side; deleting both
  sides together keeps the HACK_LEDGER symmetric.
- **Risks**: none — no build refs; any knowledge worth keeping is already in
  docs/loop_state records.
- Non-code riders deleted with them: `phaseb_{all,clean,exclude,problem,
  remaining}.txt`, `REWRITE_TODO.txt`, `WARNING_WORKLIST.txt`,
  `__pycache__/`×3, `.DS_Store`.

---

## 5. tests/ — 158 files, 55,340 lines (grouped by what they pin)

Census reconciliation: 155 .cpp/.h/.py/.sh + 3 vu0_*_image.inc = 158;
excluded from the count: tests/CMakeLists.txt (3,173 — §7 below),
recompiler_v2/corpus_observed_p4.txt (1,648 — a corpus FIXTURE, mapped in
group 5.8), .DS_Store. tests/CMakeLists.txt registers 166 ctest entries; A5's
figure of 143 entries pinning 17 distinct PS2_* values is the env-pin
population — **every pinned arm retires in the same commit as its gate**
(MIGRATION §3 step 7); tests named below as "gate-off twin" are exactly those
arms. General destination rule (§1.2): "tests/ replay tools + suites →
tests/ (paths updated; env pins retired with their gates)"; per-module
placement below follows ARCH §10 ("every module directory owns its tests" —
realized as tests/<module>/ suites building against subsystem libs).

### Group 5.1 — GS soft-renderer property suites (9 files, ~4,900 ln) — KEEP → tests/gs/
`test_pixel_format_properties.cpp` (464), `test_tfx_properties.cpp` (325),
`test_dirty_tracking_properties.cpp` (260), `test_blend_properties.cpp` (505),
`test_texture_sampling_properties.cpp` (414),
`test_rasterization_properties.cpp` (292),
`test_display_magnification_properties.cpp` (182),
`test_integration_render_queue.cpp` (709), `test_multipass_pipeline.cpp` (686).
- **Pins**: the unified-vram-renderer property set (pixel format
  encode/decode, TFX, blend/test, sampling, raster, DISPLAY magnification,
  render-queue dispatch, multi-pass) — PS2 pixel-pipeline semantics
  independent of any backend.
- **Disposition**: KEEP, move verbatim; new modules under test:
  `engine/gs/format/` (Phase 3 PsmLayout/texa/alpha), `engine/gs/hle/queue/`
  and `engine/render/backends/soft/` (the oracle backend, Phase 10).
  `test_integration_render_queue.cpp:54-65` drops its mirrored
  ComputeVertex/BatchUniforms blocks in the same commit as the compute-raster
  deletion (§1.1 explicit COVERAGE-FIX row).
- **Env pins**: none. **Risks**: property tests are backend-agnostic — they
  are the cheapest Phase-3 regression net; run before/after every format
  move.

### Group 5.2 — Swizzle/block-decode equivalence (1 file, 1,150 ln) — KEEP → tests/gs/format/
`test_block_decode_equivalence.cpp` (1,150).
- **Pins**: [R121-DEC] block-table decode equivalence across the swizzle
  families.
- **Disposition**: KEEP; Phase 3 EXTENDS it to write-side and encode-side
  cross-proofs (MIGRATION §1.5 "4 swizzle families → PsmLayout table" oracle).
  New module: `engine/gs/format/psm_layout.*`.
- Companion dead copy `host_app/tests/swizzle/test_swizzle_equivalence.cpp`
  (601) + its CMakeLists (18) **DELETE Phase 1** (§1.1 row: "dead copy; live
  equivalents in tests/").

### Group 5.3 — GL serve/target/coherence harnesses (27 files, 12,582 ln) — KEEP through Phases 5–6, then retarget; gate-off twins DELETE Phase 4
`test_ztex_equivalence.cpp` (869 — R162/163 depth-as-texture),
`test_idxtex_equivalence.cpp` (1,423 — R164 PSMT8H channel-shuffle GPU serve),
`test_resmemo_equivalence.cpp` (1,838 — R172 residency memo),
`test_page_invariance.cpp` (457 — S101 page/tier/neighbour invariance),
`test_vsuv_equivalence.cpp` (618 — R169 T1; +VsUvEquivalenceAlt16Off arm),
`test_unifpack_equivalence.cpp` (524 — R169 T3),
`test_gl_state_cache_equivalence.cpp` (534 — R166),
`test_alias_rect.cpp` (604 — HUB1-C2 rect-scoped RefreshFBO),
`test_blendcp_lazy.cpp` (144 — HUB1-C1), `test_gl_l2_entries.cpp` (192 —
HUB1-T; +GlL2EntriesSuOff arm pinning PS2_GL_L2_SU=0),
`test_shuf_snap.cpp` (240 — R175), `test_idx_epoch_guard.cpp` (266 — R176),
`test_idx16_lane.cpp` (382 — R177), `test_rt_dirty_tiles.cpp` (132 — W14R3
G4 tile algebra), `test_w14r5_units.cpp` (351), `test_w14r6_units.cpp` (463 —
RUNDRAW battery), `test_r160_draw_merge.cpp` (280 — opaque draw-merge),
`test_shadow_offset.cpp` (261 — R185 menu-logo pin),
`test_newgame_switch.cpp` (282 — R187 pin), `test_blend_ff_map.cpp` (450 —
R178 ABCD→GL blend map; +GateOn/GateOff arms pinning PS2_GL_BLEND_SUB),
`test_s8_ctxlatch.cpp` (468), `test_s8_inrt.cpp` (175),
`test_s8_wrapblend.cpp` (131), `test_s9_composite.cpp` (322 — GD4 S9 recipe),
`test_s9_shadow.cpp` (266 — loop-21 shadow), `test_probe_bench.cpp` (248 —
R123 probe microbench + XXH64 pin), `test_gif_fast_equivalence.cpp` (662 —
R158 fast=0 vs fast=1).
- **Pins**: today's 17-route texture-serve ladder, TargetFBO coherence,
  GL state cache, blend mapping, S8/S9 shadow-chain fixes, GIF fast path —
  i.e. exactly the populations Phases 5–6 collapse into ONE ResolveRead /
  RenderTarget / PageRecord / one-flush.
- **Disposition**: KEEP as the §1.5 rewrite oracles through Phases 5–6, then
  RETARGET per concept: serve/route suites → `tests/gs/texsrc/` (ResolveRead
  owner-table cases), target/coherence suites (alias_rect, rt_dirty_tiles,
  idx_epoch_guard, shuf_snap, resmemo, page_invariance) →
  `tests/gs/targets/` + `tests/gs/vram/`, blend/state (blend_ff_map,
  blendcp_lazy, gl_state_cache, s8_wrapblend) → `tests/render/gl/`,
  S9/S8 suites → `tests/gs/classrdr/` with the recipe/pin data reading the
  RendererProfile (0x230000/0x280000 constants become `layout.is(...)`
  queries — A6 §3.3). `test_gif_fast_equivalence` → `tests/gif/` and its
  fast=0 arm DELETES when PS2_GIF_FAST-family gates purge (fast is NORM;
  the test converts to the unconditional-behavior form).
  **Gate-off twin ctest ARMS** (BlendFfMapGateOff, GlL2EntriesSuOff,
  VsUvEquivalenceAlt16Off, VifRunaheadBoundOff, VuSuperblock*Off,
  Vu0ShadowReplayFastpackOff) DELETE in Phase 4 with their gates.
- **Env pins**: PS2_GS_ALT16UV=0, PS2_GL_BLEND_SUB=0/1, PS2_GL_L2_SU=0,
  PS2_GS_DEPOSIT_EEAPPLY=0 (×3 sites), PS2_VIF_FAST=1 — all TEST-ONLY today;
  outcome: the =0 arms DELETE (losing arm), the =1 pins become redundant
  (NORM) and drop.
- **Hacks**: many of these encode game VRAM constants in fixtures
  (0x230000/0x280000/0x150000) — on retarget the constants come from the
  title RendererProfile fixture, keeping engine tests token-clean
  (LintEnginePurity applies to engine/, not tests/, but the fixture split
  keeps the [GD1] empty-profile leg honest).
- **Risks**: these ARE the safety net for the biggest rewrite; sequence =
  keep green on old code → move code → keep green retargeted → only then
  delete gate arms.

### Group 5.4 — GS native pipeline: DrawIR / classrdr / interp / replay / packets (9 files, ~4,300 ln) — KEEP → tests/gs/hle/
`test_gs_draw_ir.cpp` (379 — G-C1 converter equivalence),
`test_gs_classrdr.cpp` (323 — class-renderer framework oracle),
`test_gs_glbatch.cpp` (270 — GS-C2 consumer-side merge),
`test_gs_interp.cpp` (302 — Phase-E interpolation core),
`test_gs_packet.cpp` (163 — gsp:: builder/decoder round trip),
`test_gs_replay.cpp` (389 — GA2 replay-oracle gates), `gs_replay.cpp`
(1,103 — the replay oracle tool), `drawlist_tool.cpp` (326 — .gsdl reader),
`test_drawlist_record.cpp` (232 — recorder round trip).
- **Pins**: DrawIR conversion determinism (G-C1), classifier/fallback ladder,
  batch merge, interp stamp matching, recorder format, corpus replay census.
- **Disposition**: KEEP; replay tools move to `tests/` as first-class
  layering proofs (build against subsystem libs only, no SDL/GL — ARCH §10).
  New modules: `engine/gs/hle/ir/` (+G-C1 verifier), `engine/gs/hle/classrdr/`
  (framework half; Accept-tuple fixtures become RendererProfile data),
  `engine/gs/hle/interp/`, `core/diag/recorder`. gs_replay's census gates
  re-freeze ONLY via the adjudication ledger when Phase 6 extends the
  recorder (R9/R10).
- **Env pins**: PS2_GS_DEPOSIT_EEAPPLY=0 arms (deposit path) — the =1 path
  is NORM per the shard instruction list; =0 arms delete in Phase 4.
- **Risks**: recorder-format change (Phase 6 adds CLUT bytes/mips/dither) is
  an intentional-change re-freeze — EQUIVALENCE §6 protocol, not a silent
  edit.

### Group 5.5 — VU suites (13 code files + 3 .inc fixtures, ~6,400 ln) — KEEP (AOT subset DELETES)
`vu1_replay.cpp` (1,201 — offline VU1 case replayer; gates Vu1CachedCompare_*,
Vu1Hle_*, Vu1HleRef_*, Vu1HleCatalog, Vu1ReplayCachedCompare,
Vu1ReplaySelfTest), `test_vu_superblock.cpp` (1,579 — cached-tier superblock;
+GateOff/RunFlatOn/SpanFold2On/SpanFoldOff arms),
`test_vu0_shadow_replay.cpp` (347 — +FastpackOff arm),
`test_vu0_vis_micro.cpp` (259), `test_vu0_decal_eval.cpp` (124),
`test_lazyflags_algebra.cpp` (490 — R166 property gate),
`test_vu1_transform_gt.cpp` (175 — PCSX2 ground truth),
`test_vu_spanfold2.cpp` (86 — cached-tier W14R3 FIX-W2),
`vu1_test_shims.cpp` (75), `vu1_xgk_capture.h` (8),
`vu0_shadow_image.inc` (526), `vu0_vis_image.inc` (525),
`vu0_decal_eval_image.inc` (522) — plus the AOT subset:
`test_vu_aot_config.cpp` (239), `test_vu_aot_spanfold.cpp` (85),
`test_vu_clampelide.cpp` (159).
- **Pins**: cached-tier bit-exactness vs interpreter (126/126), VU1-HLE
  program equivalence vs capture corpus, VU0-macro micro replays against
  retail ground truth, lazy-flag algebra, superblock run flags.
- **Disposition**: KEEP → `tests/vu/`; new modules `engine/vu/tiers/`
  (interp+cached), `engine/vu/hle/` (mechanism) +
  `games/<title>/vu1_library/` (the Vu1Hle_* per-program
  gates become the title library's proof — Phase 9 gate: 126/126 + 3,293
  sweep FAIL=0). The vu0_*_image.inc are DATA fixtures (D4-exempt class 1
  analog for tests; keep as binary-ish fixture includes or convert to .bin
  loads — propose .bin conversion for the no-.inc rule's spirit).
  vu1_test_shims/vu1_xgk_capture → `tests/support/`.
  **AOT subset DELETES Phase 1** with the tier (§1.1: Vu1AotCompare_*,
  Vu1AotFallbackFabricate, VuAotSpanfoldEquivalence, VuClampElideEquivalence,
  VuAotLibOptimized gates; test_vu_aot_config tests vu_aot_config.h which
  dies with vu_aot.ini). `test_vu_spanfold2` is cached-tier — KEEP.
- **Env pins**: PS2_VU_MODE=cached, PS2_VU_FASTPACK=0, PS2_VU_RUNFLAT=1,
  PS2_VU_SPANFOLD=0/PS2_VU_SPANFOLD2=1/PS2_VU_STALLFOLD=0,
  PS2_VU_SUPERBLOCK=0, PS2_VU_AOT=0/1/OFF, PS2_VU_AOT_DISABLED=1,
  PS2_VU_AOT_SPANFOLD=1 — TEST-ONLY arms; RUNFLAT/RUNRETIRE-class expired
  default-ON hatches are NORM (arms delete Phase 4 wave c); AOT vars DELETE
  with the tier; PS2_VU_MODE remains only until the AOT/cached selection
  collapses to the HLE→cached ladder (then the pin drops as redundant).
- **Risks**: the vutrace corpus files these replay live under
  `games/<title>/corpus/` after §1.2's move — path indirection must land
  with the test move (one commit).

### Group 5.6 — VIF/GIF/MTVU (4 files, ~4,000 ln) — KEEP → tests/vif/, tests/gif/
`vif_replay.cpp` (1,950 — offline VIF1 chain replay; gates
VifReplayMtvuEquivalence{,ExecOn,ExecOnFast,Fast}, VifRunaheadBound{,Off},
VifUnpackBulkEquivalence{,Fast}; + `vif_replay_tsan` instrumented build),
`test_gif_drain_thread.cpp` (285 — HUB1-B gifdrain machinery; +Tsan arm).
(`test_gif_fast_equivalence` counted in 5.3.)
- **Pins**: VIF UNPACK bulk equivalence, MTVU submit-level equivalence
  (async vs sync), runahead bound, GIF drain-thread machinery.
- **Disposition**: KEEP; new modules `engine/vif/` (+ MTVU submit seam →
  `vu/tiers/mtvu_worker.*` — §1.4 "VIF-submit equivalence gate stays a
  test"), `engine/gif/`. The MTVU equivalence arms SURVIVE the purge (risk
  R6: MTVU stays verified by its own ctest arms even as the on-path becomes
  the shipped default); RunaheadBoundOff/Fast-off twins delete with their
  gates.
- **Env pins**: PS2_VU1_EXEC_ASYNC=1 (×3), PS2_MTVU_MAX_FRAMES=0/2,
  PS2_VIF_FAST=1 — TEST-ONLY; outcome: FAST pins drop (NORM), ASYNC/depth
  pins convert to Config-field-driven test parameterization
  (engine.mtvu/mtvu_depth).
- **Risks**: TSAN twins recompile large TUs — keep them but wire to the new
  library layout; they are the only data-race net on the queue seams.

### Group 5.7 — Timing / pacing / boundary (10 files, ~3,600 ln) — KEEP → tests/ee_timing/ + tests/gs/present/
`test_time_domain.cpp` (385 — S94-TIMEDOM + S96-BUDGETEMU),
`test_ee_pace.cpp` (507 — R170 park/ordering), `test_present_pace.cpp` (821 —
R193 band tables/deadlines), `test_fmv_pace.cpp` (498 — R136 authored-rate
delivery), `test_defer_boundary_order.cpp` (403 — R168 ordered boundary),
`test_ee_timers_timr.cpp` (95 — R195 TIMR savestate codec),
`test_r150_present_edge.cpp` (181 — CRTC-fallback accounting),
`test_r140_budget.cpp` (252 — CDVD delivery-budget meter),
`test_runahead_guard.cpp` (152 — R203 depth tripwire),
`test_r139_threads.cpp` (342) + `r139_shims.cpp` (96).
- **Pins**: the TimeDomain family (TickSource→EmuDomain→DerivedClocks),
  boundary protocol ordering, present pacing bands, FMV authored-rate,
  budget-anchor accounting, EE_GAME perf booking.
- **Disposition**: KEEP — these are Phase 7's named gates ("test_time_domain
  + pace unit gates"). New modules: `engine/ee/timing/`
  (time_domain/boundary_protocol/budget anchor — budget model PARAMS are
  TitleProfile.quirks data, so r140's constants read the title fixture),
  `engine/gs/hle/present/pacing.*`, `engine/ee/intc/`. r139's perf-booking
  half retargets `core/diag/perf.*`.
- **Env pins**: none registered. **Risks**: the timing family is purge wave
  (e) — LAST (interacts with the deterministic profile); these tests must be
  green under the pinned regime before any timing gate is touched.

### Group 5.8 — Recompiler verification (50 files + 1 corpus txt, ~13,600 ln) — KEEP core; legacy/pin arms DELETE Phase 2
- `recompiler_verify/` (22 files, ~4,600 ln): `interpreter.{cpp,h}` (1,307/229
  — INDEPENDENT R5900 reference interpreter), `test_interp_core.cpp` (711),
  `test_interp_controlflow.cpp` (431), `test_sut_runner.cpp` (457),
  `sut_runner.{cpp,h}` (223/80), `sut_link_shims.cpp` (316),
  `savestate.{cpp,h}` (493/77), `test_savestate.cpp` (146),
  `state_delta.{cpp,h}` (136/60), `mem_image.{cpp,h}` (22/128),
  `test_oracle_anchor.cpp` (315), `main.cpp` (75), `verify_assert.h` (49),
  + 4 tiny *_tests.h registries.
  **Pins**: interpreter-vs-generated-code semantic equivalence on anchor
  states (the oracle that replaces "trust the emitter").
  **Disposition**: KEEP → `tests/recompiler/verify/`; the SUT runner links
  the generated tree — retargets to the v2-only tree in Phase 2.
  RecompilerVerifySutCensus/CensusRoot arms (pin PS2_CALL_CENSUS=1,
  PS2_CALL_CENSUS_ROOT=0x1014d8) become PS2R_DIAGNOSTICS-build tests (DIAG).
- `recompiler_v2/` (20 code files, ~6,000 ln + corpus_observed_p4.txt 1,648):
  discovery tests (`test_discovery_synthetic.cpp` 197,
  `test_discovery_real_elf.cpp` 417), assembler + fixtures (`mips_asm.h` 131,
  `test_mips_asm.cpp` 203, `fixtures_{delayslot,structure,switch}.h`
  257/381/563, `fixture.h` 101), convention enforcers
  (`test_convention_enforcers.cpp` 871 — the six ENF families × legacy/V2
  profiles), whole-program gates (`test_recomp_gates.cpp` 636 — regen-det,
  function-set), the semantic oracle (`test_oracle_selftest.cpp` 835,
  `test_oracle_planted_bugs.cpp` 232, `test_oracle_diff_legacy.cpp` 486,
  `test_oracle_diff_v2.cpp` 675, `oracle_executor.{cpp,h}` 99/83,
  `oracle_tiers.h` 178, `oracle_asm_ext.h` 134, `diff_harness.{cpp,h}`
  291/128).
  **Pins**: v2 discovery correctness (synthetic + real ELF), the six emitted-
  code conventions on BOTH engine profiles, regen determinism, planted-bug
  sensitivity, differential sweeps.
  **Disposition**: KEEP the v2 arms → `tests/recompiler/`; **DELETE in
  Phase 2**: every legacy-profile arm (RecompEnf* legacy rows,
  RecompRegenDeterminism legacy, RecompContractSurfaceLegacy,
  RecompV2OracleDiffLegacy, RecompFunctionSetDiffV2's Ghidra diff,
  RecompV2OverridePin/PinAlias) — same commit as the engine deletion.
  `corpus_observed_p4.txt` (the 1,642-pair observed jr/jalr corpus) →
  `engine/tools/recompiler/config/` per §1.2's "corpus pair files" row
  (regenerated from the v2-only binary at the Phase-2 gate). Phase 8 adds
  golden-emission snapshots beside these.
- `recomp_semantics/` (5 files, ~770 ln): `test_recomp_semantics.cpp` (466),
  `cases.h` (167), `mem_shim.cpp` (83), `recomp_semantics_prelude.h` (32),
  `gen_words.cpp` (25).
  **Pins**: per-instruction emitted-C++ semantics via --emit-snippets.
  **Disposition**: KEEP → `tests/recompiler/semantics/`; generator entry
  moves to the v2 CLI with the snippet emitter (see §1 main.cpp risk).
- Related singles: `test_pause_gate_codegen.cpp` (172 — golden pin of the
  FUN_00177168 HAND-EMITTER head call) — **DELETE Phase 2** (pins a deleted
  special case; the general-path body is covered by the oracle);
  `test_e1_dispatch.cpp` (240 — W16 shadow-stack dispatch armor) — KEEP
  through Phase 2, RETARGET at Phase 8 to the flat-dispatch tables (same
  containment property, new mechanism);
  `test_recomp_overrides_config.cpp` (183) — **DELETE Phase 2** (§1.1 names
  its gate).

### Group 5.9 — EE runtime differentials (7 files, ~1,700 ln) — mixed
- `fastmem_diff/` (`test_fastmem_diff.cpp` 472 + `fastmem_stubs.cpp` 94):
  inline-fastmem vs slow-path differential — KEEP → `tests/ee_mem/`
  (fastmem emission is Phase-8 NORM; this is its standing gate).
- `libc_hle_diff/` (`test_libc_hle_diff.cpp` 570): native libc vs recompiled
  differential (R138) — KEEP → `tests/ee_hle/` (the 4 SdkWrap rows,
  HOOK §3.4; PS2_LIBC_HLE=0 pin arm deletes — LIBC_HLE is an expired
  default-ON hatch → NORM).
- `anim_pose_diff/` (`test_anim_pose_diff.cpp` 503 + `gen_pose_natives.py`
  230): native pose-mirror vs recompiled differential — **serves as the
  Phase-2 adjudication harness leg for the 9 anim-pose addresses, then
  DELETES** with anim_pose_hle (§1.1 last row; HOOK §3.2).
- `test_iop_sysclib_map.cpp` (107 — S44 import-ordinal pin) +
  `test_iop_sysmem_heap.cpp` (194 — S48 heap pin): KEEP → `tests/iop/`
  (engine/iop/hle modules).

### Group 5.10 — Savestate (4 files, ~1,200 ln) — KEEP → tests/core_savestate/
`test_savestate_format.cpp` (561 — .ps2st codec), `test_savestate_atomic.cpp`
(174 — tmp+rename), `test_savestate_writer.cpp` (212 — async writer),
`test_savestate_browser.cpp` (254 — GUI browser pure logic → app/gui test).
- **Pins**: the FROZEN PS2SAVE1 envelope (R1 critical risk), atomic write,
  async capture writer, browser logic.
- **Disposition**: KEEP verbatim; modules `core/savestate/` + `app/gui/`.
  Format tests are the R1 mitigation — never loosened; cross-load gates ride
  every phase.

### Group 5.11 — Sound (5 files, ~1,600 ln) — KEEP → tests/sound/
`test_sound_ring.cpp` (157), `test_sound_state.cpp` (381 — codec + FMV PCM
source), `test_sound_voice.cpp` (359), `test_vag_decoder.cpp` (236),
`test_snd_wire_governor.cpp` (274 — S46 wire-layer).
- **Pins**: MPSC ring, SPU2 voice SM, ADPCM decode, savestate codec, wire
  governor. Modules: `engine/sound/` (GENERIC per A6 §7 row G); the
  STREAM.IRX feed-contract cases read title fixtures.

### Group 5.12 — Diag / triage / log (7 files, ~1,800 ln) — KEEP, retarget per TRIAGE §12
`test_bug_report.cpp` (337 — bundler pure logic → retargets
`core/triage/{triage_bundler,bundle_manifest}` VERBATIM per TRIAGE §12),
`test_slowspot.cpp` (221 — detector window logic → `core/triage/perf_monitor`),
`test_slowspot_paths.cpp` (113 — capture naming → DELETES with
slowspot_paths.h when TriageBundler absorbs bundle layout),
`test_ring_log.cpp` (264 + RingLogTsan arm — E2-LOG ring →
`core/log/ring.*`), `test_bench_report.cpp` (421 — R127 report writer →
`core/diag/perf.*`; its totals API also feeds triage perf_buckets.tsv),
`test_r141_call_census.cpp` (382 — census, PS2_CALL_CENSUS pins → DIAG-build
test), `test_r144_list_leak.cpp` (262 — handle-heap leak → `tests/cdvd/`
against the hle_heap-successor cdvd/ modules).
- **Env pins**: PS2_LOG_RING_EVENTS=1024 (×3), PS2_CALL_CENSUS=1,
  PS2_CALL_CENSUS_ROOT=0x1014d8 — TEST-ONLY; ring-events becomes a test
  parameter of the ring API (constructor arg, not env); census pins move
  under PS2R_DIAGNOSTICS. Note 0x1014d8 is a game address inside a test pin
  — fine in tests, but the census-root default should come from a fixture,
  not the ctest line.

### Group 5.13 — App / platform / input / misc (6 files, ~1,400 ln) — KEEP
`test_display_settings.cpp` (137 — display.ini round trip → RETARGETS the
Config loader `[video]` section per §1.4), `test_viewport_math.cpp` (196 →
`gs/hle/present` fit/aspect math), `test_vconv_equivalence.cpp` (313 — R147
PS2_GL_VCONV_FAST conversion core; fast is NORM, off-arm deletes),
`test_shutdown_policy.cpp` (115 → `app/host/shutdown.*`,
config shutdown_wait_s), `test_ps2_argv.cpp` (156 — S8-ARGV pure logic →
`ee/hle/boot_env` mechanism + TitleProfile.boot data fixture),
`test_libmc.cpp` (521 — R203 mc save-flow → `tests/mc/`),
(input tests live in host_app/input/ — registered by tests/CMakeLists but
owned by the host shard; they move to `tests/platform_input/` per §1.4).

### Group 5.14 — Test-support stubs/shims (5 files, ~206 ln) — KEEP → tests/support/
`ring_log_stub.cpp` (56), `gui_stub.cpp` (32), `ee_timers_stub.cpp` (24),
`savestate_stub.cpp` (19), plus vu1 shims counted in 5.5.
- **Purpose**: no-op link backends so unit binaries can link REAL TUs
  (opengl_backend etc.) without the full app.
- **Disposition**: KEEP; shrink as the library split removes the need for
  monolith-TU linking (each stub should become unnecessary when its subject
  builds as a real library — track as a Phase-6/7 cleanup metric).

### Test-internal env knobs (flag completeness sweep, this shard)
Beyond the ctest ENVIRONMENT pins, the test SOURCES themselves read/set env:
- **Harness-private knobs (read)**: PAGEINV_DUMP, VIF_REPLAY_{MTVU_DEBUG,
  TTE2,RGBDUMP,LOG,DUMPW}, PS2_REPLAY_{SCANOUT,LOG,DUMPPPM,DUMPPPM_FRAMES,
  CONTENT}, PS2_RESMEMO_TEST_{VSUV,U9,T12}, TMPDIR — all TEST-ONLY debug
  outputs of the replay/equivalence harnesses; they never appear in runtime
  code. Outcome: TEST-ONLY, keep (convert to CLI flags of the replay tools
  when touched — env-in-tests is legal but flags are cleaner).
- **In-process setenv injections**: tests set PS2_MTVU, PS2_GL_FAST/FAST2,
  PS2_FASTMEM, PS2_LIBC_HLE, PS2_FMV_NATIVE, PS2_MTVU_DEFER, PS2_VUTRACE*,
  PS2_GS_DEPOSIT_EEAPPLY, PS2_GL_{VSUV,IDXTEX_16,IDX16_LANE,TEXCACHE,
  EPOCH_KEY,LAZY_RTSYNC,BBOX_COPY,VBO_RING}, PS2_VU_{STALLFOLD,SUPERBLOCK,
  XGKPACE_QUANTUM}, PS2_EE_PACE, PS2_CDVD_BUDGET_KB, PS2_MC_{LATENCY,
  PERSIST}, PS2_SOUND, PS2_SND_SELFTEST, PS2_PERF, PS2_DRAWLIST_RECORD
  before constructing the units under test — i.e. the tests are latch
  clients of the runtime's static-latch config. Outcome: when each family
  purges (Phase 4), the corresponding setenv lines convert to constructor/
  Config-struct parameters in the SAME commit (MIGRATION §3 step 7's test
  conversion); after Phase 4 the getenv-count-equals-config-loader lint
  applies to tests too (no setenv-based configuration of engine objects).

Group totals: 5.1(9) 5.2(1) 5.3(27) 5.4(9) 5.5(16) 5.6(2) 5.7(11) 5.8(50)
5.9(7) 5.10(4) 5.11(5) 5.12(7) 5.13(6) 5.14(4) = **158** — exact census
match (file-by-file list: /tmp/s116/f8/tests_census_158.txt; every census
file appears in exactly one group above; the replay tools gs_replay /
vif_replay / vu1_replay / drawlist_tool are counted inside groups
5.4/5.6/5.5/5.4 respectively).

---

## 6. host_app/fun_*_body.inc — ONE grouped entry (222 files, 159,811 lines)

- **Purpose**: Machine-restitched C++ bodies for 222 EE functions the LEGACY
  recompiler could not general-emit (Ghidra-split jump-table dispatchers,
  re-merged by tools/gen_1017c8.py / gen_merge.py into single faithful
  switch functions). Textually `#include`d by
  `host_app/manual_switch_dispatchers.cpp`, which installs them over the
  generated bodies at runtime (235 installs incl. 3 fully hand-written
  dispatchers + 10 sibling raw installs; anim_pose adds 9 mirrors; 243
  distinct addresses total — HOOK §2/§3.2).
- **Public surface**: none per file (each is a function body fragment); the
  installer TU is the only includer.
- **Classification**: GAME-SPECIFIC (every file is one EE address of this
  ELF); the pattern itself is the D4 counterexample ("no textual .inc
  includes for hand-written code" — ARCH §13 names this population
  explicitly).
- **Refactor destination**: **NOT MOVED — RETIRED in Phase 2** (MIGRATION
  §1.2 explicit row). They stay in place through the per-address adjudication
  (each is the OLD arm of its own harness leg: v2 emitted body vs hand body,
  hash-identical on the motivating scenes) and delete with the ledger's
  completion. Per HOOK_RECLASSIFICATION §3.2 they are
  EMULATION-BUG-WORKAROUNDs for two root-fixed defects: the legacy
  translation defect (fixed by v2 discovery/emission — corpus audit GAPS=0)
  and the emission-perf defect (fixed by ARCH §5.2 flat dispatch + fastmem).
  Expected end state: **0 hand bodies**; a divergence is never a reason to
  keep one (either a v2 emitter bug — fix it — or a smuggled behavioral fix
  — reclassify explicitly).
- **Flags consumed here**: none in the .inc bodies themselves (gates live in
  the installer, owned by the host shard).
- **Hacks / workarounds**: the entire population is one hack class. The PS2
  rule being worked around: indirect `jr rs` dispatch through in-ELF jump
  tables — owned in the refactor by the discovery jump-table engines +
  `emit/indirect_dispatch` (generation time) and the flat dispatch tables
  (runtime).
- **Risks**: R14 — a hand body that silently fixed behavior. Mitigation is
  the adjudication ledger itself + cross-engine savestate bisection anchors;
  proving tests: the Phase-2 gate (all 243 addresses proven, harness
  identical, RecompCorpusAudit green on a v2-only-recorded corpus).

---

## 7. Build system — CMake (4-file census set + tests/CMakeLists + presets + dead copy)

### CMakeLists.txt (root, 647 lines)
- **Purpose**: Project root: SDL2/elfio/rabbitizer deps, the EEAnalyze lib,
  BOTH recompiler tool targets + the recomp_v2_engine lib, the [NOAOT]
  VU-AOT tier switch (generator, 32-chunk registry lib, vu_aot.ini
  self-healing default-write, optimized-object post-build gate), ring_decode
  tool, game-file paths + the deferred Ghidra-presence resolution, generated-
  code custom commands (main tree + v2gen/ + legacygen/ reference trees),
  RECOMP_ENGINE selector, renderer-backend resolution, IOP recompiler target
  + STREAM.IRX generation (with placeholder-file fallback), host_app +
  tests subdirs, APPLE/MSVC flag blocks, single-TU job-pool serialization.
- **Public surface**: every build target and cache option; consumed by
  CMakePresets.json and CI.
- **Classification**: MIXED — build mechanism generic; ELF/IRX paths and the
  Ghidra requirement are title-bound.
- **Refactor destination** (MIGRATION §1.4 build-system row): **rewrite**
  into the ARCH §3 layout — one top-level CMakeLists.txt (project, toolchain,
  options incl. PS2R_DIAGNOSTICS + Config schema knobs) adding one
  CMakeLists.txt per top-level dir (tools/, core/, subsystem dirs, render/,
  platform/, app/, tests/, verify/); title inputs (ELF, ISO, IRX, corpus)
  become TitleProfile.recomp parameters of the `games/<title>_…` module.
  Deleted branches ride their §1.1 rows: VU-AOT machinery (whole [NOAOT]
  block, both arms — the option itself deletes), the RECOMP_ENGINE selector +
  legacy generation target + legacygen/ reference tree + configure-time
  Ghidra requirement (Phase 2, one engine one path), sce-bindings
  source-tree write redirected to the build dir, single-TU job-pool block
  (dies with the classic mode).
- **Flags consumed here** (CMake cache options — build-time gates):

  | var | default today | what it selects | disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_VU_AOT (option) | OFF | build the whole AOT tier | NOAOT decision record | DELETE Phase 1 (both arms; stand-in targets too) |
  | PS2_VU_AOT_PROFILE (option) | OFF | STAGE-0 counters in vu_aot_lib | AOT rider | DELETE Phase 1 |
  | VU_AOT_CORPUS_DIR / VU_AOT_CONFIG_INI (cache) | vutrace_cases2 / vu_aot.ini | AOT generator inputs | AOT riders | DELETE Phase 1 |
  | PS2_SHARDED_RECOMP (option) | ON | sharded -O2 emission vs single 91 MB -O0 TU | proven winner (R116/R126) | NORM — sharded is the only mode; single-TU arm + job-pool block delete |
  | PS2_RECOMP_SHARD_COUNT (cache) | 32 | shard TU count | budgeted output parameter (R7) | SETTING (build parameter; shard size watched in CI) |
  | PS2_RECOMP_DIRECT_CALLS (option) | ON | dense-id JAL dispatch vs classic map | proven winner (R137) | NORM — classic arm deletes; Phase 8 flat tables supersede the whole choice |
  | PS2_VU_O3 (option) | ON | vu.cpp at -O3 -funroll-loops | measured win (W13R7) | NORM — becomes the unconditional per-source flag |
  | RECOMP_ENGINE (cache) | legacy | legacy vs v2 generation | D1 decision | DELETE Phase 2 — v2 is THE engine (note: shipped play binary is ALREADY build-v2-nog) |
  | RENDERER_BACKEND (cache) | auto→opengl | GL vs (stale) Vulkan backend | real choice | SETTING — backend selection per ARCH §6; the stale-Vulkan branch deletes Phase 1 (rebuilt Phase 10) |
  | GAME_FILES_DIR / IOP_ELF_PATH (cache) | game_files/… | title inputs | per-title data | SETTING → TitleProfile.recomp inputs (per-title module config) |
  | PS2R_DIAGNOSTICS (new) | — | diag-module compile | target-design | the ONE new build gate the rewrite introduces (ARCH §9) |
- **Hacks / workarounds**:
  - vu_aot.ini self-healing configure-write + CONFIGURE_DEPENDS glob
    self-heal (:150-:168) — configure-time file authorship; dies with AOT.
  - Ghidra-absent placeholder logic + tests-skipped-when-no-JSON (:585) —
    the v2-only world makes both unconditional; delete.
  - IOP placeholder-file `file(WRITE …)` fallback when STREAM.IRX absent —
    keep the graceful-absence behavior but generate into the build dir via a
    proper stub target ([GD1]-style empty-title behavior).
  - `-Wno-error`/`-Wno-unused-*` blankets over generated code — scope to the
    generated targets only in the rewrite.
- **Risks**: R12-adjacent — the custom-command dependency graph (tool →
  generated → host_app) must be reproduced exactly; regen-determinism +
  bit-identical Phase-1 harness prove each edit.

### host_app/CMakeLists.txt (624 lines)
- **Purpose**: The runtime target: FFmpeg discovery (pkg-config + Homebrew
  fallback), sce-bindings regeneration INTO THE SOURCE TREE
  (sce_hle/sce_hle_bindings.inc, :63-:82), build-id custom target
  (cmake/gen_build_id.cmake), source lists incl. graphics backend selection
  (GL vs stale Vulkan + SPIR-V shader compile branch :499-:553), per-source
  optimization pins (generated common TU -O0, shards -O2, vu.cpp -O3,
  PS2_VU_AOT_DISABLED per-source defines), EEAnalyze link (:599 — vestigial).
- **Classification**: MIXED (mechanism + title-bound generated-code wiring).
- **Refactor destination**: **rewrite** — dissolves into per-directory
  CMakeLists of `app/host`, `engine/render/backends/gl`, and the subsystem
  libs. Explicit deletions: Vulkan shader-compile branch (§1.1),
  sce-bindings write into the source tree (redirect to build dir /
  `games/<title>/gen/`), the EEAnalyze link (zero includes — restores the
  tools-never-in-runtime rule), PS2_VU_AOT_DISABLED per-source defines (die
  with the tier). gen_build_id.cmake (22 ln) → a shared `cmake/` helper
  invoked by app/ (still emitting build_id.cpp into ${CMAKE_BINARY_DIR}).
- **Flags consumed here**: the per-source COMPILE_FLAGS pins are build policy
  (NORM as table above); no env gates.
- **Hacks**: generated-into-source (bindings .inc) — the §1.1 hygiene row;
  Python3-absent warning path silently shipping a stale checked-in bindings
  copy — becomes a hard configure error once bindings generate per-title
  into gen/.
- **Risks**: per-source flag fidelity (the -O0 common TU / -O2 shards / -O3
  vu.cpp split) is perf-load-bearing; bench_report comparisons before/after.

### tests/CMakeLists.txt (3,173 lines)
- **Purpose**: Registers 117 add_executable sites (several engine-/AOT-
  conditional) / 166 ctest entries: property
  suites, equivalence harnesses, replay tools, the recompiler_v2 gate family
  (engine-conditional blocks: legacy-profile enforcers vs
  RECOMP_LEGACY_REF_DIR, v2gen-profile enforcers, G-DET regen determinism per
  engine, oracle diffs), env-pinned A/B arms (~143 entries pin 17 distinct
  PS2_* values per A5), TSAN twins, and per-target links into host_app TUs +
  vu_aot_lib placeholders.
- **Classification**: GENERIC-PS2 test plumbing with title-corpus paths.
- **Refactor destination**: **rewrite** — one `tests/CMakeLists.txt` façade +
  per-module test registration beside each module (ARCH §10); moves with
  tests/ (counted in A1's tests bucket, §1.4 note). Deletions ride their
  rows: AOT gate registrations (Phase 1), legacy/pin/Ghidra-conditional
  blocks incl. RECOMP_LEGACY_REF_DIR plumbing (Phase 2), env-pinned losing
  arms (Phase 4 waves, same commit as each gate). NEW registrations landing
  here: `tests/lint/lint_one_class.py` (ctest LintOneClass) +
  `tests/lint/lint_engine_purity.py` (LintEnginePurity) — advisory at
  Phase 0, binding from Phase 3 (§13).
- **Flags consumed here**: the ENVIRONMENT pin table (TEST-ONLY, all): see
  the FLAG rollup below — PS2_VU1_EXEC_ASYNC, PS2_MTVU_MAX_FRAMES,
  PS2_VU_AOT{,_DISABLED,_SPANFOLD}, PS2_VU_{MODE,FASTPACK,RUNFLAT,SPANFOLD,
  SPANFOLD2,STALLFOLD,SUPERBLOCK}, PS2_VIF_FAST, PS2_GS_ALT16UV,
  PS2_GS_DEPOSIT_EEAPPLY, PS2_GL_BLEND_SUB, PS2_GL_L2_SU, PS2_LIBC_HLE,
  PS2_LOG_RING_EVENTS, PS2_CALL_CENSUS{,_ROOT}, PS2_EE_RAWDISP.
- **Hacks**: test targets recompiling whole host_app TUs (opengl_backend et
  al.) with stub link partners — the "monolith link" symptom; dissolves as
  subsystems become libraries (5.14).
- **Risks**: gate/arm deletion ordering — MIGRATION §3 step 7 is the rule;
  never delete a gate's test after the gate (same commit).

### host_app/tests/swizzle/CMakeLists.txt (18) + test_swizzle_equivalence.cpp (601)
- **DELETE Phase 1** (§1.1: dead copy; live equivalent is
  tests/test_block_decode_equivalence.cpp). Not referenced by any
  add_subdirectory (verified: nothing includes host_app/tests/).

### CMakePresets.json (63 lines)
- **Purpose**: Three configure presets — macos-play (Unix Makefiles, Debug,
  OpenGL, build/), windows-mingw (Ninja, Release, GLEW path, build-win/),
  windows-msvc (experimental, documented pthread blocker) — + matching
  build/test presets.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: KEEP — presets re-pointed at the new option
  schema (§1.4); gains a `deterministic` test preset for the Phase-0 profile
  and drops RECOMP_ENGINE-era guidance. The msvc preset's honest blocker
  note graduates into platform/windows' feasibility note (§16 pattern).
- **Flags**: none. **Risks**: none.

---

## 8. scripts/ (5 files) + root inject_logging.sh

### scripts/run_play.sh (189 lines)
- **Purpose**: THE play-mode launcher: sets the CANON env (dev defaults) and
  execs `./build-v2-nog/host_app/host_app` — i.e. the v2 pure-discovery
  binary is the shipped play path today. 100+ lines of flag documentation
  (the de-facto user manual for the env-gate era).
- **Public surface**: user entry point; A2's categorization anchor
  ("CANON = shipping settings").
- **Classification**: MIXED (launcher generic; CANON values are the peak
  config; HEADWATCH value is a game address).
- **Refactor destination**: **deleted at the end of Phase 4** (gate purge):
  the CANON winners become code (NORM) or Config defaults (SETTING), so
  launching becomes `app/host` with zero env. Until then it is the
  authoritative "confirm the winner" input for every purge family
  (MIGRATION §3 step 1). Any residual launcher need (bench/verify) is owned
  by verify/ (see TREE-GAP T-G2).
- **Flags SET here** (the PEAK ledger — dispositions per the shard
  instruction + A2):

  | var (CANON value) | disposition | refactor outcome |
  |---|---|---|
  | PS2_PERF=1 | diagnostic attribution | DIAG (core/diag perf; totals also feed triage perf buckets) |
  | PS2_LOG=0 | heavyweight trace off | DIAG (core/log categories; the =1 arm is the compile-out class) |
  | PS2_VU_MODE=cached | fallback-tier select | SETTING → Config engine.vu1_mode (HLE→cached ladder is NORM; cached remains the capture/fallback tier name) |
  | PS2_VIF_FAST=1 | VIF fast path | NORM |
  | PS2_GL_FAST=1 / PS2_GL_FAST2=1 | GL fast paths | NORM |
  | PS2_MTVU=1 | VU1 worker thread | NORM (per shard instruction); NOTE for consolidator: ARCH R6 keeps a deterministic-profile synchronous arm as Config engine.mtvu=false — record as NORM-with-Config-off in the master ledger |
  | PS2_EE_TIM0=1 | timer0 model + S40 sub-tick | NORM (proven fix) |
  | PS2_NOVSYNC=1 | present without vsync-block | SETTING → Config video.vsync (default off per VIS-R2) |
  | PS2_VBLANK_FREERUN=0 | real-time 60 Hz ticks | NORM (freerun becomes the bench profile's Config field time.freerun) |
  | PS2_CDVD_INSTANT=1 / PS2_DVD_LATENCY_SCALE=0 / PS2_MC_LATENCY=0 | instant loads | SETTING → Config cdvd.latency_model (instant default; the latency model survives as the title-quirk-parameterized mechanism) |
  | PS2_FPS_CAP=60 | frame cap | SETTING → Config video.fps_cap default 60 |
  | PS2_GUI_OPEN=1 | boot with dev GUI | SETTING → Config ui.gui_open (dev default) |
  | PS2_SLOWSPOT=1 | slow-frame capture | moves to core/triage (always-on product capture per TRIAGE §12; dev.slowspot_capture absorbed by triage.*) |
  | PS2_MTVU_MAX_FRAMES=2 | runahead depth | SETTING → Config engine.mtvu_depth=2 (S29-LAT evidence) |
  | PS2_HEADWATCH=0xd07640 | per-frame float watch (game addr) | DIAG (A2 hand-review: probe riding CANON; stick_trace hosts it in core/diag) |
  | PS2_EULERTRACE=1 (presence-gated) | matrix→euler tracer | DIAG (same; presence-gating dies with env gates) |
  | PS2_VU1_HLE=1 / PS2_VU1_HLE_DIRECT=1 | native VU1 HLE + typed submit | NORM (already default-ON in binary; pins redundant) |
  | PS2_GS_SCALE=2 | internal scale | SETTING → Config video.scale default 2 (scale=1 remains the correctness contract) |
  | PS2_GS_INTERP=1 | frame interpolation | SETTING → Config video.interp default on |
  | (documented, not CANON) PS2_LOAD_TURBO=0 default | load fast-forward | DELETE (proven no-op on wall time — LOADTIME §16; both arms go) |
  | (documented) PS2_LOAD_BUDGET_LIFT default ON | budget lift during hands-off loads | NORM |
  | (documented hatches, default ON) PS2_T0_SUBTICK, PS2_GS_RTKEEP, PS2_GS_WRAPBLEND, PS2_GS_ALT16UV, PS2_S9_ABIT16/ZSRC/CLUTRAW, PS2_SNDREG | proven-fix bisect hatches | NORM (winning arm only; losing arms delete Phase 4 wave c/d) |
  | (documented) PS2_SOUND, PS2_SHUTDOWN_WAIT | kill-audio / quit escalation | SETTING (Config audio.enabled, app.shutdown_wait_s) |
  | (documented capture) PS2_SND_CENSUS/KONLOG/MUSQ/SND_SELFTEST, PS2_DRAWLIST_RECORD, PS2_VUTRACE_PER_IMAGE, PS2_PAD_SCRIPT, PS2_RECOMP_CORPUS | opt-in capture | DIAG / TEST-ONLY (drawlist+vutrace+pad-script+corpus are harness channels; pad-script recorder is REHOMED to core/triage InputLog per Phase 4) |
- **Hacks**: HEADWATCH/EULERTRACE probes riding the play canon (A2's noted
  caveat); the 100-line comment manual (history-in-script — superseded by
  Config docs); the build-v2-nog hardcoded path.
- **Risks**: run_play.sh is the purge procedure's ground truth — it must be
  the LAST thing deleted in Phase 4, after every family's step-8
  differential run.

### scripts/run_play.ps1 (72 lines)
- **Purpose**: Windows PowerShell port of run_play.sh (same CANON,
  build-win/ path).
- **Disposition**: **deleted with run_play.sh** (Phase 4); until then must
  stay byte-synced with the CANON set (a two-copy drift hazard — see hacks).
- **Hacks**: duplicated CANON (mirror-latch pattern at script level; A2 §2.3
  class). Correct: one Config, zero launchers.

### scripts/run_bench.sh (124 lines)
- **Purpose**: Measurement-mode launcher: canonical bench gate set,
  --frames/--report/--window-start, WARP=N warp-to-frame plumbing, tees
  console, produces bench_report.json.
- **Classification**: GENERIC-PS2 harness.
- **Refactor destination**: `verify/bench/run_bench.sh` — verbatim-then-thin:
  the env set becomes `--profile bench` (a Config profile: freerun on,
  vsync off, warp params), leaving arg plumbing only. Suite-cost recording
  (A5 §1.3 gap) rides Phase 0.
- **Flags**: sets the bench profile (PS2_NOVSYNC=1→FREERUN auto) —
  SETTING→profile. **Risks**: bench_report schema consumed by
  test_bench_report + perf history; keep stable.

### scripts/check_vu_aot_optimized.sh (74 lines)
- **Purpose**: [W12R2] anti-regression gate failing the build if any
  vu_aot_programs* object is -O0/oversized (the OOM-killed-compile,
  stale-object-adoption autopsy).
- **Disposition**: **DELETE Phase 1** with the AOT tier (its subject and its
  ctest registration VuAotLibOptimized both die).
- **Hacks**: DWARF DW_AT_APPLE_optimized sniffing calibrated to Apple clang —
  clever, brittle, and moot after deletion. The general lesson (verify built
  artifacts' optimization class) is worth a docs/bugs entry.

### scripts/w14_make_backstop_cases.py (125 lines)
- **Purpose**: Fabricates the two backstop-exhaustion corpus cases for the
  AOT span-fold equivalence gate (infinite-loop images hitting the 1M-bundle
  backstop inside a folded span).
- **Disposition**: **DELETE Phase 1** (fabricates inputs for
  VuAotSpanfoldEquivalence, an AOT-only gate). If the cached-tier spanfold2
  gate ever needs backstop cases, regenerate under tests/vu fixtures.

### inject_logging.sh (repo root, 74 lines)
- **Purpose**: sed-patches build/recompiled.cpp IN THE BUILD TREE at fixed
  line numbers (:892457!) to inject FUN_002555f8 branch logging.
- **Classification**: one-off forensic (concluded session).
- **Disposition**: **DELETE Phase 1** (§1.1 repo-root cleanup class).
- **Hacks**: the purest expression of the pre-probe-channel era — editing
  generated output by absolute line number. Correct implementation: GenProbe
  manifest (Phase 8) / core/diag address-hook probe kit.

---

## 9. Config ini files

### vu_aot.ini (repo root, 21 lines)
- **Purpose**: Build input for the AOT generator (corpus_compile =
  auto|all|none) + runtime capture control (collection_enabled,
  collection_per_image — arms the VU1 case collector, forcing interpreter
  tier). Currently corpus_compile=none. Self-healed by CMake if deleted.
- **Classification**: GENERIC-PS2 config for a retired tier + a capture
  knob.
- **Refactor destination**: **DELETE Phase 1** with the AOT tier. The
  capture half (collection_enabled/per_image) survives as Config fields in
  the diag/capture surface — the VU capture→lift workflow is the Appendix-A
  second-game path, so `triage.vutrace_per_image`-class fields (Config,
  default off) replace it; PS2_VUTRACE* env vars ride the same purge.
- **Flags**: `corpus_compile` DELETE; `collection_enabled/per_image` →
  SETTING (Config capture fields, default off).
- **Risks**: none — corpus_compile=none already ships.

### recomp_overrides.ini (repo root, 33 lines)
- **Purpose**: The v2 pin channel (all comments today — ZERO active pins):
  `pin = 0xADDR` forces legacy bodies; documents pin-closure semantics
  (pinning a container pins its absorbed aliases).
- **Classification**: GAME-SPECIFIC rollback channel.
- **Refactor destination**: **DELETE Phase 2** (§1.1 explicit row, with the
  parser header, ALL_MANUAL machinery, switch_manual_enumeration.json, and
  the RecompV2OverridePin*/RecompOverridesConfig gates).
- **Flags**: the `pin` key — DELETE (nothing to pin to once legacy bodies
  are gone).
- **Risks**: none — the file is empty of pins in the shipping tree, so
  deletion is proven inert by the current build.

### display.ini (runtime artifact — NOT in the source tree)
- **Purpose**: Written/read next to the executable by
  host_app/display_settings.cpp (U3-SCREENFIT): persisted display keys
  (fit/scale/geometry); absent-file == pre-U3 defaults.
- **Classification**: user config surface.
- **Refactor destination**: keys fold into THE Config `[video]` section
  (§1.4 display_settings row: persisted keys → core/config [video]; live
  geometry publication → gs/hle/present view state). The separate ini dies —
  one config file, one loader; tests/test_display_settings.cpp retargets the
  Config loader round-trip.
- **Flags**: its keys become SETTINGs (video.* Config fields).
- **Risks**: silent default drift for existing users' saved display.ini —
  migration reads-and-converts once or documents the reset.

---

## SHARD SUMMARY

Legend: class G=GENERIC-PS2, GS=GAME-SPECIFIC, M=MIXED. Shape V=verbatim,
S=split, R=rewrite, D=delete, RET=retired (adjudicated then deleted).
flags column = counts by outcome N(ORM)/S(ETTING)/D(IAG)/T(EST-ONLY)/DEL.

| file / group | lines | class | destination | shape | flags (N/S/D/T/DEL) | hacks |
|---|---|---|---|---|---|---|
| recompiler_tool/recompiler.cpp | 25,813 | M | engine/tools/recompiler/{ir,emit}/ (5 class pairs) | S (+D Phase 2 freight) | 0/0/5/0/0 (emitted gates) | 6 |
| recompiler_tool/recompiler.h | 155 | G | dissolves into the 5 new headers | S | 0 | 2 |
| recompiler_tool/main.cpp | 325 | GS | — | D Phase 2 (snippet mode rehomed) | 0 | 2 |
| recompiler_tool/main_v2.cpp | 572 | G | engine/tools/recompiler/main.cpp | S (compat branches D) | 0 | 1 |
| discovery/discovery_types.h | 133 | G | …/discovery/discovery_types.h | V | 0 | 0 |
| discovery/leaders.{h,cpp} | 122 | G | …/discovery/leaders.* | V | 0 | 0 |
| discovery/jumptables.{h,cpp} | 610 | G | …/discovery/jumptables.* | V | 0 | 0 |
| discovery/functions.{h,cpp} | 630 | G | …/discovery/functions.* | V | 0 | 0 |
| discovery/report.{h,cpp} | 111 | G | …/discovery/report.* | V | 0 | 0 |
| discovery/rv2_decode.h | 143 | G | …/discovery/rv2_decode.h | V | 0 | 0 |
| elf/elf_image.{h,cpp} | 78 | G | …/loader/elf_image.* | V | 0 | 0 |
| emit/v2_emission.h | 52 | G | …/emit/emission_config.h | S (pin/alias fields D) | 0 | 0 |
| emit/driver_v2.{h,cpp} | 311 | G | …/emit/emission_model_builder.* | S | 0 | 0 |
| translate/control_flow_v2.cpp | 105 | G | …/emit/indirect_dispatch.* | S (alias emitter D) | 0 | 0 |
| corpus/audit.{h,cpp} | 253 | G | …/corpus/audit.* | V | 0 | 0 |
| corpus/recorder_gen.{h,cpp} | 114 | G | …/corpus/recorder_gen.* | V | 0/0/0/1/0 (PS2_RECOMP_CORPUS) | 0 |
| legacy/recomp_overrides_config.h | 151 | M | — | D Phase 2 | 0 | 1 (is one) |
| inject_fun_1017c8_helpers.{h,cpp} | 233 | GS | — | D Phase 2 | 0 | 1 (is one) |
| vu_aot_gen.cpp | 1,470 | G | — | D Phase 1 (NOAOT) | 0 | 0 |
| **recompiler_tool subtotal** | **31,381** | | | | | |
| iop_recompiler/iop_recompiler.h | 59 | G | engine/tools/iop_recompiler/ | V | 0 | 0 |
| iop_recompiler/iop_recompiler.cpp | 1,010 | M | engine/tools/iop_recompiler/ (seams→title data) | V−2 injections | 0/0/0/0/1 (PS2_STRMQ) | 2 |
| iop_recompiler/main.cpp | 599 | M | engine/tools/iop_recompiler/ | V (stub range derived) | 0 | 1 |
| iop_recompiler/ExportIopFunctions.java | 73 | G | engine/tools/iop_recompiler/ | V | 0 | 0 |
| EEAnalyze/analyze.{h,cpp} | 332 | G | engine/tools/EEAnalyze/ | V (Ghidra parser D Phase 2) | 0 | 0 |
| EEAnalyze/Function.h | 47 | G | engine/tools/EEAnalyze/function.h | V | 0 | 0 |
| EEAnalyze/register_state.h | 82 | G | engine/tools/EEAnalyze/ | V | 0 | 0 |
| EEAnalyze/DataFlowEngine.h | 31 | G | engine/tools/EEAnalyze/ (then prune) | V | 0 | 1 |
| EEAnalyze/main.cpp | 165 | G | engine/tools/EEAnalyze/ (+build target) | V | 0 | 1 |
| EEAnalyze/{unused_function,jump_table_analysis,analyze_test,DataFlowEngine}.cpp | 1,449 | G | — | D Phase 1 (dead) | 0 | 0 |
| tools/ 4.1 recompiler companions (4) | ~450 | G | engine/tools/recompiler/ | V | 0 | 0 |
| tools/ 4.2 sce_lib_finder (8) | ~1,800 | G | engine/tools/sce_lib_finder/ | V (output→games/gen) | 0 | 0 |
| tools/ 4.3 diagnostics (21) | ~3,600 | G | engine/tools/diagnostics/ | V | 0 | 0 |
| tools/ 4.4 verify inputs (5) | ~700 | G/M | verify/ (+title anchor lists→games) | V | 0/0/0/all/0 | 1 |
| tools/ 4.5 loader oracle (1) | ~150 | M | tests/ | V | 0 | 0 |
| tools/ 4.6 concluded one-offs (35) | ~2,300 | GS-heavy | — | D Phase 1 (gen_merge Phase 2) | 0 | many (offline probe class) |
| tests/ 5.1 GS properties (9) | ~4,900 | G | tests/gs/ (gs/format+soft+queue) | V | 0 | 0 |
| tests/ 5.2 block-decode equiv (1) | 1,150 | G | tests/gs/format/ (extended Ph3) | V+extend | 0 | 0 |
| tests/ 5.3 GL serve/target (27) | 12,582 | M | tests/gs/{texsrc,targets,vram}, tests/render/gl (Ph5-6 oracles) | V→retarget; off-twins D Ph4 | 0/0/0/6+/0 | fixtures carry VRAM pins |
| tests/ 5.4 DrawIR/classrdr/replay (9) | ~4,300 | G/M | tests/gs/hle/ + replay tools | V | 0/0/0/1/0 | 0 |
| tests/ 5.5 VU (16 incl. 3 .inc) | ~6,400 | G/M | tests/vu/ (+ title vu1_library gates); AOT 3 D Ph1 | V / D | 0/0/0/10+/AOT | .inc fixtures→.bin |
| tests/ 5.6 VIF/GIF/MTVU (2+tsan) | ~4,000 | G | tests/vif/, tests/gif/ | V | 0/0/0/6/0 | 0 |
| tests/ 5.7 timing/pacing (11) | ~3,600 | G/M | tests/ee_timing/, tests/gs/present/ | V (Ph7 gates) | 0 | 0 |
| tests/ 5.8 recompiler suites (50+txt) | ~13,600 | G | tests/recompiler/{verify,semantics}; legacy arms D Ph2 | V / D | 0/0/2/2/0 | 0 |
| tests/ 5.9 EE differentials (7) | ~1,700 | G/GS | tests/ee_mem, tests/ee_hle, tests/iop; anim_pose D Ph2 | V / RET | 0/0/0/1/1 | 0 |
| tests/ 5.10 savestate (4) | ~1,200 | G | tests/core_savestate/ | V | 0 | 0 |
| tests/ 5.11 sound (5) | ~1,600 | G | tests/sound/ | V | 0 | 0 |
| tests/ 5.12 diag/triage/log (7) | ~1,800 | G | core/triage + core/log + core/diag tests | V→retarget; slowspot_paths D | 0/0/3/2/0 | 1 |
| tests/ 5.13 app/platform/misc (6) | ~1,400 | G/M | tests/{config,mc,present,app} | V→retarget | 0 | 0 |
| tests/ 5.14 support stubs (4) | ~131 | G | tests/support/ | V (shrink as libs land) | 0 | 1 (monolith-link symptom) |
| host_app/fun_*_body.inc (222, grouped) | 159,811 | GS | — (adjudication OLD arms) | RET Phase 2 | 0 | 1 (is one class) |
| CMakeLists.txt (root) | 647 | M | per-directory CMake tree | R | 5N/3S/1D/0/4DEL (build opts) | 4 |
| host_app/CMakeLists.txt | 624 | M | app/ + backends/ + subsystem CMake | R | (in root's counts) | 2 |
| tests/CMakeLists.txt | 3,173 | G | tests/ façade + per-module regs | R | ~17 distinct pins → T | 1 |
| host_app/tests/swizzle/* (2) | 619 | G | — | D Phase 1 | 0 | 0 |
| CMakePresets.json | 63 | G | kept, re-pointed | V | 0 | 0 |
| host_app/cmake/gen_build_id.cmake | 22 | G | cmake/ helper (app/) | V | 0 | 0 |
| scripts/run_play.sh | 189 | M | — (CANON→Config defaults) | D end of Phase 4 | 8N/8S/5D/4T/1DEL (set, not read) | 3 |
| scripts/run_play.ps1 | 72 | M | — | D with run_play.sh | (mirror) | 1 |
| scripts/run_bench.sh | 124 | G | verify/bench/ | V→thin (--profile bench) | 0/1S/0/0/0 | 0 |
| scripts/check_vu_aot_optimized.sh | 74 | G | — | D Phase 1 (NOAOT) | 0 | 1 |
| scripts/w14_make_backstop_cases.py | 125 | G | — | D Phase 1 (NOAOT) | 0 | 0 |
| inject_logging.sh (root) | 74 | GS | — | D Phase 1 | 0 | 1 (is one) |
| vu_aot.ini | 21 | G | — (capture keys→Config) | D Phase 1 | 0/2S/0/0/1 | 0 |
| recomp_overrides.ini | 33 | GS | — | D Phase 2 | 0/0/0/0/1 | 1 (is one) |
| display.ini (runtime artifact) | n/a | G | core/config [video] | R (fold) | 0/all S/0/0/0 | 0 |

### TREE-GAPs
1. **T-G1 `engine/tools/EEAnalyze/` naming**: the ARCH §3 tree spells it
   `EEAnalyze/` (CamelCase directory) — inconsistent with the snake_case
   file/dir convention (§11). Propose `engine/tools/ee_analyze/` at move
   time; MIGRATION §1.4 uses `tools/EEAnalyze/` so flagging rather than
   deciding.
2. **T-G2 launcher/bench home**: the tree has `verify/` (harness) but no
   named home for the human-facing bench launcher and its report schema.
   Propose `verify/bench/` (run_bench.sh + bench_report schema doc). The
   play launcher needs NO home (dies with the gates), but the interim
   Phase-1..4 window still needs run_play.sh — it simply stays in place
   until its deletion phase.
3. **T-G3 per-title IOP seam table**: §1.6 row G gives STREAM.IRX protocol
   handlers a home (`games/…/iop/stream_irx/`) but the GENERATOR-side
   per-title seam input (the 0x128a8 import-thunk HLE row and any future
   ones) has no named file. Propose
   `games/<title>/iop/stream_irx/recomp_seams.toml`
   consumed by `engine/tools/iop_recompiler/` via TitleProfile.recomp.
4. **T-G4 tests tree shape**: ARCH §10 says "every module directory owns its
   tests" while the tree shows a top-level `tests/`; the per-group
   destinations above (tests/gs/format, tests/vu, …) assume the top-level
   tree mirrors module paths. Either reading works; the lint + ctest
   registration need ONE convention — flag for the consolidator.
5. **T-G5 CONVENTIONS.md home**: the recompiler's testable-rule catalog has
   no named slot in docs/ (INVARIANTS.md is the GS list). Propose
   `docs/RECOMP_CONVENTIONS.md` kept in sync with the enforcer suite.

### Dead code identified in this shard (with evidence)
- `EEAnalyze/unused_function.cpp`, `jump_table_analysis.cpp`,
  `DataFlowEngine.cpp` — zero CMake references (grep of all three
  CMakeLists, this shard).
- `EEAnalyze/analyze_test.cpp` — entire body inside one `/* */` block AND no
  CMake reference.
- `EEAnalyze/main.cpp` — built by no target (kept deliberately as CLI entry;
  needs a target or it stays half-dead).
- `recompiler_tool/fun_001017c8_jumptable.txt` — zero consumers
  (repo-wide grep).
- `recompiler_tool/main.cpp` tail — ~150 lines of commented-out ancient
  host main inside the live file.
- `host_app/tests/swizzle/` (CMakeLists + test) — no add_subdirectory
  reaches it; live equivalent in tests/.
- `recompiler_tool/vu_aot_gen.cpp` + `scripts/check_vu_aot_optimized.sh` +
  `scripts/w14_make_backstop_cases.py` + `vu_aot.ini` — compiled/used only
  under PS2_VU_AOT=ON (default OFF since NOAOT); dead in every shipping
  configure.
- `recomp_overrides.ini` — zero active pins (comments only); the machinery
  it feeds is Phase-2-dead.
- `inject_logging.sh` — patches a build-tree file at hardcoded line numbers;
  meaningless against any regenerated tree.
- host_app links `EEAnalyze` (host_app/CMakeLists.txt:599) with zero
  includes — a dead edge, not a dead file.

---

## Appendix — Independent review (coverage / accuracy / flags / tree consistency)

Performed after drafting, against the repo and the plan docs (this shard,
2026-09-23):

1. **Coverage**: 28/28 recompiler_tool code files have entries (name-by-name
   checklist in /tmp/s116/f8/); 4/4 iop_recompiler (task brief said 3 — the
   `.h` was the uncounted file; documented); 10/10 EEAnalyze; 74/74 tools
   (group arithmetic 4+8+21+5+1+35 matches MIGRATION §1.4's audited row);
   158/158 tests (census file /tmp/s116/f8/tests_census_158.txt; group sum
   9+1+27+9+16+2+11+50+7+4+5+7+6+4 = 158); the 222 .inc grouped entry
   matches `ls host_app/fun_*_body.inc | wc -l` = 222 / 159,811 lines;
   CMake 4-file census set + tests/CMakeLists + presets + dead swizzle copy
   all entered; 5 scripts + inject_logging.sh + 2 source-tree inis +
   display.ini (runtime artifact) entered.
2. **Accuracy corrections made during review**: group 5.5 count 21→16,
   5.8 43→50, 5.13 7→6 (sums re-verified); "82 executables"→117
   add_executable sites; raw-string body count verified 84 `R"code(`
   (matches legacy/README's "84 raw-string bodies"; MIGRATION's "143" is the
   manual-ENUMERATION count, a different population — both stated).
   run_play.sh's binary path verified as build-v2-nog (v2 pure-discovery is
   already the played engine — strengthens the Phase-2 case; noted in §1).
3. **Flag completeness**: tool-side emitted gates (PS2_SHADOW_DIAG ×4,
   PS2_EE_RAWDISP ×1, PS2_RECOMP_CORPUS, PS2_STRMQ, crashring/census/
   backtrace references) all tabled; CMake option gates tabled (11);
   ctest ENVIRONMENT pins tabled (17 distinct per A5, sample-verified);
   test-source getenv/setenv sweep added as its own subsection (harness
   knobs + latch-client injections). PS2_RECOMP_CORPUS and PS2_STRMQ are
   NOT in the A2 registry (post-audit additions) — the master FLAG_LEDGER
   consolidator must add both rows.
4. **Tree consistency**: every destination is a path that exists in ARCH §3
   (engine/tools/recompiler/{config,loader,discovery,ir,emit,corpus},
   engine/tools/{iop_recompiler,sce_lib_finder,EEAnalyze,diagnostics,triage},
   tests/ + tests/lint/, verify/, games/<title>/{gen,
   corpus,verify,iop/stream_irx}) or is flagged TREE-GAP (5 gaps listed).
   One deliberate tension recorded for the consolidator: PS2_MTVU is NORM
   per the shard instruction's example list but ARCH R6/§8 keeps a
   deterministic-profile synchronous arm — recorded as NORM-with-Config-off
   in both the run_play.sh table and the summary.
5. **Docs-only check**: no code, no repo writes outside
   docs/filemap/FILEMAP_toolchain_tests_tools_build.md and /tmp/s116/f8/.

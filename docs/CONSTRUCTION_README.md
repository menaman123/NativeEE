# NativeEE — construction tree (NEW): the construction record

(Moved from the repository root on 2026-10-12 when README.md was rewritten for users and developers; links in this file are relative to the repository root.)

The project target is an **OpenGOAL-style native the title port**.
DMAC/VIF/GIF, the IOP kernel and STREAM.IRX, and the IPU command interface are
temporary scaffolding. New work replaces the game's stream, movie, audio and
graphics intents with native paths, using the decompilation's platform layer
as the specification. See the
[scaffolding retirement table](docs/design/HLE_COVERAGE.md#scaffolding-retirement)
and [current design rule](docs/design/ARCHITECTURE.md#current-design-rule--native-game-intents-user-2026-10-07).

This repository is the **construction site** of the the title (SLUS-12345)
PS2 static-recompilation runtime refactor. The original repository at
`$PS2R_OLD_ROOT` is **frozen read-only** since
2026-09-26 ([docs/REFERENCE_FREEZE.md](docs/REFERENCE_FREEZE.md)): its sources are
read when transplanting, its binaries run bounded as the OLD oracle, and nothing is
ever written or built there. Everything new is built HERE, bottom-up, one phase at a
time, and every phase is proven against the frozen OLD hash baselines in
[`games/<title>/verify/baseline/`](games/<title>/verify/baseline/) (moved there by path in P8.36-D; the harness stays in `verify/`).

The plan: [docs/design/CONSTRUCTION_PLAN.md](docs/design/CONSTRUCTION_PLAN.md).
The target: [docs/design/ARCHITECTURE.md](docs/design/ARCHITECTURE.md).
The proof: [docs/design/EQUIVALENCE_PLAN.md](docs/design/EQUIVALENCE_PLAN.md).
Per-file dispositions: [docs/filemap/INDEX.md](docs/filemap/INDEX.md) (+ FLAG_LEDGER,
HACK_LEDGER; `MIGRATION_PLAN.md` is superseded in shape, retained for its rows).

## Status

| Phase | Content | Status |
|---|---|---|
| 0 | Verification harness (OLD-side emitters + `verify/` scripts), deterministic profile, 12-leg OLD baselines frozen (3-rep stable); **tree skeleton, root CMake, empty `nativeee`, `LintOneClass` + `LintEnginePurity` as ctest** | **DONE** — harness: [PHASE0_IMPL.md](docs/phases/PHASE0_IMPL.md) (gate passed 2026-09-25); skeleton: this session 2026-09-26 (configure/build/ctest 6/6 green, quoted below) |
| 1 | `engine/core/{config,log,diag,savestate,threads,hash,verify}` + `platform/macos` + `app/host` + `tests/core` + harness cross-engine mode → the NEW binary writes a `diff_legs`-compatible leg from its first boot | **PLANNED** — [PHASE1_PLAN.md](docs/phases/PHASE1_PLAN.md) |
| 2 | `engine/gs/format` pure math, OG-oracle differential tests | planned (CONSTRUCTION_PLAN §4.2) |
| 3 | `engine/ee/*` + v2 recompiled output (from the frozen OLD tool, exact invocation CONSTRUCTION_PLAN §A.1) + TitleProfile + the boundary-owner stub and MMIO sinks → headless boot to the IOP handshake park; anchor states restore (`restore.mhash` == the state file's own payload hash). No lane identity claimed yet (D-SEQ-2) | planned (§4.3) |
| 4 | `engine/iop` (OpenGOAL-shaped kernel + stock module stubs) + `sif` + `cdvd` + recompiled STREAM.IRX + the IOP-bus devices (SPU2 core, pad HLE, mc HLE — D-SEQ-1) → cold boot through the IOP to the first frame boundary; `audio`+`mem` lanes through each leg's stopping-rule checkpoint | planned (§4.4) |
| 5 | `dmac`/`gif`/`vif`/`vu` (interpreter + cached tier) + the VU1 HLE library as title data → `audio`+`mem` lanes on the 11 anchors end to end (coldboot to its first FMV); VU/VIF corpus gates vs OLD tools | planned (§4.5) |
| 6 | `engine/gs` (state/vram/xfer/hle) + `render/gl` → **rendered frames**; `frames` lane on the 11 anchors + pre-FMV coldboot; gs_corpus byte-identity | planned (§4.6) |
| 7 | `ipu`/FMV + host device backends (SDL audio sink, SDL input) → all three lanes on all 12 legs end to end (D-SEQ-3) | planned (§4.7) |
| 8 | Full equivalence: 12 legs × 3 lanes IDENTICAL, 3 reps stable, savestate cross-load, long-play legs, GUI, playable | planned (§4.8) |
| 9 | `engine/tools` (recompiler transplant → byte-identical regen → IR), `core/triage` + `tools/triage`, mods host, platform folders, soft backend, Vita spike | planned (§4.9) |

Open OLD defects carried as construction work items (never fixed in OLD): BUG-026
(Phase 4), BUG-038 (Phase 4), P4FIX-F1 f57292/f59991 (work Phase 3, closure Phase 4/5) —
CONSTRUCTION_PLAN §7. Sequencing deviations from the brief (sound core/pad/mc pulled into
Phase 4; Phase 3 gate re-scoped) are recorded with rationale in CONSTRUCTION_PLAN §0.2;
the skeleton's drift from ARCHITECTURE §3 in §0.1.

## Build

```sh
cd <repo>
cmake --preset macos-dev          # Ninja, Debug, PS2R_DIAGNOSTICS=OFF  -> build/
cmake --build --preset macos-dev  # -j4 (preset jobs)
ctest --preset macos-dev          # LintOneClass, LintEnginePurity (+selftests), SmokeEmptyBinary, VerifyDiffLegsSelftest, FlagPhaseCountsReproducible
./build/app/host/nativeee --version
```

Presets: `macos-dev` (build/), `macos-diag` (build-diag/, `PS2R_DIAGNOSTICS=ON` — the
core/diag mechanism compiled in; never used for equivalence hashes), `macos-release`.
CMake cache: `PS2R_OG_ROOT` (the frozen OLD tree, read-only; `tests/oracle` SKIP when
absent), `PS2R_TITLE` (the title module linked by `app/host`).

Skeleton proof (2026-09-26, session s128):

```
$ cmake --preset macos-dev
-- nativeee: build type Debug, PS2R_DIAGNOSTICS=OFF, title <title>
-- nativeee: frozen OLD tree found (read-only oracle available to tests/oracle)
-- Build files have been written to: <repo>/build
$ cmake --build --preset macos-dev -j4
[1/4] Regenerating build id
[4/4] Linking CXX executable app/host/nativeee
$ ./build/app/host/nativeee --version
PS2RECOMP-BUILD-ID 2026-09-26 03:08:46
$ ctest --preset macos-dev
1/7 Test #1: LintOneClass .....................   Passed
2/7 Test #2: LintOneClassSelftest .............   Passed
3/7 Test #3: LintEnginePurity .................   Passed
4/7 Test #4: LintEnginePuritySelftest .........   Passed
5/7 Test #5: SmokeEmptyBinary .................   Passed
6/7 Test #6: VerifyDiffLegsSelftest ...........   Passed
7/7 Test #7: FlagPhaseCountsReproducible ......   Passed
100% tests passed, 0 tests failed out of 7
```

(Re-run 2026-09-26 03:08 after the review-loop fixes: fresh `build/`, 7/7; the
`FlagPhaseCountsReproducible` gate was shown to FAIL on a deliberately staled
`FLAG_PHASE_COUNTS.txt` and pass again after regeneration.)

Lints (also runnable directly): `python3 tests/lint/lint_one_class.py --root .` (ARCH §13:
one primary type per pair, no `.inc`, headers declarations-only, no session tags, file
name = snake_case(type)) and `python3 tests/lint/lint_engine_purity.py --root .` (A6 §1.2
game-token set over `engine/` + `verify/`, tolerance 0, reviewed allowlist
`tests/lint/purity_allowlist.txt`).

## Harness: OLD vs NEW

`verify/` is title-agnostic and owned by this repo; the OLD-side emitters live in the
frozen tree (`host_app/verify/`, armed by `PS2_VERIFY_OUT`), the NEW-side emitters land
in `engine/core/verify/` in Phase 1 (armed by CLI/Config, never env).

```sh
# OLD leg (frozen binary, env profile, bounded):
python3 verify/run_leg.py --binary $PS2R_OLD_ROOT/build/host_app/host_app \
    --tree legacy --anchor save_states/ps2state_slot0_f46203_red_crystal.ps2st --frames 600 --out /tmp/x/old
# NEW leg (this tree, CLI profile, bounded)  [Phase 1 adds --tree new]:
python3 verify/run_leg.py --binary build/app/host/nativeee --tree new \
    --anchor save_states/ps2state_slot0_f46203_red_crystal.ps2st --frames 600 --out /tmp/x/new
# compare against the FROZEN baseline, cross-engine meta gate, partial lanes allowed before Phase 8:
python3 verify/diff_legs.py games/<title>/verify/baseline/ps2state_slot0_f46203_red_crystal /tmp/x/new --cross-engine --lanes mem
# the whole 12-leg battery (Phase 1 adds battery.py):
python3 verify/battery.py --binary build/app/host/nativeee --lanes frames,audio,mem --reps 3
```

Lane contract (EQUIVALENCE §1/§2): `frames.fhash` (per-present FNV of the native-scale
scanout), `audio.ahash` (per 100 ms mixer window), `mem.mhash` (RAM+SPAD+meta arena every
60 boundaries), `run.meta` (build id, GL renderer, profile, config fingerprint —
asserted first). Before Phase 8 a phase claims only the lanes it can produce
(CONSTRUCTION_PLAN §2/§3); the others are emitted as OLD-format placeholders so streams
stay index-aligned. Every OLD binary run goes through OUR bounded runner
(`verify/run_bounded.sh`: wall timeout + RSS kill + progress watchdog) and passes
`--bench-report <leg dir>/bench_report.json` so the frozen tree's tracked report file is
never touched; the remaining gitignored `logs/*` writes are the documented side effect
(`docs/REFERENCE_FREEZE.md` "Accepted side effects"). Verified 2026-09-26 with a 60-frame
OLD leg: baseline-identical hashes, OLD `bench_report.json` untouched.

## Tree

```
engine/     title-neutral PS2 semantics + host services (core, ee, iop, dmac, gif, vif, sif, vu, gs,
            cdvd, ipu, pad, mc, sound, render, platform, tools)   -- zero game tokens (lint)
games/<title>/   ALL title-bound content behind TitleProfile / Vu1ProgramLibrary /
            RendererProfile (vu1_library, renderer_profile, hle + hle/workarounds, iop/stream_irx,
            recomp (generated), gen, savestate, symbols, mods, corpus, triage, verify, data)
app/        composition roots: host/ (the only place a title is linked), gui/
tests/      ctest suites, lint/, oracle/ (OG-oracle differential tests), replay tools
tools/      repo-level utilities (flag_phase_counts.py tally; later: regen from the frozen tools, fixture lifters)
verify/     the differential harness + frozen OLD baselines
cmake/      helpers (gen_build_id.cmake)
docs/       design, audits, file map + ledgers, phase plans/records, bug KB, freeze contract
```

Every source directory (`engine/**`, `games/**`, `app/*`, `tests/*`, `tools/`, `verify/`,
`cmake/`) and every `docs/` section has a `README.md` stating purpose / belongs /
does-not-belong / OLD sources; artifact leaves (`games/<title>/verify/baseline/<leg>/`, build dirs) do
not. That README is the only place history may be narrated.

---

## Design history (2026-09-22 → 09-26)

Produced by audit session s113 (2026-09-22): five read-only audits of the original
tree, then a design synthesis against reference architectures —
N64Recomp/N64ModernRuntime/Zelda64Recomp, OpenGOAL, XenonRecomp/XenosRecomp/Unleashed
Recompiled (+plume RHI), libultraship/Ship of Harkinian, RSDKv5, PCSX2 GS, Dolphin
VideoCommon, RPCS3 RSX, DuckStation/Panda3DS, and PS Vita/vitasdk porting constraints —
studied in [docs/design/REFERENCE_ARCHITECTURES.md](docs/design/REFERENCE_ARCHITECTURES.md)
(paraphrased; no PCSX2/OpenGOAL code or license text is copied anywhere in this tree).

Binding user decisions: **(D1)** one recompiler engine — the no-Ghidra v2 static
discovery; legacy engine, Ghidra JSON, overrides and manual bodies are never
transplanted; **(D2)** no "game hooks" — every address-bound override is reclassified
([HOOK_RECLASSIFICATION.md](docs/design/HOOK_RECLASSIFICATION.md)); **(D3)** subsystem-owned
tree, each subsystem with its own `hle/`; **(D4)** one class per `.h`/`.cpp` pair,
lint-enforced; **(D5)** `engine/` vs `games/<title>/` with a purity lint at 0;
**(D6)** mods are declared enhancement surfaces, never workarounds, harness runs
mods-off; **(D7)** platform folders behind ONE `Platform` interface with an honest
feasibility table; **(D8)** the bug knowledge base [docs/bugs/](docs/bugs/);
**(D9)** play-time triage capture ([TRIAGE_CAPTURE.md](docs/design/TRIAGE_CAPTURE.md));
**(FREEZE 2026-09-26)** the original tree is read-only and the refactor is greenfield
construction ([CONSTRUCTION_PLAN.md](docs/design/CONSTRUCTION_PLAN.md)).

## Read in this order

### Design (the plan)

| Doc | What it answers |
|---|---|
| [docs/design/ARCHITECTURE.md](docs/design/ARCHITECTURE.md) | The target: goals/non-goals, reference-architecture survey with citations, the subsystem-owned module map (each PS2 subsystem = state model + its own `hle/`), field-level sketches of the core data structures (GsRegisters, PsmLayout, VramImage, RenderTarget, TextureSource, DrawIR, TimeDomain, Config, SdkBindingManifest, VuProgramLibrary, Savestate envelope), the three-IR design (EE code IR — v2 engine only / VU1 HLE library / GS DrawIR), the backend abstraction (GL 4.1 / Vulkan / GXM / software behind the caps contract) with the pipeline-cache contract, the single-writer threading model, config + logging as modules, naming conventions, size budgets, and the "where is X" index |
| [docs/design/HOOK_RECLASSIFICATION.md](docs/design/HOOK_RECLASSIFICATION.md) | [REV 2026-09-22 D2] [REV-2 2026-09-23 recount] Every current address-bound override (747 rows: 200 dual-hook sites, 243 distinct manual/anim-pose addresses — 235 manual installs + 9 anim mirrors, one overlapping — 291 sce bindings, 4 libc, the 0x181490 piracy bypass, 3 seam-only + 1 game-address recompiler seams (5 more seam addresses count inside the 291), S8-ARGV, KEY-REDIRECT, the CDVD/loader completion-visibility model, and the no-code retail-behaviour row) dispositioned into the five classes with per-patch suspected defects, record citations, the adjudication protocol, and the SdkBindingManifest shape. Counts: 299 SDK/OS HLE · 2 hardware/timing HLE · 250 emulation-bug workarounds · 1 retail-behavior · 195 diagnostics |
| [docs/design/REFERENCE_ARCHITECTURES.md](docs/design/REFERENCE_ARCHITECTURES.md) | The researched reference-architecture study (2026-09-22, online sources cited): deep dives on N64Recomp/N64ModernRuntime/Zelda64Recomp, OpenGOAL, and XenonRecomp/XenosRecomp/Unleashed Recompiled (+plume); brief studies of libultraship/Ship of Harkinian, RSDKv5, PCSX2 GS, Dolphin VideoCommon, DuckStation/Panda3DS, and real PS Vita ports; per-project repo layout, generated/runtime split, IR & emission, renderer/platform abstraction, hooks/patches, config, testing, portability; the comparison matrix and the L1–L14 lessons list (revision annotations mark lessons superseded by D1/D2) |
| [docs/design/CONSTRUCTION_PLAN.md](docs/design/CONSTRUCTION_PLAN.md) | **The executable plan (2026-09-26):** greenfield, bottom-up construction in THIS tree with the original frozen — nine phases that each ADD modules by transplanting logic per the file map with flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written), each phase runnable and gated by harness identity against the frozen OLD baselines (partial lanes before Phase 8), per-phase module tables with OLD sources, flag-family counts, proof gates, definitions of done, the accepted-difference protocol, the risk register, and the exact frozen-tool invocation for the recompiled output |
| [docs/design/MIGRATION_PLAN.md](docs/design/MIGRATION_PLAN.md) | **Superseded in shape** (banner 2026-09-26) by CONSTRUCTION_PLAN; retained for its per-file/cluster dispositions (§1.1–§1.6 incl. the engine/game split rows), the per-family purge PROCEDURE (now applied at transplant time), and Appendix A (ADD A SECOND GAME) |
| [docs/design/EQUIVALENCE_PLAN.md](docs/design/EQUIVALENCE_PLAN.md) | How identity is proven: the definition of "identical" (bit-identical frames at native scale; adjudicated above it), the deterministic two-binary harness, the deterministic profile, anchor + long-play differential runs, the divergence escalation ladder, corpus/oracle gates, the accepted-difference protocol (for bugs in OLD — also the ladder every retired workaround patch passes through), and the CI shape. [REV2 2026-09-23 D4-D8] Mods-off is the only certified configuration. [REV3 2026-09-23 D9] Triage bundles are harness inputs |
| [docs/design/TRIAGE_CAPTURE.md](docs/design/TRIAGE_CAPTURE.md) | [REV3 2026-09-23 D9] The play-time capture subsystem: as-is audit of today's capture (bug_report/slowspot/artdet/vutrace/PERF/censuses, file:line-cited) and the gap list; `engine/core/triage/` (FallbackDetector for VU1/EE/IOP-RPC/GS/syscall fallbacks, PerfMonitor, SpikeDetector, SavestateRing, InputLog, TriageBundler/Manifest/Dedup/Writer); per-class bundle content specs (Vu1Case reuse, the NEW EeFunctionCase, RPC pairs, .gsdl windows, PERF/spike evidence) with an AI-contract MANIFEST.md; the `games/<title>/triage/` layout, content-keyed dedup + retention, the capture cost budget; the `tools/triage` handoff (list/repro/lift/perf) and the fixed-bundle → regression-test + bug-KB loop |
| [docs/bugs/](docs/bugs/) | [REV2 2026-09-23 D4-D8] The bug knowledge base: TEMPLATE.md (entry contract incl. the "PS2 rule violated" field), INDEX.md (by subsystem AND by symptom), TRIAGE.md (symptom → subsystem → first checks → tools decision tree), and 30 seeded entries (BUG-001…030) distilled from this year's loop_state records — chunk-load freeze, sound desync/quantum, music death, loader/save-select livelocks, the SH5/SH7/SH8 shadow family, interp coherence, deposit races, minification/motion aliasing, tile-patchwork→time-domain→budget chain, fog UV-phase/stale-lane, crystal stride/read-view, black boot, CT16 dither, mip levels, checkerboard scale, VU1 link truncation, S9 stencil hold, log tax, snapshot epochs, input lag, pause delay-slot, slide-jump subtick, RTKEEP — each citing its source record path |

### Audits (the evidence)

| Doc | Scope |
|---|---|
| [docs/audit/A1_STRUCTURE.md](docs/audit/A1_STRUCTURE.md) | Inventory (602 files / ~374k lines), include graph (0 cycles, 184 header globals), 40 largest functions, duplication census, dead/stale code, generated-vs-handwritten boundary, 12-concept location map |
| [docs/audit/A2_CONFIG_DIAG.md](docs/audit/A2_CONFIG_DIAG.md) | 496 env vars / 625 getenv sites with per-var dispositions, latch/mirror patterns, 4-layer logging census, diagnostics census, session-history-in-code, the ~30-field unified Config schema, gate-purge notes |
| [docs/audit/A3_GS_RENDER.md](docs/audit/A3_GS_RENDER.md) | GS/render pipeline as-is: data flow, format-layer duplication, the ONE texture-source rule and its ~17 implementations, the five VRAM page trackers, target lifecycle, draw IR assets, scale/interp/present, essential-vs-accidental, and the 38-invariant list |
| [docs/audit/A4_CPU_HLE.md](docs/audit/A4_CPU_HLE.md) | Recompiler pipeline + semantics contract, runtime glue (memory/threading/dispatch/syscalls), VU tiers + HLE library, the 191-row hook table, the 9-clock timing inventory + single-domain proposal, savestate format/contract, 12 Vita-class portability blockers |
| [docs/audit/A5_VERIFICATION.md](docs/audit/A5_VERIFICATION.md) | Verification assets: 407 ctest gates, the 19-capture/16 GB GS corpus + freeze ledger, VU/VIF corpora, pixel/anchor tooling (incl. the black-boot lesson), savestates, the 11 coverage gaps, determinism properties, and the harness build list |

## The plan in one paragraph

Build the deterministic two-binary harness first (done: frames + audio + memory hashes
over 12 savestate/cold-boot legs, OLD baselines frozen), freeze the original tree, then
construct the new tree bottom-up: core services (config schema with the peak defaults,
logger, diagnostics mechanism, frozen savestate envelope, threads) so the empty binary
already emits a comparable leg; the GS format layer as pure math proven against the
OLD decoder; the EE (memory, kernel, timing, syscalls) plus the v2 recompiled output
generated by the FROZEN OLD tool so the game boots headless to its first frame boundary
with an identical memory checkpoint; the IOP cooperative kernel, SIF, CDVD and the
recompiled STREAM.IRX so cold boot runs through; DMA/VIF/VU with the VU1 HLE library as
title data so memory is identical on every leg; the GS state/VRAM/native renderer over
GL so frames are identical; sound/FMV/pad/memory-card so audio is identical; then the
full 12-leg battery, savestate cross-load and long-play legs; and finally the tools
(recompiler transplant → byte-identical regeneration → IR), triage capture, the mod
host, platform folders and the Vita spike. Flags are never transplanted — the peak
configuration is the only code path; workarounds are transplanted quarantined and
retired only through the adjudication ladder; any intentional difference goes through
the accepted-difference protocol against the frozen OLD streams.

## Ground rules carried through all documents

- Behavior changes are forbidden except via the accepted-difference protocol.
- The savestate format and the ~40 anchor states are load-bearing: reader
  compatibility is a standing phase gate.
- One concept, one home; no session tags; no getenv outside the config loader;
  no mutable globals in headers; files ≤ ~1,500 lines, functions ≤ ~300.
- [REV 2026-09-22 D1/D2] One recompiler engine (v2, no Ghidra); no game-address
  hooks — HLE binds by library signature, hardware surface, or timing
  observable, and lives in the `hle/` of the subsystem it emulates; workaround
  patches exist only with a root-fix plan and a harness-gated expiry
  (HOOK_RECLASSIFICATION.md).
- [REV2 2026-09-23 D4-D8] One class per `.h`/`.cpp` pair, lint-enforced;
  `engine/` links with zero references into `games/` (purity lint); mods are
  declared enhancement surfaces, never workarounds, and the harness runs
  mods-off; platform folders exist only with a feasibility note + CI stub;
  every root-caused defect lands a [docs/bugs/](docs/bugs/) entry with its fix.
- [REV3 2026-09-23 D9] The runtime captures HLE fallbacks, sustained-perf and
  frame-time-spike events at play time into self-contained bundles
  (savestate + input log + config + build id + AI-ready MANIFEST) under
  `games/<title>/triage/`, deduped and cost-bounded; `tools/triage`
  (list/repro/lift/perf) turns them into lifts, regression legs and bug-KB
  entries ([docs/design/TRIAGE_CAPTURE.md](docs/design/TRIAGE_CAPTURE.md)).
- History lives in `docs/loop_state/` (283+ records) and per-directory READMEs —
  never inline in code.

## File Map

Every hand-written file of the current repo (`$PS2R_OLD_ROOT`,
602 files per A1's inventory) is mapped into the new tree, with per-file purpose, public
surface, engine/game classification, exact destination and one-class-per-file shape, every
env flag it reads (with the peak/proven configuration becoming the only code path), and
every hack with the PS2 rule and owning class it should become.

- [docs/filemap/INDEX.md](docs/filemap/INDEX.md) — every mapped file in one table
  (path | lines | class | destination | shape | flags | hacks), grouped by destination
  directory of the new tree; reverse index (new module → source files feeding it);
  destination collisions (1 hard, decided + 5 soft); merged TREE-GAP list (17);
  A1-coverage verification (589/602 by shards; 2 omissions closed with consolidator rows;
  10 vendored-imgui files exempt).
- [docs/filemap/FLAG_LEDGER.md](docs/filemap/FLAG_LEDGER.md) — master env-var table
  (541 flags = A2's 496 registry vars + 36 registry gaps found by the shards and the
  ledger review (incl. 15 tests-local harness vars + PS2_VU1_MODE) + 8 build
  options + 1 new build gate), cross-checked against A2: **195 NORM · 27 SETTING ·
  103 DIAG · 39 TEST-ONLY · 176 DELETE**. "Peak config becomes norm": everything
  run_play.sh CANON enables and every default-ON proven-fix hatch becomes the sole code
  path; the 24-field SETTINGS section carries the proposed Config defaults
  (video.scale=2, video.interp=on, video.fps_cap=60, engine.mtvu=on/depth 2, cdvd
  latency=instant, triage.enabled=on, …). 13 shard disagreements reconciled with
  decisions; A2 corrections listed.
- [docs/filemap/HACK_LEDGER.md](docs/filemap/HACK_LEDGER.md) — all 269 merged
  hack/workaround entries grouped by kind (scene constants 20 · game addresses 43 ·
  dual paths 22 · latches 25 · probes 40 · mirrors 17 · .inc includes 6 · code copies
  15 · timing fudge 24 · structural 30 · build/config/log conventions 15 · narrative
  history 12), each with file:line, the PS2 rule it should have implemented, and the
  owning class in the new tree; KEEP-marked entries are documented divergences that
  survive with their rule stated.
- Per-file detail: the nine shard maps `docs/filemap/FILEMAP_host_app_{0..5}.md`,
  `FILEMAP_graphics.md`, `FILEMAP_hle_sce_sound_input_gui_platform.md`,
  `FILEMAP_toolchain_tests_tools_build.md` (~11,700 lines total).

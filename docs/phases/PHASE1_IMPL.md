# PHASE1_IMPL — what landed for CONSTRUCTION_PLAN §4.1 (per stage; STAGE_RULES rule 6)

Plan: `PHASE1_PLAN.md`. Every OLD file was opened read-only from
`$PS2R_OLD_ROOT/`; OLD binaries ran only through
`verify/run_bounded.sh`.

## §1A — core/config + core/log + core/diag + core/threads + core/hash (stage p1a, 2026-09-26)

Gate at the stopping point: `cmake --build --preset macos-dev -j4` clean (`-Wall -Wextra -Werror`),
`ctest --preset macos-dev` **100 % — 13/13** (Phase-0's 7 + 6 new); `macos-diag` builds and passes
**14/14** (+ `CoreBenchReportFormat`). `lint_one_class.py` 40 files / 0 findings;
`lint_engine_purity.py` 41 files / 0 tokens. `grep -rn getenv engine app` → one code site
(`engine/core/config/config_loader.cpp:280`, the `PS2R_SET` dev override; the other hits are its
comment and two README sentences) — enforced from now on by ctest `CoreSingleGetenvSite`.

### Files (one primary type per pair; `engine/core/CMakeLists.txt` → `ps2r_core`, `ps2r_diag`)

| New | Type | OLD source (read-only) | Shape / notes |
|---|---|---|---|
| `core/hash/hash.h/.cpp` | free fns `Fnv1a64`, `Xxh64` | `verify/verify_env.h` Fnv1a64 (offset `0xcbf29ce484222325`, prime `0x100000001b3`), `render.h` XXH64 **algorithm only** | FNV verbatim; XXH64 is our own lane/stripe implementation (rule 8: no vendored code) |
| `core/log/ring_format.h` | PODs/enums | `ring_log_format.h` | FROZEN layout; static_asserts 24/48/16 kept |
| `core/log/ring_tags.h/.cpp` | `TagInfo` table + `TagTable/FindTag` | `ring_tags.h` | FROZEN table: ids, channels, names verbatim (29 rows); the arg-doc strings of 4 rows (`MTVU_SYNC_SITE`, `RUNAHEAD_DEPTH`, `MEM_GAUGE`, `RSS_SAMPLE`) were reworded to drop OLD identifier names — text form unaffected (oracle identical); a later `RING_LOG_SCHEMA` mirror must mirror the NEW strings; table moved to `.cpp` (headers = declarations) |
| `core/log/ring_decoder.h/.cpp` | `RingDecoder` | `ring_log_decode.h` | text form + pretty decoders verbatim; proven byte-identical (below) |
| `core/log/ring_log.h/.cpp` | `RingLog` | `ring_log.{h,cpp}` | emit path / registry / signal-safe dumps / text tail verbatim in behaviour; masks+capacity from `Configure()` (Config) instead of the static-init env latch; frame stamp owned here (`RingLog::Frame/SetFrame/BumpFrame`); guest-GPR block via `SetGuestContextDumper` hook (EE layer installs it, Phase 3); **extern-C `crashring_enabled` / `crashring_record_indirect` / `crashring_fatal_no_jump` verbatim** (linked + exercised by `test_ring_log`) |
| `core/log/log_categories.h`, `log.h` | `Category` enum, `PS2R_LOG` | `main.cpp:125` TagFilterBuf families, `hw_verbose.h`, ARCH §9 list | 32 compile-time categories: VIF GIF GS VU DMAC MEM SIF SND PAD MPEG KERNEL LOADER HEAP PERF IOP CDVD TIME SAVE HARNESS INTC TIMER IPU MC SCANOUT TEX DRAW STREAM HLE BOOT CONFIG TRIAGE HOST; `if constexpr` gate ⇒ disabled categories compile to nothing |
| `core/log/logger.h/.cpp` | `Logger` | `main.cpp:4905` PS2_LOG master | levels quiet/error/warn/info/narrate; file + stderr sinks; `dev.log_level` runtime gate |
| `core/log/log_budget.h/.cpp` | `LogBudget` | `debug_budget.h` | `FirstN` / `EveryNth` / `FrameWindow` + `Suppressed` |
| `core/threads/cmd_ring.h/.inl` | `CmdRing<T,N>` | `sound/ring.h` | verbatim contract (producer mutex, lock-free consumer, drop-oldest, exact drop count) |
| `core/threads/park.h/.cpp` | `Park` | `timeline.h` `WaitFixOn()` condvar arms (`gs_hle.cpp:243`, `ps2_vblank.cpp:1178`, `sif_hle.cpp:407`, `main.cpp:4853`) | the ONLY wait primitive: condvar + absolute steady-clock deadline + generation counter; poll+sleep arms not written |
| `core/config/config.h/.cpp` | `Config` | FLAG_LEDGER SETTINGS, `display_settings.{h,cpp}`, `main.cpp` CLI, PHASE1_PLAN §3 | 59 keys, every §3 row with its peak default; `Get/Set/Keys/IsKey`; `Fingerprint()` (sorted non-default `key=value;` + `profile=`; harness./paths./meta.* excluded) and `FingerprintHash()` (FNV fold per entry + `;`, the OLD `CollectEnv` semantics) |
| `core/config/config_loader.h/.cpp` | `ConfigLoader` | `main.cpp` arg parsing, `bench.h ParseArgs` (`--bench-frames`→`--frames`), `display_settings.cpp LoadConfig/SaveConfig` | defaults < `ps2r.ini` < `--config` < `--set` (repeatable, `;`-joined) < `--profile` < `PS2R_SET` (dev only; the ONE getenv); display.ini `[video]` compat with the OLD clamps, never written back unless `--save-display`; CLI plumbing re-applied over file tiers |
| `core/config/config_validator.h/.cpp` | `ConfigValidator` | NOVSYNC→FREERUN→CDVD_INSTANT cascade (A2 f3), `tick_sync.h` MTVU refusal | `tick_sync ⇒ !mtvu` refused; `freerun ⇒ latency_model=instant` unless explicit; `mtvu_depth ≥ 1`; scale 1..4; display clamps; ring_events pow2 1024..4M; `harness.*` needs `--headless`/`--harness-window` |
| `core/config/deterministic_profile.h/.cpp` | `DeterministicProfile` | `verify/profile_deterministic.py`, `verify/PROFILE.md` | every PROFILE row → field (mapping listed in the .cpp header); canonical fingerprint golden in `test_config` |
| `core/diag/diag_registry.h/.cpp` | `DiagRegistry` | `install_dual_hook` MECHANISM (`main.cpp:464`), `s8cnt`/`BullseyeDiag` idea | name/description/enabled/counter/report registration + startup config dump; no address-keyed sites |
| `core/diag/perf_profiler.h/.cpp` | `PerfProfiler` (+ nested `Scope`) | `perf_profiler.h` profiler half | 31 buckets, names/order FROZEN (`ee … p2copy`); `[PERF]` / summary lines; `TakeSnapshot`; enabled by `diag.perf`/Bench (no `PS2_PERF` getenv, no `setenv`) |
| `core/diag/bench_report.h/.cpp` | `BenchReport` (+ `Census`, `Gates`, `MtvuSites`, `Derived`, `ReportInput` PODs) | `bench_report.h` | JSON renderer, key order and census/gates vocabulary verbatim (`bench_report.json` schema kept); census-line parsers verbatim |
| `core/diag/bench.h/.cpp` | `Bench` | `bench.h` | process-local tick indexing, window snapshots, anchors via registered functions, report written only to the configured path; the funopen stderr shim, signal handlers and `setenv("PS2_PERF")` are NOT transplanted |

Not transplanted from `ring_log.cpp` (by design): `WriteBundle/WriteBundleTo` (env.txt/perf.txt
assembly → core/triage 9.2), `HotkeyDump` (app/host hotkey → Phase 8), the boot-object word dump in
the fatal path (keys on a game object layout → games/<title> triage), the mach RSS sampler
(`SampleGauges` takes the numbers from platform/). `EEGameSlice/EEGamePause/IoCensus` from
`perf_profiler.h` wait for their consumers (Phase 3 ee/ and cdvd/).

### Flags consumed (FLAG_LEDGER outcomes applied while writing)

| Flag | Outcome | Where |
|---|---|---|
| `PS2_WAITFIX` | NORM — condvar/deadline arm only | `Park` |
| `PS2_CRASHRING` | NORM — ring always on, alias gone | `RingLog` (no EE-channel clear) |
| `PS2_LOG` | SETTING `dev.log_level` (quiet) | `Config`, `Logger` |
| `PS2_LOG_CHANNELS` | SETTING `log.channels` ("all") | `Config` → `RingLog::Configure` |
| `PS2_LOG_RING_EVENTS` | SETTING `log.ring_events` (65536) | `Config` → `RingLog::Configure` |
| `PS2_GUI_OPEN` | SETTING `ui.gui_open` (on; det off) | `Config` |
| `PS2_SAVE_SLOT` | SETTING `save.slot` (0) | `Config` |
| `PS2_SHUTDOWN_WAIT` | SETTING `engine.shutdown_wait_s` (10, 0..600) | `Config`/validator (ShutdownLadder consumer → §1B) |
| `PS2_MAINT_EEPACED` | SETTING `time.maint_eepaced` (off; det on) | `Config` |
| `PS2_SLOWSPOT`, `PS2_BUGREPORT`, `PS2_BUGREPORT_SLOWSPOT`, `PS2_BUGREPORT_SLOWSPOT_MAX` | SETTING `triage.enabled/perf/...` | `Config` (fields exist; consumers 9.2) |
| `PS2_PERF` | SETTING `diag.perf` (off) | `Config`, `PerfProfiler::SetEnabled` |
| `PS2_LOG_VERBOSE` | DIAG — verbose tier mask is a `RingLog::Configure` argument; the app passes 0 outside diag builds | `RingLog` |
| `PS2_VERBOSE_HW` + the 17 census/probe DIAG vars | DIAG — `DiagRegistry` mechanism only; bodies land with their subsystems | `core/diag` |
| `PS2_LOG_FASTFILTER`, `PS2_LOG_ELIDE_DROPPED`, `CT_FULL_LOG` | DELETE — the 4 filter layers do not exist (compile-time categories) | — |
| `PS2_U3_VIEWPORT` | DELETE — `display::ProbeOn` not written | — |
| `PS2_RING_TEST_*`, `PS2_GUI_TEST_*`, `PS2_BUGREPORT_TEST_*` | TEST — not written (tests take arguments) | — |
| the remaining Phase-1 NORM rows (`PS2_SAVE_ATOMIC`, `PS2_EE_RUNTOKEN`, `PS2_NO_QOS`, `PS2_SLOWSPOT_*`) | belong to §1B modules (savestate, platform, triage constants) | — |

### Tests (tests/core; `ctest` names)

| Test | Proves |
|---|---|
| `Coretest_hash` | FNV vectors == OLD `test_verify_units` (`""`, `"a"`, `"foobar"`), seed chaining; XXH64 == the 9 reference vectors OLD `test_probe_bench` pins (`""`→`0xEF46DB3751D8E999`, seeds 0/1/golden ratio, 16/31/32/43-byte inputs) |
| `Coretest_config` | every peak default; `Get/Set` round-trip of all 59 keys; unknown key = error; fingerprint canonical form + hash fold; deterministic preset == PROFILE.md rows and its golden fingerprint `cdvd.latency_scale=0;…;video.scale=1;profile=deterministic`; validator DAG; loader precedence (ini < --config < --set < --profile), CLI plumbing, error paths; display.ini compat (OLD `test_display_settings` cases: names, clamps, unknown mode ignored, whitespace/comment) |
| `Coretest_threads_log` | CmdRing order / drop-oldest exact count / 3-producer integrity; Park deadline + notify + generation; Logger level gate and `[CAT] line` sink format; LogBudget primitives |
| `Coretest_ring_log` | wrap + EXACT drop count, 4×100k threaded producers, decoder round-trip, masks + verbose tier, `crashring_enabled` follows the verbose EE bit, `crashring_record_indirect` emits, `crashring_fatal_no_jump` links, text tail; writes the oracle fixture |
| `CoreRingDecodeOracle` | **NEW `RingDecoder` text == frozen OLD `build/ring_decode` text, byte-identical, over a fixture with every tag on both tiers + an unknown tag + a wrap (5126 lines, 329,751 bytes)**; SKIPs when OLD is absent |
| `CoreSingleGetenvSite` | exactly one getenv site, in `config_loader.cpp` |
| `CoreBenchReportFormat` (diag) | derived stats, frozen top-level key order, gates/perf vocabulary (31 buckets `ee…p2copy`), census-line parsers, summary line |

Quoted lines: `100% tests passed, 0 tests failed out of 13` (macos-dev),
`100% tests passed, 0 tests failed out of 14` (macos-diag),
`ring_decode_oracle: IDENTICAL (5126 lines, 329751 bytes) vs frozen OLD ring_decode`,
`getenv sites: 1 -> PASS`, `deterministic env_hash=0x3542d8e175cb1bb0` (NEW fingerprint hash;
not comparable to OLD's env-list hash by construction — the cross-engine gate is `profile`).

Process note: one helper edit in this stage used a `python3 - <<EOF` heredoc (CMake list
insertions) against STAGE_RULES rule 2; no other stdin scripts were used.

### HANDOFF

Done: the five §1A modules, their tests, CMake registration (`engine/core`, `tests/core`), lints
green, single getenv site enforced.

Not done (Phase 1 §6 steps 4, 7–10 → stage 1B): `core/savestate` (format verbatim + registry +
writer + the 12-anchor parse test), `core/verify` + `core/triage/input_log`, `platform/platform.h` +
`macos/`, `app/host/main.cpp` composition (`ConfigLoader → Validator → Logger → RingLog::Configure →
DiagRegistry → VerifyHost → TickSource stub → FrameCap → ShutdownLadder`), `verify/run_leg.py --tree
new`, `diff_legs --cross-engine`, `battery.py`, the 12-leg META battery, `settings_table.tsv` +
`tools/gen_settings_table.py` (R14 — `test_config` currently enumerates the schema by hand; generate
the TSV from FLAG_LEDGER and add `test_config_schema` over it), per-directory README rows for
`hash/` (no README exists yet) and the `diag/` README flag rows.

Next command: `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (green), then start
§6 step 4 with `OLD/host_app/savestate_format.{h,cpp}` read-only.

### Review 1A (stage p1ar, 2026-09-26) — verdict: NEEDS_CHANGES (all items small; fix first in 1B)

Method: fresh `cmake --build --preset macos-dev --target clean` then `cmake --build --preset macos-dev
-j4` (17 CXX objects, every one `-Wall -Wextra -Werror`, zero warnings) → `ctest --preset macos-dev`
**`100% tests passed, 0 tests failed out of 13`**; `cmake --preset macos-diag` + build → `ctest
--preset macos-diag` **`100% tests passed, 0 tests failed out of 14`**. Lints run directly:
`LintOneClass: 40 files scanned, 0 findings -> PASS`, `LintEnginePurity: 41 files scanned, 0 game
tokens -> PASS`. All OLD files opened read-only; OLD `build/ring_decode` ran only via
`run_bounded.sh` (inside `CoreRingDecodeOracle`). Helper scripts under `/tmp/p1ar/` only, no stdin
scripts, no commits.

Checklist results:

| Check | Result |
|---|---|
| getenv sites | `getenv sites: 1 -> PASS` (`config_loader.cpp:280`, `PS2R_SET`, `#ifndef NDEBUG`); an independent grep of `engine app tests games` finds the same single code site |
| Config schema vs PHASE1_PLAN §3 | 59 keys, all §3 rows present, `Keys()` sorted; peak defaults match §3 for every row EXCEPT the two §3 rows that defer to "TRIAGE §3": `triage.disk_budget_mb` = 512 (TRIAGE_CAPTURE §3 says **2048**) and `triage.ring_period_s` = 60 (TRIAGE §3 says **10**, `0 = ring off`). `triage.fps_target` = 60 matches the §3 table but TRIAGE §3 defines `0 = auto`; the validator refuses `fps_target <= 0` and `ring_period_s <= 0`, i.e. it rejects the documented legal values. §3's det column says triage keeps "on (small disk budget)" — `DeterministicProfile` leaves the budget untouched (PROFILE.md has no triage row; ambiguity to settle, not a bug) |
| DeterministicProfile vs PROFILE.md | Every one of the 25 `DETERMINISTIC_ENV` vars is accounted for: 18 map to the fields set in `Apply()` (checked one by one), 7 are NORM (`PS2_TIME_EMU`, `PS2_T0_SUBTICK`, `PS2_HB_TICKCLOCK`, `PS2_SND_TICKCLOCK`, `PS2_FIXC_TICKCLOCK`, `PS2_IOP_KERNEL`, `PS2_EE_RUNTOKEN`) — FLAG_LEDGER rows checked for `PS2_HB_TICKCLOCK` (NORM, iop_stubs.cpp:1175) and `PS2_EE_RUNTOKEN` (NORM, ee_run_token.h). Golden fingerprint `cdvd.latency_scale=0;…;video.scale=1;profile=deterministic` reproduced |
| FingerprintHash vs OLD FNV | `hash.cpp` Fnv1a64 is byte-for-byte the `verify_env.h` loop (offset/prime identical). Independent Python reference (`/tmp/p1ar/fnv_vec.py`, spec constants only): `Fnv1a64("a") = 0xaf63dc4c8601ec8c`, `"foobar" = 0x85944171f73967e8` (== OLD `test_verify_units`); OLD `run_meta.cpp CollectEnv` fold (entry, then `";"`) applied to the deterministic fingerprint gives **`0x3542d8e175cb1bb0`**, equal to the value `test_config` prints — the fold semantics are reproduced |
| RingLog round-trip oracle | Meaningful: `CoreRingDecodeOracle` runs the FROZEN OLD compiled decoder (`OLD/build/ring_decode`, 189,000 B, Sep 13) over a NEW-written `.rlog` holding every tag on both tiers + an unknown tag + a wrapped ring, and byte-compares with the NEW `RingDecoder` text: `ring_decode_oracle: IDENTICAL (5126 lines, 329751 bytes) vs frozen OLD ring_decode`. Tag table: 29 rows, ids/channels/names identical to OLD `ring_tags.h`; `ring_format.h` static_asserts 24/48/16 kept. Gap noted: no OLD-written `.rlog` is decoded by the NEW decoder (the reverse half) |
| Bench report header | Summary line format string identical to OLD `bench_report.h:678` (`[R127-BENCH] exit=%s frames=%u window=[%u,%u] (%u frames) fps{…} worst_ms=…@f%u rss_mb{…} anchor{…}`); ordered JSON key emission sequence identical (133 keys, `/tmp/p1ar/keys.py` diff empty); perf bucket names identical (31, `ee … p2copy`). No `setenv`/`signal`/`funopen` in `core/diag` |
| Session tags / license / one class | Lint: 0 tag findings in comments; no PCSX2/OpenGOAL/GPL/MIT text under `engine app tests/core`. One primary type per pair holds (helper PODs only). BUT three OLD session-tag prefixes survive as string literals in `engine/`: `[R127-BENCH]` (`bench.cpp:188`, `bench_report.cpp:459`), `[R123-MTVUSYNC]` (`bench_report.cpp:472`), `[R147-GFXRES]` (`:510`) — they are the frozen report/census vocabulary the checklist itself requires, and the lint only inspects comments, so they pass silently |
| Flags consumed vs FLAG_LEDGER (spot-check 15) | `PS2_WAITFIX` NORM ✓ (Park only), `PS2_CRASHRING` NORM ✓, `PS2_LOG` SETTING `dev.log_level` quiet ✓, `PS2_LOG_CHANNELS` `log.channels` (OLD default `all`) ✓, `PS2_LOG_RING_EVENTS` (OLD `kDefaultEvents = 65536`) ✓, `PS2_GUI_OPEN` `ui.gui_open` dev on ✓, `PS2_SAVE_SLOT` 0 ✓, `PS2_SHUTDOWN_WAIT` 10 ✓, `PS2_MAINT_EEPACED` SETTING ✓, `PS2_PERF` `diag.perf` off ✓, `PS2_LOG_VERBOSE` DIAG ✓ (verbose mask argument), `PS2_LOG_FASTFILTER` DELETE ✓ (absent), `PS2_U3_VIEWPORT` DELETE ✓ (absent), `PS2_HB_TICKCLOCK` NORM ✓, `PS2_EE_RUNTOKEN` NORM ✓ (no field) |

Findings (numbered; 1–2 are behaviour, the rest are accuracy/hygiene):

1. **Triage peak defaults / validator disagree with TRIAGE_CAPTURE §3** (the authority §3 of
   PHASE1_PLAN points at): set `triage.disk_budget_mb = 2048`, `triage.ring_period_s = 10`; let the
   validator accept `ring_period_s = 0` (ring off) and `fps_target = 0` (auto) — or amend TRIAGE §3
   and PHASE1_PLAN §3 explicitly if 60/512/60 is the intended peak. Update `TestPeakDefaults`.
2. **`ConfigLoader::Load` silently discards `harness.*` from every non-CLI tier**: after the tiers it
   does `out.harness = cli_harness`, so `--set harness.frames=600`, a `[harness]` ini section or
   `PS2R_SET=harness.headless=on` are accepted by `Set()` and then thrown away. Either refuse
   `harness.*` outside the CLI flags (error) or re-apply only the CLI-given fields. Add a test.
3. **§1A table over-claims "arg docs verbatim"** for `ring_tags`: 4 of 29 rows were reworded
   (`MTVU_SYNC_SITE`, `RUNAHEAD_DEPTH`, `MEM_GAUGE`, `RSS_SAMPLE` — OLD identifier names removed).
   Harmless to the text form (oracle identical) but the row must say so; if a
   `RING_LOG_SCHEMA`-style mirror is transplanted later it must mirror the NEW strings.
4. **Session-tag string literals** (`[R127-BENCH]`, `[R123-MTVUSYNC]`, `[R147-GFXRES]`) in
   `core/diag`: record them as an accepted frozen-vocabulary exception (PHASE1_IMPL + the lint README)
   or have the lint scan string literals with an allowlist — today the rule is satisfied only
   because the lint does not look at code strings.
5. `cmd_ring.h:4` cites `tests/core/test_cmd_ring.cpp`; the contract tests live in
   `test_threads_log.cpp`. Fix the reference.
6. `tests/core/check_getenv_sites.py` scans `engine/` and `app/` only; add `games/` (and the other
   code roots that will exist) so "exactly ONE site in the whole tree" stays enforced when game code
   lands.
7. `engine/core/hash/` has no README (every other module directory has one) — already in the
   handoff; do it in 1B.
8. `Park::WaitUntil` builds the deadline as
   `steady_clock::time_point(std::chrono::nanoseconds(ns))`, which only compiles where
   `steady_clock::duration` is nanoseconds (true on macOS/Linux, not MSVC). Use
   `time_point_cast`/`duration_cast` now so the platform stage does not touch core/.
9. Process: the stage's own note admits one `python3 - <<EOF` heredoc (STAGE_RULES rule 2). No
   repeat; nothing to fix in the tree.

Not findings (verified OK): loader precedence `defaults < ps2r.ini < --config < --set < --profile <
PS2R_SET` is exactly PHASE1_PLAN §2's row (so `--set video.scale=4 --profile deterministic` → 1 is by
design); `Configure()` derives `crashring_enabled` from the verbose EE bit as OLD did; the extern-C
boundary is linked and exercised; `FatalNoJump` `_exit(1)`s without static destructors; CmdRing keeps
the OLD contract (producer mutex, lock-free consumer, claim-then-overwrite drop-oldest, exact count).

## §1B — Review-1A fixes + core/savestate + core/verify + platform + app composition + harness (stage p1b, 2026-09-26)

Gate at the stopping point: `cmake --build --preset macos-dev -j4` clean (`-Wall -Wextra -Werror`),
`ctest --preset macos-dev` **`100% tests passed, 0 tests failed out of 15`**; `macos-diag` builds and
**`100% tests passed, 0 tests failed out of 16`**. `LintOneClass: 72 files scanned, 0 findings -> PASS`,
`LintEnginePurity: 73 files scanned, 0 game tokens -> PASS`, `getenv sites: 1 -> PASS` (now scanning
`engine app games tests tools`). All OLD files opened read-only; the OLD binary was not run in this stage
(the battery compares against the frozen `verify/baseline/` legs). Helpers under `/tmp/p1b/` only; no
stdin scripts; no commits.

### Review 1A — all 9 items closed first

| # | Fix | Where / proof |
|---|---|---|
| 1 | `triage.disk_budget_mb` 512→**2048**, `triage.ring_period_s` 60→**10**; validator now accepts `fps_target = 0` (auto) and `ring_period_s = 0` (ring off), refuses negatives | `config.h`, `config_validator.cpp`; `TestPeakDefaults` + validator cases in `test_config` |
| 2 | `harness.*` outside the CLI flags is **refused** (ini `[harness]`, `--set harness.x`, `PS2R_SET`) with a named error; the silent `out.harness = cli_harness` discard is gone | `config_loader.cpp` `IsHarnessKey/kHarnessRefusal`; two new `test_config` cases |
| 3 | `ring_tags` row reworded: ids/channels/names verbatim, 4 arg-doc strings reworded (listed) | §1A table |
| 4 | Frozen-vocabulary exception recorded for the 3 report literals (`[R127-BENCH]`, `[R123-MTVUSYNC]`, `[R147-GFXRES]`) | `tests/lint/README.md` "Accepted exception" + this row; the lint still does not scan string literals |
| 5 | `cmd_ring.h` cites `tests/core/test_threads_log.cpp` | header comment |
| 6 | `check_getenv_sites.py` scans `engine app games tests tools` | `CoreSingleGetenvSite` |
| 7 | `engine/core/hash/README.md` written | file |
| 8 | `Park::WaitUntil` deadline via `duration_cast<steady_clock::duration>` | `park.cpp` |
| 9 | Process only — no heredoc/stdin scripts this stage | — |

### Files (one primary type per pair; `engine/core/CMakeLists.txt` → `ps2r_core`; `engine/platform` → `ps2r_platform`)

| New | Type | OLD source (read-only) | Shape / notes |
|---|---|---|---|
| `core/savestate/savestate_format.h/.cpp/.inl` | free fns `Crc32`, `SerializeStateFile`, `WriteStateFile`, `ParseStateFile`, `ReadStateFile`, `TagName`, `PutPod/PutBytes`; PODs `FileHeader`(32) / `SectionHeader`(24) / `Blob` / `FileMeta` | `savestate_format.{h,cpp}` | PS2SAVE1 layout **FROZEN** (static_asserts 32/24); file versions 1..4 (2 legacy, 3 TIMR, 4 SNDC), `kSectionRequired`, all 30 section tags; reader keeps `file_version` in `FileMeta` so a parsed file re-serializes exactly; `PS2_SAVE_ATOMIC` **NORM**: tmp + fclose + rename always, tmp removed on any failure; the GSST vertex-record codec is NOT here (belongs to gs/ with its section, Phase 6) |
| `core/savestate/payload_reader.h/.cpp/.inl` | `PayloadReader` | `savestate_format.h` PayloadReader | bounds-checked cursor (`Get<T>/GetBytes/Skip/Remaining/Offset`) |
| `core/savestate/section_registry.h/.cpp` | `SectionRegistry` (+ `SectionSpec`) | `savestate.{h,cpp}` orchestration | register/capture (registration order, REQUIRED flag from spec) / `FileVersionFor` (TIMR→3, SNDC→4) / restore: unknown non-required skipped, unknown REQUIRED = error, missing required = error, validate-all-then-restore-all; `PS2_W13R7_NORESEED` DELETE (nothing written) |
| `core/savestate/savestate_writer.h/.cpp` | `SavestateWriter` (+ `WriteJob`, `WriteResult`) | `savestate_writer.{h,cpp}` | one-slot async writer thread woken through `Park` (the only wait primitive); `TryEnqueue` false when busy (never queue two); `Flush/JoinForExit` idempotent; async bytes == sync bytes (test); `PS2_SLOWSPOT_ASYNC` **NORM** (async always); the PPM-copy slot and the `[E2-SAVE-TIMING]` print are not transplanted (triage 9.2 consumers) |
| `core/verify/frame_hasher.h/.cpp` | `FrameHasher` | `verify/frame_hasher.{h,cpp}` | verbatim: u32 nw/nh LE header + RGB top-down over a bottom-up buffer, top-left texel per S×S block, 0 = no readable scanout |
| `core/verify/audio_hasher.h/.cpp` | `AudioHasher` | `verify/audio_hasher.{h,cpp}` | verbatim: 4800-frame windows over raw float bytes, tail folds its frame count |
| `core/verify/memory_checkpointer.h/.cpp` | `MemoryCheckpointer` (+ `MemorySpan`) | `verify/memory_checkpointer.{h,cpp}` | every 60 boundaries; spans supplied by the caller (no memory globals) — chained FNV in span order |
| `core/verify/run_meta.h/.cpp` | `RunMeta` (+ `Identity`) | `verify/run_meta.{h,cpp}` | OLD key set + order (`build_id gl_renderer tree profile env_ps2 env_hash frames wall_ms finalized`), `tree=new`, `env_ps2 = Config::Fingerprint()`, `env_hash = Config::FingerprintHash()`; early stamp on first present (frames=0, as OLD), rewrite every 64 presents + finalize; NEW-only informational keys appended via `Set()` (ignored by the gate) |
| `core/verify/frame_cap.h/.cpp` | `FrameCap` | `bench.h` frame cap + `main.cpp` exit path + `verify::FinalizeForExit` | count boundaries, finalizers in order, `std::_Exit`; compiled in all builds |
| `core/verify/verify_host.h/.cpp` | `VerifyHost` | `verify/verify_env.{h,cpp}`, `verify_present.cpp`, `verify_boundary.cpp`, `verify_pad.cpp` | armed from `Config.harness` only (no env); creates the leg dir, owns the 3 streams + run.meta + padlog recorder; `OnPresent/OnBoundary/OnPadServe/Finalize`; line formats frozen (`%u,0x%016llx`); the graphics-seam settle waits (`WaitPresentSeamIdle`, `BoundarySequencer::InFlight`) are not transplanted — Phase 3's tick source owns ordering |
| `core/triage/input_log.h/.cpp` | `InputLog` | `verify/input_recorder.{h,cpp}` + `pad_input.cpp` PadScript grammar/loader | the D9 single implementation: recorder (range/axis tokens, `HeaderLine`) + replayer (`ParseSpec` comma-joined, `LoadFile` comment-skipping, `Apply` active-low overlay, last axis segment wins); the padlog header `title=` is a parameter (no game token in engine/) |
| `platform/platform.h/.cpp` | `Platform` struct of function pointers + `PlatformCaps` + POD arg types; shared `Unimplemented*` stubs; `Register/Current` | ARCH §16 list, `thread_qos.h`, `proc_stats.h` idea | every §16 entry declared; stubs report once and fail (never crash) |
| `platform/macos/platform_macos.h/.cpp` | `PlatformMacos` | `thread_qos.h` | `now_ns` (steady clock), `thread_create/join` (pthread, name, USER_INTERACTIVE QoS), `open`, `cpu_relax`, page size, caps; `PS2_NO_QOS` **NORM** (QoS always; no opt-out) |
| `platform/macos/crash_handler.h/.cpp` | `CrashHandler` | `platform/crash_handler.h`, `platform/macos/crash_handler_macos.cpp` | SIGSEGV/BUS/ILL/FPE/ABRT → backtrace to fd 2 + `RingLog::DumpAllToPath(…, DUMP_CRASH)` + default action re-raise |
| `app/host/shutdown_ladder.h/.cpp` | `ShutdownLadder` | `shutdown_policy.h` | wait clamp 0..600 (default 10), `ShouldUnwindGuestThread`, `JoinActionAt` (warn at 5 s, hard exit at the wait); `PS2_SHUTDOWN_WAIT` → `engine.shutdown_wait_s` consumer |
| `app/host/main.cpp` (189 lines, class-free) | composition | `main.cpp` shell | `ConfigLoader(+Validator) → Logger → RingLog::Configure/RegisterThread → DiagRegistry (diag) → PlatformMacos::RegisterSelf + QoS → CrashHandler → --load-state envelope probe (parse+CRC now; payload restore Phase 3) → VerifyHost → TickSource stub → FrameCap → ShutdownLadder`; flags `--version --frames N --harness-out DIR --headless --profile deterministic --load-state --inputs --state-every`; title id from CMake `PS2R_TITLE` |
| `verify/run_leg.py --tree new` | — | — | CLI-armed leg, no env (`--anchor S`→`--load-state S`, `--frames`, `--inputs`, `--state-every`); bounded via `run_bounded.sh` |
| `verify/diff_legs.py --cross-engine --lanes` | — | — | cross-engine gate = `profile` equal + both `finalized=1`; `gl_renderer/env_hash/build_id/tree` differences are NOTEs; `--lanes meta,frames,audio,mem` (also `|`); `--selftest` extended (lanes, cross-engine pass/fail cases) |
| `verify/battery.py` | — | — | 12 legs × N reps: run NEW, ADMISSIBLE (reps), cross-engine per lane, `FP-OK` = `env_ps2 == NEW_DETERMINISTIC_FINGERPRINT` (added to `profile_deterministic.py`; == the `test_config` golden) |

The TickSource stub (main.cpp): one boundary per 16,667 virtual µs, no wall sleep under
`pacing=freerun`; per boundary it presents a black 640×448 native frame (non-zero hash, `gl_renderer=none`),
pumps 800 stereo frames of silence into the audio lane and hashes zeroed 32 MB + 16 KB + 2 MB spans at the
60-boundary cadence. 600 frames run in ~1.3 s.

### Flags consumed (FLAG_LEDGER outcomes applied while writing)

| Flag | Outcome | Where |
|---|---|---|
| `PS2_SAVE_ATOMIC` | NORM — atomic tmp+rename always | `WriteStateFile` |
| `PS2_SLOWSPOT_ASYNC` | NORM — async writer always | `SavestateWriter` |
| `PS2_W13R7_NORESEED` | DELETE | — |
| `PS2_NO_QOS` | NORM — QoS pin always | `PlatformMacos` |
| `PS2_SHUTDOWN_WAIT` | SETTING consumer | `ShutdownLadder` |
| `PS2_VERIFY_OUT/HEADLESS/RECORD_INPUT/STATE_EVERY/TREE/PROFILE` | SETTING `harness.*` (CLI-only) | `VerifyHost::Arm`, `RunMeta` |
| `PS2_PAD_SCRIPT`, `PS2_PAD_SCRIPT_FILE` | TEST channel → `harness.inputs` | `InputLog::LoadFile` (consumer: pad/, Phase 7) |
| `PS2_GS_SCALE` (frame hasher's scale read) | SETTING `video.scale` | `VerifyHost::OnPresent(scale)` argument |
| `PS2_EE_RUNTOKEN` | NORM — schema note only (consumer Phase 3) | — |

### Tests (tests/core)

| Test | Proves |
|---|---|
| `Coretest_savestate_format` | CRC32 check value (`0xCBF43926`) + chaining; synthetic round-trip; every reader failure (section CRC, trailer CRC, 4 truncations, magic, version, unknown REQUIRED); atomic write leaves no `.tmp`; `PayloadReader`; `SectionRegistry` capture/version/restore policy; `SavestateWriter` async==sync bytes, slot semantics, failure reporting, idempotent join; **the 12 anchor states** (`artdet_f37152`, 9× `ps2state_slot0_*`, 2× `slowspot_*`, + `ps2state_slot0.ps2st`) **parse and re-serialize byte-identical** — SKIPs without OLD |
| `Coretest_verify_units` | OLD `test_verify_units` vectors verbatim (downsample rule, audio windows, recorder ranges `10-19:CROSS` / `30-34:LX=0` / `40-45:SELECT`); silence window 0 == `0x0a6fb8af536edb25`; memory checkpoint cadence/order; InputLog replayer (`LoadFile` round-trip of the recorder's lines, unknown tokens skipped, active-low overlay); run.meta key order/early stamp/finalize; FrameCap; a VerifyHost leg end-to-end (121 frame lines, 20 windows, 2 checkpoints, padlog header + range, disarmed host inert) |
| `VerifyDiffLegsSelftest` | extended: `--lanes`, `--cross-engine` NOTE/PASS/FAIL cases |

Quoted lines:
`anchor_roundtrip: 12/12 byte-identical through core/savestate` (all 12 are `v4 sections=30`, 44,155,482–44,155,690 bytes);
`silence window 0 = 0x0a6fb8af536edb25`; NEW coldboot leg `audio.ahash` line 0: `0,0x0a6fb8af536edb25`
(== OLD `verify/baseline/coldboot/audio.ahash` window 0);
`100% tests passed, 0 tests failed out of 15` (macos-dev), `100% tests passed, 0 tests failed out of 16` (macos-diag).

### The 12-leg META battery (`verify/battery.py --binary build/app/host/nativeee --lanes meta`, 62 s)

```
leg                                  frames reps        cross-engine (lanes=meta)                fingerprint note
artdet_f37152                           600 n/a         META-OK                                  FP-OK       frames=600
coldboot                               1500 n/a         META-OK                                  FP-OK       frames=1500
ps2state_slot0_f36935                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f39807                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f40391                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f44111                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f46203_red_crystal       600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f46949_yellow_crystal    600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f57292                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f59991                   600 n/a         META-OK                                  FP-OK       frames=600
slowspot_f37092_s1789428268             600 n/a         META-OK                                  FP-OK       frames=600
slowspot_f40298_s1789693059             600 n/a         META-OK                                  FP-OK       frames=600
battery: META-OK 12/12 legs; PASS
```

Cross-engine NOTEs on every leg (expected, not gated): `gl_renderer 'Apple M4' vs 'none'`,
`env_hash 0x9a0f815a02d994d4 vs 0x3542d8e175cb1bb0` (different vocabularies by construction; the
gate is `profile` + `FP-OK`), `build_id`, `tree legacy vs new`. Lane status for the record (PHASE1_PLAN §5):
frames DIVERGENT at OLD's first non-zero present (NEW hashes a black frame), audio IDENTICAL on
silence windows until OLD's first sound, mem DIVERGENT (no game). No lane identity is claimed.

### §7 gate checklist (evidence)

| Gate item | Status | Evidence |
|---|---|---|
| configure/build/ctest 100 % dev + diag | ✅ | `15/15` dev, `16/16` diag (above) |
| `lint_one_class` 0 findings, `lint_engine_purity` 0 tokens | ✅ | `72 files … 0 findings`, `73 files … 0 game tokens` |
| exactly one getenv site | ✅ | `getenv sites: 1 -> PASS` (roots now include games/tests/tools) |
| battery 12/12 META-OK, coldboot audio window 0 == `0x0a6fb8af536edb25` | ✅ | table above; `/tmp/p1b/battery/coldboot.rep1/audio.ahash` line 0 |
| 12 anchor `.ps2st` parse + re-serialize byte-identical | ✅ | `anchor_roundtrip: 12/12` |
| every new directory's README rows filled | ◐ | `hash/` written; `savestate/ verify/ triage/ platform/ app/host` READMEs carry their OLD-source rows from Phase 0 — the per-flag rows are in the table above, not yet copied into those READMEs |
| `settings_table.tsv` + `tools/gen_settings_table.py` + `test_config_schema` (R14) | ❌ not started | HANDOFF |
| nothing game-visible; no behaviour claim | ✅ | the stub presents black frames and silence |

### HANDOFF

Done: Review-1A items 1–9; §6 steps 4, 7, 8, 9, 10 (savestate, verify + InputLog, platform, app
composition, run_leg/diff_legs/battery + the META battery); tests + CMake registration
(`engine/core`, `engine/platform`, `app/host`, `tests/core`); tree green at 15/15 + 16/16.

Not done (stage 1C):
1. `tools/gen_settings_table.py` → `docs/filemap/settings_table.tsv` (from FLAG_LEDGER SETTINGS rows
   + PHASE1_PLAN §3) and `tests/core/test_config_schema.cpp` enumerating it (CONSTRUCTION_PLAN R14);
   ctest name `Coretest_config_schema`; the golden fingerprint should then be read from the TSV, not typed
   in `test_config`/`profile_deterministic.py` twice.
2. README flag rows for `core/savestate`, `core/verify`, `core/triage`, `platform/`, `app/host`
   (copy the "Flags consumed" rows above), and `verify/README.md` rows for `battery.py`,
   `--tree new`, `--cross-engine`, `--lanes`.
3. Optional hardening: `test_shutdown_ladder` (PHASE1_PLAN §2.9 row — the ladder is exercised only by
   compilation today); `battery.py --reps 2` ADMISSIBLE run (~2 min) quoted in this file.
4. The `--load-state` envelope probe in `main.cpp` reads a 44 MB file per anchor leg (~0.5 s); once
   Phase 3 restores payloads this becomes the real load — no change needed now.

Next command: `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (green), then write
`tools/gen_settings_table.py` from `docs/filemap/FLAG_LEDGER.md`'s SETTING rows and register
`test_config_schema` in `tests/core/CMakeLists.txt`.

### Review 1B (stage p1br, 2026-09-26) — verdict: NEEDS_CHANGES (the 1B HANDOFF items remain; what landed is sound)

Method: fresh `cmake --build --preset macos-dev --target clean` (44 files) then `cmake --build --preset
macos-dev -j4` (34 CXX objects, `-Wall -Wextra -Werror`, zero compiler warnings; ONE linker warning, finding 3)
→ `ctest --preset macos-dev` **`100% tests passed, 0 tests failed out of 15`**; `cmake --build --preset
macos-diag -j4` → `ctest --preset macos-diag` **`100% tests passed, 0 tests failed out of 16`**. Lints run
directly: `LintOneClass: 72 files scanned, 0 findings -> PASS`, `LintEnginePurity: 73 files scanned, 0 game
tokens -> PASS`, `getenv sites: 1 -> PASS` (`config_loader.cpp:293`; an independent grep of
`engine app games tests tools verify` finds the same single code site). OLD opened read-only; OLD binaries
not run; helpers under `/tmp/p1br/` only (`cmp_anchor.cpp`); no stdin scripts; no commits.

| Check | Result |
|---|---|
| Review-1A items 1–9 | all closed as recorded: defaults 2048/10 + validator `>= 0` (`config.h:108-110`, `config_validator.cpp:54-56`); `harness.*` refused in `ApplyIniFile` (:98), `ApplySetBlob` (:189) which `PS2R_SET` also goes through (:293) — no `out.harness = cli_harness` remains; `cmd_ring.h:4` cites `test_threads_log.cpp`; `park.cpp:26` `duration_cast<steady_clock::duration>`; `check_getenv_sites.py` roots `engine app games tests tools`; `hash/README.md` present; `tests/lint/README.md` "Accepted exception" names the 3 literals (the only `[R…]` hits in `engine/` are those 3 + `bench.cpp:188`, all frozen report vocabulary) |
| 12 anchor round-trip | `test_savestate_format build/test_scratch/core <OLD>` → 12 × `IDENTICAL`, `anchor_roundtrip: 12/12 byte-identical through core/savestate` (v4, 30 sections, 44,155,690 / 44,155,482 bytes). Independent: `/tmp/p1br/cmp_anchor` (NEW `ReadStateFile` → NEW atomic `WriteStateFile` to /tmp) then `cmp` vs OLD: `ps2state_slot0_f46203_red_crystal` IDENTICAL (44155690 B, sha256 `31b0c1be26bc7dd6…` both) and `ps2state_slot0_f57292` IDENTICAL (44155482 B, `d920ed07a1a3e135…` both); no `.tmp` left |
| `run_leg --tree new` | coldboot 1500 frames → `run_leg: OK`; leg has `frames.fhash` (1500 lines, `1,0x…`), `audio.ahash` (250), `mem.mhash` (25), `run.meta` (`tree=new`, `profile=deterministic`, `env_hash=0x3542d8e175cb1bb0`, `frames=1500`, `finalized=1`); CLI-armed only (`--profile --headless --harness-out --frames [--load-state --inputs --state-every]`), through `run_bounded.sh` |
| coldboot audio window 0 | NEW `0,0x0a6fb8af536edb25` == OLD `verify/baseline/coldboot/audio.ahash` line 0 |
| META gate vs 12 frozen baselines | `battery.py --lanes meta`: **`battery: META-OK 12/12 legs; PASS`** (table identical to §1B's). Independent loop of `diff_legs.py --cross-engine --lanes meta <baseline> <new>` over all 12: every leg `meta: gate PASS (profile=deterministic, both finalized)`; NOTEs only for gl_renderer/env_hash/build_id/tree |
| hasher vectors | NEW `test_verify_units` carries OLD's downsample rule (`HashNativeScale(phys,4,4,2) == (native,2,2,1)`, scale-1 differs), the 3 audio-window cases + index increment, recorder `10-19:CROSS` / `30-34:LX=0` / `40-45:SELECT`; FNV `""`/`"a"`/`"foobar"` in `test_hash` |
| composition root | `main.cpp` 189 lines, class-free, order as §1B; `ShutdownLadder` 21+31 lines |
| Platform vs ARCH §16 | every §16 member present (`create_surface poll_input audio_open open thread_create fiber_create fiber_switch now_ns timer_create map_reserve map_protect page_size caps`) + `thread_join`, `cpu_relax`; no platform `#ifdef` outside `engine/platform` |
| settings table (R14) | **absent**: no `tools/gen_settings_table.py`, `docs/filemap/settings_table.tsv`, `tests/core/test_config_schema.cpp`; golden fingerprint typed in `test_config.cpp` and `profile_deterministic.py` |
| README rows | `savestate/ verify/ triage/ platform/ app/host` READMEs: 0 flag rows; `verify/README.md`: no `battery.py` / `--tree new` / `--cross-engine` / `--lanes` rows |
| license / one class / stdin / multiprocessing | no license text under `engine app tests games tools`; lint 0 findings; no `multiprocessing`/stdin in `verify tests tools` scripts |

Findings (1–2 are the open HANDOFF steps; 3–5 small defects; 6–7 informational):

1. **Settings table (CONSTRUCTION_PLAN R14) not started**: write `tools/gen_settings_table.py` (FLAG_LEDGER SETTING
   rows + PHASE1_PLAN §3) → `docs/filemap/settings_table.tsv`; add `tests/core/test_config_schema.cpp`
   (`Coretest_config_schema`) enumerating the TSV against `Config::Keys()`/defaults; read the deterministic
   golden fingerprint from the TSV in `test_config` and `profile_deterministic.py` (typed once in each today).
   Then flip the §7 row to ✅.
2. **README flag rows**: copy the §1B "Flags consumed" rows into `engine/core/savestate/README.md`,
   `engine/core/verify/README.md`, `engine/core/triage/README.md`, `engine/platform/README.md`,
   `app/host/README.md`; add `verify/README.md` rows for `battery.py`, `run_leg --tree new`,
   `diff_legs --cross-engine`, `--lanes`. Flip the §7 row to ✅.
3. **Fresh build is not warning-free**: `ld: warning: ignoring duplicate libraries: 'engine/core/libps2r_core.a'`
   (dev and diag) — `app/host/CMakeLists.txt:15` links `ps2r_core ps2r_platform` while `ps2r_platform`
   already PUBLIC-links `ps2r_core`. Link `ps2r_platform` (and `ps2r_diag`) only.
4. **`diff_legs` verdict over-claims under `--lanes`**: with `--lanes meta` the final line is
   `diff_legs: IDENTICAL` (exit 0 = "all requested lanes", documented) — print the lane set in the verdict
   (`IDENTICAL (lanes=meta)`) so a log reader cannot mistake a META-only pass for lane identity. Extend the
   selftest for the text.
5. **`main.cpp:103` paces with `std::this_thread::sleep_for`** in the TickSource stub (non-freerun arm) — the
   tree's rule is "Park is the ONLY wait primitive"; use `Park::WaitUntil(target)` (or drop the arm until Phase 3).
   Unreachable in harness runs (`pacing=freerun`); hygiene only.
6. Informational (frames lane, Phase 3+): OLD `baseline/coldboot/frames.fhash` has 1494 lines for 1500 vsyncs
   (gaps after 63→66, 419→422, 618→620, 965→967 — present-skip paths); the NEW stub emits 1..1500
   contiguously. Not gated by META; record when the frames lane is claimed (EQUIVALENCE_PLAN §7 if OLD is wrong).
7. Nit: `verify/run_leg.py` still carries a Phase-0 `[P0-HARNESS]` comment tag (pre-1B); the session-tag rule
   is stated for code — strip it when the file is next touched. Optional items from the HANDOFF
   (`test_shutdown_ladder`, `battery.py --reps 2` ADMISSIBLE quote) stay optional.

Not findings (verified OK): the loader's `PS2R_SET` tier shares `ApplySetBlob`, so the harness refusal covers it;
`SavestateWriter` waits only through `Park` (`wake_`/`idle_`); `CrashHandler` uses `write`/`backtrace_symbols_fd`/
`sigaction`/`raise` (async-signal-safe) + `RingLog::DumpAllToPath`; `run_leg_new` sets only `RB_*` for the runner,
never `PS2_*` for the binary; `FP-OK` would catch any stray `PS2R_SET` in a dev-build leg.

## §1C — Phase 1 gate close (stage p1c, 2026-09-26): Review-1B items 1–7 + full re-proof

Gate at the stopping point: fresh `cmake --build --preset macos-dev --target clean` → `cmake --build
--preset macos-dev -j4` (`-Wall -Wextra -Werror`, **0 compiler or linker warnings**) → `ctest --preset
macos-dev` **`100% tests passed, 0 tests failed out of 18`**; fresh `macos-diag` clean + build (**0
warnings**) → **`100% tests passed, 0 tests failed out of 19`**. `LintOneClass: 74 files scanned, 0
findings -> PASS`, `LintEnginePurity: 73 files scanned, 0 game tokens -> PASS`, `getenv sites: 1 -> PASS`.
OLD opened read-only (`tests/test_shutdown_policy.cpp` for the ladder tables, `verify/baseline/coldboot`
for the present-skip count); OLD binaries not run; helpers/logs under `/tmp/p1c/` only; no stdin scripts;
no commits.

### Review 1B — all 7 items closed

| # | Fix | Where / proof |
|---|---|---|
| 1 | **Settings table (R14)**: `tools/gen_settings_table.py` → `docs/filemap/settings_table.tsv` (59 rows: `key type peak det source_var ledger`; header `# deterministic_fingerprint=`). The generator holds PHASE1_PLAN §3 resolved to `Config::Get()` strings (the §3 prose defaults — "dev on", "today's set", "TRIAGE §3", "GUI default" — are not machine-parsable) and refuses to write unless every key is named by a §3 row and no §3 key is missing, every FLAG_LEDGER SETTINGS-section var maps to a key or is listed `FOLDED` (`PS2_BUGREPORT` → `triage.enabled`; `PS2_BUGREPORT_SLOWSPOT_MAX` → no field, TRIAGE §3 cooldown constant) / `BUILD_ONLY` (`PS2_RECOMP_SHARD_COUNT`), and every `PS2_*` source var has a master-table row (its outcome = the `ledger` column: 28 SETTING, 3 NORM — `PS2_MTVU`, `PS2_VBLANK_FREERUN`, `PS2_TIME_EMU_REGIME` —, 1 TEST — `PS2_PAD_SCRIPT_FILE` via its parent —, 27 `-` plumbing). `tests/core/test_config_schema.cpp` (`Coretest_config_schema`) enumerates the TSV against the compiled `Config`: bijection with `Config::Keys()` in order, peak column == `Config()` defaults, det column == `DeterministicProfile::Apply`, every bool/int/float/str/enum domain parses (and rejects a non-member), header fingerprint == fingerprint derived from the rows == `Config::Fingerprint()` of the preset. **Single source**: `test_config.cpp` now READS the golden from the TSV header (argv[2]); `verify/profile_deterministic.py` `NEW_DETERMINISTIC_FINGERPRINT = load_new_fingerprint()` reads the same header (the typed constant is gone from both). ctest `SettingsTableReproducible` (`--check`) fails when the TSV is stale | `settings_table: 59 rows == Config::Keys() (59); peak + deterministic columns match; deterministic_fingerprint header == rows == preset`; `gen_settings_table: docs/filemap/settings_table.tsv reproducible (59 rows) -> PASS`; `deterministic env_hash=0x3542d8e175cb1bb0` unchanged; battery `FP-OK` ×12 with the TSV-sourced value |
| 2 | **README flag rows** copied from the §1B "Flags consumed" table into `engine/core/savestate/README.md` (`PS2_SAVE_ATOMIC`, `PS2_SLOWSPOT_ASYNC`, `PS2_W13R7_NORESEED`, `PS2_SAVE_SLOT`), `engine/core/verify/README.md` (`PS2_VERIFY_*`, `PS2_GS_SCALE`, `CollectEnv`, `--bench-frames`), `engine/core/triage/README.md` (`PS2_PAD_SCRIPT(_FILE)`, `PS2_VERIFY_RECORD_INPUT`, `PS2_SLOWSPOT`/`PS2_BUGREPORT*`, the `PS2_SLOWSPOT_*` constants), `engine/platform/README.md` (`PS2_NO_QOS`, crash handler), `app/host/README.md` (`PS2_SHUTDOWN_WAIT`, `PS2_LOG`, `PS2_LOG_CHANNELS/RING_EVENTS`, `PS2_VERIFY_*` → CLI, `PS2_WAITFIX`, chdir/argv join not transplanted); `verify/README.md` rows for `run_leg.py --tree new`, `diff_legs.py --cross-engine`, `--lanes`, `battery.py`, and `profile_deterministic.py`'s TSV-sourced fingerprint; `docs/filemap/README.md`, `tools/README.md`, `tests/core/README.md` name the new artifacts | files |
| 3 | `app/host/CMakeLists.txt` links **`ps2r_platform` only** (it PUBLIC-links `ps2r_core`; `ps2r_diag` still added under the diag preset) | fresh dev build log `grep -c warning` → **0**; fresh diag build log → **0** (the `ld: warning: ignoring duplicate libraries: 'engine/core/libps2r_core.a'` line is gone from both) |
| 4 | `diff_legs.py` verdict names its lane set: `verdict(code, lanes)` → `diff_legs: IDENTICAL (lanes=meta)` / `DIVERGENT (lanes=meta,frames,audio,mem)` / `META-GATE-FAIL (lanes=…)`; docstring states it; `--selftest` covers the four texts | `diff_legs selftest: PASS`; on the real coldboot leg: `--lanes meta` → `diff_legs: IDENTICAL (lanes=meta)`, all lanes → `diff_legs: DIVERGENT (lanes=meta,frames,audio,mem)` |
| 5 | `main.cpp` **keeps the realtime pacing arm and waits through `Park::WaitUntil`** (decision: keep, not drop — the arm is the stub's only non-freerun behaviour and costs 3 lines): a local `Park pacer`; `wall0`/`now` in `Park::NowNs()`'s steady-clock domain; `while (NowNs() < target) pacer.WaitUntil(target, pacer.Generation())` (nobody notifies, so it returns at the deadline). `<thread>`/`sleep_for` gone from `app/` | `grep -rn sleep_for engine app` → 0 hits; harness legs unaffected (`pacing=freerun`) |
| 6 | **Present-skip observation recorded** in `EQUIVALENCE_PLAN.md` §7.1 "Pending adjudication items" row P1: OLD `baseline/coldboot/frames.fhash` = **1494 lines, last key 1500**, gaps **63→66, 419→422, 618→620, 965→967** (re-counted from the frozen file, not copied from the review); NEW stub emits 1..1500; not gated by META; adjudication deferred to the frames-lane claim (Phase 6); **OLD not patched**, neither tree changed | `docs/design/EQUIVALENCE_PLAN.md` §7.1 |
| 7 | `[P0-HARNESS]` tag stripped from `verify/run_leg.py` (comment reworded, content kept) | `grep -rn "P0-HARNESS" verify tests tools engine app` → 0 |
| opt | `tests/core/test_shutdown_ladder.cpp` (`Coretest_shutdown_ladder`): the OLD `test_shutdown_policy` truth tables ported — (a) env parse → `Clamp` (≤0→0, 601/99999→600), (b) `ShouldUnwindGuestThread` (never while running, never on host threads, never when wait=0), (c) `JoinActionAt` ladder (Wait <5 s, Warn once ≥5 s, HardExit ≥ wait with priority, wait=0 never hard-exits, wait=1 immediate bound). Compiles `app/host/shutdown_ladder.cpp` directly (app code, not a library) | `test_shutdown_ladder: all checks passed` |
| opt | `battery.py --reps 2` ADMISSIBLE run | see the quoted line below |

### The 12-leg META battery, re-run on the gate build (`verify/battery.py --binary build/app/host/nativeee --lanes meta`, 66 s)

```
leg                                  frames reps        cross-engine (lanes=meta)                fingerprint note
artdet_f37152                           600 n/a         META-OK                                  FP-OK       frames=600
coldboot                               1500 n/a         META-OK                                  FP-OK       frames=1500
ps2state_slot0_f36935                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f39807                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f40391                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f44111                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f46203_red_crystal       600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f46949_yellow_crystal    600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f57292                   600 n/a         META-OK                                  FP-OK       frames=600
ps2state_slot0_f59991                   600 n/a         META-OK                                  FP-OK       frames=600
slowspot_f37092_s1789428268             600 n/a         META-OK                                  FP-OK       frames=600
slowspot_f40298_s1789693059             600 n/a         META-OK                                  FP-OK       frames=600
battery: META-OK 12/12 legs; PASS
```

`FP-OK` now compares against the TSV-sourced fingerprint. Coldboot `audio.ahash` line 0 (NEW
`/tmp/p1c/battery/coldboot.rep1`) `0,0x0a6fb8af536edb25` == OLD `verify/baseline/coldboot/audio.ahash`
line 0 `0,0x0a6fb8af536edb25`.

Optional ADMISSIBLE run, `verify/battery.py --binary build/app/host/nativeee --lanes meta --reps 2`
(127 s, 24 legs; reps compared with the full meta gate — same binary — then cross-engine):
**`12 × ADMISSIBLE META-OK FP-OK`**, `battery: META-OK 12/12 legs; PASS` (every leg's two reps
byte-identical across frames/audio/mem/meta; the stub is deterministic run to run).

### PHASE1_PLAN §7 gate — every row with its evidence (this stage's fresh runs)

| Gate row (PHASE1_PLAN §7) | Status | Evidence (quoted) |
|---|---|---|
| `cmake --preset macos-dev && cmake --build --preset macos-dev && ctest --preset macos-dev` 100 % (lints + `tests/core/*` + `VerifyDiffLegsSelftest`) | ✅ | fresh clean build, 0 warnings; **`100% tests passed, 0 tests failed out of 18`** (LintOneClass, LintOneClassSelftest, LintEnginePurity, LintEnginePuritySelftest, SmokeEmptyBinary, VerifyDiffLegsSelftest, FlagPhaseCountsReproducible, SettingsTableReproducible, Coretest_hash, Coretest_threads_log, Coretest_ring_log, Coretest_verify_units, Coretest_config, Coretest_config_schema, Coretest_shutdown_ladder, Coretest_savestate_format, CoreRingDecodeOracle, CoreSingleGetenvSite) |
| `macos-diag` preset also builds and passes | ✅ | fresh clean build, 0 warnings; **`100% tests passed, 0 tests failed out of 19`** (+ `CoreBenchReportFormat`) |
| `lint_one_class.py --root .` → 0 findings; `lint_engine_purity.py --root .` → 0 tokens | ✅ | `LintOneClass: 74 files scanned, 0 findings -> PASS`; `LintEnginePurity: 73 files scanned, 0 game tokens -> PASS` |
| `grep -rn getenv engine app` → exactly one site (`core/config/config_loader.cpp`, dev builds, `PS2R_SET`) | ✅ | `getenv sites: 1 -> PASS` (`check_getenv_sites.py` over `engine app games tests tools`; ctest `CoreSingleGetenvSite`) |
| `verify/battery.py --binary build/app/host/nativeee --lanes meta` → 12/12 `META-OK`, quoted; coldboot `audio.ahash` window 0 == OLD `0x0a6fb8af536edb25` | ✅ | **`battery: META-OK 12/12 legs; PASS`** (table above); `0,0x0a6fb8af536edb25` both trees |
| The 12 OLD anchor `.ps2st` parse and re-serialize byte-identical through `core/savestate` (envelope only) | ✅ | `Coretest_savestate_format` Passed in both presets (`anchor_roundtrip: 12/12 byte-identical through core/savestate`, §1B; unchanged code) |
| Every new directory's README rows filled (OLD sources, flags applied) | ✅ | all ten Phase-1 directories carry "Transplanted from" + "Flags applied" rows: `engine/core/config` (13 rows; points at `docs/filemap/settings_table.tsv` as the SETTING source + the §1A DELETE/TEST rows), `engine/core/log` (9 rows: `PS2_CRASHRING` NORM, `PS2_LOG`/`PS2_LOG_CHANNELS`/`PS2_LOG_RING_EVENTS` SETTING, `PS2_LOG_VERBOSE` DIAG, `PS2_LOG_FASTFILTER`/`PS2_LOG_ELIDE_DROPPED`/`CT_FULL_LOG` DELETE), `engine/core/diag` (10 rows: `PS2_PERF` → `diag.perf`, `PS2_VERBOSE_HW` + the 17 census/probe DIAG vars, `PS2_U3_VIEWPORT` DELETE), `engine/core/threads` (2 rows: `PS2_WAITFIX` NORM), `engine/core/hash`, `engine/core/savestate`, `engine/core/verify`, `engine/core/triage`, `engine/platform`, `app/host` (item 2 above + Review-1C finding 2, stage p1d); `verify/README.md` script rows; `docs/filemap`, `tools`, `tests/core` name the generated table and new tests. Stray-artifact fix (Review-1C finding 1): `tests/core/test_savestate_format.cpp` `main` now refuses to run without an explicit scratch dir (usage error, exit 2; `"."` rejected), `rt.ps2st`/`async1.ps2st` removed — repo root clean (`ls *.ps2st` → none) |
| `settings_table.tsv` + `tools/gen_settings_table.py` + `test_config_schema` (R14) | ✅ | item 1 above: `Coretest_config_schema` + `SettingsTableReproducible` Passed; fingerprint single-sourced |
| Nothing game-visible exists; no behaviour claim is made or accepted | ✅ | the TickSource stub presents black 640×448 frames and silence; frames/mem lanes DIVERGENT for the record only; the frames-lane present-skip question is parked in EQUIVALENCE_PLAN §7.1 P1, not resolved |

### Phase 1 gate: PASSED

Quoted: `100% tests passed, 0 tests failed out of 18` (macos-dev, fresh clean build, 0 warnings);
`100% tests passed, 0 tests failed out of 19` (macos-diag, fresh clean build, 0 warnings);
`LintOneClass: 74 files scanned, 0 findings -> PASS`; `LintEnginePurity: 73 files scanned, 0 game tokens
-> PASS`; `getenv sites: 1 -> PASS`; `battery: META-OK 12/12 legs; PASS`; coldboot audio window 0
`0,0x0a6fb8af536edb25` (NEW == OLD); `settings_table: 59 rows == Config::Keys() (59); peak + deterministic
columns match; deterministic_fingerprint header == rows == preset`; `diff_legs: IDENTICAL (lanes=meta)`.

Phase 1 claims META only (PHASE1_PLAN §1/§5). Open for later phases, none gating: the frames-lane
present-skip adjudication (EQUIVALENCE_PLAN §7.1 P1, Phase 6); `--load-state` restores payloads from
Phase 3; `ShutdownLadder` gains its thread-teardown consumer in Phase 3; the string-literal lint gap for
the three frozen report literals stays recorded in `tests/lint/README.md`.

### HANDOFF

Done: Review-1B items 1–7 and both optional items; full fresh re-proof on both presets; §7 rows flipped
to evidence; gate verdict recorded. Tree green at 18/18 (dev) + 19/19 (diag).

Not done: nothing remains in Phase 1. Next: `docs/phases/PHASE2_PLAN.md` (CONSTRUCTION_PLAN §4.2,
`gs/format` pure math) — start from `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`
(green) and open OLD `host_app/texture_decoder.*` / `gs_deswizzle.*` read-only.

### Review 1C (stage p1cr, 2026-09-26) — verdict: NEEDS_CHANGES (technical gate fully reproduced; two hygiene items)

Method (own runs, all bounded through `verify/run_bounded.sh`, helpers/logs under `/tmp/p1cr/` only, OLD
read-only — `find OLD -newer PHASE1_PLAN.md` → 0 files; no stdin scripts; no commits): fresh
`cmake --preset macos-dev` → `--target clean` → `cmake --build --preset macos-dev -j4` (49 steps,
`grep -c warning` → **0**, no `duplicate libraries` line) → `ctest --preset macos-dev`
**`100% tests passed, 0 tests failed out of 18`**; fresh `macos-diag` clean + build (56 steps, `grep -c warning`
→ **0**) → `ctest --preset macos-diag` **`100% tests passed, 0 tests failed out of 19`**.

| Check | Result |
|---|---|
| Lints / getenv | `LintOneClass: 74 files scanned, 0 findings -> PASS`; `LintEnginePurity: 73 files scanned, 0 game tokens -> PASS`; `check_getenv_sites.py` (roots `engine app games tests tools`) → `getenv sites: 1 -> PASS` (`config_loader.cpp:293`); `grep -rn getenv games` → 0 |
| Settings table (R14) | `docs/filemap/settings_table.tsv` present (59 data rows, `# rows=59`, `# deterministic_fingerprint=` header); `gen_settings_table.py --check` → `reproducible (59 rows) -> PASS`; `test_config_schema` direct → `settings_table: 59 rows == Config::Keys() (59); peak + deterministic columns match; deterministic_fingerprint header == rows == preset` / `all checks passed` |
| Fingerprint single-sourced | exact 317-char fingerprint string occurs ONLY in the TSV (and runtime `run.meta` outputs under `build*/test_scratch`); `test_config.cpp` reads it via `ReadGoldenFingerprint(argv[2])` and CHECKs non-empty + `== d.Fingerprint()`; `profile_deterministic.py` `NEW_DETERMINISTIC_FINGERPRINT = load_new_fingerprint()` from the TSV header (raises if absent); NEW coldboot `run.meta` `env_ps2` == TSV header (MATCH) |
| diff_legs verdict | real coldboot leg: `--lanes meta` → `diff_legs: IDENTICAL (lanes=meta)`; all lanes → `diff_legs: DIVERGENT (lanes=meta,frames,audio,mem)`; `--selftest` → `diff_legs selftest: PASS` |
| Park-only waiting | `grep -rn sleep_for engine app` → 0; `main.cpp:91-106` paces through `Park::WaitUntil` in `Park::NowNs()`'s domain; only other yield primitive is `platform_macos.cpp:65 CpuRelax() { std::this_thread::yield(); }` (platform relax, not a wait — acceptable) |
| Present-skip note | `EQUIVALENCE_PLAN.md` §7.1 row P1 present (1494 lines / last key 1500, gaps 63→66, 419→422, 618→620, 965→967, Pending, OLD not patched); re-counted: OLD `frames.fhash` 1494 lines, NEW 1500 lines, both last key 1500 |
| `[P0-HARNESS]` | `grep -rn P0-HARNESS verify tests tools engine app` → 0 (only the two PHASE1_IMPL history rows) |
| Link hygiene | `app/host/CMakeLists.txt:17` `target_link_libraries(nativeee PRIVATE ps2r_platform)` (+ `ps2r_diag` under diag, :20) |
| 12 anchors | `test_savestate_format /tmp/p1cr/scratch <OLD>` → 12 × `IDENTICAL`, `anchor_roundtrip: 12/12 byte-identical through core/savestate` |
| META battery | `battery.py --binary build/app/host/nativeee --lanes meta --out /tmp/p1cr/battery` (fresh dev binary) → all 12 legs `META-OK … FP-OK` (`FP-OK` ×12), **`battery: META-OK 12/12 legs; PASS`**; coldboot `audio.ahash` line 0 NEW `0,0x0a6fb8af536edb25` == OLD `0,0x0a6fb8af536edb25` |
| README rows | flag rows present in `hash/ savestate/ verify/ triage/ platform/ app/host` and `verify/README.md` (`run_leg --tree new`, `--cross-engine`, `--lanes`, `battery.py`, `profile_deterministic.py` TSV source); `docs/filemap`, `tools`, `tests/core` READMEs name the new artifacts. **Gap**: `engine/core/config/`, `log/`, `diag/`, `threads/` READMEs have "Transplanted from" but NO "Flags applied" rows (0 table lines) although they consumed the §1A flags table; `config/README.md` does not mention `settings_table.tsv` (finding 2) |
| Tree hygiene | `git status` shows untracked, un-ignored **`rt.ps2st` and `async1.ps2st` (4216 B each, 13:31:57 local, i.e. during stage p1c) in the repo root** — written by `tests/core/test_savestate_format.cpp:134/:209` when the binary is run without argv[1] (`:308` defaults `g_scratch` to `"."`); ctest passes `${PS2R_TEST_SCRATCH}` so ctest is unaffected (finding 1) |

Findings (both small; neither changes code behaviour or requires a new proof beyond `ctest`):

1. **Stray test artifacts in the repo root**: delete `rt.ps2st` and `async1.ps2st` (they would be swept into
   the user's commit), and stop the foot-gun — `test_savestate_format.cpp:308` should refuse to run without a
   scratch dir (usage error) or default to a temp directory, never `"."`. Re-run `ctest --preset macos-dev`.
2. **§7 row "Every new directory's README rows filled (OLD sources, flags applied)" is only partially
   evidenced**: copy the §1A "Flags consumed" rows into `engine/core/config/README.md` (and point it at
   `docs/filemap/settings_table.tsv` as its SETTING-row source), `engine/core/log/README.md`
   (`PS2_CRASHRING`, `PS2_LOG`, `PS2_LOG_CHANNELS`, `PS2_LOG_RING_EVENTS`, `PS2_LOG_VERBOSE`, the DELETE rows
   `PS2_LOG_FASTFILTER` / `PS2_LOG_ELIDE_DROPPED` / `CT_FULL_LOG`), `engine/core/diag/README.md` (`PS2_PERF`,
   `PS2_VERBOSE_HW` + the 17 census/probe DIAG vars, `PS2_U3_VIEWPORT` DELETE), `engine/core/threads/README.md`
   (`PS2_WAITFIX` NORM); then update the §1C gate-row evidence cell to name all ten directories.

Everything else in the §1C gate record is confirmed by this stage's own runs; once 1–2 land, the Phase 1
gate stands as PASSED with no further re-proof needed beyond the dev `ctest`.

### Review 1C — both hygiene items closed (stage p1d, 2026-09-26)

1. Stray artifacts: `rt.ps2st` / `async1.ps2st` gone from the repo root (`ls *.ps2st` → none);
   `tests/core/test_savestate_format.cpp` `main` refuses to run without an explicit scratch dir
   (usage error, exit 2; `"."` rejected) — ctest passes `${PS2R_TEST_SCRATCH}` and is unaffected.
2. README flag rows: `engine/core/config/README.md` (13 rows; `docs/filemap/settings_table.tsv` named as
   the SETTING source; §1A DELETE/TEST rows), `engine/core/log/README.md` (9 rows), `engine/core/diag/README.md`
   (10 rows: `PS2_PERF`, `PS2_VERBOSE_HW` + the 17 census/probe DIAG vars, `PS2_U3_VIEWPORT` DELETE),
   `engine/core/threads/README.md` (2 rows: `PS2_WAITFIX` NORM). §1C gate cell above now names all ten
   directories (`config log diag threads hash savestate verify triage` + `engine/platform` + `app/host`).

Re-proof (docs + one test-argument guard only): `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`
(bounded, `/tmp/p1d/build_ctest.log`) → **`100% tests passed, 0 tests failed out of 18`**. Phase 1 gate stands
as PASSED. Next: `docs/phases/PHASE2_PLAN.md` (written this stage) → stage 2A.

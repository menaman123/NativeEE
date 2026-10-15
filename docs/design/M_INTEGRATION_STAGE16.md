# Stage 16: cutover round 4 integrated — Graphics fixes 2/3/4 and the legacy ServiceImport

Integration branch `integration-20261009-r4` from `main` 0ecce12c (flip 8 reverted; defaults
`hle.movie_lifecycle=on`, `hle.math_helpers=on` plus the earlier `hle.meta_arena`,
`hle.sound_bank_headers`), executed on 2026-10-08 23:50 → 2026-10-09 00:25 EDT by the integrator.
Two owners' reviewed rounds were cherry-picked (`-x`) onto `main` in dependency order — Herschel
(services) first, then Zeno (graphics) — and Zeno's one shared-file patch was applied in the
single-writer slot; no merge commit, no conflict, no hand edit. Full command/result ledger:
`int_work/m_series/integration/r4/INTEGRATION_LOG.md` (logs alongside). Builds bounded
(`verify/run_bounded.sh`) at `-j4`; the full component suite ALONE at `-j8`; every game-process
class at `-j1`, one process. `main` was not committed to. No owner worktree was touched.

Branch HEAD (code) **d75b6ac8**; `git cherry main HEAD` → fifteen `+`; `git diff --stat main..HEAD`:
57 files, +1401/−77.

## What moved (serial order)

1. **Herschel `port-services-r3` c239eaf9** — eight commits, HANDOFF #3 §2 order (`r4-01` requires
   `97490eed` first): `1986f60d → bc02c1b3` (docs: M4 TRANSPORT_SCOPE), `97490eed → 6d20210b`
   *Import frozen legacy anchors into the selected file composition* (`app/host/service_import.{h,cpp}`,
   `ServiceRestore::SetImport`, `Section(legacy_absent)` on SessionServices/FileServices,
   `files.h kStepFunctions`, `tests/services/test_service_import_anchors.cpp`), `f6f6e6f5 → b2ff8328`
   *Compose the legacy service import in the root restore* (= patch **r4-01**: `ee_bootstrap_sections.cpp`,
   `ee_bootstrap_census.cpp` `service_import=` row, `leg_state.h`, `app/host/CMakeLists.txt`),
   `15235cc3 → 6aa72f20` *Census the StreamLibrary original's execution per entry and reason*
   (`StreamLibrary::CountFallbacks`, `run.meta stream_library_fallbacks=` — eight members partitioning
   the unchanged `fallbacks` total), `c6588bf1 → 392209d2` *Pin the retained CDVD-poll and headroom arms at
   hle.stream_files=off* (= patch **r4-03**: `RootCdvdPollVisibility` / `RootHeadroomQueryRecompiled` take
   `--set hle.stream_files=off` explicitly), `44fe9d8c → 064d3651` *Catalog the StreamLibrary original's
   per-entry execution counters* (= patch **r4-02**: `retirement_counters.json` +7 `StreamLibrary.original.*`
   rows, 97 → 104 counters), `ff448ec7 → 545d771c` (docs: INTENTS_SCOPE), `c239eaf9 → 2f4c5184` *Name the
   music Play/Child refusal clause in the census* (`music_library refused_window/refused_buffers/
   refused_voice`). The three patches are the cherry-picks of their commits: each `git apply --check
   --reverse` is clean afterwards (content present), and every file his commits touch is byte-identical
   to his HEAD (the only `tests/app/CMakeLists.txt` difference is main's Stage-15 selftest block).
   The frozen legacy anchors (OLD schema, no GCLK/SVCS/FILS) now restore under `hle.stream_files` /
   `hle.files` with every owner value derived from the image's own sections — the flip-6 gap
   ("selected service deadlines require GCLK") — provided on the 12 inventoried images without a game
   process; the root-level proof (RootAnchor* with the key ON) is the parent's flip-6 re-attempt.
2. **Zeno `port-graphics-r2` 04eabb34** — six commits: `d0457b63 → 78aef486` fix 4 (i)
   *Publish the census of a faulted Graphics run* (`GraphicsOutput::Record` publishes every row and
   `graphics_output_faulted=0|1`, never throws), `505b4bf6 → 3fa81f53` fix 2 *Complete the title's first
   frame Send before any Link* (`FrameEnvironment::Linked()`, `Counters::empty_sends`,
   `graphics_frame_other=…,empty_sends=N`; TR `renderer.cpp:436-440` Present precedes StartNextFrame),
   `6466a0b1 → e57648fc` fix 3 *Retain a refused owned Strip at its ordered slot*
   (`GraphicsStrip::Work → Outcome{Completed,Retained,Failed}`, `GraphicsOutput::RecordOwnedRetainable`,
   the captured `DrawIr` submitted through `GsState::SubmitDrawIr` at the slot — the Plain control's route;
   `graphics_strip_output=captured,completed,failed,retained,first_refusal`, `graphics_refusals_strips`
   detail `0x200|GraphicsScene::Result`), `5ac500ad → db8e86d3` *Retain a refused owned frame clear at its
   ordered slot* (`graphics_frame_output=…,retained_clears=N`, `graphics_refusals_frame` producer `0x19f1c8`),
   `23c07151 → 21b7cc3e` *Read Text items and strings on the scratchpad stack* (the fix-1 window rule for
   `TextIntent`/`TextService`), `04eabb34 → 52194a91` (docs: M6_M7_PHASE). Every file these commits touch
   is byte-identical to his HEAD.
3. **Zeno's isolated patch fix 4 (ii) → d75b6ac8** `fix(host): Publish the census of a faulted leg before
   rethrowing` — the one SHARED file (`app/host/ee_bootstrap_leg.cpp`, +18/−2;
   `int_work/m_series/graphics/integration/fix4-ee_bootstrap_leg-census-before-rethrow.patch`,
   `git apply --check` clean on `main` 0ecce12c and on the branch). Session path: a `std::exception` out of
   `session->Run` reaches `write_census()` and `closer.Close("exception", 0, 0, e.what())` before it
   propagates (so a faulted play-mode smoke reads `finalized=1`, `video=gl`, `graphics_output_faulted=1`
   and the family rows, and the runner still exits non-zero → Bernoulli's smoke verdict FAILS it). Cold-boot
   path: a caught failure parks as `park=exception` / `park_detail=<message>` instead of `guest_returned`.

Shared files vs `main`: root `CMakeLists.txt`, `engine/ee/cpu/dispatch_table.*`,
`games/<t>/hle/CMakeLists.txt`, `app/host/ee_bootstrap.cpp`, `engine/core/config/*` — all
**byte-identical**. Changed in the owners' slots: `retirement_counters.json` (+63, Herschel r4-02),
`app/host/ee_bootstrap_sections.cpp` (+34/−2), `ee_bootstrap_census.cpp` (+2), `leg_state.h` (+2),
`app/host/CMakeLists.txt` (+1) (Herschel r4-01), `app/host/ee_bootstrap_leg.cpp` (+18/−2, Zeno fix 4 ii).

## Defaults — unchanged vs `main`

`docs/filemap/settings_table.tsv`, `engine/core/config/`, `tools/gen_settings_table.py` have no diff vs
`main` 0ecce12c; `gen_settings_table: docs/filemap/settings_table.tsv reproducible (96 rows) -> PASS`.
The `hle.*` keys at `on` are exactly `hle.math_helpers`, `hle.meta_arena`, `hle.movie_lifecycle`,
`hle.sound_bank_headers`; `hle.graphics_frame off`, `hle.graphics_strips plain`, `hle.stream_files off`,
`hle.files off`. No gate ran this stage; nothing flips.

## Validation on the integration branch (Release preset)

- Step 1: `cmake --preset macos-release` rc 0; build `cmake --build --preset macos-release -j4 -- -k 0` →
  0 FAILED, 0 `warning:`, 0 `ld: warning` (`step1-build.log`). Herschel's focused set + lints/config →
  `100% tests passed, 0 tests failed out of 21` (`step1-ctest.log`): `ServiceRestore: 80 checks, 0 failures;
  pending bytes and descriptors resumed, model calls=0; legacy import admitted=1 refused=1`;
  `ServiceImportAnchors: 12 anchors, 86 checks, 0 failures` (+ `PASS no original call and no model enqueue
  during import`); `PASS fallback_census_splits_unowned_bypassed_and_bank_start_per_entry`,
  `PASS fallback_census_partitions_the_total`, `StreamLibrary: PASS (0 failures)`;
  `VerifyRetirementGateSelftest Passed`; `python3 -m pytest -q verify/tests_retirement_dt12.py` → `69 passed`.
  `ctest --show-only=json-v1`: `RootHeadroomQueryRecompiled … --frames 130 --timeout 120 --set
  hle.stream_files=off`, `RootCdvdPollVisibility … --timeout 700 --ring-cache off --set hle.stream_files=off`.
- Step 2: build → 0/0/0 (`step2-build.log`). Zeno's focused set (his handoff's 28-name regex + GraphicsMovie,
  MovieGraphicsSelfOracle, VerifyRetirementGateSelftest, LintSdkBindings) → `100% tests passed, 0 tests
  failed out of 31` (`step2-ctest.log`): `GraphicsFrame: 150 checks, 0 failures`, `FrameEnvironment: 1020
  checks, 0 failures`, `GraphicsStrip: 47 checks, 0 failures`, `GraphicsStrip: 169 checks, 0 failures`
  (`GraphicsStripGL`), `GraphicsRefusals: 20 checks, 0 failures`, `TextService: 106 checks, 0 failures`,
  `TextureConsumers Passed` (the compiled defaults still pinned — none changed).
- Final tree (BUILD-ID `2026-10-08 23:57:41`, code = d75b6ac8; the stage's last commit is docs-only):
  Release build → 0 FAILED, 0 `warning:`, 0 `ld: warning`, 0 `ignoring duplicate` (`final-build.log`).
  Full component suite ALONE `-j8 --timeout 900 -E '^(Root|GsCorpus|Render_AttractReplay|Render_TitleFrame|
  GsReplay)'` → **`100% tests passed, 0 tests failed out of 598`** (597 + `ServiceImportAnchors`), 133.72 s
  (`final-component-ctest.log`); 22 fixture skips = the Stage-13 classes (Cloth*/Shadow*/Distortion*/Wave*/
  Sky*/Mesh* asset oracles, Gs_Inv25/26/30, CoreDrawlistRecorder, VifArmsVsOld, Vu1Replay*/
  Vu1CatalogIdentity) + `Vu1CollectSweep (Disabled)`.
- `cmake --build --preset macos-dev --target nativeee -j4` → 0/0 (Debug BUILD-ID `2026-10-09 00:00:43`,
  rebuilt first for `RootReleaseLanesIdentical`; `final-dev-nativeee-build.log`), then the game-process
  classes `-j1 --timeout 1800 -R '^(Root|GsCorpus|Render_AttractReplay|Render_TitleFrame|GsReplay)'` →
  **`100% tests passed, 0 tests failed out of 76`**, 712.68 s (`final-root-ctest.log`; `GsCorpus_slowspot`
  Disabled, pre-existing): `RootPlayModeSmoke Passed 6.31 sec`, `RootMovieLifecyclePlay Passed 24.56 sec`,
  `RootPlayWallLock Passed 24.54 sec`, `RootReleaseLanesIdentical Passed 76.16 sec`, `RootCdvdPollVisibility
  Passed 58.24 sec`, `RootHeadroomQueryRecompiled Passed 2.18 sec` (both on the explicit old value),
  RootAnchor* 6/6, `RootColdbootProbeResumes Passed 2.18 sec`, `RootGsQueueThreadIdentical Passed 20.76 sec`.
- Default-binary smoke rows (a 120-frame `--harness-window` run after the group; `runner exit=0`,
  `smoke-default-runmeta-rows.log`): `finalized=1`, `video=gl`, `park=none`, `graphics_output_faulted=0`,
  `graphics_frame_other=…,empty_sends=0`, `graphics_frame_output=…,retained_clears=0`,
  `graphics_strip_output=captured=0,completed=0,failed=0,retained=0,first_refusal=0`,
  `graphics_refusals_{frame,strips}=total=0,…,first_fault=none`, `service_import=admitted=0,refused=0,
  files_published=0,disc_published=0,files=0,pending_windows=0,last_refusal=none`,
  `stream_library_fallbacks=start_read_declined=0,is_reading_declined=0,wait_declined=0,start_read_unowned=0,
  is_reading_unowned=0,wait_unowned=0,bank_start=0,bypassed=297` (= `stream_library … fallbacks=297`; the
  eight members partition the total). The new rows read their control-arm values; nothing else moved.

## Decisions and notes for the orchestrator

- The flip commits **`e57a3281` (flip 2), `e3435827` (flip 3) and `16c409bd` (flip 6) do not re-apply
  as-is** on this HEAD (`flips-reapply-check.log`: `git format-patch -1 --stdout <c> | git apply --check`
  fails on `tests/app/root_anchor_{frames,heartbeat,pad_ready}.py`, `tests/core/test_config.cpp`,
  `tests/title/textures/test_texture_consumers.cpp` for flips 2/3 and on the latter two for flip 6 —
  flips 4/5/7/8 and their reverts added explicit old-default pins at the same lines; the `config.h`,
  `settings_table.tsv`, `gen_settings_table.py` hunks apply). Re-do each as a fresh one-line default change
  + table regen + the explicit old-default pins beside the existing ones (the Stage-15 flip-1 finding).
- Flip 2 re-test (Zeno §4): `RootPlayModeSmoke` run.meta must read `graphics_frame_send=calls=N,replaced=N,
  model_calls=0`, `graphics_frame_other=…,refused=0,empty_sends=1` (2 if the interrupt Send also precedes
  the first Link), `graphics_refusals_frame=total=0,…,first_fault=none`, `graphics_frame_output=completed>0,
  setups>0,layouts>0,retained_clears=0`, `graphics_output_faulted=0`; then `--frames 63/64` probes and the
  4600 leg. `retained_clears>0` → the flip does not pass; send `graphics_refusals_frame.last` to Zeno. Mem
  advisory: the boundary-0 pair in `graphics/cutover-repairs/MEM_BOUNDARY0_PIN.md` (`--mask-ranges
  cutover-repairs/mem_boundary0_mask.txt` → expected `IDENTICAL-UNDER-MASK … masked 1376 B`; the
  `P7-FRAME-CHAIN` row for EQUIVALENCE_PLAN §7.1 is in that file, parent adds it).
- Flip 3 re-test: `--frames 118/120` probes and both smokes must COMPLETE (`finalized=1`,
  `graphics_output_faulted=0`, `runner exit=0`); acceptance needs `graphics_strip_output … completed=C>0`
  with `Graphics.strip.{plain,tinted,placed,placed_tinted}.model_calls=0`; `retained=R>0` is honest and
  attributed (`graphics_refusals_strips … first=frame:118/…/producer:0x1a9120/detail:F`, `F = 0x200|Result`:
  InvalidFrame=1, UnclosedDependency=2, ResourceFailure=3, MissingScene=4, StaleScene=5) — report `F` to Zeno.
- Flip 6 re-test (Herschel §3.1/§3.2/§7): with `hle.stream_files=on`, RootAnchor* 6/6, `RootColdbootProbeResumes`,
  the slowspot bench anchor must LOAD (expected `service_import=admitted=1,refused=0,files_published=1,
  disc_published=1,files=2,pending_windows=0,last_refusal=none`); criterion (c) is decided by
  `stream_library_fallbacks … start_read_declined == is_reading_declined == wait_declined == 0` on the leg
  and both smokes with `stream_library.reads/deliveries` and `file_schedule.issued/delivered/consumed`
  positive — expected `bank_start=120`, `is_reading_unowned + wait_unowned ≈ 2525` (the sound-bank path's
  by-design executions, retiring with `hle.sound_samples`); any `declined` non-zero is an owner defect and the
  flip must not hold. `hle.files` after `stream_files` holds. `RootCdvdPollVisibility` /
  `RootHeadroomQueryRecompiled` already carry `--set hle.stream_files=off` (nothing to weaken).
- With fix 4 (i)+(ii) in, a flip whose game process faults now leaves `finalized=1`, `park=exception`,
  `park_detail=<fault message>`, `graphics_output_faulted=1` and the family's `graphics_refusals_*.first_fault`
  in run.meta — and the smokes FAIL on exit code (Stage 15's verdict), never Skip.
- The two Herschel docs commits (`1986f60d`, `ff448ec7`) were included as his handoff orders; the task
  named only his six code commits. No other deviation.

## Next (orchestrator)

`git switch main && git merge --ff-only integration-20261009-r4` (the branch is `main` 0ecce12c + sixteen
commits, this doc included). Then CUTOVER.md: re-attempt flip 2 (`hle.graphics_frame`), flip 3
(`hle.graphics_strips=all`), flip 6 (`hle.stream_files`, then `hle.files`) as fresh default changes, one
stage each, REFERENCE = `int_work/m_series/cutover/REFERENCE` (→ `flip5/leg`).

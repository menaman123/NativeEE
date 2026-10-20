# FLAG LEDGER — master env-var table (merged across all nine filemap shards)

Cross-checked against `audit/A2_CONFIG_DIAG.md` §1 (the 496-var registry CSV).
Universe here: **541 named flags** = 496 A2 registry vars + 20 vars the shards found
that A2 missed + 16 vars added by ledger review (15 tests-local harness vars +
`PS2_VU1_MODE`) + 8 CMake build options dispositioned by shard 8 + `PS2R_DIAGNOSTICS`
(the ONE new build gate the rewrite introduces — not a today-var).

Outcome legend:
- **NORM** — the winning arm becomes the only code path; var and losing arm delete.
- **SETTING** — survives as a field of the ONE Config schema (core/config); env var deletes.
- **DIAG** — mechanism survives only in PS2R_DIAGNOSTICS builds (core/diag); env var deletes.
- **TEST** — harness/test-only channel (tests/, verify/, corpus tools); env var deletes from the product.
- **DELETE** — var, gate, and subject code all go (concluded experiments, retired tiers, session probes).

Scope rule (harness vars): the universe covers every env var the repo reads —
product code AND `tests/` harness code, regardless of prefix (`PS2_` or not).
Vars read ONLY by harness code under `tests/` (e.g. `PS2_REPLAY_*`,
`PS2_RESMEMO_TEST_*`, `EEPACE_SOAK_SEED`, `PAGEINV_DUMP`, `VIF_REPLAY_*`) get
outcome **TEST** — the knob survives in the harness; there is no product
`getenv` site to purge. Non-`PS2_`-prefixed vars read by PRODUCT code
(`CT_FULL_LOG` in main.cpp, `LOGO_MEMSNAP*` in manual_switch_dispatchers.cpp)
keep their rows and their product-side outcomes. Standard OS vars the harness
consults (`TMPDIR`) and toolchain-locator envs consumed only by the build system
(`VULKAN_SDK`, host_app/CMakeLists.txt:507 `$ENV{VULKAN_SDK}`) are not flags and
stay out of the universe. One stated exception, so this rule is literally true:
the 14 FAST/FAST2 `GateOn` sub-gates that default to their family master and have
no first-class row here — r118 master `PS2_GL_FAST` (texture_decoder.h:51):
`PS2_GL_NO_FLUSH`, `PS2_GL_VBO_RING`; r120/r121+ master `PS2_GL_FAST2`
(render.h:766): `PS2_GL_ALT16_GPU`, `PS2_GL_BLOCK_DECODE`, `PS2_GL_EPOCH_KEY`,
`PS2_GL_FASTHASH`, `PS2_GL_HASH_L2`, `PS2_GL_IDX_PBO`, `PS2_GL_LAZY_RTSYNC`,
`PS2_GL_LAZY_ZSYNC`, `PS2_GL_PAR_DECODE`, `PS2_GL_SYNC_RECT`, `PS2_GL_TEXSUB`,
`PS2_GL_UNIF_CACHE` — are dispositioned via their family master rows (NORM: the
winning arm is inlined and each sub-var deletes with its master), NOT as
first-class rows; sub-gates that do have their own rows (e.g. `PS2_GL_TEXCACHE`,
`PS2_GL_BBOX_COPY`, `PS2_GL_BIND_CACHE`) keep them. Independent gates that key
only on their own env (the R169-GLRUN set, amendment A8) are NOT family-mastered
and get first-class rows.

## Counts per outcome (all 541)

| Outcome | Vars |
|---|---:|
| NORM | 195 |
| SETTING | 27 |
| DIAG | 103 |
| TEST-ONLY | 39 |
| DELETE | 176 |
| NEW (PS2R_DIAGNOSTICS) | 1 |
| **Total** | **541** |

End state: **zero `getenv` outside core/config** (ARCH §8). Every SETTING row becomes a
Config field; everything else is code, a diagnostics-build capability, or gone.

Later additions not folded into the counts above (rows in the master table carry their
date tag): 2026-09-24 IOPK/MAINT pins (`PS2_IOP_KERNEL`, `PS2_IOP_DISPATCH`,
`PS2_RTC_FIXED`, `PS2_TICK_DETERMINISTIC`, `PS2_MAINT_EEPACED`, `PS2_T0_TICKPACED`,
`PS2_DETWATCH`, `PS2_FRAME_LOCKSTEP`); 2026-09-25 round 1A: `PS2_TICK_SYNC` (SETTING),
`PS2_EE_RUNTOKEN` (NORM), `PS2_EESCHED_DIAG` (DIAG) — +3 rows.

## PEAK CONFIG BECOMES NORM

The proven peak configuration — `scripts/run_play.sh` CANON plus every default-ON
proven-fix hatch — is the **sole code path** in the refactor. For each var below, the
winning arm is inlined and the gate plus losing arm are deleted (MIGRATION §3 gate
purge, equivalence-gated per family).

run_play.sh CANON vars that NORM (winning arm = the CANON value):
`PS2_VIF_FAST=1`, `PS2_GL_FAST=1`, `PS2_GL_FAST2=1` (and the entire FAST/FAST2
sub-gate family they master), `PS2_MTVU=1`¹, `PS2_EE_TIM0=1`, `PS2_VBLANK_FREERUN=0`
(real-time ticks; freerun survives only as the bench profile's `time.mode`),
`PS2_VU1_HLE=1`, `PS2_VU1_HLE_DIRECT=1` (already default-ON in the binary; pins
redundant), plus the documented default-ON companions `PS2_LOAD_BUDGET_LIFT`,
`PS2_GS_DRAWIR`, `PS2_GIF_FAST`, `PS2_GSK_SIMD`.

Default-ON proven-fix hatches that NORM (A2 category `proven-fix-hatch-ON`, winning
arm = ON): `PS2_BUDGET_PHASE`, `PS2_GIF_DRAIN_THREAD`, `PS2_GL_ALIAS_RECT`,
`PS2_GL_BLENDCP_LAZY`, `PS2_GL_RUNSUBMIT`, `PS2_GS_ALT16UV`, `PS2_GS_RTKEEP`,
`PS2_GS_TERM_NATIVE`, `PS2_GS_TYPED_BULK`, `PS2_GS_WRAPBLEND`, `PS2_INTERP_COHERENT`,
`PS2_INTERP_PHASEFIX`, `PS2_LIBC_HLE`, `PS2_MTVU_DYNDEPTH`, `PS2_MTVU_VBSYNC`,
`PS2_SND_EAGER_HOLD`, `PS2_SND_PROCBATCH`, `PS2_T0_SUBTICK`, `PS2_VU_RUNRETIRE`, and
the S9/shadow hatch family (`PS2_S9_ABIT16`, `PS2_S9_ZSRC`, `PS2_S9_CLUTRAW`,
`PS2_SNDREG`). Exceptions inside that A2 category: `PS2_FMV_NATIVE`, `PS2_VU_AOT`,
`PS2_VU_AOT_SPANFOLD`, `PS2_VU_CLAMPELIDE` → **DELETE** (their subjects — the FMV
native arm and the whole AOT tier — retire under NOAOT/MIGRATION §1.1).

Default-ON-in-code fixes where the shards corrected A2's stale rows to NORM (code
evidence over CSV): `PS2_GS_DEPOSIT_EEAPPLY` (BUG-010 fix), `PS2_S8_INRT`,
`PS2_WAITFIX` (shipped behavioral master gate found living in a census header),
`PS2_GL_BIND_CACHE`, `PS2_GL_STATE_CACHE`, `PS2_R199_XFERLATCH`,
`PS2_R199_UPLOADEAGER`, `PS2_GS_SNAPSHOT_FAST`, `PS2_XFER_FAST`, `PS2_R198_D1FMV`,
`PS2_SH5_ALIASEPOCH`, `PS2_GS_RESIDENT`, `PS2_GL_RT_NEVERSTALE`, `PS2_GL_RT_READVIEW`,
`PS2_GL_UVNATIVE`, `PS2_GS_MIP_ALLCLASS`, `PS2_CRASHRING` (the ring is the always-on
core/triage flight recorder). Also NORM'd as engine constants: the `PS2_LOAD_TURBO_*`
and `PS2_LOAD_BUDGET_LIFT_*` and `PS2_SLOWSPOT_{MS,GF_MS,COOLDOWN_S}` tuning sub-keys
(governor/threshold constants with documented basis, TRIAGE §3), `PS2_BUDGET_STEP/
BASE/LOWDRAW/BASE_LOW` (→ TitleProfile.quirks.budget data), and build options
`PS2_SHARDED_RECOMP`, `PS2_RECOMP_DIRECT_CALLS`, `PS2_VU_O3` (always on).

¹ `PS2_MTVU` is recorded NORM per the peak-config rule, with the noted ARCH-R6
tension: the deterministic profile keeps a synchronous arm as `Config engine.mtvu=false`
— i.e. NORM-with-a-Config-off-switch, the one CANON var that retains a field.

## SETTINGS — the surviving Config schema rows (proposed defaults)

Field names below are the shard-proposed ones; where two shards proposed different
namespaces for one field the consolidator picked one (noted). Defaults = today's peak
(run_play.sh) values.

| Var (dies) | Config field | Default | Note |
|---|---|---|---|
| PS2_GS_SCALE | `video.scale` | **1** | scale=1 is the byte-identity correctness contract and, since track 1d (2026-10-11), the play default; 2..4 remain the option (`--set video.scale=2`) |
| PS2_GS_INTERP | `video.interp` | **on** (1 frame) | shard3 proposed `video.interp_frames`; pick `video.interp` bool + frames=1 constant |
| PS2_FPS_CAP | `video.fps_cap` | **60** | 0 = uncapped (shard5 said `display.`; pick `video.`) |
| PS2_NOVSYNC | `video.vsync` | **off** | cascade broken into explicit `time.mode` (A2 finding 3) |
| RENDERER_BACKEND (CMake) | `video.backend` / build option | gl | Vulkan branch deletes Phase 1, rebuilt later (ARCH §6) |
| PS2_VU_MODE | `engine.vu1_mode` | `cached` | HLE→cached ladder is NORM; cached = capture/fallback tier (shard1 said `vu.mode`; pick `engine.`) |
| PS2_MTVU (see ¹) | `engine.mtvu` | **on** | deterministic profile sets false (ARCH R6) |
| PS2_MTVU_MAX_FRAMES | `engine.mtvu_depth` | **2** | S29-LAT evidence |
| PS2_EE_RUNAHEAD | `engine.ee_runahead` | **2** | ARCH names it a surviving knob |
| PS2_SHUTDOWN_WAIT | `engine.shutdown_wait_s` | 10 | 0/legacy infinite-join arm deletes |
| PS2_CDVD_INSTANT | `cdvd.latency_model` | `instant` | model survives as title-quirk-parameterized mechanism (shard0 said `loads.instant_cdvd`; pick `cdvd.`) |
| PS2_DVD_LATENCY_SCALE | `cdvd.latency_scale` | 1.0 | freerun inversion becomes explicit time.mode validation |
| PS2_MC_LATENCY | `cdvd.mc_latency_polls` | 4 | documented divergence (real MCSERV is multi-frame) |
| PS2_LOAD_TURBO | `cdvd.load_turbo` | **false** | s71 user directive; sub-knobs NORM'd as governor constants (see reconciliation) |
| PS2_SOUND | `audio.enabled` | true | |
| PS2_SDR_BLOCK_MS | `audio.sdr_block_ms` | 8 | 4E″ it.2: the virtual SPU2 block duration, paced on the mixer frame clock (48 frames/ms), not the wall clock |
| (none) | `audio.interpolation` | `linear` | the SPU2 voice resampler: `linear` (2-tap; the audio lane's hashes/fixtures) or `gaussian` (the hardware's 4-tap gauss table, PCSX2's; opt-in -- changes the audio-lane bytes; the deterministic profile pins `linear`) |
| PS2_GUI_OPEN | `ui.gui_open` | dev default on | shard0 said `dev.gui_open`; pick `ui.` |
| PS2_SAVE_SLOT | `save.slot` | 0 | GUI savestate browser is the richer chooser |
| PS2_LOG | `dev.log_level` | quiet | compile-time categories do the heavy lifting |
| PS2_LOG_CHANNELS | `log.channels` | today's set | user-facing ring filter (ARCH §4.8 `dev.log_categories[]` is the same field) |
| PS2_LOG_RING_EVENTS | `log.ring_events` | today's default | triage rings are all-builds product capture |
| PS2_PERF | `diag.perf` | off | one startup config dump replaces the 21-site announce idiom; always-on share feeds core/triage PerfMonitor |
| PS2_SLOWSPOT | `triage.enabled` (master) | **on** | capture always-bundled per TRIAGE §3; thresholds are constants |
| PS2_BUGREPORT | `triage.enabled` | on | pre-D9 var family folds into triage.* |
| PS2_BUGREPORT_SLOWSPOT | `triage.perf_capture` | on (GUI-writable) | |
| PS2_BUGREPORT_SLOWSPOT_MAX | `triage.perf_max_per_run` | today's cap | TRIAGE §3 |
| PS2_WARP_TO | `harness.warp_to_frame` | 0 (off) | harness/verify section of the schema (TREE-GAP note, shard 5) |
| PS2_TICK_SYNC | `time.tick_sync` | **false** (play) / true (deterministic profile) | EE-synchronous ticks + lockstep present (ARCH §3 boundary sequence); requires `engine.mtvu=false`; round-1B decides the play default from the §11.8 fps pairs [TICKSYNC 2026-09-25] |
| PS2_RECOMP_SHARD_COUNT (CMake) | build parameter | today's count | shard size watched in CI |
| GAME_FILES_DIR (CMake) | build/TitleProfile.recomp input path | — | per-title module config |

(28 rows listed; `RENDERER_BACKEND`, `PS2_RECOMP_SHARD_COUNT`, `GAME_FILES_DIR` are
build-time settings rather than runtime Config fields — 24 runtime fields + 3 build
settings + the `engine.mtvu` field carried by a NORM var. The master table's SETTING
count (27) excludes `engine.mtvu` since PS2_MTVU is bucketed NORM.)

## Reconciliations — vars where shards disagreed (both positions stated, decided)

1. **PS2_CRASHRING** — shard2: DELETE (alias); shard5: NORM (ring = always-on triage
   flight recorder); shard8: DIAG (generated-code consult). **Decision: NORM** — the
   crash ring is unconditional product capture (TRIAGE/D9); the env alias and the
   generated consult both re-home to core/triage-owned hooks; var deletes.
2. **PS2_GL_RESMEMO** — graphics: NORM (winner=OFF ⇒ memo arm deleted); shard4:
   DELETE. Substantively identical. **Decision: DELETE** (var + memo arm go; the
   no-memo baseline is the norm; equivalence test retires with it).
3. **PS2_LOG** — shard0: SETTING `dev.log_level`; shard8 (run_play row): DIAG.
   **Decision: SETTING** — A2 disposition is keep-as-setting; the =1 heavyweight-trace
   arm still dies into compile-time categories.
4. **PS2_LOG_RING_EVENTS** — shard2: NORM (constant); shard5: SETTING.
   **Decision: SETTING** `log.ring_events` (shard5 read the owning latch in ring_log.cpp).
5. **PS2_LOG_VERBOSE** — shard2: SETTING; shard5: DIAG. **Decision: DIAG** — matches
   A2 move-to-diagnostics-build; verbose tier is a diagnostics-build capability (it
   gates crashring INDIRECT_JUMP tiering — that rate rule moves into the ring schema).
6. **PS2_PAD_SCRIPT** — shard7: TEST-ONLY; shard4: DIAG. **Decision: TEST** — the
   script grammar survives as the verify/triage InputLog replay channel; the live
   probe deletes ("Remove after investigation" is in-source).
7. **PS2_PERF** — 14 rows: SETTING ×7, DIAG ×5, site-DELETE ×2. **Decision: SETTING**
   `diag.perf` (A2 keep-as-setting): one startup config dump + always-compiled
   PerfMonitor share; every per-site announce latch/mirror deletes.
8. **PS2_SAVE_SLOT** — shard0: NORM (slot 0 only); shard5: SETTING. **Decision:
   SETTING** `save.slot` default 0 — cheap, real user knob; A2 row notwithstanding,
   the GUI browser complements rather than replaces it.
9. **PS2_SH5_ALIASEPOCH** — graphics: DELETE (per A2 delete-both); shard3: NORM
   (code default ON — proven fix; A2 row stale). **Decision: NORM**; A2 correction
   recorded below.
10. **PS2_U3_VIEWPORT** — shard0: DIAG; shard3: DELETE. **Decision: DELETE** per A2
    (concluded U3 experiment); residual visibility folds into the view_state dump.
11. **PS2_LOAD_TURBO** — shard4 (owner file turbo.h): SETTING `cdvd.load_turbo`
    default false (s71 user directive), sub-keys NORM'd as governor constants;
    shard8 (run_play doc note): DELETE both arms (LOADTIME §16 "no-op on wall time").
    **Decision: SETTING (default false)** — the owner-file read + explicit user
    directive outrank a launcher doc comment; if Phase-4 confirms LOADTIME §16 on the
    differential run, demote to DELETE in that wave (recorded on the purge checklist).
    **Engaged form (P3 3D2 reconciliation of `PS2_LOAD_TURBO_K`):** OLD `turbo.h
    AutoK()` default 0 = FREERUN-engaged (consumption-driven ticks); finite K was
    kept "for testing/diagnosis" only. NORM → the winning arm is FREERUN:
    `TurboGovernor` engaged ⇒ `TickSource::Mode::Freerun` (K = 0, a bypass);
    `PS2_LOAD_TURBO_K` has no Config field. Bypass rule alongside (OLD
    `ps2_vblank.cpp BypassNow`): freerun ‖ warp ‖ load-turbo-freerun — finite-K
    turbo is NOT a bypass (`TickSource::Bypass()` is Freerun/Warp only).
12. **PS2_VBLANK_FREERUN** — one shard-0 row's outcome cell was prose ("env
    inheritance chain dies"); all substantive rows agree: **NORM** (real-time ticks),
    freerun preserved as the bench profile's `time.mode` value.
13. **PS2_SLOWSPOT** — shard0 rows split SETTING (master) / NORM (thresholds);
    run_play row: "always-on product capture". **Decision: SETTING** = the
    `triage.enabled` master (default on); thresholds NORM as constants — consistent
    with all three statements.

## A2 registry vars with no shard row of their own (coverage note)

17 of A2's 496 vars appear in no shard flag table under their own name. 15 are family
sub-keys whose parent WAS mapped — they inherit the parent's outcome (all resolve, no
orphans): `PS2_CRYSENV_DUMPDIR` (→PS2_CRYSENV_DIAG, DELETE), `PS2_INTERP_DUMP_DIR`
(→PS2_INTERP_DUMP, DIAG), `PS2_S106_DIR` (→PS2_S106_STAGE, DELETE),
`PS2_S13_TBLCAP_{ENTRY,N}`, `PS2_S15_HDRCAP_{ENTRY,MINVF,N}`,
`PS2_S8_MTXCAP_{EERAM,MINVF,N}` (→ their vu.cpp probe parents, DELETE),
`PS2_S8_PKTDUMP_MINVF` (→PS2_S8_PKTDUMP, DELETE), `PS2_TEXBLKDUMP_DIR`
(→PS2_TEXBLKDUMP, DIAG), `PS2_VU0_VISCAP_{MINVF,N}` (→PS2_VU0_VISCAP, DELETE).
The other 2 — `LOGO_MEMSNAP`, `LOGO_MEMSNAP_FRAMES` — WERE mapped by shard 3
(manual_switch_dispatchers.cpp rows → DELETE with the retired file); they were only
missed by the first-pass name grep (non-PS2_ prefix) and are in the master table.

## Vars the shards found that are MISSING from the A2 registry (corrections to A2)

Runtime env vars (add to A2 §1): `PS2_VU0_MODE` / `PS2_VU1_MODE` (vu.cpp:726 per-unit
tier override — DELETE), `PS2_RECOMP_CORPUS` (corpus recorder — TEST),
`PS2_STRMQ` (iop_recompiler probe injection — DELETE), `PS2_SHADOW_DIAG`,
`PS2_EE_RAWDISP` (generator-emitted gates — DIAG via GenProbe manifest),
`PS2_HAVE_BACKTRACE` (build define consumed by generated FATAL paths — DIAG/platform
capability), `PS2_GL_TEXCACHE` (texture_decoder.h family row — NORM),
`PS2_LOAD_TURBO_{K,ENGAGE_MS,GAP_MS,RELEASE_MS,INPUT_MS}` (EnvU32 non-literal sites —
NORM constants), `PS2_LOAD_BUDGET_LIFT_{ENGAGE_MS,GAP_MS,INPUT_MS,RELEASE_MS}` (NORM
constants), `PS2_SLOWSPOT_{MS,GF_MS,COOLDOWN_S}` (NORM constants), plus the
`PS2_GS_CLASSRDR_*` per-class severability tokens and ~23 graphics-shard family
sub-gates A2's finding-4 table lists but the CSV lacks as first-class rows
(FTT_FORCE_EVICT/FORCE_DECLINE → TEST; CLASSRDR_* → NORM; S32_FORCESTALL, GL_NO_FLUSH
→ per graphics shard table). CMake options (new ledger section, not A2 scope):
`PS2_SHARDED_RECOMP`, `PS2_RECOMP_DIRECT_CALLS`, `PS2_VU_O3` (NORM),
`PS2_RECOMP_SHARD_COUNT`, `RENDERER_BACKEND`, `GAME_FILES_DIR` (SETTING),
`RECOMP_ENGINE` + AOT options (DELETE). New: `PS2R_DIAGNOSTICS` (the one build gate
the rewrite introduces, ARCH §9).

Ledger-review additions (second-pass `getenv` grep of `tests/`, per the scope rule
above — all TEST, add to A2 §1): `PS2_REPLAY_{SCANOUT,CONTENT,DUMPPPM,DUMPPPM_FRAMES,LOG}`
(tests/gs_replay.cpp:555/:564/:711/:717/:1021), `PS2_RESMEMO_TEST_{U9,T12,VSUV}`
(tests/test_resmemo_equivalence.cpp:593/:594/:600), `EEPACE_SOAK_SEED`
(tests/test_ee_pace.cpp:477), `PAGEINV_DUMP` (tests/test_page_invariance.cpp:387),
`VIF_REPLAY_{RGBDUMP,DUMPW,MTVU_DEBUG,LOG,TTE2}` (tests/vif_replay.cpp:183/:266/
:1072/:1816/:1880). Plus a first-class master-table row for `PS2_VU1_MODE`
(vu.cpp:726 reads it alongside `PS2_VU0_MODE`; previously only the prose pair above
covered it — DELETE).

A2 disposition corrections found by code reading (CSV row stale vs code default):
`PS2_GS_DEPOSIT_EEAPPLY`, `PS2_EE_TIM0`, `PS2_S8_INRT`, `PS2_SH5_ALIASEPOCH`,
`PS2_R199_XFERLATCH`, `PS2_R199_UPLOADEAGER`, `PS2_GS_SNAPSHOT_FAST`, `PS2_XFER_FAST`,
`PS2_R198_D1FMV`, `PS2_GL_BIND_CACHE`, `PS2_GL_STATE_CACHE` → all default-ON in code
(NORM), vs A2 delete-both/keep-as-setting rows; `PS2_VU1_CYCLE_CAP` → TEST (offline
oracle raises it); `PS2_MIPCEN` → DIAG (census, not tuning); `PS2_VU_RUNFLAT_CENSUS` →
DELETE (subject deletes); `PS2_LOADSPAN` kept DIAG per MIGRATION over A2 delete-both
(purge-loop check recorded); `PS2_GS_SNAP_FOOTPRINT` behavioral-not-diagnostic;
`PS2_MID_TEMPLATE_FORCEMISS` / `PS2_GS_RUNDRAW_CHECK` are test hooks (TEST).

---

## Master table

Columns: var | default today (A2) | A2 category | A2 disposition | refactor outcome |
sites (files reading it, from A2 §1; for non-A2 vars, the shard-doc source). Sub-key
rows inheriting a parent's outcome are marked `INHERIT(parent)`. Generated from the
shard flag tables joined against the A2 CSV; reconciliation decisions above are baked in.

| var | default | A2 category | A2 disposition | outcome | sites |
|---|---|---|---|---|---|
| CT_FULL_LOG | OFF-unless-1 | diagnostic | move-to-diagnostics-build | DELETE | host_app/main.cpp:4943 |
| EEPACE_SOAK_SEED | — | NOT-IN-A2 | — | TEST | tests/test_ee_pace.cpp:477 |
| GAME_FILES_DIR | — | NOT-IN-A2 | — | SETTING | CMakeLists.txt |
| LOGO_MEMSNAP | OFF-unless-nonzero | diagnostic | move-to-diagnostics-build | DELETE | host_app/manual_switch_dispatchers.cpp:778 |
| LOGO_MEMSNAP_FRAMES | num-if-set | diagnostic | move-to-diagnostics-build | DELETE | host_app/manual_switch_dispatchers.cpp:783 |
| PAGEINV_DUMP | — | NOT-IN-A2 | — | TEST | tests/test_page_invariance.cpp:387 |
| PS2R_DIAGNOSTICS | — | NOT-IN-A2 | — | NEW | CMakeLists.txt |
| PS2_ARGV | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/main.cpp:561;host_app/ps2_threading.cpp:825 |
| PS2_ARTDET | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/artdet_flicker.cpp:255;host_app/graphics_thread.cpp:2547 |
| PS2_ARTDET_DIAG | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:8452;host_app/graphics_thread.cpp:2654 |
| PS2_ARTDET_EVERY | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:2551 |
| PS2_ARTDET_FLICKER | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/artdet_flicker.cpp:257 |
| PS2_ARTDET_GREEN | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:2556 |
| PS2_ARTDET_MIN | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:2564 |
| PS2_ARTDET_TEST | numeric-other | test-hook | move-to-test-only | TEST | host_app/artdet_flicker.cpp:43 |
| PS2_ARTDET_WEIRD | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:2560 |
| PS2_B57Q | OFF-unless-1 | experiment | delete-both | DELETE | host_app/main.cpp:10474 |
| PS2_BAKE_DIAG | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/dmac.cpp:1201;host_app/gs.cpp:510;host_app/gs_state.cpp:2867;host_app/gs_state.cpp:4178;host_app/gs_state.cpp:4331;host_app/gs_s… |
| PS2_BAKE_DUMP | presence-gated | diagnostic | move-to-diagnostics-build | DIAG | host_app/gs_state.cpp:4356 |
| PS2_BAKE_SRCSCAN | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/gs_state.cpp:4376 |
| PS2_BB_SNAP | unresolved | tuning-knob | delete-losing-arm-keep-winner | TEST | host_app/graphics_thread.cpp:3600 |
| PS2_BB_WINDOWS | unresolved | tuning-knob | delete-losing-arm-keep-winner | TEST | host_app/graphics_thread.cpp:3576 |
| PS2_BOOT_WATCH | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:5074 |
| PS2_BTRACE | unresolved | diagnostic | move-to-diagnostics-build | DELETE | host_app/bullseye_trace.cpp:59 |
| PS2_BUDGET_BASE | num=-1 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/main.cpp:3449 |
| PS2_BUDGET_BASE_LOW | num=-1 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/main.cpp:3490 |
| PS2_BUDGET_DIAG | num=0 | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:3315 |
| PS2_BUDGET_EMU | num=0 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/main.cpp:3308 |
| PS2_BUDGET_LOWDRAW | num=0 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/main.cpp:3484 |
| PS2_BUDGET_PHASE | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/main.cpp:3479 |
| PS2_BUDGET_STEP | num=0 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/main.cpp:3424 |
| PS2_BUGREPORT | unresolved | diagnostic | move-to-diagnostics-build | SETTING | host_app/bug_report.cpp:75 |
| PS2_BUGREPORT_SLOWSPOT | unresolved | diagnostic | move-to-diagnostics-build | SETTING | host_app/bug_report.cpp:94 |
| PS2_BUGREPORT_SLOWSPOT_MAX | numeric-other | diagnostic | move-to-diagnostics-build | SETTING | host_app/bug_report.cpp:83 |
| PS2_BUGREPORT_TEST_MANUAL | numeric-other | test-hook | move-to-test-only | TEST | host_app/gui/bug_report_section.cpp:80 |
| PS2_BUGREPORT_TEST_MANUAL_STATE | unresolved | test-hook | move-to-test-only | TEST | host_app/gui/bug_report_section.cpp:92 |
| PS2_CALL_CENSUS | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/call_census.cpp:232 |
| PS2_CALL_CENSUS_ROOT | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/call_census.cpp:235 |
| PS2_CALL_CENSUS_SLOWSEG | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/call_census.cpp:242 |
| PS2_CAP_STEPDOWN | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/present_pace.h:671 |
| PS2_CAP_TRIPFIT | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/present_pace.h:696 |
| PS2_CAP_TRIPFIT_PCT | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/present_pace.h:707 |
| PS2_CAP_UPHOLD_MS | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/present_pace.h:722 |
| PS2_CDVD_BUDGET_KB | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/ps2_vblank.h:163 |
| PS2_CDVD_INSTANT | presence-gated | shipping-setting | keep-as-setting | SETTING | host_app/ps2_vblank.cpp:469 |
| PS2_CD_LAT_US | presence-gated | experiment | delete-both | NORM | host_app/iop_stubs.cpp:1804 |
| PS2_CHUNKQ | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/hle_heap.cpp:1663 |
| PS2_CHUNK_APPLY_DORMANT | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/hle_heap.cpp:1694 |
| PS2_CHUNK_APPLY_ORDER | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/hle_heap.cpp:2069 |
| PS2_CHUNK_APPLY_SCOPED | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/hle_heap.cpp:1644 |
| PS2_CHUNK_APPLY_VALVE_TICKS | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/hle_heap.cpp:1709 |
| PS2_CLEAR_DIAG | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:9060 |
| PS2_CRASHRING | unresolved | diagnostic | move-to-diagnostics-build | NORM | host_app/ring_log.cpp:104 |
| PS2_CRYSENV_DIAG | unresolved | diagnostic | move-to-diagnostics-build | DELETE | host_app/graphics/opengl/opengl_backend.cpp:10424 |
| PS2_CRYSENV_DUMPDIR | unresolved | diagnostic | move-to-diagnostics-build | DELETE INHERIT(PS2_CRYSENV_DIAG) | host_app/graphics/opengl/opengl_backend.cpp:10576 |
| PS2_CUELAG | OFF-unless-nonzero | experiment | delete-both | DELETE | host_app/cuelag_probe.h:87 |
| PS2_CUELAG_WIN_TICKS | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/cuelag_probe.h:98 |
| PS2_DRAWLIST_RECORD | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/hle/drawlist_record.cpp:96 |
| PS2_DRAWLIST_RECORD_FRAMES | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/hle/drawlist_record.cpp:101 |
| PS2_DRAWLIST_RECORD_PIXELS | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/hle/drawlist_record.cpp:99 |
| PS2_DRAWLIST_RECORD_PPM | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/hle/drawlist_record.cpp:100 |
| PS2_DRAWLIST_RECORD_VERTS | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/hle/drawlist_record.cpp:98 |
| PS2_DRAW_DIAG | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:8439 |
| PS2_DRAW_SKIP | presence-gated | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:8413 |
| PS2_DVD_LATENCY_SCALE | presence-gated | shipping-setting | keep-as-setting | SETTING | host_app/hle_heap.cpp:2765 |
| PS2_EDGEPROBE | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:1204 |
| PS2_EEFEED | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/w10_eefeed.h:50 — ledgered P3, CONSUMED in Phase 5 (PHASE5_IMPL 5F/5G, checklist (h)4): B1 (dmac.cpp staged submit) → NORM, the staged Batch is the only DMAC→MTVU-worker feed shape (`ChainWalker::SetVif1StagedFeed` ← `DmacMtvuFeed`); B2 (gif.cpp:4147 `w10eefeed::On()`) → NORM, the rowable row loop is unconditional [IDOC-A 2026-10-01] |
| PS2_EESTALL_CENSUS | OFF-unless-nonzero | diagnostic | move-to-diagnostics-build | DIAG | host_app/timeline.h:133 |
| PS2_EE_PACE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/ee_pace.h:169 |
| PS2_EE_RAWDISP | — | NOT-IN-A2 | — | DIAG | recompiler_tool/recompiler.cpp |
| PS2_EE_RUNAHEAD | unresolved | tuning-knob | delete-losing-arm-keep-winner | SETTING | host_app/ee_pace.h:118 |
| PS2_EE_TIM0 | unresolved | shipping-setting | keep-as-setting | NORM | host_app/ee_timers.h:46 |
| PS2_EULERTRACE | unresolved | shipping-setting | keep-as-setting | DIAG | host_app/main.cpp:1050 |
| PS2_FASTMEM | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/memory.h:146 |
| PS2_FMV3_BLIT | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/graphics_thread.cpp:1366 |
| PS2_FMV3_COMPGATE | OFF-unless-nonzero | experiment | delete-both | DELETE | host_app/graphics_thread.cpp:1486 |
| PS2_FMV5_SCAN | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:18558 |
| PS2_FMV5_SRC | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:10614 |
| PS2_FMV_COMPFIX | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics_thread.cpp:1467 |
| PS2_FMV_DECODE_CAP | presence-gated | experiment | delete-both | NORM | host_app/mpeg_hle.cpp:1197 — reclassified DELETE → NORM (winning arm = ON, default 64) per PHASE7_IMPL checklist item 9 / PHASE7_PLAN §5.5 P7-LEDGER-CAP: the bounded decode-ahead + `pending_es` deferral is the baseline path (`cap == 0` eager arm is the loser, not written); NEW constant `IpuSettings::decode_ahead_pictures = 64` (`engine/ipu/ipu_settings.h`); no Config SETTING [IDOC-A 2026-10-01] |
| PS2_FMV_NATIVE | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | DELETE | host_app/anim_pose_hle.cpp:200 |
| PS2_FMV_OPEN_BURST | presence-gated | experiment | delete-both | NORM | host_app/mpeg_hle.cpp:532 — reclassified DELETE → NORM (winning arm = ON / burst) per PHASE7_IMPL checklist item 9 / PHASE7_PLAN §5.5 P7-LEDGER-BURST: `NextBurst` is the runtime path of every baseline leg; the debt-clamp `Next` (mpeg_hle.h:68-88 wall, :158-178 tick) is the losing arm; NEW `FmvPacer::Next` = `TickPace::NextBurst` semantics as the SOLE core (`engine/ipu/hle/fmv_pacer.cpp`), `test_fmv_pacer` pins no-floor [IDOC-A 2026-10-01] |
| PS2_FOG_DIAG | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:15470 |
| PS2_FOG_SKIP | num=0 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:8652 |
| PS2_FPS_CAP | unresolved | shipping-setting | keep-as-setting | SETTING | host_app/present_pace.h:629 |
| PS2_FRAME_LOCKSTEP | OFF-unless-1 | experiment-hatch | keep-as-setting | TEST | host_app/gs_hle.cpp + ee_timing/frame_rendezvous.h — [P0-LOCKSTEP] lockstep present rendezvous WITHOUT synchronous ticks (attribution leg for BUG-033 item (1) alone). The first form (R170 counter pair rebased at the first boundary) WEDGED because the --load-state restore inside that boundary rewrote the pair; REPLACED 2026-09-25 by the process-local ticket barrier (Issue on push → Ack at both gfx VSync-case exits → EE WaitFor, no wall valve). Implied by PS2_TICK_SYNC; refused when PS2_MTVU is on. Verify-harness use only [TICKSYNC 2026-09-25] |
| PS2_TICK_SYNC | OFF-unless-1 | verify-profile-pin | keep-as-setting | SETTING | host_app/ee_timing/tick_sync.h (+ ps2_vblank.cpp, gs_hle.cpp, sif_hle.cpp, graphics_thread.cpp, mpeg_hle.cpp) — deterministic-profile pin (BUG-033 items (1)+(3)): NO vblank ticker thread; exactly ONE tick body per EE frame boundary fired in-line by the boundary thread (sema 1/3 + INTC VBON/TIM2/VBOF direct, GS_CSR.VSINT), cdvd-delivery and FMV-demand ticks in-line at their EE-thread trigger sites, off-thread demands deferred to the next boundary; the graphics thread raises no INTC; PLUS the lockstep present rendezvous replacing the MAX_FRAME_LAG park + 100 ms wall valve; fixed boundary order in ee_timing/boundary_sequencer.h. Requires PS2_MTVU=0 (refused loudly otherwise). Result: 24/24 non-defect battery legs ADMISSIBLE-STABLE. Play default OFF: ~10-20 % lower fps (no EE/gfx overlap). **Round-1B decision: stays PROFILE-ONLY (play default OFF confirmed)**; Config field proposal `time.tick_sync` [TICKSYNC 2026-09-25][R1B 2026-09-25] |
| PS2_EE_RUNTOKEN | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/ee_sched/ee_run_token.h — [P0-EETOK] EE guest-thread run token (BUG-033 item (2) / BUG-034): one guest thread executes at a time, token changes hands only at kernel scheduling points (thbase 1.02 election); foreign host threads route wakes through EeEventQueue (BUG-035 holder-only drain). 2026-09-25: savestate relaunch handoff BeginRelaunch/EndRelaunch + entering-thread-only GuestExit (BUG-036). =0 = legacy free-running pthread hosting. **Round 1B: play-neutrality PROVEN** (user env ON vs =0: fps p50 within rep spread on build/ and build-v2/, CUELAG slopes equal, 0 underruns, identical pad frame latency, load frames within spread, FMV ok, coldboot gates PASS — PHASE0_IMPL §12.1); one open observation on the post-FMV cold-boot title load (§12.6 = **BUG-038**, OPEN: cold boot never reaches the title under the default-ON token; `=0` reaches it at vf 2629). The =0 arm is now the deletable losing arm once BUG-038 is closed [R1B 2026-09-25]. Deterministic profile pins `=1` explicitly since 2026-09-26 (baselines' env_hash re-stamped, streams unchanged — `verify/baseline/README_FREEZE.md`) |
| PS2_EESCHED_DIAG | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/ee_sched/ee_run_token.cpp — [EESCHED] paired-run interleave stream (ready/enter/run/block/preempt/rotate/exit/sig/enq/drain lines, vb-stamped); 2026-09-25: emitted via eesched::DiagPrintf (one write(2) per line, splice-proof), `enter` logged after the acquire [P0-EETOK 2026-09-25] |
| PS2_FTT_CENSUS | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/rt_neverstale.h:213 |
| PS2_GFXLOG | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics_thread.cpp:2076 |
| PS2_GFXLOOP_ARM | presence-gated | experiment | delete-both | DELETE | host_app/graphics_thread.cpp:382 |
| PS2_GFXLOOP_CENSUS | OFF-unless-nonzero | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics_thread.cpp:365;host_app/graphics_thread.cpp:372 |
| PS2_GFX_FBPCENSUS | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics_thread.cpp:4183 |
| PS2_GFX_SLOWJOB | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:496 |
| PS2_GIF_DRAIN_THREAD | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/gif_arbiter.h:48 |
| PS2_GIF_FAST | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gif.cpp:92;host_app/graphics_thread.cpp:2035 |
| PS2_GL_ALIAS_ALPHAQUAD | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1490 |
| PS2_GL_ALIAS_DEPCOPY | presence-gated | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:671 |
| PS2_GL_ALIAS_DEPCOPY2 | presence-gated | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:677;host_app/graphics/opengl/opengl_backend.cpp:703 |
| PS2_GL_ALIAS_DIRTYRECT | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/alias_rect.h:58 |
| PS2_GL_ALIAS_GPUROUTE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:652 |
| PS2_GL_ALIAS_RECT | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/alias_rect.h:32 |
| PS2_GL_BBOX_COPY | presence-gated | experiment | delete-both | DELETE | host_app/render.h:2487 |
| PS2_GL_BIND_CACHE | presence-gated | experiment | delete-both | NORM | host_app/graphics/opengl/gl_state_cache.h:82 |
| PS2_GL_BLENDCP_LAZY | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/rt_copy_tracker.h:40 |
| PS2_GL_BLEND_SUB | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/blend_ff_map.h:182 |
| PS2_GL_BSTATE_FAST | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1463 |
| PS2_GL_CDTILES | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/rt_copy_tracker.h:103 |
| PS2_GL_CPBO_REISSUE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:804 |
| PS2_GL_DUMPS | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:1188 |
| PS2_GL_FAST | presence-gated | shipping-setting | keep-as-setting | NORM | host_app/render.h:449;host_app/render.h:503;host_app/render.h:2489;host_app/texture_decoder.h:28 |
| PS2_GL_FAST2 | presence-gated | shipping-setting | keep-as-setting | NORM | host_app/graphics/opengl/gl_state_cache.h:84;host_app/render.h:753 |
| PS2_GL_FOGLANE_LIVE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/foglane_live.h:64 |
| PS2_GL_FOGNATIVE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:2412 |
| PS2_GL_FRAMEFLUSH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1536 |
| PS2_GL_G7C_FLUSH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:730 |
| PS2_GL_G7C_PBOUP | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:737 |
| PS2_GL_IDX16_LANE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/idx16_lane.h:78 |
| PS2_GL_IDXEPOCH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/idx_epoch_guard.h:60 |
| PS2_GL_IDXSCALE | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:2383 |
| PS2_GL_IDXTEX | presence-gated | experiment | delete-both | DELETE | host_app/render.h:1188 |
| PS2_GL_IDXTEX_16 | presence-gated | experiment | delete-both | DELETE | host_app/render.h:1245 |
| PS2_GL_IDXTEX_32 | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1212 |
| PS2_GL_IDXTEX_CLUTSNAP | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1307 |
| PS2_GL_IDXTEX_FRESH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1287 |
| PS2_GL_L2_MB | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:428 |
| PS2_GL_L2_PROB | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1544 |
| PS2_GL_L2_SU | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1569 |
| PS2_GL_L2_SU_MB | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1578 |
| PS2_GL_L2_VERIFY | numeric-other | tuning-knob | delete-losing-arm-keep-winner | TEST | host_app/graphics/opengl/opengl_backend.cpp:22564 |
| PS2_GL_MARKMEMO | numeric-other | diagnostic | move-to-diagnostics-build | NORM | host_app/graphics/opengl/opengl_backend.cpp:837 |
| PS2_GL_MARKMEMO_CHECK | numeric-other | diagnostic | move-to-diagnostics-build | TEST | host_app/graphics/opengl/opengl_backend.cpp:849 |
| PS2_GL_PAR_ENCODE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:781 |
| PS2_GL_RESID_DIAG | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:484;host_app/render.h:2194 |
| PS2_GL_RESMEMO | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/graphics/opengl/gl_state_cache.h:809;host_app/uv_bbox.h:80 |
| PS2_GL_RESMEMO_STAMPF1 | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/uv_bbox.h:109 |
| PS2_GL_RT_NEVERSTALE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/rt_neverstale.h:110 |
| PS2_GL_RT_READVIEW | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/rt_neverstale.h:150 |
| PS2_GL_RUNSUBMIT | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/gl_state_cache.h:286 |
| PS2_GL_SCANOUT_RT | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/render.h:1649 |
| PS2_GL_SHUF_SNAP | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/shuf_snap.h:66 |
| PS2_GL_STATE_CACHE | presence-gated | experiment | delete-both | NORM | host_app/graphics/opengl/gl_state_cache.h:399 |
| PS2_GL_TEXCACHE | — | NOT-IN-A2 | — | NORM | host_app/texture_decoder.h |
| PS2_GL_TEXDEMAND | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1601 |
| PS2_GL_UPLOAD_DEPOSIT | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1876 |
| PS2_GL_UVNATIVE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:2370 |
| PS2_GL_UVREGION | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1610 |
| PS2_GL_UVREGION_V2 | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1623 |
| PS2_GL_VCONV_FAST | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:1413 |
| PS2_GL_ZTEX | presence-gated | experiment | delete-both | DELETE | host_app/render.h:1088 |
| PS2_GPUFENCE | presence-gated | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:23824 |
| PS2_GSK_SIMD | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gif.cpp:122 |
| PS2_GS_ABIT16 | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/s9_shadow.h:54 |
| PS2_GS_ALT16UV | unresolved | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/s9_shadow.h:78 |
| PS2_GS_CLASSRDR | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gs_class_renderer.h:242 |
| PS2_GS_CLASSRDR_ | — | NOT-IN-A2 | — | DELETE | host_app/gs_class_renderer.h |
| PS2_GS_CLASSRDR_WORLD_SPEC | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/world_tristrip_native.inc:147 |
| PS2_GS_CLUTRAW | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/s9_shadow.h:70 |
| PS2_GS_CT16_QUANT | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/s9_shadow.h:102 |
| PS2_GS_DEPOSIT_EEAPPLY | presence-gated | experiment | delete-both | NORM | host_app/gs_deposit_overlay.h:66 |
| PS2_GS_DRAWIR | num-if-set | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/hle/vu1_hle_runtime.cpp:122 |
| PS2_GS_DRAWIR_VERIFY | num-if-set | tuning-knob | delete-losing-arm-keep-winner | TEST | host_app/hle/vu1_hle_runtime.cpp:149 |
| PS2_GS_DRAWMERGE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:2000;host_app/render.h:2032 |
| PS2_GS_FMVRDR | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/fmv_blit_native.inc:168 |
| PS2_GS_FMVRDR_SPEC | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/fmv_blit_native.inc:179 |
| PS2_GS_FOGLANE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:14066;host_app/graphics/opengl/opengl_backend.cpp:20680 |
| PS2_GS_GLBATCH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/gs_glbatch.h:68 |
| PS2_GS_INTERP | num=0 | shipping-setting | keep-as-setting | SETTING | host_app/artdet_flicker.cpp:234;host_app/gs_interp.h:78 |
| PS2_GS_INTERP_SNAP | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gs_interp.h:93 |
| PS2_GS_MIPMAP | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gs_mipmap.h:64 |
| PS2_GS_MIP_ALLCLASS | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gs_mipmap.h:128 |
| PS2_GS_MIP_LODREF | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/gs_mipmap.h:126 |
| PS2_GS_MIP_TRILINEAR | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gs_mipmap.h:278 |
| PS2_GS_PARTRDR | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/particles_native.inc:166 |
| PS2_GS_PARTRDR_SPEC | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/particles_native.inc:177 |
| PS2_GS_RAWSKIN | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/skinned_native.inc:238 |
| PS2_GS_READER_CENSUS | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/gs_vram_census.h:120 |
| PS2_GS_RESIDENT | presence-gated | experiment | delete-both | NORM | host_app/gs_page_tracker.h:72 |
| PS2_GS_RESIDENT_PBO | presence-gated | experiment | delete-both | NORM | host_app/gs_page_tracker.h:102 |
| PS2_GS_RTKEEP | unresolved | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/s9_shadow.h:86 |
| PS2_GS_RUNDRAW | num-if-set | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:2029 |
| PS2_GS_RUNDRAW_CHECK | numeric-other | tuning-knob | delete-losing-arm-keep-winner | TEST | host_app/render.h:2054 |
| PS2_GS_S9RDR | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/s9_composite.h:96 |
| PS2_GS_S9RDR_DBGBOTH | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/s9_composite.h:145 |
| PS2_GS_S9RDR_DBGPIX | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/s9_composite_native.inc:471;host_app/graphics/opengl/s9_composite_native.inc:591;host_app/graphics/opengl/s9_com… |
| PS2_GS_S9RDR_DBGQUANT | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:8334 |
| PS2_GS_S9RDR_HOLD | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/s9_composite.h:132 |
| PS2_GS_SCALE | presence-gated | shipping-setting | keep-as-setting | SETTING | host_app/artdet_flicker.cpp:233;host_app/graphics/opengl/gs_scale.h:45;host_app/gs_interp.h:423;host_app/gs_interp.h:577 |
| PS2_GS_SKINRDR | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/skinned_native.inc:202 |
| PS2_GS_SKINRDR_SPEC | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/skinned_native.inc:224 |
| PS2_GS_SKINRDR_WORLD2 | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/skinned_native.inc:213 |
| PS2_GS_SNAPSHOT_FAST | presence-gated | experiment | delete-both | NORM | host_app/render.h:447 |
| PS2_GS_SNAP_FOOTPRINT | presence-gated | diagnostic | move-to-diagnostics-build | NORM | host_app/render.h:486 |
| PS2_GS_SPRRDR | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/sprite_hud_native.inc:183 |
| PS2_GS_SPRRDR_SPEC | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/sprite_hud_native.inc:194 |
| PS2_GS_TERM_NATIVE | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/gs_state.cpp:2367 |
| PS2_GS_TYPED_BULK | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/gs_state.cpp:2600 |
| PS2_GS_WRAPBLEND | unresolved | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/blend_ff_map.h:232 |
| PS2_GS_ZSRC | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/s9_shadow.h:62 |
| PS2_GUI_OPEN | unresolved | shipping-setting | keep-as-setting | SETTING | host_app/main.cpp:5047 |
| PS2_GUI_TEST_LOAD | unresolved | test-hook | move-to-test-only | TEST | host_app/gui/savestate_browser.cpp:245 |
| PS2_GUI_TEST_SAVE | numeric-other | test-hook | move-to-test-only | TEST | host_app/gui/savestate_browser.cpp:263 |
| PS2_FIXC_TICKCLOCK | num-if-set | tuning-knob | delete-both | DELETE | host_app/iop_stubs.cpp:2187 — PumpDeferredEvtsTickClock is a no-op under the kernel arm (:2459 "events ride the kernel queue"): deferred CD/voice completions are IopKernel events (PHASE4_PLAN 3.1 line 7); no other arm exists [4A′ 2026-09-27, PHASE4_PLAN 5.6] |
| PS2_HAVE_BACKTRACE | — | NOT-IN-A2 | — | DIAG | recompiler_tool/recompiler.cpp |
| PS2_HB_BOUNDARY | num-if-set | tuning-knob | delete-both | DELETE | host_app/iop_stubs.cpp:1379 — PumpHeartbeatAtBoundary returns at once under the kernel arm (:2485); the heartbeat is the IopKernel periodic timer (clock 7) [4A′ 2026-09-27, PHASE4_PLAN 5.6] |
| PS2_HB_TICKCLOCK | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/iop_stubs.cpp:1175 |
| PS2_HEADWATCH | unresolved | shipping-setting | keep-as-setting | DIAG | host_app/pad_input.cpp:604 |
| PS2_INJ_PROBE | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/ps2_vblank.cpp:873 |
| PS2_INLAT_ANALOG | presence-gated | diagnostic | move-to-diagnostics-build | DIAG | host_app/sce_hle/libpad.cpp:416 |
| PS2_INPUT | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/input/input_system.cpp:214 |
| PS2_INPUT_LAT | unresolved | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/input/input_latency.h:18 |
| PS2_IOP_DISPATCH | string realtime/frame | shipping-setting | keep-as-setting | SETTING | host_app/iop_kernel/iop_module_table.cpp:19 — Config field iop.dispatch, default realtime (play; OpenGOAL IopHostThread + steady_clock); the deterministic profile sets frame (EE-boundary dispatch, frame-derived clock) [IOPK 2026-09-24] |
| PS2_IOPK_WATCH | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/iop_kernel/iop_module_table.cpp:65 — 5 s kernel-state watchdog on stderr (thread states/wait kinds + unparked census) → the `watch` line of the iopsched category (IopschedTrace::StartWatch over IopModuleTable::FormatWatchLine); the REALTIME stall discriminator for BUG-026 [4A′ 2026-09-27, PHASE4_PLAN 5.6] |
| PS2_IOP_KERNEL | **OFF-unless-1** (play) / 1 in the deterministic profile | fix-hatch-default-OFF (realtime boot stall open) | keep-until-BUG-026-closed-then-delete-losing-arm | NORM | host_app/iop_kernel/iop_module_table.cpp:11 — cooperative IOP kernel (OpenGOAL shape); =1 opts in; unset/=0 = the per-guest-thread std::thread hosting verbatim (the shipped play path); losing arm (once closed) = the legacy thread bodies/waits in iop_stubs.cpp + the S43 dispatch-bracket impersonation. Reconciled 2026-09-25 round 1B: `Enabled()` has been default OFF since 2026-09-24 (the header comment said ON — fixed); realtime stall signature = RPC-dispatcher tid 1 DelayThread(20 µs) poll at ra 0x8c94 ⇄ EE main thread loader spin holding the token (BUG-026, PHASE0_IMPL §12.2); gate variant `tools/coldboot_gate.py --iop-kernel on` [IOPK 2026-09-24][R1B 2026-09-25] |
| PS2_IOPSCHED_DIAG | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/iop_stubs.cpp — log-only IOP scheduling trace (thread/sema/delay/heartbeat/CD events, vf+virtual-clock stamped); the Part-A characterization instrument [IOPK 2026-09-24] |
| PS2_INPUT_SELFTEST | unresolved | test-hook | move-to-test-only | TEST | host_app/input/input_system.cpp:127 |
| PS2_INTERP_CENSUS | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:18704 |
| PS2_INTERP_COHERENT | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/gs_interp.h:139 |
| PS2_INTERP_DUMP | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:18743 |
| PS2_INTERP_DUMP_DIR | unresolved | diagnostic | move-to-diagnostics-build | DIAG INHERIT(PS2_INTERP_DUMP) | host_app/graphics/opengl/opengl_backend.cpp:18750 |
| PS2_INTERP_PHASEFIX | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/gs_interp.h:112 |
| PS2_IOPQ | OFF-unless-1 | experiment | delete-both | DELETE | host_app/iop_stubs.cpp:1231 |
| PS2_LIBC_HLE | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/libc_hle.cpp:440 |
| PS2_LOADER_HLE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/hle/loader_sync_hle.h:99 |
| PS2_LOADER_ORACLE | presence-gated | experiment | delete-both | TEST | host_app/hle/loader_oracle.h:55 |
| PS2_LOADER_ORACLE_DIR | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/hle/loader_oracle.h:70 |
| PS2_LOADER_PROF | OFF-unless-nonzero | experiment | delete-both | DELETE | host_app/loader_prof.h:88 |
| PS2_LOADSPAN | OFF-unless-nonzero | experiment | delete-both | DIAG | host_app/loadspan.h:98 |
| PS2_LOADSPAN_DRAWMAX | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/loadspan.h:105;host_app/loadspan.h:106;host_app/loadspan.h:115 |
| PS2_LOAD_BUDGET_LIFT | numeric-other | shipping-setting | keep-as-setting | NORM | host_app/budget_lift.h:105 |
| PS2_LOAD_BUDGET_LIFT_ENGAGE_MS | — | NOT-IN-A2 | — | NORM | host_app/budget_lift.h |
| PS2_LOAD_BUDGET_LIFT_GAP_MS | — | NOT-IN-A2 | — | NORM | host_app/budget_lift.h |
| PS2_LOAD_BUDGET_LIFT_INPUT_MS | — | NOT-IN-A2 | — | NORM | host_app/budget_lift.h |
| PS2_LOAD_BUDGET_LIFT_RELEASE_MS | — | NOT-IN-A2 | — | NORM | host_app/budget_lift.h |
| PS2_LOAD_TURBO | numeric-other | shipping-setting | keep-as-setting | SETTING | host_app/turbo.h:285 |
| PS2_LOAD_TURBO_ENGAGE_MS | — | NOT-IN-A2 | — | NORM | host_app/turbo.h |
| PS2_LOAD_TURBO_GAP_MS | — | NOT-IN-A2 | — | NORM | host_app/turbo.h |
| PS2_LOAD_TURBO_INPUT_MS | — | NOT-IN-A2 | — | NORM | host_app/turbo.h |
| PS2_LOAD_TURBO_K | — | NOT-IN-A2 | — | NORM | host_app/turbo.h |
| PS2_LOAD_TURBO_RELEASE_MS | — | NOT-IN-A2 | — | NORM | host_app/turbo.h |
| PS2_LOG | numeric-other | shipping-setting | keep-as-setting | SETTING | host_app/main.cpp:4905 |
| PS2_LOG_CHANNELS | unresolved | diagnostic | move-to-diagnostics-build | SETTING | host_app/ring_log.cpp:98 |
| PS2_LOG_ELIDE_DROPPED | ON-unless-0 | diagnostic | move-to-diagnostics-build | DELETE | host_app/main.cpp:331 |
| PS2_LOG_FASTFILTER | numeric-other | diagnostic | move-to-diagnostics-build | DELETE | host_app/main.cpp:174 |
| PS2_LOG_RING_EVENTS | unresolved | diagnostic | move-to-diagnostics-build | SETTING | host_app/ring_log.cpp:109 |
| PS2_LOG_VERBOSE | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/ring_log.cpp:101 |
| PS2_LOOPHIST | num-if-set | diagnostic | move-to-diagnostics-build | DELETE | host_app/loop_hist.h:42 |
| PS2_LVL_DIAG | OFF-unless-nonzero | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:3524 |
| PS2_MAINT_EEPACED | OFF-unless-1 | verify-profile-pin | keep-as-setting | SETTING | host_app/main.cpp + maint_pump.h — deterministic-profile pin: the CPU maintenance loop's guest-visible duties (SIF RPC end-callbacks, deferred-ISO delivery, suspended-DMA resume) + the pad slot stamp run at EE boundaries / park-loop pumps instead of ~1 kHz wall cadence; removes the cdvd-fired-tick binning clock and the loader spin-exit race (BUG-026 r2) [MAINT 2026-09-24] |
| PS2_MC_LATENCY | num=4 | shipping-setting | keep-as-setting | SETTING | host_app/sce_hle/libmc.cpp:196 |
| PS2_MC_PERSIST | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/sce_hle/libmc.cpp:96 |
| PS2_DETWATCH | hex-list-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/memory.cpp — [DETW] determinism-escalation write watch: comma list of guest words; every overlapping store logs before/after + EE dispatch call stack (names the writer FUN); arming forces the fastmem slow path. Default OFF = zero fast-path cost [MAINT 2026-09-24] |
| PS2_MEMWATCH | OFF-unless-1 | diagnostic | move-to-diagnostics-build | DIAG | host_app/memory.h:169 |
| PS2_MID_CENSUS | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/mid_census.h:36 |
| PS2_MID_CENSUS_FROM | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/mid_census.h:48 |
| PS2_MID_TEMPLATE | presence-gated | experiment | delete-both | DELETE | host_app/gif_template.h:51 |
| PS2_MID_TEMPLATE_FORCEMISS | numeric-other | tuning-knob | delete-losing-arm-keep-winner | TEST | host_app/gif_template.h:63 |
| PS2_MINIF_DIAG | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:2466 |
| PS2_MIPCEN | num-if-set | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/gif.cpp:5043;host_app/graphics/opengl/opengl_backend.cpp:10332;host_app/gs_mipmap.h:187;host_app/gs_mipmap.h:326;host_app/gs_sta… |
| PS2_MIPDUMP | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:22266 |
| PS2_MIPSRC_DIAG | numeric-other | diagnostic | move-to-diagnostics-build | DELETE | host_app/mipsrc_diag.cpp:145 |
| PS2_MIPSRC_DUMP | presence-gated | diagnostic | move-to-diagnostics-build | DELETE | host_app/mipsrc_diag.cpp:250 |
| PS2_MIPSRC_RAMDUMP | presence-gated | diagnostic | move-to-diagnostics-build | DELETE | host_app/mipsrc_diag.cpp:234 |
| PS2_MIPWR | presence-gated | experiment | delete-both | DELETE | host_app/mipsrc_diag.cpp:77 |
| PS2_MPEG_DUMP_N | num=0 | diagnostic | move-to-diagnostics-build | DIAG | host_app/mpeg_hle.cpp:2248 |
| PS2_MPEG_PACE_AUTHORED | presence-gated | experiment | delete-both | NORM | host_app/mpeg_hle.cpp:459 |
| PS2_MPEG_PACE_OFF | num-if-set | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/mpeg_hle.cpp:566 |
| PS2_MPEG_PDIFF | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/mpeg_hle.cpp:2056 |
| PS2_MPEG_SEQCENSUS | num-if-set | diagnostic | move-to-diagnostics-build | DELETE | host_app/mpeg_hle.cpp:1404 |
| PS2_MPEG_WM | num=0 | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/mpeg_hle.cpp:2192 |
| PS2_MTVU | numeric-other | shipping-setting | keep-as-setting | NORM | host_app/ee_pace.h:175;host_app/vu1_async.cpp:1011 — ledgered P3 (see ¹), CONSUMED in Phase 5 (PHASE5_IMPL 5G, checklist (h)4): `MtvuSettings::enabled` mirrors Config `engine.mtvu`; every MTVU object exists only when the composition root constructs it; `Vif_VuCtx` `CtxFor(1)` returns the bound unit (`BindVu1`), no env [IDOC-A 2026-10-01] |
| PS2_MTVU_DEFER | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/ee_pace.h:173;host_app/vu1_async.cpp:1656 — ledgered P3, CONSUMED in Phase 5 (PHASE5_IMPL 5G, checklist (h)4): NORM — `MtvuSync::DeferOn() ≡ worker.IsRunning()`; no latch, no off arm [IDOC-A 2026-10-01] |
| PS2_MTVU_DYNDEPTH | unresolved | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/runahead_guard.h:108 |
| PS2_MTVU_EAGER_DRAIN | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu1_async.cpp:1537 |
| PS2_MTVU_MAX_FRAMES | unresolved | shipping-setting | keep-as-setting | SETTING | host_app/runahead_guard.h:66 |
| PS2_MTVU_MAX_MB | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu1_async.cpp:187 |
| PS2_MTVU_PATHSYNC_RELAX | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/vu1_async.cpp:1642 |
| PS2_MTVU_SPIN | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu1_async.cpp:610 |
| PS2_MTVU_SPIN_NS | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu1_async.cpp:628 |
| PS2_MTVU_VBSYNC | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/vu1_async.cpp:1620 |
| PS2_MUSQ | OFF-unless-1 | diagnostic | move-to-diagnostics-build | DIAG | host_app/iop_stubs.cpp:1386;host_app/iop_stubs.cpp:1413;host_app/iop_stubs.cpp:1450;host_app/iop_stubs.cpp:1514;host_app/iop_stubs.cpp:15… |
| PS2_MUSQ_VFMIN | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/iop_stubs.cpp:1390;host_app/iop_stubs.cpp:1418 |
| PS2_NAXQ | OFF-unless-1 | experiment | delete-both | DELETE | host_app/iop_stubs.cpp:1473 |
| PS2_NOVSYNC | numeric-other | shipping-setting | keep-as-setting | SETTING | host_app/render.h:871 |
| PS2_NO_QOS | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/thread_qos.h:12 |
| PS2_OCEAN_DUMP | unresolved | diagnostic | move-to-diagnostics-build | DELETE | host_app/graphics/opengl/opengl_backend.cpp:1181 |
| PS2_PACEBRK | OFF-unless-nonzero | experiment | delete-both | DELETE | host_app/pacebrk.h:101 |
| PS2_PACE_FMV_BYPASS | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/present_pace.h:662 |
| PS2_PACE_LOADGRACE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/present_pace.h:684 |
| PS2_PACE_SMOOTH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/present_pace.h:641;host_app/present_pace.h:652 |
| PS2_PAD_SCRIPT | unresolved | diagnostic | move-to-diagnostics-build | TEST | host_app/pad_input.cpp:476;host_app/sce_hle/libpad.cpp:374 |
| PS2_PERF | num-if-set | shipping-setting | keep-as-setting | SETTING | host_app/ee_pace.h:184;host_app/ee_timers.cpp:116;host_app/gif_arbiter.h:635;host_app/mpeg_hle.cpp:462;host_app/mpeg_hle.cpp:534;host_app… |
| PS2_POPUP_DIAG | OFF-unless-1 | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:10305 |
| PS2_POSWATCH | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/pad_input.cpp:553 |
| PS2_PRESENT_EDGE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/ps2_vblank.cpp:446 |
| PS2_PROJ_DIAG | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:3705 |
| PS2_QUANTUM | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/quantum.h:73 |
| PS2_R121_ATTR | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:163 |
| PS2_R197_D1FIX | numeric-other | experiment | delete-both | DELETE | host_app/render.h:1807 |
| PS2_R197_D1P | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:10276 |
| PS2_R197_D2FIX | numeric-other | experiment | delete-both | DELETE | host_app/render.h:1791 |
| PS2_R197_D2P | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:5101;host_app/graphics/opengl/opengl_backend.cpp:6652;host_app/graphics/opengl/opengl_backend… |
| PS2_R197_D2S | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:13026 |
| PS2_R197_D2V2 | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:20722 |
| PS2_R197_D3FIX | numeric-other | experiment | delete-both | DELETE | host_app/render.h:1830 |
| PS2_R197_D3LOG | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/render.h:2551 |
| PS2_R197_DRAWDUMP | num-if-set | diagnostic(one-off) | delete-both | DELETE | host_app/graphics_thread.cpp:4121 |
| PS2_R197_MERGELOG | num-if-set | diagnostic(one-off) | delete-both | DELETE | host_app/render.h:3558 |
| PS2_R197_MERGEMAX | num=-1L | experiment | delete-both | DELETE | host_app/render.h:3630 |
| PS2_R198_D1FMV | numeric-other | experiment | delete-both | NORM | host_app/render.h:1930 |
| PS2_R198_IDX8CT16 | presence-gated | experiment | delete-both | DELETE | host_app/render.h:1912 — context for the `PS2_S8_INRT` L5 correction: `r198::Idx8Ct16On()` returns `r201::UploadDepositOn()` (default OFF) in the peak profile, so the fmt-6 serve it gates is OFF (PHASE6_IMPL item 7 L5) [IDOC-A 2026-10-01] |
| PS2_R198_PATH3DEFER | num=0 | experiment | delete-both | DELETE | host_app/gif_arbiter.cpp:55 |
| PS2_R198_PATH3DEFER_BASE | numeric-other | experiment | delete-both | DELETE | host_app/gif_arbiter.cpp:62 |
| PS2_R198_QORDER | num-if-set | experiment | delete-both | DELETE | host_app/graphics_thread.cpp:569;host_app/gs_state.cpp:4222;host_app/render.h:2704 |
| PS2_R198_QORDER_BASE | numeric-other | experiment | delete-both | DELETE | host_app/graphics_thread.cpp:576;host_app/gs_state.cpp:4227;host_app/render.h:2709 |
| PS2_R198_QORDER_ZCEN | num-if-set | experiment | delete-both | DELETE | host_app/graphics_thread.cpp:738 |
| PS2_R198_RTPROBE | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:10699;host_app/graphics/opengl/opengl_backend.cpp:20478 |
| PS2_R198_XGKPROBE | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/vu.cpp:11408 |
| PS2_R198_XGK_WINDOW | num-if-set | experiment | delete-both | DELETE | host_app/vu.cpp:11445;host_app/vu.cpp:11449 |
| PS2_R199_UPLOADEAGER | numeric-other | experiment | delete-both | NORM | host_app/gs_state.cpp:5579 |
| PS2_R199_XFERLATCH | numeric-other | experiment | delete-both | NORM | host_app/gs_state.cpp:152 |
| PS2_RECOMP_CORPUS | — | NOT-IN-A2 | — | TEST | recompiler_tool/corpus/recorder_gen.h |
| PS2_RECOMP_DIRECT_CALLS | — | NOT-IN-A2 | — | NORM | CMakeLists.txt |
| PS2_RECOMP_SHARD_COUNT | — | NOT-IN-A2 | — | SETTING | CMakeLists.txt |
| PS2_REPLAY_CONTENT | — | NOT-IN-A2 | — | TEST | tests/gs_replay.cpp:564 |
| PS2_REPLAY_DUMPPPM | — | NOT-IN-A2 | — | TEST | tests/gs_replay.cpp:711 |
| PS2_REPLAY_DUMPPPM_FRAMES | — | NOT-IN-A2 | — | TEST | tests/gs_replay.cpp:717 |
| PS2_REPLAY_LOG | — | NOT-IN-A2 | — | TEST | tests/gs_replay.cpp:1021 |
| PS2_REPLAY_SCANOUT | — | NOT-IN-A2 | — | TEST | tests/gs_replay.cpp:555 |
| PS2_RESMEMO_TEST_T12 | — | NOT-IN-A2 | — | TEST | tests/test_resmemo_equivalence.cpp:594 |
| PS2_RESMEMO_TEST_U9 | — | NOT-IN-A2 | — | TEST | tests/test_resmemo_equivalence.cpp:593 |
| PS2_RESMEMO_TEST_VSUV | — | NOT-IN-A2 | — | TEST | tests/test_resmemo_equivalence.cpp:600 |
| PS2_RING_TEST_BUNDLE | unresolved | test-hook | move-to-test-only | TEST | host_app/main.cpp:14210 |
| PS2_RING_TEST_CRASH | unresolved | test-hook | move-to-test-only | TEST | host_app/main.cpp:14199 |
| PS2_RING_TEST_HOTKEY | unresolved | test-hook | move-to-test-only | TEST | host_app/main.cpp:14206 |
| PS2_RQ_HIGHWATER | num=0 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/render.h:3136 |
| PS2_RQ_MAXJOBS | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/ee_pace.h:144 |
| PS2_RQ_MAXMB | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/ee_pace.h:156 |
| PS2_RQ_SAFETY | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/ee_pace.h:131 |
| PS2_RUNAHEAD_ALARM | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/runahead_guard.h:77 |
| PS2_S103_DRAWLIM | unresolved | experiment | delete-both | DIAG | host_app/gs_class_renderer.h:440 |
| PS2_S103_ZBAND | unresolved | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:6345 |
| PS2_S105_SKIP | unresolved | experiment | delete-both | DIAG | host_app/gs_class_renderer.h:536 |
| PS2_S106_DIR | presence-gated | experiment | delete-both | DELETE INHERIT(PS2_S106_STAGE) | host_app/graphics/opengl/opengl_backend.cpp:25328 |
| PS2_S106_STAGE | presence-gated | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:25326;host_app/graphics/opengl/opengl_backend.cpp:25592 |
| PS2_S107_LOG | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/foglane_live.h:72 |
| PS2_S13_PCRING | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/vu.cpp:109 |
| PS2_S13_TBLCAP | numeric-other | experiment | delete-both | DELETE | host_app/vu.cpp:4612 |
| PS2_S13_TBLCAP_ENTRY | num=8 | experiment | delete-both | DELETE INHERIT(PS2_S13_TBLCAP) | host_app/vu.cpp:4616 |
| PS2_S13_TBLCAP_N | num=8 | experiment | delete-both | DELETE INHERIT(PS2_S13_TBLCAP) | host_app/vu.cpp:4621 |
| PS2_S13_TBLWATCH | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/vu.cpp:1609;host_app/vu.cpp:4712 |
| PS2_S15_HDRCAP | numeric-other | experiment | delete-both | DELETE | host_app/vu.cpp:4936 |
| PS2_S15_HDRCAP_ENTRY | numeric-other | experiment | delete-both | DELETE INHERIT(PS2_S15_HDRCAP) | host_app/vu.cpp:4940 |
| PS2_S15_HDRCAP_MINVF | num=8 | experiment | delete-both | DELETE INHERIT(PS2_S15_HDRCAP) | host_app/vu.cpp:4944 |
| PS2_S15_HDRCAP_N | num=8 | experiment | delete-both | DELETE INHERIT(PS2_S15_HDRCAP) | host_app/vu.cpp:4948 |
| PS2_S15_VUENT | num-if-set | experiment | delete-both | DELETE | host_app/vu.cpp:4913 |
| PS2_S15_WATCH | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/memory.cpp:1176 |
| PS2_S16_CHAIN | num=400 | experiment | delete-both | DELETE | host_app/main.cpp:13707 |
| PS2_S16_CHAIN_CAP | num=400 | experiment | delete-both | DELETE | host_app/main.cpp:13712 |
| PS2_S26_DECAL | num=400 | experiment | delete-both | DELETE | host_app/dmac.cpp:1969;host_app/gif.cpp:3686;host_app/main.cpp:13868;host_app/vu.cpp:4346 |
| PS2_S26_DECAL_CAP | num=400 | experiment | delete-both | DELETE | host_app/main.cpp:13872 |
| PS2_S26_FORCEPASS | numeric-other | experiment | delete-both | DELETE | host_app/vu.cpp:4381 |
| PS2_S26_KILL | numeric-other | experiment | delete-both | DELETE | host_app/main.cpp:13880 |
| PS2_S26_UTRACE | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/vu.cpp:10082 |
| PS2_S26_VUDUMP | presence-gated | diagnostic(one-off) | delete-both | DELETE | host_app/vu.cpp:4406 |
| PS2_S32_GREENTEX | unresolved | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:7108;host_app/graphics/opengl/opengl_backend.cpp:12868;host_app/graphics/opengl/opengl_backen… |
| PS2_S83_SEQSCAN | num-if-set | diagnostic(one-off) | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:6427 |
| PS2_RTC_FIXED | num-if-set | verify-profile-pin | keep-as-setting | SETTING | host_app/sce_hle/libscf.cpp — deterministic-profile pin: sceScfGetLocalTimefromRTC returns a constant instead of the host wall RTC (per-run seed in EE RAM) [IOPK 2026-09-24] |
| PS2_S83_SKIP0221 | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:8296 |
| PS2_S83_SKIPORD | unresolved | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:8306 |
| PS2_S83_ZGRID | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:6238 |
| PS2_S8_ARENA_MINVF | numeric-other | experiment | delete-both | DELETE | host_app/dmac.cpp:1935 |
| PS2_S8_FORCECLAIM | OFF-unless-nonzero | experiment | delete-both | DELETE | host_app/main.cpp:13588 |
| PS2_S8_FORCEVIS | num-if-set | experiment | delete-both | DELETE | host_app/vu.cpp:10993 |
| PS2_S8_INRT | presence-gated | experiment | delete-both | NORM | host_app/s8_inrt.h:38 — L5 (PHASE6_IMPL 6F″ from 6D′ P6-FMT6-OFF, carried by INTEGRATION CHECKLIST item 7): **Correction: the fmt-6 / PSMT8-window-in-target serves are OFF at peak.** OLD `render.h:1901-1923` `r198::Idx8Ct16On()` has no explicit env in the peak profile and returns `r201::UploadDepositOn()` (`render.h:1869-1885`, default OFF); `s8inrt::On(r198::Idx8Ct16On())` (`s8_inrt.h`, call `opengl_backend.cpp:21144-21145`) follows it. The ledger's NORM classification must read "winner = OFF — arm not written" (NEW `TextureResolver` has no fmt-6 serve; `WindowInTarget` keeps the geometry for `Texsrc_WindowInTarget`). A future setting flipping the upload-deposit gate must re-land BOTH arms together with a corpus re-proof [IDOC-A 2026-10-01] |
| PS2_S8_MTXCAP | numeric-other | experiment | delete-both | DELETE | host_app/vu.cpp:4832 |
| PS2_S8_MTXCAP_EERAM | unresolved | experiment | delete-both | DELETE INHERIT(PS2_S8_MTXCAP) | host_app/vu.cpp:4860 |
| PS2_S8_MTXCAP_MINVF | num=6 | experiment | delete-both | DELETE INHERIT(PS2_S8_MTXCAP) | host_app/vu.cpp:4836 |
| PS2_S8_MTXCAP_N | num=6 | experiment | delete-both | DELETE INHERIT(PS2_S8_MTXCAP) | host_app/vu.cpp:4840 |
| PS2_S8_MTXWATCH | num-if-set | diagnostic(one-off) | delete-both | DELETE | host_app/memory.cpp:1166 |
| PS2_S8_PKTDUMP | presence-gated | diagnostic(one-off) | delete-both | DELETE | host_app/gif_arbiter.cpp:1496 |
| PS2_S8_PKTDUMP_MINVF | numeric-other | diagnostic(one-off) | delete-both | DELETE INHERIT(PS2_S8_PKTDUMP) | host_app/gif_arbiter.cpp:1504 |
| PS2_S8_REGHOOK | numeric-other | experiment | delete-both | DELETE | host_app/main.cpp:13438 |
| PS2_S8_REGHOOK_BATCAP | num=100 | experiment | delete-both | DELETE | host_app/main.cpp:13453 |
| PS2_S8_REGHOOK_CAP | num=400 | experiment | delete-both | DELETE | host_app/main.cpp:13447 |
| PS2_S8_RTDUMP | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:21200 |
| PS2_S8_TEXDUMP | num-if-set | diagnostic(one-off) | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:83;host_app/graphics/opengl/opengl_backend.cpp:87 |
| PS2_S8_VIFTAP | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/vif.cpp:2553 |
| PS2_S9_DEMOTE | num-if-set | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:17130;host_app/graphics/opengl/s9_composite_native.inc:258 |
| PS2_S9_DIAG | num-if-set | diagnostic(one-off) | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:12341 |
| PS2_SAVE_ATOMIC | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/savestate_format.h:39 |
| PS2_SAVE_PROBE | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/savestate.cpp:181 |
| PS2_SAVE_SLOT | num=0 | tuning-knob | delete-losing-arm-keep-winner | SETTING | host_app/main.cpp:14271;host_app/savestate.cpp:338 |
| PS2_SAVE_WATCH | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/savestate.cpp:2159 |
| PS2_SDR_BLOCK_MS | num=8 | shipping-setting | keep-as-setting | SETTING | host_app/sce_hle/libsdr.cpp:113 |
| PS2_SH1_DEPEPOCH | numeric-other | experiment | delete-both | DELETE | host_app/render.h:565 |
| PS2_SH1_LASTWRITER | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:16379 |
| PS2_SH1_SNAPPROBE | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/render.h:595 |
| PS2_SH1_T8HPROBE | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:936;host_app/graphics/opengl/opengl_backend.cpp:19947 |
| PS2_SH2_DRAWDELTA | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:12698 |
| PS2_SH2_T8HDUMP | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:12744 |
| PS2_SH2_T8HTEX | numeric-other | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:12683 |
| PS2_SH5_ALIASEPOCH | num-if-set | experiment | delete-both | NORM | host_app/graphics/opengl/opengl_backend.cpp:17830;host_app/render.h:1983 |
| PS2_SH5_BIND | num-if-set | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:8900;host_app/graphics/opengl/opengl_backend.cpp:14579;host_app/graphics/opengl/opengl_backen… |
| PS2_SH5_IDXDIRTY | numeric-other | experiment | delete-both | DELETE | host_app/render.h:1964 |
| PS2_SH6_SWEEP | presence-gated | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:12454;host_app/graphics/opengl/s9_composite_native.inc:1771 |
| PS2_SH7 | presence-gated | experiment | delete-both | NORM | host_app/graphics/opengl/opengl_backend.cpp:9013 — L3 (PHASE6_IMPL 6E, carried by INTEGRATION CHECKLIST item 7): Reclassify DELETE → **NORM (winning arm = ON)**. The `opengl_backend.cpp:9008-9034` same-buffer pre-draw flush defaults ON when the variable is unset (`:9013-9017`), the peak profile never sets it, so it is live behind every frozen corpus hash and the 12-leg baselines. NEW: `DrawPipeline::SameBufferPreDrawFlush` (`engine/gs/hle/classrdr/draw_pipeline.cpp`), pinned by `Classrdr_DrawPipelineOrder`. GS rule: a FRAME write is authoritative for its rows — a deferred writeback of the same buffer's earlier 16-bit content must reach the mirror before the repaint destroys its source image [IDOC-A 2026-10-01] |
| PS2_SH7_DEC | unresolved | experiment | delete-both | DELETE | host_app/graphics/opengl/opengl_backend.cpp:9255 |
| PS2_SH8 | presence-gated | experiment | delete-both | NORM | host_app/graphics/opengl/world_tristrip_native.inc:293 |
| PS2_SHADOW8_KTAP | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/dmac.cpp:1926;host_app/gif.cpp:391;host_app/gif.cpp:2842;host_app/gif.cpp:4337;host_app/gif_arbiter.cpp:299;host_app/gif_arbiter… |
| PS2_SHADOW_CTXLATCH | presence-gated | experiment | delete-both | DELETE | host_app/s8_ctxlatch.h:114 |
| PS2_SHADOW_DIAG | — | NOT-IN-A2 | — | DIAG | recompiler_tool/recompiler.cpp |
| PS2_SHADOW_ORDERFIX | presence-gated | experiment | delete-both | DELETE | host_app/s8_ctxlatch.h:112 |
| PS2_SHADOW_TAILFIX | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/gif_arbiter.cpp:160 |
| PS2_SHARDED_RECOMP | — | NOT-IN-A2 | — | NORM | CMakeLists.txt |
| PS2_SHUTDOWN_WAIT | unresolved | shipping-setting | keep-as-setting | SETTING | host_app/shutdown_policy.h:62 |
| PS2_SITEB_DIAG | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:3623 |
| PS2_SLOWSPOT | unresolved | shipping-setting | keep-as-setting | SETTING | host_app/slowspot.cpp:36;host_app/slowspot.cpp:39;host_app/slowspot.cpp:55 |
| PS2_SLOWSPOT_ASYNC | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/savestate_writer.h:55 |
| PS2_SLOWSPOT_COOLDOWN_S | — | NOT-IN-A2 | — | NORM | host_app/slowspot.cpp |
| PS2_SLOWSPOT_DEDUPE | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/slowspot.cpp:62 |
| PS2_SLOWSPOT_GF_MS | — | NOT-IN-A2 | — | NORM | host_app/slowspot.cpp |
| PS2_SLOWSPOT_MS | — | NOT-IN-A2 | — | NORM | host_app/slowspot.cpp |
| PS2_SLOWSPOT_SUFFIX | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/slowspot_paths.h:29 |
| PS2_SMWATCH | OFF-unless-nonzero | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:795 |
| PS2_SNAP | unresolved | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:218 |
| PS2_SNAPVERIFY | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/render.h:633 |
| PS2_SNAP_DIR | unresolved | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:232 |
| PS2_SNAP_DISP_ONLY | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:292 |
| PS2_SNAP_RT | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics_thread.cpp:320 |
| PS2_SND_TICKCLOCK | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/sound/sound_core.cpp:196 — ON under time.tick_sync: the mixer is pumped 800 frames per boundary in the Verify slot (PHASE4_PLAN 4.5); the device-clock arm is Phase 7's time.tick_sync=false path, no field of its own [4A′ 2026-09-27, PHASE4_PLAN 5.6] |
| PS2_SNDREG | numeric-other | shipping-setting | keep-as-setting | NORM | host_app/hle_heap.cpp:685 |
| PS2_SND_BANKTRACE | OFF-unless-nonzero | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:843 |
| PS2_SND_CENSUS | OFF-unless-nonzero | diagnostic | move-to-diagnostics-build | DIAG | host_app/snd_census.h:45 |
| PS2_SND_EAGER_HOLD | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/sound/sound_core.cpp:353 |
| PS2_SND_FMV | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/mpeg_hle.cpp:1282 |
| PS2_SND_FMVPACE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/sce_hle/libsdr.cpp:143 |
| PS2_SND_FMVQ_DIV | num=2 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/sce_hle/libsdr.cpp:153 |
| PS2_SND_KONLOG | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/iop_stubs.cpp:1558;host_app/iop_stubs.cpp:1658;host_app/sound/sound_core.cpp:337;host_app/sound/sound_core.cpp:473 |
| PS2_SND_PAUSE_DEFER | OFF-unless-1 | experiment | delete-both | DELETE | host_app/sif_rpc_handlers.cpp:3199 |
| PS2_SND_PROCBATCH | ON-unless-0 | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/iop_stubs.cpp:1506 |
| PS2_SND_SELFTEST | num-if-set | test-hook | move-to-test-only | TEST | host_app/sound/sound_core.cpp:237 |
| PS2_SND_STATEHASH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/savestate.cpp:369 |
| PS2_SOUND | numeric-other | shipping-setting | keep-as-setting | SETTING | host_app/sound/sound_core.cpp:187 |
| PS2_SPIKE_DUMP | unresolved | diagnostic | move-to-diagnostics-build | DELETE | host_app/graphics_thread.cpp:3716 |
| PS2_SPIKE_GUARD | num-if-set | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gs_state.cpp:1931;host_app/gs_state.cpp:2374 |
| PS2_SPU_LAT_US | presence-gated | experiment | delete-both | NORM | host_app/iop_stubs.cpp:1813 |
| PS2_STALL_INJECT | unresolved | tuning-knob | delete-losing-arm-keep-winner | TEST | host_app/graphics_thread.cpp:2211 |
| PS2_STICKTRACE | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/stick_trace.h:69 |
| PS2_STRMQ | — | NOT-IN-A2 | — | DELETE | iop_recompiler/iop_recompiler.cpp |
| PS2_T0TRACE | numeric-other | diagnostic | move-to-diagnostics-build | DELETE | host_app/ee_timers.cpp:32 |
| PS2_T0_NOISR | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/ee_timers.cpp:482 |
| PS2_T0_SUBTICK | presence-gated | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/ee_timers.cpp:113 |
| PS2_T0_TICKPACED | OFF-unless-1 | verify-profile-pin | keep-as-setting | SETTING | host_app/memory.cpp — deterministic-profile pin: the LIVE legacy T0 read path's raw-wall sub-frame interpolation term is zeroed (PS2_T0_SUBTICK never covered it — that gate is PS2_EE_TIM0-path-only); COUNT advances only at EE boundaries (BUG-026 r2) [MAINT 2026-09-24] |
| PS2_TEST_RESIZE | unresolved | test-hook | move-to-test-only | TEST | host_app/main.cpp:14479 |
| PS2_TEXBLKDUMP | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:6490 |
| PS2_TEXBLKDUMP_DIR | unresolved | diagnostic | move-to-diagnostics-build | DIAG INHERIT(PS2_TEXBLKDUMP) | host_app/graphics/opengl/opengl_backend.cpp:6491 |
| PS2_TEXPOP_ALL | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/graphics/opengl/opengl_backend.cpp:7816 |
| PS2_TEXPOP_DIAG | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:7806 |
| PS2_TEXPOP_GLHASH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/graphics/opengl/opengl_backend.cpp:7969 |
| PS2_TEXPOP_ZDUMP | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:8018 |
| PS2_TEXVAL | numeric-other | tuning-knob | delete-losing-arm-keep-winner | TEST | host_app/graphics/opengl/opengl_backend.cpp:5886 |
| PS2_TICKWATCH | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/main.cpp:1139 |
| PS2_TIMEDOM_DIAG | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/ee_timers.cpp:552 |
| PS2_TICK_DETERMINISTIC | num-if-set | verify-profile-pin | keep-as-setting | SETTING | host_app/ps2_vblank.cpp — deterministic-profile pin: freerun timeout-floor ticks are skipped so the vblank tick count is a pure function of EE progress (consumption/vsync/cdvd); play default keeps the wall floor (real-time pacing on slow hosts). The BUG-014/S94 family pin [IOPK 2026-09-24]. SUPERSEDED under PS2_TICK_SYNC=1 (no ticker thread exists); kept for legacy-generator legs [TICKSYNC 2026-09-25] |
| PS2_TIME_EMU | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/time_domain.h:76 |
| PS2_TIME_EMU_INTRA | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/ee_timers.cpp:154 |
| PS2_TIME_EMU_REGIME | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/time_domain.h:93 |
| PS2_TURBO | numeric-other | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/turbo.h:194 |
| PS2_TURBO_DUCK | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/turbo.h:252 |
| PS2_U32POKE | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/pad_input.cpp:700 |
| PS2_U32WATCH | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/pad_input.cpp:653 |
| PS2_U3_VIEWPORT | numeric-other | experiment | delete-both | DELETE | host_app/display_settings.cpp:185 |
| PS2_UNPKT_CENSUS | OFF-unless-nonzero | diagnostic | move-to-diagnostics-build | DIAG | host_app/vif.cpp:344 |
| PS2_VBLANK_FREERUN | presence-gated | shipping-setting | keep-as-setting | NORM | host_app/ps2_vblank.cpp:411 |
| PS2_VERBOSE_HW | unresolved | diagnostic | move-to-diagnostics-build | DIAG | host_app/hw_verbose.h:26 |
| PS2_VIF_ALIGN_LOG | num-if-set | diagnostic | move-to-diagnostics-build | DIAG | host_app/vif.cpp:1515;host_app/vif.cpp:1730;host_app/vif.cpp:1789 |
| PS2_VIF_FAST | numeric-other | shipping-setting | keep-as-setting | NORM | host_app/graphics_thread.cpp:2021;host_app/vif.cpp:801 |
| PS2_VIF_UNPACK_SIMD | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vif.h:40 |
| PS2_VIF_UNPKBX | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vif.h:91 |
| PS2_VIF_WTRACE | numeric-other | diagnostic | move-to-diagnostics-build | DELETE | host_app/vif.cpp:3456 |
| PS2_VRAM_EPOCH_REACT | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/gs_state.cpp:4763 |
| PS2_VTQ | OFF-unless-1 | experiment | delete-both | DELETE | host_app/iop_stubs.cpp:1894 |
| PS2_VU0_MODE | — | NOT-IN-A2 | — | DELETE | host_app/vu.cpp:726 |
| PS2_VU0_SHADOWGATE | num=0 | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/vu.cpp:4460 |
| PS2_VU0_VISCAP | num=64 | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/vu.cpp:4548 |
| PS2_VU0_VISCAP_MINVF | num=64 | tuning-knob | delete-losing-arm-keep-winner | DELETE INHERIT(PS2_VU0_VISCAP) | host_app/vu.cpp:4551 |
| PS2_VU0_VISCAP_N | num=64 | tuning-knob | delete-losing-arm-keep-winner | DELETE INHERIT(PS2_VU0_VISCAP) | host_app/vu.cpp:4555 |
| PS2_VU0_XCHECK | OFF-unless-1 | experiment | delete-both | DELETE | host_app/vu.cpp:5041 |
| PS2_VU1_CRYS_GATE | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/hle/vu1_hle_programs.cpp:267 |
| PS2_VU1_CRYS_RING | unresolved | diagnostic | move-to-diagnostics-build | NORM | host_app/hle/vu1_hle_programs.cpp:253 |
| PS2_VU1_CYCLE_CAP | presence-gated | experiment | delete-both | TEST | host_app/vu.cpp:458 |
| PS2_VU1_EXEC_ASYNC | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/vu1_async.cpp:1040 |
| PS2_VU1_EXEC_IDLE_SPIN_NS | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/vu1_async.cpp:390 |
| PS2_VU1_EXEC_WAIT_SPIN_NS | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/vu1_async.cpp:400 |
| PS2_VU1_HLE | num-if-set | shipping-setting | keep-as-setting | NORM | host_app/hle/vu1_hle_runtime.cpp:61 |
| PS2_VU1_HLE_DEBUG | num-if-set | tuning-knob | delete-losing-arm-keep-winner | DIAG | host_app/hle/vu1_hle_runtime.cpp:222 |
| PS2_VU1_HLE_DIRECT | num-if-set | shipping-setting | keep-as-setting | NORM | host_app/hle/vu1_hle_runtime.cpp:90 |
| PS2_VU1_HLE_HASHEPOCH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/hle/vu1_hle_runtime.cpp:182 |
| PS2_VU1_HLE_WHY | OFF-unless-1 | experiment | delete-both | DELETE | host_app/hle/vu1_hle_programs.cpp:214;host_app/hle/vu1_hle_programs.cpp:225 |
| PS2_VU1_LINK11 | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu_fast_exec.h:113 |
| PS2_VU1_MODE | — | NOT-IN-A2 | — | DELETE | host_app/vu.cpp:726 |
| PS2_VUAOT_COLLECT_DIR | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/main.cpp:5020 |
| PS2_VUAOT_INI | unresolved | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/gui/vu_microcode_section.cpp:79;host_app/gui/vu_microcode_section.cpp:163;host_app/main.cpp:5002 |
| PS2_VUAOT_TEST_REGEN | numeric-other | test-hook | move-to-test-only | DELETE | host_app/gui/vu_microcode_section.cpp:360 |
| PS2_VUAOT_TEST_SETCOLLECT | unresolved | test-hook | move-to-test-only | DELETE | host_app/gui/vu_microcode_section.cpp:348 |
| PS2_VUAOT_TEST_SETMODE | unresolved | test-hook | move-to-test-only | DELETE | host_app/gui/vu_microcode_section.cpp:339 |
| PS2_VUTRACE | OFF-unless-1 | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:136;host_app/vu.cpp:875 |
| PS2_VUTRACE_CAPTURE | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:142 |
| PS2_VUTRACE_DIR | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:148 |
| PS2_VUTRACE_FILE | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:138 |
| PS2_VUTRACE_IMG_GAP | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:154 |
| PS2_VUTRACE_MAX | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:140 |
| PS2_VUTRACE_MINFRAME | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:150 |
| PS2_VUTRACE_NEWIMG | OFF-unless-1 | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:246 |
| PS2_VUTRACE_PC | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:144 |
| PS2_VUTRACE_PER_IMAGE | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/vu.cpp:152 |
| PS2_VU_AOT | unresolved | proven-fix-hatch-ON | delete-losing-arm-keep-winner | DELETE | host_app/vu_aot_programs.cpp:37 |
| PS2_VU_AOT_PROFILE | — | NOT-IN-A2 | — | DELETE | CMakeLists.txt;host_app/vu_aot_registry.h |
| PS2_VU_AOT_SKELFULL | OFF-unless-1 | experiment | delete-both | DELETE | host_app/vu_aot_programs.cpp:47 |
| PS2_VU_AOT_SPANFOLD | numeric-other | proven-fix-hatch-ON | delete-losing-arm-keep-winner | DELETE | host_app/vu_aot_programs.cpp:70 |
| PS2_VU_CLAMPELIDE | numeric-other | proven-fix-hatch-ON | delete-losing-arm-keep-winner | DELETE | host_app/vu_fast_exec.h:83 |
| PS2_VU_FASTPACK | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:443 |
| PS2_VU_HASHEPOCH | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:2474 |
| PS2_VU_LAZYFLAGS | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:873 |
| PS2_VU_MODE | ON-unless-0 | shipping-setting | keep-as-setting | SETTING | host_app/vu.cpp:373 |
| PS2_VU_NOCLAMP | num-if-set | tuning-knob | delete-losing-arm-keep-winner | DELETE | host_app/vu_fast_exec.h:61 |
| PS2_VU_O3 | — | NOT-IN-A2 | — | NORM | CMakeLists.txt |
| PS2_VU_PACE_NEXTDUE | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:747 |
| PS2_VU_RUNFLAT | OFF-unless-1 | experiment | delete-both | DELETE | host_app/vu.cpp:575 |
| PS2_VU_RUNFLAT_CENSUS | OFF-unless-1 | diagnostic | move-to-diagnostics-build | DELETE | host_app/vu.cpp:585 |
| PS2_VU_RUNPACE | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:755 |
| PS2_VU_RUNRETIRE | numeric-other | proven-fix-hatch-ON | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:903 |
| PS2_VU_SPANFOLD | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:526 |
| PS2_VU_SPANFOLD2 | OFF-unless-1 | experiment | delete-both | DELETE | host_app/vu.cpp:547 |
| PS2_VU_STALLFOLD | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:510 |
| PS2_VU_STALLSPEC | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:497 |
| PS2_VU_SUPERBLOCK | unresolved | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:478 |
| PS2_VU_XGKPACE_QUANTUM | num=32 | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/vu.cpp:738 |
| PS2_W13R7_CENSUS_EVERY | unresolved | diagnostic(one-off) | delete-both | DELETE | host_app/main.cpp:14321 |
| PS2_W13R7_NORESEED | numeric-other | experiment | delete-both | DELETE | host_app/savestate.cpp:106 |
| PS2_W13R7_RELOAD_EVERY | unresolved | experiment | delete-both | DELETE | host_app/main.cpp:14318 |
| PS2_W13R7_RELOAD_PATH | unresolved | experiment | delete-both | DELETE | host_app/main.cpp:14324 |
| PS2_W15_KICKCENSUS | numeric-other | diagnostic(one-off) | delete-both | DELETE | host_app/vu.cpp:3998 |
| PS2_W16_SPIKE | num=0 | experiment | delete-both | DELETE | host_app/graphics_thread.cpp:2443 |
| PS2_WAITFIX | numeric-other | tuning-knob | delete-losing-arm-keep-winner | NORM | host_app/timeline.h:54 |
| PS2_WARP_TO | presence-gated | shipping-setting | keep-as-setting | SETTING | host_app/warp.h:102 |
| PS2_XFER_FAST | presence-gated | experiment | delete-both | NORM | host_app/render.h:501 |
| PS2_ZSERVE_DIAG | numeric-other | diagnostic | move-to-diagnostics-build | DIAG | host_app/graphics/opengl/opengl_backend.cpp:21431 |
| RECOMP_ENGINE | — | NOT-IN-A2 | — | DELETE | CMakeLists.txt |
| RENDERER_BACKEND | — | NOT-IN-A2 | — | SETTING | CMakeLists.txt |
| VIF_REPLAY_DUMPW | — | NOT-IN-A2 | — | TEST | tests/vif_replay.cpp:266 |
| VIF_REPLAY_LOG | — | NOT-IN-A2 | — | TEST | tests/vif_replay.cpp:1816 |
| VIF_REPLAY_MTVU_DEBUG | — | NOT-IN-A2 | — | TEST | tests/vif_replay.cpp:1072 |
| VIF_REPLAY_RGBDUMP | — | NOT-IN-A2 | — | TEST | tests/vif_replay.cpp:183 |
| VIF_REPLAY_TTE2 | — | NOT-IN-A2 | — | TEST | tests/vif_replay.cpp:1880 |

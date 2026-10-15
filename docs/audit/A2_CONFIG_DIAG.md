# A2 Audit — Configuration, Gates, Diagnostics, Logging, Session-History-in-Code

Audit stage A2 of the the title PS2 static-recomp runtime at
`$PS2R_OLD_ROOT` (read-only; no builds, no code changes).
Scan scope: `host_app/`, `recompiler_tool/`, `EEAnalyze/`, `iop_recompiler/`, `tools/`,
`.cpp/.h/.hpp/.inc` — excluding `third_party/`, `legacy/`, build dirs, `main_old.cpp`,
`ps2_scheduler_old_fiber.*`. All numbers below are from the scan scripts in `/tmp/s113/a2/`
(scan_envvars.py, scan_latches.py, scan_patterns.py, classify_defaults.py,
map_vars_docs.py, scan_hotlog_diag.py, scan_session_ns.py, finalize.py).

## Headline numbers

| Metric | Value |
|---|---|
| `getenv` call sites (string-literal) | **625** |
| Unique environment variables | **496** (493 `PS2_*`, 2 `LOGO_*`, 1 `CT*`) |
| Non-literal `getenv(expr)` sites | 19 |
| Static-lambda latch blocks (`static const T x = []{...}()`) | **482** (449 read env) |
| `static x = getenv(...)` one-liner latches | 20 |
| Multi-level fallback chains (≥2 vars in one latch) | **26** |
| Env vars latched in >1 place (mirror risk) | **38** |
| `g_logFile <<` statement sites | **6,869** |
| `fprintf(stderr, ...)` sites | 855 |
| `std::cout <<` sites | 759 |
| `std::cerr <<` sites | 180 |
| `[TAG]`-style comment mentions | **7,469** (1,374 unique tags) |
| Comment blocks > 15 consecutive `//` lines | **797** |
| Session-named namespaces (`r118::`, `r120gl::`, `zk::`, …) | **36** |
| Forensic hook installs in main.cpp (`install_dual_hook` etc.) | 208 |
| docs/loop_state session records | 283 .md files |

## Disposition summary (496 unique vars)

| Disposition | Count | Meaning |
|---|---|---|
| keep-as-setting | **29** | Real user/config surface → unified Config schema |
| delete-losing-arm-keep-winner | **208** | A/B or fix hatch with proven winner: make winner unconditional, delete var + losing code path |
| delete-both | **136** | Concluded experiment / one-off probe for a solved bug: delete var and both code arms (fix stays as plain code) |
| move-to-diagnostics-build | **109** | Durable tooling → compile-time diagnostics module (absent from release build) |
| move-to-test-only | **14** | Test/CI hook → test harness only |
| **Total** | **496** | |

Categorization method: (a) CANON set in `scripts/run_play.sh:141-176` +
documented user knobs = shipping settings; (b) `run_play.sh:85-99` 'Correctness
hatches (all default ON …)' + code default `!(e && e[0]=='0')` = proven-fix
hatches; (c) name contains LOG/TRACE/PROBE/DUMP/DIAG/CENSUS/WATCH/… = diagnostic;
session-coded diagnostics (`PS2_R197_D3LOG`) = one-off; (d) session-coded
non-diagnostics = concluded experiments; (e) TEST/FAKE/CI = test hooks.
Per-var dispositions are a *recommendation baseline* — the migration plan's
gate-purge procedure (§7 below) requires a docs/loop_state verdict check before
each deletion.

Known auto-categorization caveats (hand-reviewed): `PS2_HEADWATCH` and
`PS2_EULERTRACE` land in keep-as-setting because they sit in run_play.sh CANON
(:170-171), but they are [S30] slide-jump *probes* riding in the dev-mode canon —
run_play.sh:127-129's pure-play recipe says to delete them (EULERTRACE is
presence-gated: any value enables, run_play.sh:81-84). Actual disposition:
move-to-diagnostics-build. Effective keep-as-setting count is therefore 27.

## Top-10 findings

1. **496 env vars / 625 getenv sites is the project's #1 maintainability hazard.**
   Only ~29 are real settings; 344 (69%) are concluded A/B arms or dead experiments
   (`delete-losing-arm-keep-winner` 208 + `delete-both` 136). Every one multiplies
   the test matrix and hides the real control flow.
2. **`PS2_PERF` pulls triple duty and is latched in 21+ translation units.** It gates the
   [PERF] reporting AND is re-read inside ~15 *other* gates' latches as the standardized
   'gate self-report announce' idiom ('One-shot gate line under PS2_PERF', ee_pace.h:166)
   — e.g. vu1_async.cpp:1013, ee_timers.cpp:116, timeline.h:56, mpeg_hle.cpp:1200,
   vu_aot_programs.cpp:72, ps2_vblank.h:164. Verified announce-only (no behavioral
   default flows from PERF), but 21 independent hand-rolled latches of one var is the
   mirror hazard at its worst; the replacement is a single startup config-dump.
3. **The real behavioral cascade is the bench-env chain `PS2_NOVSYNC` → `PS2_VBLANK_FREERUN`
   → `PS2_CDVD_INSTANT`, plus an inversion**: FREERUN defaults to NOVSYNC
   (ps2_vblank.cpp:410-413 'default: ride PS2_NOVSYNC'), CDVD_INSTANT defaults to FREERUN
   (ps2_vblank.cpp:468-471), and `PS2_MPEG_PACE_AUTHORED` defaults to the *inverse* of
   FREERUN (mpeg_hle.cpp:460). Setting one benchmarking var silently flips vblank pacing,
   load semantics, and FMV pacing. Separately, prerequisite conjunctions:
   `PS2_MTVU_DEFER` requires `PS2_MTVU` (vu1_async.cpp:1658-59); `PS2_EE_PACE` requires
   both (ee_pace.h:168-178, demotion warning printed). Config must encode these as one
   explicit `pacing` enum + dependent-field validation, not env inheritance.
4. **38 vars are latched in more than one place and can disagree.** Worst:
   `PS2_PERF` 21 latch sites, `PS2_SHADOW8_KTAP` 16 sites across 7 files (vif.cpp:2172,
   vu.cpp:1635, gif_arbiter.cpp:299, gs_state.cpp:101, dmac.cpp:1926, gif.cpp:391 …),
   `PS2_MUSQ` 9, `PS2_SH5_BIND` 8. One is a *documented intentional mirror*:
   graphics/opengl/gl_state_cache.h:75-89 re-implements r120::BindCacheOn's exact
   3-level resolution to avoid an include chain — logic duplicated, drift possible.
5. **The logging system is 4 retrofitted layers doing a leveled logger's job**:
   PS2_LOG master open-gate (main.cpp:4905), TagFilterBuf prefix drop + [M3A]
   PS2_LOG_FASTFILTER (main.cpp:157), [M4A] PS2_LOG_ELIDE_DROPPED dead-format elision
   (main.cpp:319 + hw_verbose.h:32), and hwlog::VerboseOn() per-event narration re-arm
   (hw_verbose.h:24). 6,869 `g_logFile<<` sites; the standing rule (hw_verbose.h:8-11,
   journal W10-GLOG) exists precisely because a 2.8 GB log happened. A compile-time
   category logger replaces all four layers.
6. **Hot-path logging is still live in the hottest loops**: memory.cpp has 295
   log sites with only 35 gated nearby (e.g. per-MMIO-read `VIF0_FBRST` lines at
   memory.cpp:557-566); vu.cpp 283 sites/54 gated. These pay formatting+I/O in any
   default run because the log opens by default (PS2_LOG unset ⇒ open, main.cpp:4905-08).
7. **The recompiler emits env-gated diagnostics into generated code** —
   recompiler_tool/recompiler.cpp:2412/3198/3247/3312 inject `PS2_SHADOW_DIAG` blocks and
   :25786 injects `PS2_EE_RAWDISP` into recompiled.cpp. Gate-purge must cover the
   *generator*, not just checked-in runtime code.
8. **208 forensic hook installs in main.cpp**, many with hardcoded save-state-specific
   host addresses and frame windows — e.g. [SEQ54] sail-node probe main.cpp:1530-1650
   (nodes 0xd8c5d0/0xd8c1a0/0xd8c130/0xd8c050, frames ≥1180, 400-line budget) for the
   *solved* R53/R54 look-at-tick bug. These are session archaeology, not tooling:
   delete-both; keep the *mechanism* (install_dual_hook) in the diagnostics module.
9. **7,469 [TAG] mentions (1,374 unique) + 36 session namespaces + 797 >15-line comment
   blocks** constitute a parallel VCS-in-comments. r120gl:: appears on 691 lines of
   .cpp/.h source — 1,110 including .inc files (scope: host_app/ + tests/, excluding
   build/ outputs and third_party/; the previously quoted 1,041 came from an unscoped
   scan and is superseded by these reproducible counts) — session names became
   load-bearing API. Ship-quality modules can keep ~5% of this prose (the
   hardware-invariant 'why'); the narrative belongs in docs/loop_state (283 files already
   exist for exactly this).
10. **The real user config surface is tiny and already known**: run_play.sh CANON (23
    vars) + display.ini (5 keys) + vu_aot.ini (3 keys) + recomp_overrides.ini (1 key) +
    GUI (input device, kbm-pad, scaling mode/aspect/scale/offsets, volume). A unified
    Config schema of ~30 fields covers everything a player or dev-runner needs (§9).

---
## 1. Complete env-var registry

496 unique vars, 625 literal sites. Full CSV below (one row per var; `sites` lists
every file:line). `default` legend: `ON-unless-0` = enabled when unset (=0 disables);
`OFF-unless-1` = disabled when unset (=1 enables); `OFF-unless-nonzero`; `num=N` =
numeric with default N; `num-if-set` = numeric, only applies when set; `presence-gated`
= ANY value (even 0) enables; `numeric-other`/`unresolved` = multi-line/bespoke parse
(hand-check before purge). `introduced-by` = docs/loop_state record(s) mentioning the
var (first 2; blank = undocumented).

Category legend: `shipping-setting` | `proven-fix-hatch-ON` | `experiment` |
`diagnostic` / `diagnostic(one-off)` | `tuning-knob` | `test-hook`.

```csv
var,nsites,sites,default,category,mirrored,introduced_by,disposition
PS2_B57Q,1,host_app/main.cpp:10474,OFF-unless-1,experiment,,LOADTIME_IMPL.md,delete-both
PS2_CD_LAT_US,1,host_app/iop_stubs.cpp:1804,presence-gated,experiment,,TIMING_NATIVE_ROADMAP.md;SOUND_P8_REVIEW5.md,delete-both
PS2_CUELAG,1,host_app/cuelag_probe.h:87,OFF-unless-nonzero,experiment,,GS_PHASEB_REVIEW.md;GS_PHASEB_IMPL.md,delete-both
PS2_DRAW_SKIP,1,host_app/graphics/opengl/opengl_backend.cpp:8413,presence-gated,experiment,,CAVERN_FOG_IMPL.md;MENU_VIS.md,delete-both
PS2_FMV3_COMPGATE,1,host_app/graphics_thread.cpp:1486,OFF-unless-nonzero,experiment,,FMV3_ROUND.md,delete-both
PS2_FMV_DECODE_CAP,1,host_app/mpeg_hle.cpp:1197,presence-gated,experiment,,FMV2_IMPL.md,delete-both
PS2_FMV_OPEN_BURST,1,host_app/mpeg_hle.cpp:532,presence-gated,experiment,,FMV2_IMPL.md,delete-both
PS2_GFXLOOP_ARM,1,host_app/graphics_thread.cpp:382,presence-gated,experiment,,,delete-both
PS2_GL_ALIAS_DEPCOPY,1,host_app/graphics/opengl/opengl_backend.cpp:671,presence-gated,experiment,,W14_CHECKER.md;W14_PROFILE.md,delete-both
PS2_GL_ALIAS_DEPCOPY2,2,host_app/graphics/opengl/opengl_backend.cpp:677;host_app/graphics/opengl/opengl_backend.cpp:703,presence-gated,experiment,Y,W14_CHECKER.md;W14_PROFILE.md,delete-both
PS2_GL_BBOX_COPY,1,host_app/render.h:2487,presence-gated,experiment,,W14_IMPL.md,delete-both
PS2_GL_BIND_CACHE,1,host_app/graphics/opengl/gl_state_cache.h:82,presence-gated,experiment,,R198_ROUND.md,delete-both
PS2_GL_IDXTEX,1,host_app/render.h:1188,presence-gated,experiment,,TEXCORRUPT_REPORT.md;R198_ROUND.md,delete-both
PS2_GL_IDXTEX_16,1,host_app/render.h:1245,presence-gated,experiment,,VIS_REVIEW.md;PLAYTEST_R1_IMPL.md,delete-both
PS2_GL_STATE_CACHE,1,host_app/graphics/opengl/gl_state_cache.h:399,presence-gated,experiment,,R166_impl.md;VIS_ROOTCAUSE3.md,delete-both
PS2_GL_ZTEX,1,host_app/render.h:1088,presence-gated,experiment,,CAVERN_DEPTHPATCH_IMPL.md;CAVERN_CHECKER_IMPL.md,delete-both
PS2_GPUFENCE,1,host_app/graphics/opengl/opengl_backend.cpp:23824,presence-gated,experiment,,INPUTLAG_IMPL.md,delete-both
PS2_GS_DEPOSIT_EEAPPLY,1,host_app/gs_deposit_overlay.h:66,presence-gated,experiment,,CAVERN_WALLALT_REVIEW.md;BLACKBOOT_IMPL.md,delete-both
PS2_GS_RESIDENT,1,host_app/gs_page_tracker.h:72,presence-gated,experiment,,CAVERN_WALLTEX_RESEARCH.md;CAVERN_FLICKER_IMPL.md,delete-both
PS2_GS_RESIDENT_PBO,1,host_app/gs_page_tracker.h:102,presence-gated,experiment,,CAVERN_WALLTEX_RESEARCH.md;CAVERN_FLICKER_IMPL.md,delete-both
PS2_GS_SNAPSHOT_FAST,1,host_app/render.h:447,presence-gated,experiment,,CAVERN_WALLTEX_RESEARCH.md;PLAYTEST_R1_IMPL.md,delete-both
PS2_IOPQ,1,host_app/iop_stubs.cpp:1231,OFF-unless-1,experiment,,LOADTIME_REPORT.md,delete-both
PS2_LOADER_ORACLE,1,host_app/hle/loader_oracle.h:55,presence-gated,experiment,,LOADER_HLE_IMPL.md,delete-both
PS2_LOADER_PROF,1,host_app/loader_prof.h:88,OFF-unless-nonzero,experiment,,LOADER_HLE_IMPL.md,delete-both
PS2_LOADSPAN,1,host_app/loadspan.h:98,OFF-unless-nonzero,experiment,,LOADTIME_IMPL.md;LOADTIME_REPORT.md,delete-both
PS2_MID_TEMPLATE,1,host_app/gif_template.h:51,presence-gated,experiment,,MIDDLE_NATIVE_IMPL.md,delete-both
PS2_MIPWR,1,host_app/mipsrc_diag.cpp:77,presence-gated,experiment,,CAVERN_MIPSRC_IMPL.md,delete-both
PS2_MPEG_PACE_AUTHORED,1,host_app/mpeg_hle.cpp:459,presence-gated,experiment,,CRASH_TRIAGE.md,delete-both
PS2_NAXQ,1,host_app/iop_stubs.cpp:1473,OFF-unless-1,experiment,,LOADTIME_REPORT.md,delete-both
PS2_PACEBRK,1,host_app/pacebrk.h:101,OFF-unless-nonzero,experiment,,CRASH_TRIAGE.md;W10_IMPL.md,delete-both
PS2_R121_ATTR,1,host_app/graphics/opengl/opengl_backend.cpp:163,numeric-other,experiment,,CAVERN_SLOWSPOTS_IMPL.md;CAVERN_DIRECTIONAL_IMPL.md,delete-both
PS2_R197_D1FIX,1,host_app/render.h:1807,numeric-other,experiment,,R198_ROUND.md;R201_AUDIT_REVIEW.md,delete-both
PS2_R197_D1P,1,host_app/graphics/opengl/opengl_backend.cpp:10276,numeric-other,experiment,,R198_ROUND.md;R197_PROFILE.md,delete-both
PS2_R197_D2FIX,1,host_app/render.h:1791,numeric-other,experiment,,R201_AUDIT_FINDINGS.md;R198_PROFILE.md,delete-both
PS2_R197_D2P,4,host_app/graphics/opengl/opengl_backend.cpp:5101;host_app/graphics/opengl/opengl_backend.cpp:6652;host_app/graphics/opengl/opengl_backend.cpp:19698;host_app/graphics/opengl/opengl_backend.cpp:19977,numeric-other,experiment,Y,R198_ROUND.md;R198_PROFILE.md,delete-both
PS2_R197_D2S,1,host_app/graphics/opengl/opengl_backend.cpp:13026,numeric-other,experiment,,,delete-both
PS2_R197_D2V2,1,host_app/graphics/opengl/opengl_backend.cpp:20722,numeric-other,experiment,,R198_ROUND.md;R198_PROFILE.md,delete-both
PS2_R197_D3FIX,1,host_app/render.h:1830,numeric-other,experiment,,R201_AUDIT_REVIEW.md;R197_PROFILE.md,delete-both
PS2_R197_D3LOG,1,host_app/render.h:2551,numeric-other,diagnostic(one-off),,,delete-both
PS2_R197_DRAWDUMP,1,host_app/graphics_thread.cpp:4121,num-if-set,diagnostic(one-off),,R198_ROUND.md;R201_AUDIT_FINDINGS.md,delete-both
PS2_R197_MERGELOG,1,host_app/render.h:3558,num-if-set,diagnostic(one-off),,,delete-both
PS2_R197_MERGEMAX,1,host_app/render.h:3630,num=-1L,experiment,,,delete-both
PS2_R198_D1FMV,1,host_app/render.h:1930,numeric-other,experiment,,R198_ROUND.md,delete-both
PS2_R198_IDX8CT16,1,host_app/render.h:1912,presence-gated,experiment,,SHADOW2_IMPL.md;PLAYTEST_R1_IMPL.md,delete-both
PS2_R198_PATH3DEFER,1,host_app/gif_arbiter.cpp:55,num=0,experiment,,R198_IMPL.md;R198_PROFILE.md,delete-both
PS2_R198_PATH3DEFER_BASE,1,host_app/gif_arbiter.cpp:62,numeric-other,experiment,,R198_IMPL.md,delete-both
PS2_R198_QORDER,3,host_app/graphics_thread.cpp:569;host_app/gs_state.cpp:4222;host_app/render.h:2704,num-if-set,experiment,Y,R202_FIX1_REVIEW.md;R201_AUDIT_FINDINGS.md,delete-both
PS2_R198_QORDER_BASE,3,host_app/graphics_thread.cpp:576;host_app/gs_state.cpp:4227;host_app/render.h:2709,numeric-other,experiment,Y,R201_AUDIT_FINDINGS.md;R202_FIX1_IMPL.md,delete-both
PS2_R198_QORDER_ZCEN,1,host_app/graphics_thread.cpp:738,num-if-set,experiment,,,delete-both
PS2_R198_RTPROBE,2,host_app/graphics/opengl/opengl_backend.cpp:10699;host_app/graphics/opengl/opengl_backend.cpp:20478,numeric-other,diagnostic(one-off),Y,R198_IMPL.md,delete-both
PS2_R198_XGKPROBE,1,host_app/vu.cpp:11408,numeric-other,diagnostic(one-off),,SHADOW2_IMPL.md;R198_IMPL.md,delete-both
PS2_R198_XGK_WINDOW,2,host_app/vu.cpp:11445;host_app/vu.cpp:11449,num-if-set,experiment,Y,SHADOW2_IMPL.md,delete-both
PS2_R199_UPLOADEAGER,1,host_app/gs_state.cpp:5579,numeric-other,experiment,,R198_ROUND.md,delete-both
PS2_R199_XFERLATCH,1,host_app/gs_state.cpp:152,numeric-other,experiment,,R198_ROUND.md;R202_FIX1_IMPL.md,delete-both
PS2_S103_DRAWLIM,1,host_app/gs_class_renderer.h:440,unresolved,experiment,,CAVERN_DEPTHPATCH_IMPL.md,delete-both
PS2_S103_ZBAND,1,host_app/graphics/opengl/opengl_backend.cpp:6345,unresolved,experiment,,CAVERN_DEPTHPATCH_IMPL.md,delete-both
PS2_S105_SKIP,1,host_app/gs_class_renderer.h:536,unresolved,experiment,,,delete-both
PS2_S106_DIR,1,host_app/graphics/opengl/opengl_backend.cpp:25328,presence-gated,experiment,,CAVERN_FOGCONTENT_IMPL.md,delete-both
PS2_S106_STAGE,2,host_app/graphics/opengl/opengl_backend.cpp:25326;host_app/graphics/opengl/opengl_backend.cpp:25592,presence-gated,experiment,,CAVERN_FOGCONTENT_IMPL.md,delete-both
PS2_S107_LOG,1,host_app/foglane_live.h:72,numeric-other,diagnostic(one-off),,CAVERN_FOGCONTENT_IMPL.md,delete-both
PS2_S13_PCRING,1,host_app/vu.cpp:109,numeric-other,diagnostic(one-off),,SHADOW2_IMPL.md,delete-both
PS2_S13_TBLCAP,1,host_app/vu.cpp:4612,numeric-other,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S13_TBLCAP_ENTRY,1,host_app/vu.cpp:4616,num=8,experiment,,,delete-both
PS2_S13_TBLCAP_N,1,host_app/vu.cpp:4621,num=8,experiment,,,delete-both
PS2_S13_TBLWATCH,2,host_app/vu.cpp:1609;host_app/vu.cpp:4712,numeric-other,diagnostic(one-off),Y,SHADOW2_IMPL.md,delete-both
PS2_S15_HDRCAP,1,host_app/vu.cpp:4936,numeric-other,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S15_HDRCAP_ENTRY,1,host_app/vu.cpp:4940,numeric-other,experiment,,,delete-both
PS2_S15_HDRCAP_MINVF,1,host_app/vu.cpp:4944,num=8,experiment,,,delete-both
PS2_S15_HDRCAP_N,1,host_app/vu.cpp:4948,num=8,experiment,,,delete-both
PS2_S15_VUENT,1,host_app/vu.cpp:4913,num-if-set,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S15_WATCH,1,host_app/memory.cpp:1176,numeric-other,diagnostic(one-off),,SHADOW2_IMPL.md;FREEZE_CHUNKLOAD_REPORT.md,delete-both
PS2_S16_CHAIN,1,host_app/main.cpp:13707,num=400,experiment,,,delete-both
PS2_S16_CHAIN_CAP,1,host_app/main.cpp:13712,num=400,experiment,,,delete-both
PS2_S26_DECAL,4,host_app/dmac.cpp:1969;host_app/gif.cpp:3686;host_app/main.cpp:13868;host_app/vu.cpp:4346,num=400,experiment,Y,SHADOW2_IMPL.md,delete-both
PS2_S26_DECAL_CAP,1,host_app/main.cpp:13872,num=400,experiment,,,delete-both
PS2_S26_FORCEPASS,1,host_app/vu.cpp:4381,numeric-other,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S26_KILL,1,host_app/main.cpp:13880,numeric-other,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S26_UTRACE,1,host_app/vu.cpp:10082,numeric-other,diagnostic(one-off),,,delete-both
PS2_S26_VUDUMP,1,host_app/vu.cpp:4406,presence-gated,diagnostic(one-off),,,delete-both
PS2_S32_GREENTEX,4,host_app/graphics/opengl/opengl_backend.cpp:7108;host_app/graphics/opengl/opengl_backend.cpp:12868;host_app/graphics/opengl/opengl_backend.cpp:19726;host_app/graphics/opengl/opengl_backend.cpp:21823,unresolved,experiment,,TEXCORRUPT_REPORT.md;TEXCORRUPT_REVIEW.md,delete-both
PS2_S83_SEQSCAN,1,host_app/graphics/opengl/opengl_backend.cpp:6427,num-if-set,diagnostic(one-off),,CAVERN_CHECKER_IMPL.md,delete-both
PS2_S83_SKIP0221,1,host_app/graphics/opengl/opengl_backend.cpp:8296,numeric-other,experiment,,CAVERN_CHECKER_IMPL.md,delete-both
PS2_S83_SKIPORD,1,host_app/graphics/opengl/opengl_backend.cpp:8306,unresolved,experiment,,CAVERN_DEPTHPATCH_IMPL.md;CAVERN_CHECKER_IMPL.md,delete-both
PS2_S83_ZGRID,1,host_app/graphics/opengl/opengl_backend.cpp:6238,numeric-other,experiment,,CAVERN_CHECKER_IMPL.md,delete-both
PS2_S8_ARENA_MINVF,1,host_app/dmac.cpp:1935,numeric-other,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S8_FORCECLAIM,1,host_app/main.cpp:13588,OFF-unless-nonzero,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S8_FORCEVIS,1,host_app/vu.cpp:10993,num-if-set,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S8_INRT,1,host_app/s8_inrt.h:38,presence-gated,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S8_MTXCAP,1,host_app/vu.cpp:4832,numeric-other,experiment,,,delete-both
PS2_S8_MTXCAP_EERAM,1,host_app/vu.cpp:4860,unresolved,experiment,,,delete-both
PS2_S8_MTXCAP_MINVF,1,host_app/vu.cpp:4836,num=6,experiment,,,delete-both
PS2_S8_MTXCAP_N,1,host_app/vu.cpp:4840,num=6,experiment,,,delete-both
PS2_S8_MTXWATCH,1,host_app/memory.cpp:1166,num-if-set,diagnostic(one-off),,,delete-both
PS2_S8_PKTDUMP,1,host_app/gif_arbiter.cpp:1496,presence-gated,diagnostic(one-off),,SHADOW2_IMPL.md,delete-both
PS2_S8_PKTDUMP_MINVF,1,host_app/gif_arbiter.cpp:1504,numeric-other,diagnostic(one-off),,SHADOW2_IMPL.md,delete-both
PS2_S8_REGHOOK,1,host_app/main.cpp:13438,numeric-other,experiment,,SHADOW2_IMPL.md,delete-both
PS2_S8_REGHOOK_BATCAP,1,host_app/main.cpp:13453,num=100,experiment,,,delete-both
PS2_S8_REGHOOK_CAP,1,host_app/main.cpp:13447,num=400,experiment,,,delete-both
PS2_S8_RTDUMP,1,host_app/graphics/opengl/opengl_backend.cpp:21200,numeric-other,diagnostic(one-off),,SHADOW2_IMPL.md,delete-both
PS2_S8_TEXDUMP,2,host_app/graphics/opengl/opengl_backend.cpp:83;host_app/graphics/opengl/opengl_backend.cpp:87,num-if-set,diagnostic(one-off),Y,SHADOW2_IMPL.md,delete-both
PS2_S8_VIFTAP,1,host_app/vif.cpp:2553,numeric-other,diagnostic(one-off),,SHADOW2_IMPL.md,delete-both
PS2_S9_DEMOTE,2,host_app/graphics/opengl/opengl_backend.cpp:17130;host_app/graphics/opengl/s9_composite_native.inc:258,num-if-set,experiment,Y,CAVERN_CRYSTAL_IMPL.md,delete-both
PS2_S9_DIAG,1,host_app/graphics/opengl/opengl_backend.cpp:12341,num-if-set,diagnostic(one-off),,SHADOW2_IMPL.md;PLAYTEST_R1_IMPL.md,delete-both
PS2_SH1_DEPEPOCH,1,host_app/render.h:565,numeric-other,experiment,,PLAYTEST_R1_IMPL.md;MIDDLE_NATIVE_IMPL.md,delete-both
PS2_SH1_LASTWRITER,1,host_app/graphics/opengl/opengl_backend.cpp:16379,numeric-other,experiment,,PLAYTEST_R1_IMPL.md,delete-both
PS2_SH1_SNAPPROBE,1,host_app/render.h:595,numeric-other,diagnostic(one-off),,PLAYTEST_R1_IMPL.md,delete-both
PS2_SH1_T8HPROBE,2,host_app/graphics/opengl/opengl_backend.cpp:936;host_app/graphics/opengl/opengl_backend.cpp:19947,numeric-other,diagnostic(one-off),Y,PLAYTEST_R1_IMPL.md,delete-both
PS2_SH2_DRAWDELTA,1,host_app/graphics/opengl/opengl_backend.cpp:12698,numeric-other,experiment,,PLAYTEST_R1_IMPL.md,delete-both
PS2_SH2_T8HDUMP,1,host_app/graphics/opengl/opengl_backend.cpp:12744,numeric-other,diagnostic(one-off),,CAVERN_DEPTHPATCH_IMPL.md;CAVERN_FOGCONTENT_IMPL.md,delete-both
PS2_SH2_T8HTEX,1,host_app/graphics/opengl/opengl_backend.cpp:12683,numeric-other,experiment,,PLAYTEST_R1_IMPL.md;CAVERN_FOGCONTENT_IMPL.md,delete-both
PS2_SH5_ALIASEPOCH,2,host_app/graphics/opengl/opengl_backend.cpp:17830;host_app/render.h:1983,num-if-set,experiment,Y,PLAYTEST_R1_IMPL.md,delete-both
PS2_SH5_BIND,8,host_app/graphics/opengl/opengl_backend.cpp:8900;host_app/graphics/opengl/opengl_backend.cpp:14579;host_app/graphics/opengl/opengl_backend.cpp:15256;host_app/graphics/opengl/opengl_backend.cpp:15522;host_app/graphics/opengl/opengl_backend.cpp:15717;host_app/graphics/opengl/opengl_backend.cpp:17806;(+2 more),num-if-set,experiment,Y,PLAYTEST_R1_IMPL.md,delete-both
PS2_SH5_IDXDIRTY,1,host_app/render.h:1964,numeric-other,experiment,,,delete-both
PS2_SH6_SWEEP,2,host_app/graphics/opengl/opengl_backend.cpp:12454;host_app/graphics/opengl/s9_composite_native.inc:1771,presence-gated,experiment,,PLAYTEST_R1_IMPL.md,delete-both
PS2_SH7,1,host_app/graphics/opengl/opengl_backend.cpp:9013,presence-gated,experiment,,CAVERN_FLICKER_IMPL.md;PLAYTEST_R1_IMPL.md,delete-both
PS2_SH7_DEC,1,host_app/graphics/opengl/opengl_backend.cpp:9255,unresolved,experiment,,PLAYTEST_R1_IMPL.md,delete-both
PS2_SH8,1,host_app/graphics/opengl/world_tristrip_native.inc:293,presence-gated,experiment,,CAVERN_FLICKER_IMPL.md;PLAYTEST_R1_IMPL.md,delete-both
PS2_SHADOW_CTXLATCH,1,host_app/s8_ctxlatch.h:114,presence-gated,experiment,,SHADOW2_IMPL.md;SHADOW2_ROOTCAUSE.md,delete-both
PS2_SHADOW_ORDERFIX,1,host_app/s8_ctxlatch.h:112,presence-gated,experiment,,SHADOW2_IMPL.md,delete-both
PS2_SND_PAUSE_DEFER,1,host_app/sif_rpc_handlers.cpp:3199,OFF-unless-1,experiment,,LOADTIME_IMPL.md,delete-both
PS2_SPU_LAT_US,1,host_app/iop_stubs.cpp:1813,presence-gated,experiment,,TIMING_NATIVE_ROADMAP.md;LOADTIME_IMPL.md,delete-both
PS2_U3_VIEWPORT,1,host_app/display_settings.cpp:185,numeric-other,experiment,,U3_SCREENFIT_REVIEW.md;U3_SCREENFIT_IMPL.md,delete-both
PS2_VTQ,1,host_app/iop_stubs.cpp:1894,OFF-unless-1,experiment,,LOADTIME_REPORT.md,delete-both
PS2_VU0_XCHECK,1,host_app/vu.cpp:5041,OFF-unless-1,experiment,,,delete-both
PS2_VU1_CYCLE_CAP,1,host_app/vu.cpp:458,presence-gated,experiment,,CAVERN_DIRECTIONAL_IMPL.md,delete-both
PS2_VU1_HLE_WHY,2,host_app/hle/vu1_hle_programs.cpp:214;host_app/hle/vu1_hle_programs.cpp:225,OFF-unless-1,experiment,Y,CAVERN_DIRECTIONAL_IMPL.md;CAVERN_CRYSTAL_IMPL.md,delete-both
PS2_VU_AOT_SKELFULL,1,host_app/vu_aot_programs.cpp:47,OFF-unless-1,experiment,,U6_VUCTRL_REPORT.md,delete-both
PS2_VU_RUNFLAT,1,host_app/vu.cpp:575,OFF-unless-1,experiment,,W13_CHECKER.md;W13_GOAL.md,delete-both
PS2_VU_SPANFOLD2,1,host_app/vu.cpp:547,OFF-unless-1,experiment,,PERF_ROUNDS.md;W14_CHECKER.md,delete-both
PS2_W13R7_CENSUS_EVERY,1,host_app/main.cpp:14321,unresolved,diagnostic(one-off),,W13_PROFILE.md,delete-both
PS2_W13R7_NORESEED,1,host_app/savestate.cpp:106,numeric-other,experiment,,W13_PROFILE.md,delete-both
PS2_W13R7_RELOAD_EVERY,1,host_app/main.cpp:14318,unresolved,experiment,,W13_PROFILE.md;CAVERN_MIPBAND_IMPL.md,delete-both
PS2_W13R7_RELOAD_PATH,1,host_app/main.cpp:14324,unresolved,experiment,,,delete-both
PS2_W15_KICKCENSUS,1,host_app/vu.cpp:3998,numeric-other,diagnostic(one-off),,W15_PROFILE.md,delete-both
PS2_W16_SPIKE,1,host_app/graphics_thread.cpp:2443,num=0,experiment,,W16_IMPL.md;W16_PROFILE.md,delete-both
PS2_XFER_FAST,1,host_app/render.h:501,presence-gated,experiment,,MIDDLE_NATIVE_IMPL.md,delete-both
PS2_ARGV,2,host_app/main.cpp:561;host_app/ps2_threading.cpp:825,unresolved,tuning-knob,,SHADOW2_IMPL.md,delete-losing-arm-keep-winner
PS2_ARTDET_EVERY,1,host_app/graphics_thread.cpp:2551,numeric-other,tuning-knob,,R198_ROUND.md;CAVERN_FLICKER_IMPL.md,delete-losing-arm-keep-winner
PS2_ARTDET_FLICKER,1,host_app/artdet_flicker.cpp:257,numeric-other,tuning-knob,,CAVERN_FLICKER_IMPL.md,delete-losing-arm-keep-winner
PS2_ARTDET_GREEN,1,host_app/graphics_thread.cpp:2556,numeric-other,tuning-knob,,CAVERN_FLICKER_IMPL.md,delete-losing-arm-keep-winner
PS2_ARTDET_MIN,1,host_app/graphics_thread.cpp:2564,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_ARTDET_WEIRD,1,host_app/graphics_thread.cpp:2560,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_BB_SNAP,1,host_app/graphics_thread.cpp:3600,unresolved,tuning-knob,,SELECT_IMPL.md;HYGIENE_IMPL.md,delete-losing-arm-keep-winner
PS2_BB_WINDOWS,1,host_app/graphics_thread.cpp:3576,unresolved,tuning-knob,,HYGIENE_IMPL.md;NEWGAME_REVIEW.md,delete-losing-arm-keep-winner
PS2_BUDGET_BASE,1,host_app/main.cpp:3449,num=-1,tuning-knob,,CAVERN_BUDGET_IMPL.md;BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_BUDGET_BASE_LOW,1,host_app/main.cpp:3490,num=-1,tuning-knob,,BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_BUDGET_EMU,1,host_app/main.cpp:3308,num=0,tuning-knob,,CAVERN_BUDGET_IMPL.md;BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_BUDGET_LOWDRAW,1,host_app/main.cpp:3484,num=0,tuning-knob,,BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_BUDGET_PHASE,1,host_app/main.cpp:3479,ON-unless-0,proven-fix-hatch-ON,,BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_BUDGET_STEP,1,host_app/main.cpp:3424,num=0,tuning-knob,,CAVERN_BUDGET_IMPL.md,delete-losing-arm-keep-winner
PS2_CAP_STEPDOWN,1,host_app/present_pace.h:671,numeric-other,tuning-knob,,TIMING_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_CAP_TRIPFIT,1,host_app/present_pace.h:696,numeric-other,tuning-knob,,PERF_ROUNDS.md,delete-losing-arm-keep-winner
PS2_CAP_TRIPFIT_PCT,1,host_app/present_pace.h:707,unresolved,tuning-knob,,,delete-losing-arm-keep-winner
PS2_CAP_UPHOLD_MS,1,host_app/present_pace.h:722,unresolved,tuning-knob,,,delete-losing-arm-keep-winner
PS2_CDVD_BUDGET_KB,1,host_app/ps2_vblank.h:163,numeric-other,tuning-knob,,CRASH_TRIAGE.md;LOADTIME_IMPL.md,delete-losing-arm-keep-winner
PS2_CHUNKQ,1,host_app/hle_heap.cpp:1663,numeric-other,tuning-knob,,LOADTIME_IMPL.md,delete-losing-arm-keep-winner
PS2_CHUNK_APPLY_DORMANT,1,host_app/hle_heap.cpp:1694,numeric-other,tuning-knob,,LOADTIME_IMPL.md,delete-losing-arm-keep-winner
PS2_CHUNK_APPLY_ORDER,1,host_app/hle_heap.cpp:2069,numeric-other,tuning-knob,,TIMING_RESEARCH_BRIEF.md;LOADTIME_IMPL.md,delete-losing-arm-keep-winner
PS2_CHUNK_APPLY_SCOPED,1,host_app/hle_heap.cpp:1644,numeric-other,tuning-knob,,LOADTIME_REVIEW.md;LOADTIME_IMPL.md,delete-losing-arm-keep-winner
PS2_CHUNK_APPLY_VALVE_TICKS,1,host_app/hle_heap.cpp:1709,numeric-other,tuning-knob,,LOADTIME_IMPL.md,delete-losing-arm-keep-winner
PS2_CUELAG_WIN_TICKS,1,host_app/cuelag_probe.h:98,unresolved,tuning-knob,,TIMING_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_EEFEED,1,host_app/w10_eefeed.h:50,numeric-other,tuning-knob,,W10_GOAL.md;W10_IMPL.md,delete-losing-arm-keep-winner
PS2_EE_PACE,1,host_app/ee_pace.h:169,numeric-other,tuning-knob,,W12_PROFILE.md;W12_IMPL.md,delete-losing-arm-keep-winner
PS2_EE_RUNAHEAD,1,host_app/ee_pace.h:118,unresolved,tuning-knob,,INPUT_LAG.md;HEAVY_REVIEW.md,delete-losing-arm-keep-winner
PS2_FASTMEM,1,host_app/memory.h:146,unresolved,tuning-knob,,W10_GOAL.md;W10_PROFILE.md,delete-losing-arm-keep-winner
PS2_FMV3_BLIT,1,host_app/graphics_thread.cpp:1366,numeric-other,tuning-knob,,FMV3_REVIEW.md;FMV3_ROUND.md,delete-losing-arm-keep-winner
PS2_FMV5_SRC,1,host_app/graphics/opengl/opengl_backend.cpp:10614,numeric-other,tuning-knob,,FMV3_REVIEW.md;FMV3_ROUND.md,delete-losing-arm-keep-winner
PS2_FMV_COMPFIX,1,host_app/graphics_thread.cpp:1467,numeric-other,tuning-knob,,FMV3_REVIEW.md;FMV3_ROUND.md,delete-losing-arm-keep-winner
PS2_FMV_NATIVE,1,host_app/anim_pose_hle.cpp:200,ON-unless-0,proven-fix-hatch-ON,,,delete-losing-arm-keep-winner
PS2_FOG_SKIP,1,host_app/graphics/opengl/opengl_backend.cpp:8652,num=0,tuning-knob,,CAVERN_FOG_IMPL.md,delete-losing-arm-keep-winner
PS2_GFX_SLOWJOB,1,host_app/graphics_thread.cpp:496,numeric-other,tuning-knob,,CAVERN_FLICKER_IMPL.md;FMV3_REVIEW.md,delete-losing-arm-keep-winner
PS2_GIF_DRAIN_THREAD,1,host_app/gif_arbiter.h:48,ON-unless-0,proven-fix-hatch-ON,,HUB1_IMPL.md;W13_CHECKER.md,delete-losing-arm-keep-winner
PS2_GIF_FAST,2,host_app/gif.cpp:92;host_app/graphics_thread.cpp:2035,unresolved,tuning-knob,,CAVERN_WALLNOISE_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_ALIAS_ALPHAQUAD,1,host_app/render.h:1490,numeric-other,tuning-knob,,CAVERN_PERF_IMPL.md;BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_ALIAS_DIRTYRECT,1,host_app/graphics/opengl/alias_rect.h:58,numeric-other,tuning-knob,,CAVERN_PERF_IMPL.md;R201_AUDIT_FINDINGS.md,delete-losing-arm-keep-winner
PS2_GL_ALIAS_GPUROUTE,1,host_app/graphics/opengl/opengl_backend.cpp:652,numeric-other,tuning-knob,,W14_CHECKER.md;W14_PROFILE.md,delete-losing-arm-keep-winner
PS2_GL_ALIAS_RECT,1,host_app/graphics/opengl/alias_rect.h:32,ON-unless-0,proven-fix-hatch-ON,,HUB1_IMPL.md;W13_CHECKER.md,delete-losing-arm-keep-winner
PS2_GL_BLENDCP_LAZY,1,host_app/graphics/opengl/rt_copy_tracker.h:40,ON-unless-0,proven-fix-hatch-ON,,HUB1_IMPL.md;W13_CHECKER.md,delete-losing-arm-keep-winner
PS2_GL_BLEND_SUB,1,host_app/graphics/opengl/blend_ff_map.h:182,unresolved,tuning-knob,,PORTAL_IMPL.md;PORTAL_REVIEW.md,delete-losing-arm-keep-winner
PS2_GL_BSTATE_FAST,1,host_app/render.h:1463,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_GL_CDTILES,1,host_app/graphics/opengl/rt_copy_tracker.h:103,numeric-other,tuning-knob,,W14_CHECKER.md;W14_PROFILE.md,delete-losing-arm-keep-winner
PS2_GL_CPBO_REISSUE,1,host_app/graphics/opengl/opengl_backend.cpp:804,numeric-other,tuning-knob,,W14_CHECKER.md;W14_PROFILE.md,delete-losing-arm-keep-winner
PS2_GL_FOGLANE_LIVE,1,host_app/foglane_live.h:64,numeric-other,tuning-knob,,BLACKBOOT_IMPL.md;CAVERN_FOGCONTENT_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_FOGNATIVE,1,host_app/graphics/opengl/opengl_backend.cpp:2412,numeric-other,tuning-knob,,CAVERN_FLICKER_IMPL.md;CAVERN_DEPTHPATCH_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_FRAMEFLUSH,1,host_app/render.h:1536,numeric-other,tuning-knob,,VIS_ROOTCAUSE3.md,delete-losing-arm-keep-winner
PS2_GL_G7C_FLUSH,1,host_app/graphics/opengl/opengl_backend.cpp:730,numeric-other,tuning-knob,,W14_CHECKER.md;W14_PROFILE.md,delete-losing-arm-keep-winner
PS2_GL_G7C_PBOUP,1,host_app/graphics/opengl/opengl_backend.cpp:737,numeric-other,tuning-knob,,W14_CHECKER.md;W14_PROFILE.md,delete-losing-arm-keep-winner
PS2_GL_IDX16_LANE,1,host_app/idx16_lane.h:78,numeric-other,tuning-knob,,PLAYTEST_R1_IMPL.md;VIS_IMPL3.md,delete-losing-arm-keep-winner
PS2_GL_IDXEPOCH,1,host_app/idx_epoch_guard.h:60,numeric-other,tuning-knob,,CAVERN_WALLTEX_RESEARCH.md;PLAYTEST_R1_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_IDXSCALE,1,host_app/graphics/opengl/opengl_backend.cpp:2383,unresolved,tuning-knob,,CAVERN_FLICKER_IMPL.md;CAVERN_DEPTHPATCH_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_IDXTEX_32,1,host_app/render.h:1212,numeric-other,tuning-knob,,VIS_REVIEW.md;VIS_DISTRUST.md,delete-losing-arm-keep-winner
PS2_GL_IDXTEX_CLUTSNAP,1,host_app/render.h:1307,numeric-other,tuning-knob,,VIS_REVIEW.md;VIS_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_IDXTEX_FRESH,1,host_app/render.h:1287,numeric-other,tuning-knob,,VIS_REVIEW.md;VIS_ROOTCAUSE3.md,delete-losing-arm-keep-winner
PS2_GL_L2_MB,1,host_app/graphics/opengl/opengl_backend.cpp:428,unresolved,tuning-knob,,U2_VITA_FEASIBILITY.md;HUB1_PROFILE.md,delete-losing-arm-keep-winner
PS2_GL_L2_PROB,1,host_app/render.h:1544,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_GL_L2_SU,1,host_app/render.h:1569,numeric-other,tuning-knob,,R198_ROUND.md;W13_GOAL.md,delete-losing-arm-keep-winner
PS2_GL_L2_SU_MB,1,host_app/render.h:1578,unresolved,tuning-knob,,,delete-losing-arm-keep-winner
PS2_GL_L2_VERIFY,1,host_app/graphics/opengl/opengl_backend.cpp:22564,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_GL_PAR_ENCODE,1,host_app/graphics/opengl/opengl_backend.cpp:781,numeric-other,tuning-knob,,R198_ROUND.md;R197_ROUND.md,delete-losing-arm-keep-winner
PS2_GL_RESMEMO,2,host_app/graphics/opengl/gl_state_cache.h:809;host_app/uv_bbox.h:80,numeric-other,tuning-knob,Y,W9_impl.md;W9_CHECKER.md,delete-losing-arm-keep-winner
PS2_GL_RESMEMO_STAMPF1,1,host_app/uv_bbox.h:109,numeric-other,tuning-knob,,W9_impl.md,delete-losing-arm-keep-winner
PS2_GL_RT_NEVERSTALE,1,host_app/graphics/opengl/rt_neverstale.h:110,numeric-other,tuning-knob,,CAVERN_CRYSTRIPES_IMPL.md;BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_RT_READVIEW,1,host_app/graphics/opengl/rt_neverstale.h:150,numeric-other,tuning-knob,,CAVERN_CRYSENV_IMPL.md;BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_RUNSUBMIT,1,host_app/graphics/opengl/gl_state_cache.h:286,ON-unless-0,proven-fix-hatch-ON,,W9_GOAL.md;VIS_ROOTCAUSE.md,delete-losing-arm-keep-winner
PS2_GL_SHUF_SNAP,1,host_app/shuf_snap.h:66,numeric-other,tuning-knob,,VIS_IMPL2.md;VIS_REVIEW2.md,delete-losing-arm-keep-winner
PS2_GL_TEXDEMAND,1,host_app/render.h:1601,numeric-other,tuning-knob,,VIS_ROOTCAUSE3.md,delete-losing-arm-keep-winner
PS2_GL_UPLOAD_DEPOSIT,1,host_app/render.h:1876,numeric-other,tuning-knob,,SHADOW2_IMPL.md;R201_AUDIT_FINDINGS.md,delete-losing-arm-keep-winner
PS2_GL_UVNATIVE,1,host_app/graphics/opengl/opengl_backend.cpp:2370,numeric-other,tuning-knob,,CAVERN_DEPTHPATCH_IMPL.md;BLACKBOOT_IMPL.md,delete-losing-arm-keep-winner
PS2_GL_UVREGION,1,host_app/render.h:1610,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_GL_UVREGION_V2,1,host_app/render.h:1623,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_GL_VCONV_FAST,1,host_app/render.h:1413,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_GSK_SIMD,1,host_app/gif.cpp:122,numeric-other,tuning-knob,,W10_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_ABIT16,1,host_app/graphics/opengl/s9_shadow.h:54,unresolved,tuning-knob,,SHADOW2_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_ALT16UV,1,host_app/graphics/opengl/s9_shadow.h:78,unresolved,proven-fix-hatch-ON,,TEXCORRUPT_REPORT.md;SHADOW2_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_CLASSRDR,1,host_app/gs_class_renderer.h:242,numeric-other,tuning-knob,,GS_PHASED_P1_REVIEW.md;CAVERN_FLICKER_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_CLASSRDR_WORLD_SPEC,1,host_app/graphics/opengl/world_tristrip_native.inc:147,numeric-other,tuning-knob,,GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_CLUTRAW,1,host_app/graphics/opengl/s9_shadow.h:70,unresolved,tuning-knob,,SHADOW2_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_CT16_QUANT,1,host_app/graphics/opengl/s9_shadow.h:102,unresolved,tuning-knob,,CAVERN_DEPTHPATCH_IMPL.md;CAVERN_WALLNOISE_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_DRAWIR,1,host_app/hle/vu1_hle_runtime.cpp:122,num-if-set,tuning-knob,,GS_PHASEC_REVIEW.md;CAVERN_FLICKER_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_DRAWIR_VERIFY,1,host_app/hle/vu1_hle_runtime.cpp:149,num-if-set,tuning-knob,,GS_PHASEC_REVIEW.md;GS_PHASEC_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_DRAWMERGE,2,host_app/render.h:2000;host_app/render.h:2032,numeric-other,tuning-knob,Y,HUB1_PROFILE.md;W14_PROFILE.md,delete-losing-arm-keep-winner
PS2_GS_FMVRDR,1,host_app/graphics/opengl/fmv_blit_native.inc:168,numeric-other,tuning-knob,,GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_FMVRDR_SPEC,1,host_app/graphics/opengl/fmv_blit_native.inc:179,numeric-other,tuning-knob,,GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_FOGLANE,2,host_app/graphics/opengl/opengl_backend.cpp:14066;host_app/graphics/opengl/opengl_backend.cpp:20680,numeric-other,tuning-knob,Y,CAVERN_FOG_IMPL.md;CAVERN_DEPTHPATCH_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_GLBATCH,1,host_app/gs_glbatch.h:68,numeric-other,tuning-knob,,GS_PHASEC_REVIEW.md;GS_PHASEC_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_INTERP_SNAP,1,host_app/gs_interp.h:93,numeric-other,tuning-knob,,GS_PHASEE_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_MIPMAP,1,host_app/gs_mipmap.h:64,numeric-other,tuning-knob,,CAVERN_MOTION_IMPL.md;CAVERN_WALLTEX_RESEARCH.md,delete-losing-arm-keep-winner
PS2_GS_MIP_ALLCLASS,1,host_app/gs_mipmap.h:128,numeric-other,tuning-knob,,CAVERN_MOTION_IMPL.md;CAVERN_MINIF_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_MIP_LODREF,1,host_app/gs_mipmap.h:126,numeric-other,tuning-knob,,CAVERN_MINIF_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_MIP_TRILINEAR,1,host_app/gs_mipmap.h:278,numeric-other,tuning-knob,,CAVERN_MOTION_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_PARTRDR,1,host_app/graphics/opengl/particles_native.inc:166,numeric-other,tuning-knob,,GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_PARTRDR_SPEC,1,host_app/graphics/opengl/particles_native.inc:177,numeric-other,tuning-knob,,GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_RAWSKIN,1,host_app/graphics/opengl/skinned_native.inc:238,numeric-other,tuning-knob,,CAVERN_CRYSTAL_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_RTKEEP,1,host_app/graphics/opengl/s9_shadow.h:86,unresolved,proven-fix-hatch-ON,,TEXCORRUPT_REVIEW.md;TEXCORRUPT_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_RUNDRAW,1,host_app/render.h:2029,num-if-set,tuning-knob,,R198_ROUND.md;MIDDLE_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_RUNDRAW_CHECK,1,host_app/render.h:2054,numeric-other,tuning-knob,,W14_PROFILE.md;W14_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_S9RDR,1,host_app/graphics/opengl/s9_composite.h:96,numeric-other,tuning-knob,,CAVERN_FLICKER_IMPL.md;PLAYTEST_R1_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_S9RDR_HOLD,1,host_app/graphics/opengl/s9_composite.h:132,numeric-other,tuning-knob,,CAVERN_FLICKER_IMPL.md;CAVERN_CRYSTAL_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_SKINRDR,1,host_app/graphics/opengl/skinned_native.inc:202,numeric-other,tuning-knob,,CAVERN_MINIF_IMPL.md;CAVERN_CRYSTAL_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_SKINRDR_SPEC,1,host_app/graphics/opengl/skinned_native.inc:224,numeric-other,tuning-knob,,CAVERN_MINIF_IMPL.md;GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_SKINRDR_WORLD2,1,host_app/graphics/opengl/skinned_native.inc:213,numeric-other,tuning-knob,,GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_SPRRDR,1,host_app/graphics/opengl/sprite_hud_native.inc:183,numeric-other,tuning-knob,,GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_SPRRDR_SPEC,1,host_app/graphics/opengl/sprite_hud_native.inc:194,numeric-other,tuning-knob,,GS_PHASED_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_TERM_NATIVE,1,host_app/gs_state.cpp:2367,ON-unless-0,proven-fix-hatch-ON,,CAVERN_FLICKER_IMPL.md;MIDDLE_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_TYPED_BULK,1,host_app/gs_state.cpp:2600,ON-unless-0,proven-fix-hatch-ON,,CAVERN_FLICKER_IMPL.md;MIDDLE_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_WRAPBLEND,1,host_app/graphics/opengl/blend_ff_map.h:232,unresolved,proven-fix-hatch-ON,,SHADOW2_IMPL.md;CAVERN_CHECKER_IMPL.md,delete-losing-arm-keep-winner
PS2_GS_ZSRC,1,host_app/graphics/opengl/s9_shadow.h:62,unresolved,tuning-knob,,SHADOW2_IMPL.md,delete-losing-arm-keep-winner
PS2_HB_TICKCLOCK,1,host_app/iop_stubs.cpp:1175,numeric-other,tuning-knob,,CAVERN_TIMEDOMAIN_IMPL.md;LOADTIME_REPORT.md,delete-losing-arm-keep-winner
PS2_INPUT,1,host_app/input/input_system.cpp:214,unresolved,tuning-knob,,TIM0_IMPL.md;R198_ROUND.md,delete-losing-arm-keep-winner
PS2_INPUT_LAT,1,host_app/input/input_latency.h:18,unresolved,tuning-knob,,PAUSE_IMPL.md;W12_PROFILE.md,delete-losing-arm-keep-winner
PS2_INTERP_COHERENT,1,host_app/gs_interp.h:139,ON-unless-0,proven-fix-hatch-ON,,CAVERN_FLICKER_IMPL.md,delete-losing-arm-keep-winner
PS2_INTERP_PHASEFIX,1,host_app/gs_interp.h:112,ON-unless-0,proven-fix-hatch-ON,,CAVERN_PIXEL_IMPL.md;CAVERN_PIXEL_REVIEW.md,delete-losing-arm-keep-winner
PS2_LIBC_HLE,1,host_app/libc_hle.cpp:440,ON-unless-0,proven-fix-hatch-ON,,,delete-losing-arm-keep-winner
PS2_LOADER_HLE,1,host_app/hle/loader_sync_hle.h:99,numeric-other,tuning-knob,,LOADER_HLE_BRIEF.md;CAVERN_DETAILPASS_ANALYSIS.md,delete-losing-arm-keep-winner
PS2_LOADER_ORACLE_DIR,1,host_app/hle/loader_oracle.h:70,unresolved,tuning-knob,,,delete-losing-arm-keep-winner
PS2_LOADSPAN_DRAWMAX,3,host_app/loadspan.h:105;host_app/loadspan.h:106;host_app/loadspan.h:115,numeric-other,tuning-knob,Y,LOADTIME_REPORT.md,delete-losing-arm-keep-winner
PS2_MC_PERSIST,1,host_app/sce_hle/libmc.cpp:96,numeric-other,tuning-knob,,R202_FIX1_IMPL.md;R202_FIX1_INVEST.md,delete-losing-arm-keep-winner
PS2_MID_TEMPLATE_FORCEMISS,1,host_app/gif_template.h:63,numeric-other,tuning-knob,,MIDDLE_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_MIPCEN,5,host_app/gif.cpp:5043;host_app/graphics/opengl/opengl_backend.cpp:10332;host_app/gs_mipmap.h:187;host_app/gs_mipmap.h:326;host_app/gs_state.cpp:3067,num-if-set,tuning-knob,Y,CAVERN_WALLTEX_RESEARCH.md;CAVERN_MINIF_IMPL.md,delete-losing-arm-keep-winner
PS2_MPEG_PACE_OFF,1,host_app/mpeg_hle.cpp:566,num-if-set,tuning-knob,,FMV3_REVIEW.md;FMV3_ROUND.md,delete-losing-arm-keep-winner
PS2_MPEG_PDIFF,1,host_app/mpeg_hle.cpp:2056,numeric-other,tuning-knob,,FMV3_REVIEW.md,delete-losing-arm-keep-winner
PS2_MPEG_WM,1,host_app/mpeg_hle.cpp:2192,num=0,tuning-knob,,FMV3_REVIEW.md;FMV3_ROUND.md,delete-losing-arm-keep-winner
PS2_MTVU_DEFER,2,host_app/ee_pace.h:173;host_app/vu1_async.cpp:1656,numeric-other,tuning-knob,Y,W8_CHECKER.md;HUB1_IMPL.md,delete-losing-arm-keep-winner
PS2_MTVU_DYNDEPTH,1,host_app/runahead_guard.h:108,unresolved,proven-fix-hatch-ON,,PERF_ROUNDS.md,delete-losing-arm-keep-winner
PS2_MTVU_EAGER_DRAIN,1,host_app/vu1_async.cpp:1537,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_MTVU_MAX_MB,1,host_app/vu1_async.cpp:187,unresolved,tuning-knob,,W12_PROFILE.md;E2_SAVE_REVIEW.md,delete-losing-arm-keep-winner
PS2_MTVU_PATHSYNC_RELAX,1,host_app/vu1_async.cpp:1642,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_MTVU_SPIN,1,host_app/vu1_async.cpp:610,numeric-other,tuning-knob,,R166_impl.md;W8_RUNNER.md,delete-losing-arm-keep-winner
PS2_MTVU_SPIN_NS,1,host_app/vu1_async.cpp:628,unresolved,tuning-knob,,,delete-losing-arm-keep-winner
PS2_MTVU_VBSYNC,1,host_app/vu1_async.cpp:1620,ON-unless-0,proven-fix-hatch-ON,,,delete-losing-arm-keep-winner
PS2_MUSQ_VFMIN,2,host_app/iop_stubs.cpp:1390;host_app/iop_stubs.cpp:1418,numeric-other,tuning-knob,Y,SOUND_P8_REVIEW6E.md;SOUND_P8_IMPL.md,delete-losing-arm-keep-winner
PS2_NO_QOS,1,host_app/thread_qos.h:12,numeric-other,tuning-knob,,W13_PROFILE.md,delete-losing-arm-keep-winner
PS2_PACE_FMV_BYPASS,1,host_app/present_pace.h:662,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_PACE_LOADGRACE,1,host_app/present_pace.h:684,numeric-other,tuning-knob,,PERF_ROUNDS.md,delete-losing-arm-keep-winner
PS2_PACE_SMOOTH,2,host_app/present_pace.h:641;host_app/present_pace.h:652,numeric-other,tuning-knob,Y,PACE_REVIEW.md;PACE_IMPL.md,delete-losing-arm-keep-winner
PS2_PRESENT_EDGE,1,host_app/ps2_vblank.cpp:446,numeric-other,tuning-knob,,CRASH_TRIAGE.md,delete-losing-arm-keep-winner
PS2_QUANTUM,1,host_app/quantum.h:73,numeric-other,tuning-knob,,PLAYTEST_R1_IMPL.md;TIMING_NATIVE_ROADMAP.md,delete-losing-arm-keep-winner
PS2_RQ_HIGHWATER,1,host_app/render.h:3136,num=0,tuning-knob,,CAVERN_FLICKER_IMPL.md;W9_ROOTCAUSE.md,delete-losing-arm-keep-winner
PS2_RQ_MAXJOBS,1,host_app/ee_pace.h:144,unresolved,tuning-knob,,CAVERN_FLICKER_IMPL.md,delete-losing-arm-keep-winner
PS2_RQ_MAXMB,1,host_app/ee_pace.h:156,unresolved,tuning-knob,,,delete-losing-arm-keep-winner
PS2_RQ_SAFETY,1,host_app/ee_pace.h:131,unresolved,tuning-knob,,CAVERN_FLICKER_IMPL.md,delete-losing-arm-keep-winner
PS2_RUNAHEAD_ALARM,1,host_app/runahead_guard.h:77,unresolved,tuning-knob,,,delete-losing-arm-keep-winner
PS2_SAVE_ATOMIC,1,host_app/savestate_format.h:39,unresolved,tuning-knob,,W14_CHECKER.md;SOUND_P5_REPORT.md,delete-losing-arm-keep-winner
PS2_SAVE_SLOT,2,host_app/main.cpp:14271;host_app/savestate.cpp:338,num=0,tuning-knob,,MIDDLE_NATIVE_IMPL.md;RECOMP_P3B_IMPL.md,delete-losing-arm-keep-winner
PS2_SDR_BLOCK_MS,1,host_app/sce_hle/libsdr.cpp:113,num=8,tuning-knob,,FMV3_REVIEW.md;SOUND_P5_REPORT.md,delete-losing-arm-keep-winner
PS2_SHADOW_TAILFIX,1,host_app/gif_arbiter.cpp:160,numeric-other,tuning-knob,,SHADOW2_IMPL.md,delete-losing-arm-keep-winner
PS2_SLOWSPOT_ASYNC,1,host_app/savestate_writer.h:55,unresolved,tuning-knob,,U5_BUGREPORT_REPORT.md;W16_CHECKER.md,delete-losing-arm-keep-winner
PS2_SLOWSPOT_DEDUPE,1,host_app/slowspot.cpp:62,unresolved,tuning-knob,,U5_BUGREPORT_REPORT.md;U5_BUGREPORT_IMPL.md,delete-losing-arm-keep-winner
PS2_SLOWSPOT_SUFFIX,1,host_app/slowspot_paths.h:29,unresolved,tuning-knob,,W14_CHECKER.md;U4_SAVESTATE_REPORT.md,delete-losing-arm-keep-winner
PS2_SNAP,1,host_app/graphics_thread.cpp:218,unresolved,tuning-knob,,W8_CHECKER.md;FREEZE_REVIEW.md,delete-losing-arm-keep-winner
PS2_SNAP_DIR,1,host_app/graphics_thread.cpp:232,unresolved,tuning-knob,,TEXCORRUPT_REPORT.md;R198_ROUND.md,delete-losing-arm-keep-winner
PS2_SNAP_DISP_ONLY,1,host_app/graphics_thread.cpp:292,numeric-other,tuning-knob,,TEXCORRUPT_REPORT.md;CAVERN_MOTION_IMPL.md,delete-losing-arm-keep-winner
PS2_SNAP_RT,1,host_app/graphics_thread.cpp:320,numeric-other,tuning-knob,,CAVERN_DEPTHPATCH_IMPL.md;CAVERN_SHADE_IMPL.md,delete-losing-arm-keep-winner
PS2_SND_EAGER_HOLD,1,host_app/sound/sound_core.cpp:353,ON-unless-0,proven-fix-hatch-ON,,LOADTIME_REVIEW.md;LOADTIME_IMPL.md,delete-losing-arm-keep-winner
PS2_SND_FMV,1,host_app/mpeg_hle.cpp:1282,numeric-other,tuning-knob,,SOUND_P5_IMPL.md,delete-losing-arm-keep-winner
PS2_SND_FMVPACE,1,host_app/sce_hle/libsdr.cpp:143,numeric-other,tuning-knob,,SOUND_P5_IMPL.md,delete-losing-arm-keep-winner
PS2_SND_FMVQ_DIV,1,host_app/sce_hle/libsdr.cpp:153,num=2,tuning-knob,,SOUND_P5_IMPL.md,delete-losing-arm-keep-winner
PS2_SND_PROCBATCH,1,host_app/iop_stubs.cpp:1506,ON-unless-0,proven-fix-hatch-ON,,PLAYTEST_R1_IMPL.md,delete-losing-arm-keep-winner
PS2_SND_STATEHASH,1,host_app/savestate.cpp:369,numeric-other,tuning-knob,,SOUND_P5_IMPL.md;SOUND_P5_REPORT.md,delete-losing-arm-keep-winner
PS2_SPIKE_GUARD,2,host_app/gs_state.cpp:1931;host_app/gs_state.cpp:2374,num-if-set,tuning-knob,Y,MIDDLE_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_STALL_INJECT,1,host_app/graphics_thread.cpp:2211,unresolved,tuning-knob,,CAVERN_BUDGET_IMPL.md;CAVERN_TIMEDOMAIN_IMPL.md,delete-losing-arm-keep-winner
PS2_T0_NOISR,1,host_app/ee_timers.cpp:482,numeric-other,tuning-knob,,CAVERN_TIMEDOMAIN_IMPL.md;CAVERN_TILEPATCH_IMPL.md,delete-losing-arm-keep-winner
PS2_T0_SUBTICK,1,host_app/ee_timers.cpp:113,presence-gated,proven-fix-hatch-ON,,CAVERN_TIMEDOMAIN_IMPL.md;TIMING_RESEARCH_BRIEF.md,delete-losing-arm-keep-winner
PS2_TEXPOP_ALL,1,host_app/graphics/opengl/opengl_backend.cpp:7816,numeric-other,tuning-knob,,CAVERN_BUDGET_IMPL.md;CAVERN_BAKE_IMPL.md,delete-losing-arm-keep-winner
PS2_TEXPOP_GLHASH,1,host_app/graphics/opengl/opengl_backend.cpp:7969,numeric-other,tuning-knob,,CAVERN_TILEPATCH_IMPL.md,delete-losing-arm-keep-winner
PS2_TEXVAL,1,host_app/graphics/opengl/opengl_backend.cpp:5886,numeric-other,tuning-knob,,R202_FIXPLAN.md;MIDDLE_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_TIME_EMU,1,host_app/time_domain.h:76,unresolved,tuning-knob,,CAVERN_BUDGET_IMPL.md;CAVERN_TIMEDOMAIN_IMPL.md,delete-losing-arm-keep-winner
PS2_TIME_EMU_INTRA,1,host_app/ee_timers.cpp:154,numeric-other,tuning-knob,,CAVERN_BUDGET_IMPL.md;CAVERN_TIMEDOMAIN_IMPL.md,delete-losing-arm-keep-winner
PS2_TIME_EMU_REGIME,1,host_app/time_domain.h:93,numeric-other,tuning-knob,,CAVERN_BUDGET_IMPL.md;CAVERN_TIMEDOMAIN_IMPL.md,delete-losing-arm-keep-winner
PS2_TURBO,1,host_app/turbo.h:194,numeric-other,tuning-knob,,TIMING_NATIVE_ROADMAP.md;TIMING_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_TURBO_DUCK,1,host_app/turbo.h:252,unresolved,tuning-knob,,TIMING_NATIVE_IMPL.md,delete-losing-arm-keep-winner
PS2_U32POKE,1,host_app/pad_input.cpp:700,unresolved,tuning-knob,,PAUSE_IMPL.md,delete-losing-arm-keep-winner
PS2_VIF_UNPACK_SIMD,1,host_app/vif.h:40,numeric-other,tuning-knob,,W10_GOAL.md;W10_IMPL.md,delete-losing-arm-keep-winner
PS2_VIF_UNPKBX,1,host_app/vif.h:91,numeric-other,tuning-knob,,W10_GOAL.md;W10_IMPL.md,delete-losing-arm-keep-winner
PS2_VRAM_EPOCH_REACT,1,host_app/gs_state.cpp:4763,numeric-other,tuning-knob,,,delete-losing-arm-keep-winner
PS2_VU0_SHADOWGATE,1,host_app/vu.cpp:4460,num=0,tuning-knob,,,delete-losing-arm-keep-winner
PS2_VU0_VISCAP,1,host_app/vu.cpp:4548,num=64,tuning-knob,,,delete-losing-arm-keep-winner
PS2_VU0_VISCAP_MINVF,1,host_app/vu.cpp:4551,num=64,tuning-knob,,,delete-losing-arm-keep-winner
PS2_VU0_VISCAP_N,1,host_app/vu.cpp:4555,num=64,tuning-knob,,,delete-losing-arm-keep-winner
PS2_VU1_CRYS_GATE,1,host_app/hle/vu1_hle_programs.cpp:267,unresolved,tuning-knob,,CAVERN_DIRECTIONAL_IMPL.md,delete-losing-arm-keep-winner
PS2_VU1_EXEC_ASYNC,1,host_app/vu1_async.cpp:1040,unresolved,tuning-knob,,W13_CHECKER.md;W13_IMPL.md,delete-losing-arm-keep-winner
PS2_VU1_EXEC_IDLE_SPIN_NS,1,host_app/vu1_async.cpp:390,unresolved,tuning-knob,,W13_CHECKER.md;W13_IMPL.md,delete-losing-arm-keep-winner
PS2_VU1_EXEC_WAIT_SPIN_NS,1,host_app/vu1_async.cpp:400,unresolved,tuning-knob,,W13_CHECKER.md;W13_IMPL.md,delete-losing-arm-keep-winner
PS2_VU1_HLE_DEBUG,1,host_app/hle/vu1_hle_runtime.cpp:222,num-if-set,tuning-knob,,VU1_HLE_R1_IMPL.md;CAVERN_SLOWSPOTS_IMPL.md,delete-losing-arm-keep-winner
PS2_VU1_HLE_HASHEPOCH,1,host_app/hle/vu1_hle_runtime.cpp:182,numeric-other,tuning-knob,,CAVERN_PERF_IMPL.md,delete-losing-arm-keep-winner
PS2_VU1_LINK11,1,host_app/vu_fast_exec.h:113,unresolved,tuning-knob,,CAVERN_DIRECTIONAL_IMPL.md,delete-losing-arm-keep-winner
PS2_VUAOT_COLLECT_DIR,1,host_app/main.cpp:5020,unresolved,tuning-knob,,U6_VUCTRL_REVIEW.md;U6_VUCTRL_IMPL.md,delete-losing-arm-keep-winner
PS2_VUAOT_INI,3,host_app/gui/vu_microcode_section.cpp:79;host_app/gui/vu_microcode_section.cpp:163;host_app/main.cpp:5002,unresolved,tuning-knob,,U6_VUCTRL_REVIEW.md;U6_VUCTRL_IMPL.md,delete-losing-arm-keep-winner
PS2_VU_AOT,1,host_app/vu_aot_programs.cpp:37,unresolved,proven-fix-hatch-ON,,HUB1_IMPL.md;U6_VUCTRL_REPORT.md,delete-losing-arm-keep-winner
PS2_VU_AOT_SPANFOLD,1,host_app/vu_aot_programs.cpp:70,numeric-other,proven-fix-hatch-ON,,U6_VUCTRL_REPORT.md;PERF_ROUNDS.md,delete-losing-arm-keep-winner
PS2_VU_CLAMPELIDE,1,host_app/vu_fast_exec.h:83,numeric-other,proven-fix-hatch-ON,,U6_VUCTRL_REPORT.md;PERF_ROUNDS.md,delete-losing-arm-keep-winner
PS2_VU_FASTPACK,1,host_app/vu.cpp:443,unresolved,tuning-knob,,U6_VUCTRL_REPORT.md;R166_impl.md,delete-losing-arm-keep-winner
PS2_VU_HASHEPOCH,1,host_app/vu.cpp:2474,numeric-other,tuning-knob,,U6_VUCTRL_REPORT.md;U6_VU_BGJIT_DESIGN.md,delete-losing-arm-keep-winner
PS2_VU_LAZYFLAGS,1,host_app/vu.cpp:873,numeric-other,tuning-knob,,SHADOW2_IMPL.md;R166_impl.md,delete-losing-arm-keep-winner
PS2_VU_NOCLAMP,1,host_app/vu_fast_exec.h:61,num-if-set,tuning-knob,,W16_CHECKER.md;W16_GOAL.md,delete-losing-arm-keep-winner
PS2_VU_PACE_NEXTDUE,1,host_app/vu.cpp:747,numeric-other,tuning-knob,,W13_CHECKER.md;W13_IMPL.md,delete-losing-arm-keep-winner
PS2_VU_RUNPACE,1,host_app/vu.cpp:755,unresolved,tuning-knob,,W13_CHECKER.md;W14_CHECKER.md,delete-losing-arm-keep-winner
PS2_VU_RUNRETIRE,1,host_app/vu.cpp:903,numeric-other,proven-fix-hatch-ON,,PERF_ROUNDS.md,delete-losing-arm-keep-winner
PS2_VU_SPANFOLD,1,host_app/vu.cpp:526,unresolved,tuning-knob,,W13_CHECKER.md;PERF_ROUNDS.md,delete-losing-arm-keep-winner
PS2_VU_STALLFOLD,1,host_app/vu.cpp:510,unresolved,tuning-knob,,W13_CHECKER.md;W13_GOAL.md,delete-losing-arm-keep-winner
PS2_VU_STALLSPEC,1,host_app/vu.cpp:497,unresolved,tuning-knob,,W13_CHECKER.md;W13_IMPL.md,delete-losing-arm-keep-winner
PS2_VU_SUPERBLOCK,1,host_app/vu.cpp:478,unresolved,tuning-knob,,W13_CHECKER.md;W13_IMPL.md,delete-losing-arm-keep-winner
PS2_VU_XGKPACE_QUANTUM,1,host_app/vu.cpp:738,num=32,tuning-knob,,R166_impl.md;W12_IMPL.md,delete-losing-arm-keep-winner
PS2_WAITFIX,1,host_app/timeline.h:54,numeric-other,tuning-knob,,CRASH_TRIAGE.md;W8_SOLUTION.md,delete-losing-arm-keep-winner
PS2_CDVD_INSTANT,1,host_app/ps2_vblank.cpp:469,presence-gated,shipping-setting,,W8_CHECKER.md;FREEZE_REVIEW.md,keep-as-setting
PS2_DVD_LATENCY_SCALE,1,host_app/hle_heap.cpp:2765,presence-gated,shipping-setting,,W8_CHECKER.md;FREEZE_REVIEW.md,keep-as-setting
PS2_EE_TIM0,1,host_app/ee_timers.h:46,unresolved,shipping-setting,,TIM0_IMPL.md;CAVERN_TIMEDOMAIN_IMPL.md,keep-as-setting
PS2_EULERTRACE,1,host_app/main.cpp:1050,unresolved,shipping-setting,,SLIDEJUMP_R2_REPORT.md;SLIDEJUMP_IMPL.md,keep-as-setting
PS2_FPS_CAP,1,host_app/present_pace.h:629,unresolved,shipping-setting,,CAVERN_PIXEL_IMPL.md;CAVERN_MOTION_IMPL.md,keep-as-setting
PS2_GL_FAST,4,host_app/render.h:449;host_app/render.h:503;host_app/render.h:2489;host_app/texture_decoder.h:28,presence-gated,shipping-setting,Y,W8_CHECKER.md;FREEZE_REVIEW.md,keep-as-setting
PS2_GL_FAST2,2,host_app/graphics/opengl/gl_state_cache.h:84;host_app/render.h:753,presence-gated,shipping-setting,Y,W8_CHECKER.md;FREEZE_REVIEW.md,keep-as-setting
PS2_GS_INTERP,2,host_app/artdet_flicker.cpp:234;host_app/gs_interp.h:78,num=0,shipping-setting,,CAVERN_PIXEL_IMPL.md;CAVERN_MOTION_IMPL.md,keep-as-setting
PS2_GS_SCALE,4,host_app/artdet_flicker.cpp:233;host_app/graphics/opengl/gs_scale.h:45;host_app/gs_interp.h:423;host_app/gs_interp.h:577,presence-gated,shipping-setting,Y,CAVERN_PIXEL_IMPL.md;CAVERN_MOTION_IMPL.md,keep-as-setting
PS2_GUI_OPEN,1,host_app/main.cpp:5047,unresolved,shipping-setting,,U3_SCREENFIT_REVIEW.md;U6_VUCTRL_REPORT.md,keep-as-setting
PS2_HEADWATCH,1,host_app/pad_input.cpp:604,unresolved,shipping-setting,,RECOMP_P3B_REVIEW.md;PLAYTEST_R1_IMPL.md,keep-as-setting
PS2_LOAD_BUDGET_LIFT,1,host_app/budget_lift.h:105,numeric-other,shipping-setting,,LOADTIME_IMPL.md,keep-as-setting
PS2_LOAD_TURBO,1,host_app/turbo.h:285,numeric-other,shipping-setting,,LOADTIME_IMPL.md;LOADTIME_REPORT.md,keep-as-setting
PS2_LOG,1,host_app/main.cpp:4905,numeric-other,shipping-setting,,W8_CHECKER.md;W11_GOAL.md,keep-as-setting
PS2_MC_LATENCY,1,host_app/sce_hle/libmc.cpp:196,num=4,shipping-setting,,U1_WINDOWS_REPORT.md,keep-as-setting
PS2_MTVU,2,host_app/ee_pace.h:175;host_app/vu1_async.cpp:1011,numeric-other,shipping-setting,Y,W8_CHECKER.md;SOUND_P8_REVIEW6E.md,keep-as-setting
PS2_MTVU_MAX_FRAMES,1,host_app/runahead_guard.h:66,unresolved,shipping-setting,,CAVERN_WALLTEX_RESEARCH.md;W12_PROFILE.md,keep-as-setting
PS2_NOVSYNC,1,host_app/render.h:871,numeric-other,shipping-setting,,W8_CHECKER.md;FREEZE_REVIEW.md,keep-as-setting
PS2_PERF,26,host_app/ee_pace.h:184;host_app/ee_timers.cpp:116;host_app/gif_arbiter.h:635;host_app/mpeg_hle.cpp:462;host_app/mpeg_hle.cpp:534;host_app/mpeg_hle.cpp:598;(+20 more),num-if-set,shipping-setting,Y,W8_CHECKER.md;W11_GOAL.md,keep-as-setting
PS2_SHUTDOWN_WAIT,1,host_app/shutdown_policy.h:62,unresolved,shipping-setting,,INPUTLAG_IMPL.md,keep-as-setting
PS2_SLOWSPOT,3,host_app/slowspot.cpp:36;host_app/slowspot.cpp:39;host_app/slowspot.cpp:55,unresolved,shipping-setting,,U4_SAVESTATE_IMPL.md;U5_BUGREPORT_REPORT.md,keep-as-setting
PS2_SNDREG,1,host_app/hle_heap.cpp:685,numeric-other,shipping-setting,,SOUND_P8_REVIEW6E.md;SOUND_P2_IMPL.md,keep-as-setting
PS2_SOUND,1,host_app/sound/sound_core.cpp:187,numeric-other,shipping-setting,,SOUND_P8_REVIEW6E.md;SOUND_REPORT.md,keep-as-setting
PS2_VBLANK_FREERUN,1,host_app/ps2_vblank.cpp:411,presence-gated,shipping-setting,,W8_CHECKER.md;FREEZE_REVIEW.md,keep-as-setting
PS2_VIF_FAST,2,host_app/graphics_thread.cpp:2021;host_app/vif.cpp:801,numeric-other,shipping-setting,,W8_CHECKER.md;FREEZE_REVIEW.md,keep-as-setting
PS2_VU1_HLE,1,host_app/hle/vu1_hle_runtime.cpp:61,num-if-set,shipping-setting,,CAVERN_PIXEL_IMPL.md;CAVERN_MOTION_IMPL.md,keep-as-setting
PS2_VU1_HLE_DIRECT,1,host_app/hle/vu1_hle_runtime.cpp:90,num-if-set,shipping-setting,,CAVERN_PIXEL_IMPL.md;CAVERN_MOTION_IMPL.md,keep-as-setting
PS2_VU_MODE,1,host_app/vu.cpp:373,ON-unless-0,shipping-setting,,W8_CHECKER.md;FREEZE_REVIEW.md,keep-as-setting
PS2_WARP_TO,1,host_app/warp.h:102,presence-gated,shipping-setting,,,keep-as-setting
CT_FULL_LOG,1,host_app/main.cpp:4943,OFF-unless-1,diagnostic,,MIDDLE_NATIVE_IMPL.md,move-to-diagnostics-build
LOGO_MEMSNAP,1,host_app/manual_switch_dispatchers.cpp:778,OFF-unless-nonzero,diagnostic,,,move-to-diagnostics-build
LOGO_MEMSNAP_FRAMES,1,host_app/manual_switch_dispatchers.cpp:783,num-if-set,diagnostic,,,move-to-diagnostics-build
PS2_ARTDET,2,host_app/artdet_flicker.cpp:255;host_app/graphics_thread.cpp:2547,numeric-other,diagnostic,Y,TEXCORRUPT_REPORT.md;R198_ROUND.md,move-to-diagnostics-build
PS2_ARTDET_DIAG,2,host_app/graphics/opengl/opengl_backend.cpp:8452;host_app/graphics_thread.cpp:2654,num-if-set,diagnostic,Y,TEXCORRUPT_REPORT.md,move-to-diagnostics-build
PS2_BAKE_DIAG,8,host_app/dmac.cpp:1201;host_app/gs.cpp:510;host_app/gs_state.cpp:2867;host_app/gs_state.cpp:4178;host_app/gs_state.cpp:4331;host_app/gs_state.cpp:4867;(+2 more),num-if-set,diagnostic,Y,CAVERN_BAKE_IMPL.md,move-to-diagnostics-build
PS2_BAKE_DUMP,1,host_app/gs_state.cpp:4356,presence-gated,diagnostic,,,move-to-diagnostics-build
PS2_BAKE_SRCSCAN,1,host_app/gs_state.cpp:4376,unresolved,diagnostic,,,move-to-diagnostics-build
PS2_BOOT_WATCH,1,host_app/main.cpp:5074,num-if-set,diagnostic,,,move-to-diagnostics-build
PS2_BTRACE,1,host_app/bullseye_trace.cpp:59,unresolved,diagnostic,,,move-to-diagnostics-build
PS2_BUDGET_DIAG,1,host_app/main.cpp:3315,num=0,diagnostic,,CAVERN_BUDGET_IMPL.md;BLACKBOOT_IMPL.md,move-to-diagnostics-build
PS2_BUGREPORT,1,host_app/bug_report.cpp:75,unresolved,diagnostic,,U5_BUGREPORT_REPORT.md;U5_BUGREPORT_IMPL.md,move-to-diagnostics-build
PS2_BUGREPORT_SLOWSPOT,1,host_app/bug_report.cpp:94,unresolved,diagnostic,,U5_BUGREPORT_REPORT.md;U5_BUGREPORT_IMPL.md,move-to-diagnostics-build
PS2_BUGREPORT_SLOWSPOT_MAX,1,host_app/bug_report.cpp:83,numeric-other,diagnostic,,U5_BUGREPORT_REPORT.md;U5_BUGREPORT_IMPL.md,move-to-diagnostics-build
PS2_CALL_CENSUS,1,host_app/call_census.cpp:232,numeric-other,diagnostic,,W10_GOAL.md;W10_IMPL.md,move-to-diagnostics-build
PS2_CALL_CENSUS_ROOT,1,host_app/call_census.cpp:235,numeric-other,diagnostic,,W10_GOAL.md;W10_CHECKER.md,move-to-diagnostics-build
PS2_CALL_CENSUS_SLOWSEG,1,host_app/call_census.cpp:242,num-if-set,diagnostic,,W10_GOAL.md;W10_IMPL.md,move-to-diagnostics-build
PS2_CLEAR_DIAG,1,host_app/graphics/opengl/opengl_backend.cpp:9060,numeric-other,diagnostic,,,move-to-diagnostics-build
PS2_CRASHRING,1,host_app/ring_log.cpp:104,unresolved,diagnostic,,HYGIENE_IMPL.md;NEWGAME_REVIEW.md,move-to-diagnostics-build
PS2_CRYSENV_DIAG,1,host_app/graphics/opengl/opengl_backend.cpp:10424,unresolved,diagnostic,,CAVERN_CRYSENV_IMPL.md,move-to-diagnostics-build
PS2_CRYSENV_DUMPDIR,1,host_app/graphics/opengl/opengl_backend.cpp:10576,unresolved,diagnostic,,CAVERN_CRYSENV_IMPL.md,move-to-diagnostics-build
PS2_DRAWLIST_RECORD,1,host_app/hle/drawlist_record.cpp:96,numeric-other,diagnostic,,CAVERN_FOG_IMPL.md;GS_NATIVE_PLAN.md,move-to-diagnostics-build
PS2_DRAWLIST_RECORD_FRAMES,1,host_app/hle/drawlist_record.cpp:101,unresolved,diagnostic,,,move-to-diagnostics-build
PS2_DRAWLIST_RECORD_PIXELS,1,host_app/hle/drawlist_record.cpp:99,numeric-other,diagnostic,,CAVERN_FOG_IMPL.md;CAVERN_SLOWSPOTS_IMPL.md,move-to-diagnostics-build
PS2_DRAWLIST_RECORD_PPM,1,host_app/hle/drawlist_record.cpp:100,numeric-other,diagnostic,,CAVERN_WALLALT_IMPL.md;CAVERN_TEXPOP_IMPL.md,move-to-diagnostics-build
PS2_DRAWLIST_RECORD_VERTS,1,host_app/hle/drawlist_record.cpp:98,numeric-other,diagnostic,,CAVERN_MINIF_IMPL.md;GS_PHASEA_IMPL.md,move-to-diagnostics-build
PS2_DRAW_DIAG,1,host_app/graphics/opengl/opengl_backend.cpp:8439,unresolved,diagnostic,,HYGIENE_IMPL.md;MENU_VIS.md,move-to-diagnostics-build
PS2_EDGEPROBE,1,host_app/main.cpp:1204,unresolved,diagnostic,,PAUSE_IMPL.md,move-to-diagnostics-build
PS2_EESTALL_CENSUS,1,host_app/timeline.h:133,OFF-unless-nonzero,diagnostic,,W10_IMPL.md,move-to-diagnostics-build
PS2_FMV5_SCAN,1,host_app/graphics/opengl/opengl_backend.cpp:18558,num-if-set,diagnostic,,FMV3_REVIEW.md,move-to-diagnostics-build
PS2_FOG_DIAG,1,host_app/graphics/opengl/opengl_backend.cpp:15470,numeric-other,diagnostic,,CAVERN_FOG_IMPL.md,move-to-diagnostics-build
PS2_FTT_CENSUS,1,host_app/graphics/opengl/rt_neverstale.h:213,num-if-set,diagnostic,,CAVERN_CRYSTRIPES_IMPL.md,move-to-diagnostics-build
PS2_GFXLOG,1,host_app/graphics_thread.cpp:2076,num-if-set,diagnostic,,U5_BUGREPORT_REPORT.md;W10_GOAL.md,move-to-diagnostics-build
PS2_GFXLOOP_CENSUS,2,host_app/graphics_thread.cpp:365;host_app/graphics_thread.cpp:372,OFF-unless-nonzero,diagnostic,Y,,move-to-diagnostics-build
PS2_GFX_FBPCENSUS,1,host_app/graphics_thread.cpp:4183,num-if-set,diagnostic,,FMV3_REVIEW.md,move-to-diagnostics-build
PS2_GL_DUMPS,1,host_app/graphics/opengl/opengl_backend.cpp:1188,unresolved,diagnostic,,HYGIENE_IMPL.md;NEWGAME_REVIEW.md,move-to-diagnostics-build
PS2_GL_MARKMEMO,1,host_app/graphics/opengl/opengl_backend.cpp:837,numeric-other,diagnostic,,R198_ROUND.md;R197_PROFILE.md,move-to-diagnostics-build
PS2_GL_MARKMEMO_CHECK,1,host_app/graphics/opengl/opengl_backend.cpp:849,numeric-other,diagnostic,,W14_PROFILE.md;W14_IMPL.md,move-to-diagnostics-build
PS2_GL_RESID_DIAG,2,host_app/graphics/opengl/opengl_backend.cpp:484;host_app/render.h:2194,numeric-other,diagnostic,Y,PERF_ROUNDS.md;W14_CHECKER.md,move-to-diagnostics-build
PS2_GL_SCANOUT_RT,1,host_app/render.h:1649,numeric-other,diagnostic,,GS_PHASEB_IMPL.md;MENU_VIS.md,move-to-diagnostics-build
PS2_GS_READER_CENSUS,1,host_app/gs_vram_census.h:120,unresolved,diagnostic,,GS_PHASEB_IMPL.md,move-to-diagnostics-build
PS2_GS_S9RDR_DBGBOTH,1,host_app/graphics/opengl/s9_composite.h:145,numeric-other,diagnostic,,,move-to-diagnostics-build
PS2_GS_S9RDR_DBGPIX,3,host_app/graphics/opengl/s9_composite_native.inc:471;host_app/graphics/opengl/s9_composite_native.inc:591;host_app/graphics/opengl/s9_composite_native.inc:1868,num-if-set,diagnostic,Y,GS_PHASED_IMPL.md,move-to-diagnostics-build
PS2_GS_S9RDR_DBGQUANT,1,host_app/graphics/opengl/opengl_backend.cpp:8334,numeric-other,diagnostic,,,move-to-diagnostics-build
PS2_GS_SNAP_FOOTPRINT,1,host_app/render.h:486,presence-gated,diagnostic,,MIDDLE_NATIVE_IMPL.md,move-to-diagnostics-build
PS2_INJ_PROBE,1,host_app/ps2_vblank.cpp:873,num-if-set,diagnostic,,FMV3_REVIEW.md,move-to-diagnostics-build
PS2_INLAT_ANALOG,1,host_app/sce_hle/libpad.cpp:416,presence-gated,diagnostic,,INPUTLAG_IMPL.md,move-to-diagnostics-build
PS2_INTERP_CENSUS,1,host_app/graphics/opengl/opengl_backend.cpp:18704,num-if-set,diagnostic,,CAVERN_PIXEL_IMPL.md;CAVERN_WALLALT_IMPL.md,move-to-diagnostics-build
PS2_INTERP_DUMP,1,host_app/graphics/opengl/opengl_backend.cpp:18743,unresolved,diagnostic,,CAVERN_PIXEL_IMPL.md;CAVERN_FLICKER_IMPL.md,move-to-diagnostics-build
PS2_INTERP_DUMP_DIR,1,host_app/graphics/opengl/opengl_backend.cpp:18750,unresolved,diagnostic,,,move-to-diagnostics-build
PS2_LOG_CHANNELS,1,host_app/ring_log.cpp:98,unresolved,diagnostic,,E2_LOG_REVIEW.md;E2_LOG_IMPL.md,move-to-diagnostics-build
PS2_LOG_ELIDE_DROPPED,1,host_app/main.cpp:331,ON-unless-0,diagnostic,,MIDDLE_NATIVE_IMPL.md,move-to-diagnostics-build
PS2_LOG_FASTFILTER,1,host_app/main.cpp:174,numeric-other,diagnostic,,MIDDLE_NATIVE_IMPL.md,move-to-diagnostics-build
PS2_LOG_RING_EVENTS,1,host_app/ring_log.cpp:109,unresolved,diagnostic,,E2_LOG_REVIEW.md;W16_CHECKER.md,move-to-diagnostics-build
PS2_LOG_VERBOSE,1,host_app/ring_log.cpp:101,unresolved,diagnostic,,E2_LOG_IMPL.md,move-to-diagnostics-build
PS2_LOOPHIST,1,host_app/loop_hist.h:42,num-if-set,diagnostic,,TIMING_NATIVE_IMPL.md,move-to-diagnostics-build
PS2_LVL_DIAG,1,host_app/main.cpp:3524,OFF-unless-nonzero,diagnostic,,CAVERN_BUDGET_IMPL.md,move-to-diagnostics-build
PS2_MEMWATCH,1,host_app/memory.h:169,OFF-unless-1,diagnostic,,HYGIENE_IMPL.md;W10_GOAL.md,move-to-diagnostics-build
PS2_MID_CENSUS,1,host_app/mid_census.h:36,num-if-set,diagnostic,,MIDDLE_NATIVE_IMPL.md;MIDDLE_NATIVE_PLAN.md,move-to-diagnostics-build
PS2_MID_CENSUS_FROM,1,host_app/mid_census.h:48,numeric-other,diagnostic,,MIDDLE_NATIVE_PLAN.md,move-to-diagnostics-build
PS2_MINIF_DIAG,1,host_app/graphics/opengl/opengl_backend.cpp:2466,numeric-other,diagnostic,,CAVERN_MINIF_IMPL.md,move-to-diagnostics-build
PS2_MIPDUMP,1,host_app/graphics/opengl/opengl_backend.cpp:22266,numeric-other,diagnostic,,CAVERN_MIPBAND_IMPL.md,move-to-diagnostics-build
PS2_MIPSRC_DIAG,1,host_app/mipsrc_diag.cpp:145,numeric-other,diagnostic,,CAVERN_MIPSRC_IMPL.md,move-to-diagnostics-build
PS2_MIPSRC_DUMP,1,host_app/mipsrc_diag.cpp:250,presence-gated,diagnostic,,CAVERN_MIPSRC_IMPL.md,move-to-diagnostics-build
PS2_MIPSRC_RAMDUMP,1,host_app/mipsrc_diag.cpp:234,presence-gated,diagnostic,,,move-to-diagnostics-build
PS2_MPEG_DUMP_N,1,host_app/mpeg_hle.cpp:2248,num=0,diagnostic,,FMV3_REVIEW.md;FMV3_ROUND.md,move-to-diagnostics-build
PS2_MPEG_SEQCENSUS,1,host_app/mpeg_hle.cpp:1404,num-if-set,diagnostic,,FMV3_REVIEW.md,move-to-diagnostics-build
PS2_MUSQ,9,host_app/iop_stubs.cpp:1386;host_app/iop_stubs.cpp:1413;host_app/iop_stubs.cpp:1450;host_app/iop_stubs.cpp:1514;host_app/iop_stubs.cpp:1569;host_app/sif_rpc_handlers.cpp:3158;(+3 more),OFF-unless-1,diagnostic,Y,SOUND_P8_REVIEW6E.md;PLAYTEST_R1_IMPL.md,move-to-diagnostics-build
PS2_OCEAN_DUMP,1,host_app/graphics/opengl/opengl_backend.cpp:1181,unresolved,diagnostic,,HYGIENE_IMPL.md;NEWGAME_REVIEW.md,move-to-diagnostics-build
PS2_PAD_SCRIPT,2,host_app/pad_input.cpp:476;host_app/sce_hle/libpad.cpp:374,unresolved,diagnostic,,CAVERN_PIXEL_IMPL.md;CAVERN_MOTION_IMPL.md,move-to-diagnostics-build
PS2_POPUP_DIAG,1,host_app/main.cpp:10305,OFF-unless-1,diagnostic,,LOADTIME_IMPL.md,move-to-diagnostics-build
PS2_POSWATCH,1,host_app/pad_input.cpp:553,unresolved,diagnostic,,PAUSE_IMPL.md;INPUTLAG_IMPL.md,move-to-diagnostics-build
PS2_PROJ_DIAG,1,host_app/main.cpp:3705,unresolved,diagnostic,,CAVERN_PROJTEST_IMPL.md,move-to-diagnostics-build
PS2_SAVE_PROBE,1,host_app/savestate.cpp:181,unresolved,diagnostic,,E2_SAVE_IMPL.md,move-to-diagnostics-build
PS2_SAVE_WATCH,1,host_app/savestate.cpp:2159,num-if-set,diagnostic,,E2_SAVE_IMPL.md,move-to-diagnostics-build
PS2_SHADOW8_KTAP,16,host_app/dmac.cpp:1926;host_app/gif.cpp:391;host_app/gif.cpp:2842;host_app/gif.cpp:4337;host_app/gif_arbiter.cpp:299;host_app/gif_arbiter.cpp:520;(+10 more),num-if-set,diagnostic,Y,SHADOW2_IMPL.md,move-to-diagnostics-build
PS2_SITEB_DIAG,1,host_app/main.cpp:3623,unresolved,diagnostic,,CAVERN_BUDGET_IMPL.md,move-to-diagnostics-build
PS2_SMWATCH,1,host_app/main.cpp:795,OFF-unless-nonzero,diagnostic,,,move-to-diagnostics-build
PS2_SNAPVERIFY,1,host_app/render.h:633,num-if-set,diagnostic,,CAVERN_WALLTEX_RESEARCH.md;MIDDLE_NATIVE_IMPL.md,move-to-diagnostics-build
PS2_SND_BANKTRACE,1,host_app/main.cpp:843,OFF-unless-nonzero,diagnostic,,SOUND_P6_REPORT.md;SOUND_P6_IMPL.md,move-to-diagnostics-build
PS2_SND_CENSUS,1,host_app/snd_census.h:45,OFF-unless-nonzero,diagnostic,,SOUND_P8_REVIEW6E.md;SOUND_REPORT.md,move-to-diagnostics-build
PS2_SND_KONLOG,4,host_app/iop_stubs.cpp:1558;host_app/iop_stubs.cpp:1658;host_app/sound/sound_core.cpp:337;host_app/sound/sound_core.cpp:473,num-if-set,diagnostic,Y,SOUND_P4_REVIEW.md;SOUND_P6_REPORT.md,move-to-diagnostics-build
PS2_SPIKE_DUMP,1,host_app/graphics_thread.cpp:3716,unresolved,diagnostic,,HYGIENE_IMPL.md;NEWGAME_REVIEW.md,move-to-diagnostics-build
PS2_STICKTRACE,1,host_app/stick_trace.h:69,unresolved,diagnostic,,,move-to-diagnostics-build
PS2_T0TRACE,1,host_app/ee_timers.cpp:32,numeric-other,diagnostic,,SLIDEJUMP_R2_IMPL.md;SLIDEJUMP_R2_REPORT.md,move-to-diagnostics-build
PS2_TEXBLKDUMP,1,host_app/graphics/opengl/opengl_backend.cpp:6490,unresolved,diagnostic,,CAVERN_CHECKER_IMPL.md,move-to-diagnostics-build
PS2_TEXBLKDUMP_DIR,1,host_app/graphics/opengl/opengl_backend.cpp:6491,unresolved,diagnostic,,,move-to-diagnostics-build
PS2_TEXPOP_DIAG,1,host_app/graphics/opengl/opengl_backend.cpp:7806,numeric-other,diagnostic,,CAVERN_BAKE_IMPL.md;CAVERN_MINIF_IMPL.md,move-to-diagnostics-build
PS2_TEXPOP_ZDUMP,1,host_app/graphics/opengl/opengl_backend.cpp:8018,unresolved,diagnostic,,CAVERN_TILEPATCH_IMPL.md,move-to-diagnostics-build
PS2_TICKWATCH,1,host_app/main.cpp:1139,unresolved,diagnostic,,CAVERN_BUDGET_IMPL.md;SLIDEJUMP_R2_IMPL.md,move-to-diagnostics-build
PS2_TIMEDOM_DIAG,1,host_app/ee_timers.cpp:552,numeric-other,diagnostic,,CAVERN_TIMEDOMAIN_IMPL.md,move-to-diagnostics-build
PS2_U32WATCH,1,host_app/pad_input.cpp:653,unresolved,diagnostic,,PAUSE_IMPL.md;RECOMP_P3B_REVIEW.md,move-to-diagnostics-build
PS2_UNPKT_CENSUS,1,host_app/vif.cpp:344,OFF-unless-nonzero,diagnostic,,W10_IMPL.md;W10_PROFILE.md,move-to-diagnostics-build
PS2_VERBOSE_HW,1,host_app/hw_verbose.h:26,unresolved,diagnostic,,HYGIENE_IMPL.md;MIDDLE_NATIVE_IMPL.md,move-to-diagnostics-build
PS2_VIF_ALIGN_LOG,3,host_app/vif.cpp:1515;host_app/vif.cpp:1730;host_app/vif.cpp:1789,num-if-set,diagnostic,Y,MIDDLE_NATIVE_IMPL.md,move-to-diagnostics-build
PS2_VIF_WTRACE,1,host_app/vif.cpp:3456,numeric-other,diagnostic,,,move-to-diagnostics-build
PS2_VU1_CRYS_RING,1,host_app/hle/vu1_hle_programs.cpp:253,unresolved,diagnostic,,CAVERN_DIRECTIONAL_IMPL.md,move-to-diagnostics-build
PS2_VUTRACE,2,host_app/vu.cpp:136;host_app/vu.cpp:875,OFF-unless-1,diagnostic,,HUB1_IMPL.md;U6_VUCTRL_REPORT.md,move-to-diagnostics-build
PS2_VUTRACE_CAPTURE,1,host_app/vu.cpp:142,numeric-other,diagnostic,,HUB1_IMPL.md;U6_VUCTRL_REPORT.md,move-to-diagnostics-build
PS2_VUTRACE_DIR,1,host_app/vu.cpp:148,numeric-other,diagnostic,,HUB1_IMPL.md;U6_VUCTRL_REPORT.md,move-to-diagnostics-build
PS2_VUTRACE_FILE,1,host_app/vu.cpp:138,numeric-other,diagnostic,,,move-to-diagnostics-build
PS2_VUTRACE_IMG_GAP,1,host_app/vu.cpp:154,numeric-other,diagnostic,,CAVERN_CRYSTRIPES_IMPL.md,move-to-diagnostics-build
PS2_VUTRACE_MAX,1,host_app/vu.cpp:140,numeric-other,diagnostic,,,move-to-diagnostics-build
PS2_VUTRACE_MINFRAME,1,host_app/vu.cpp:150,numeric-other,diagnostic,,U6_VUCTRL_REPORT.md;CAVERN_DIRECTIONAL_IMPL.md,move-to-diagnostics-build
PS2_VUTRACE_NEWIMG,1,host_app/vu.cpp:246,OFF-unless-1,diagnostic,,HUB1_IMPL.md;U6_VUCTRL_REPORT.md,move-to-diagnostics-build
PS2_VUTRACE_PC,1,host_app/vu.cpp:144,numeric-other,diagnostic,,U6_VUCTRL_REPORT.md;CAVERN_SLOWSPOTS_IMPL.md,move-to-diagnostics-build
PS2_VUTRACE_PER_IMAGE,1,host_app/vu.cpp:152,numeric-other,diagnostic,,VU1_HLE_R1_IMPL.md;CAVERN_SLOWSPOTS_IMPL.md,move-to-diagnostics-build
PS2_VU_RUNFLAT_CENSUS,1,host_app/vu.cpp:585,OFF-unless-1,diagnostic,,W13_CHECKER.md;W13_IMPL.md,move-to-diagnostics-build
PS2_ZSERVE_DIAG,1,host_app/graphics/opengl/opengl_backend.cpp:21431,numeric-other,diagnostic,,,move-to-diagnostics-build
PS2_ARTDET_TEST,1,host_app/artdet_flicker.cpp:43,numeric-other,test-hook,,CAVERN_FLICKER_IMPL.md,move-to-test-only
PS2_BUGREPORT_TEST_MANUAL,1,host_app/gui/bug_report_section.cpp:80,numeric-other,test-hook,,U5_BUGREPORT_REPORT.md;U5_BUGREPORT_IMPL.md,move-to-test-only
PS2_BUGREPORT_TEST_MANUAL_STATE,1,host_app/gui/bug_report_section.cpp:92,unresolved,test-hook,,U5_BUGREPORT_IMPL.md,move-to-test-only
PS2_GUI_TEST_LOAD,1,host_app/gui/savestate_browser.cpp:245,unresolved,test-hook,,U4_SAVESTATE_IMPL.md;U5_BUGREPORT_REPORT.md,move-to-test-only
PS2_GUI_TEST_SAVE,1,host_app/gui/savestate_browser.cpp:263,numeric-other,test-hook,,U4_SAVESTATE_IMPL.md;U5_BUGREPORT_REPORT.md,move-to-test-only
PS2_INPUT_SELFTEST,1,host_app/input/input_system.cpp:127,unresolved,test-hook,,TIM0_IMPL.md;R198_ROUND.md,move-to-test-only
PS2_RING_TEST_BUNDLE,1,host_app/main.cpp:14210,unresolved,test-hook,,U5_BUGREPORT_REPORT.md,move-to-test-only
PS2_RING_TEST_CRASH,1,host_app/main.cpp:14199,unresolved,test-hook,,E2_LOG_REVIEW.md;U5_BUGREPORT_REPORT.md,move-to-test-only
PS2_RING_TEST_HOTKEY,1,host_app/main.cpp:14206,unresolved,test-hook,,U5_BUGREPORT_REPORT.md;W16_CHECKER.md,move-to-test-only
PS2_SND_SELFTEST,1,host_app/sound/sound_core.cpp:237,num-if-set,test-hook,,SOUND_P2_IMPL.md;SOUND_P2_REVIEW.md,move-to-test-only
PS2_TEST_RESIZE,1,host_app/main.cpp:14479,unresolved,test-hook,,MENU_VIS.md,move-to-test-only
PS2_VUAOT_TEST_REGEN,1,host_app/gui/vu_microcode_section.cpp:360,numeric-other,test-hook,,,move-to-test-only
PS2_VUAOT_TEST_SETCOLLECT,1,host_app/gui/vu_microcode_section.cpp:348,unresolved,test-hook,,,move-to-test-only
PS2_VUAOT_TEST_SETMODE,1,host_app/gui/vu_microcode_section.cpp:339,unresolved,test-hook,,,move-to-test-only
```

**Undocumented vars** (no mention in any docs/*.md, run scripts, or README): 64 —
these have *no introducing record*; purge candidates unless code comments carry a verdict:
`LOGO_MEMSNAP`, `LOGO_MEMSNAP_FRAMES`, `PS2_ARTDET_MIN`, `PS2_ARTDET_WEIRD`, `PS2_BAKE_DUMP`, `PS2_BAKE_SRCSCAN`, `PS2_BOOT_WATCH`, `PS2_BTRACE`, `PS2_CAP_TRIPFIT_PCT`, `PS2_CAP_UPHOLD_MS`, `PS2_CLEAR_DIAG`, `PS2_FMV_NATIVE`, `PS2_GFXLOOP_ARM`, `PS2_GFXLOOP_CENSUS`, `PS2_GL_BSTATE_FAST`, `PS2_GL_L2_SU_MB`, `PS2_GL_VCONV_FAST`, `PS2_GS_S9RDR_DBGBOTH`, `PS2_GS_S9RDR_DBGQUANT`, `PS2_INTERP_DUMP_DIR`, `PS2_LIBC_HLE`, `PS2_LOADER_ORACLE_DIR`, `PS2_MIPSRC_RAMDUMP`, `PS2_MTVU_SPIN_NS`, `PS2_PACE_FMV_BYPASS`, `PS2_R197_D2S`, `PS2_R197_D3LOG`, `PS2_R197_MERGELOG`, `PS2_R197_MERGEMAX`, `PS2_R198_QORDER_ZCEN`, `PS2_RQ_MAXMB`, `PS2_S105_SKIP`, `PS2_S13_TBLCAP_ENTRY`, `PS2_S13_TBLCAP_N`, `PS2_S15_HDRCAP_ENTRY`, `PS2_S15_HDRCAP_MINVF`, `PS2_S15_HDRCAP_N`, `PS2_S16_CHAIN`, `PS2_S16_CHAIN_CAP`, `PS2_S26_DECAL_CAP`, `PS2_S26_UTRACE`, `PS2_S26_VUDUMP`, `PS2_S8_MTXCAP`, `PS2_S8_MTXCAP_EERAM`, `PS2_S8_MTXCAP_MINVF`, `PS2_S8_MTXCAP_N`, `PS2_S8_MTXWATCH`, `PS2_S8_REGHOOK_BATCAP`, `PS2_S8_REGHOOK_CAP`, `PS2_SH5_IDXDIRTY`, `PS2_SMWATCH`, `PS2_STICKTRACE`, `PS2_TEXBLKDUMP_DIR`, `PS2_VRAM_EPOCH_REACT`, `PS2_VU0_SHADOWGATE`, `PS2_VU0_VISCAP`, `PS2_VU0_VISCAP_MINVF`, `PS2_VU0_VISCAP_N`, `PS2_VU0_XCHECK`, `PS2_VUAOT_TEST_REGEN`, `PS2_VUAOT_TEST_SETCOLLECT`, `PS2_VUAOT_TEST_SETMODE`, `PS2_W13R7_RELOAD_PATH`, `PS2_ZSERVE_DIAG`.

Non-literal `getenv(expr)` sites (19) route through helpers — the important ones:
- `render.h:767` `GateOn(const char* name)` (r120 family) and `texture_decoder.h:52` (r118 family)
- `main.cpp` env-prefix scanners for bug-report env capture
These helpers are where sub-gate → master fallback happens (see §2).

---
## 2. Latch / mirror pattern

### 2.1 The canonical latch
**482** `static const T x = []{ ... }();` blocks
(449 read env), + 20
one-line `static x = getenv(...)` latches. Values latch **once per translation-unit-site**
on first call — env changes mid-run are ignored, and two sites latching at different
times still agree only because env is immutable in practice. Example
(gs_state.cpp:2599-2603):
```
static const bool s_bulk_on = [] {
    const char* e = std::getenv("PS2_GS_TYPED_BULK");
    return !(e && e[0] == '0');  // default ON; =0 restores per-vertex path
}();
```

### 2.2 Master → sub-gate families (multi-level fallback)
Resolution rule everywhere: *explicit sub-var wins; else master; else default ON*.

| Family | Master var | Helper (file:line) | Sub-gates |
|---|---|---|---|
| r118 texture/GL fast | `PS2_GL_FAST` | texture_decoder.h:26 `MasterOn`, :51 `GateOn` | PS2_GL_TEXCACHE, PS2_GL_BBOX_COPY, PS2_GL_NO_FLUSH, PS2_GL_VBO_RING (texture_decoder.h:57-60) |
| r120/r121+ GL fast tier 2/3 | `PS2_GL_FAST2` | render.h:751 `MasterOn`, :766 `GateOn` | PS2_GL_EPOCH_KEY, PS2_GL_UNIF_CACHE, PS2_GL_BIND_CACHE (render.h:772-774), PS2_GL_BLOCK_DECODE (:817), PS2_GL_TEXSUB (:821), PS2_GL_LAZY_RTSYNC (:822), PS2_GL_FASTHASH (:858), PS2_GL_PAR_DECODE (:865), PS2_GL_HASH_L2 (:1009), PS2_GL_ALT16_GPU (:1010), PS2_GL_SYNC_RECT (:1011), PS2_GL_LAZY_ZSYNC (:1050), PS2_GL_IDX_PBO (:1419) |
| bench-env cascade | `PS2_NOVSYNC` | per-site latches | PS2_VBLANK_FREERUN defaults to NOVSYNC (ps2_vblank.cpp:410-413); PS2_CDVD_INSTANT defaults to FREERUN (:468-471); PS2_MPEG_PACE_AUTHORED defaults to !FREERUN (mpeg_hle.cpp:460) — a 3-level behavioral chain with one inversion |
| prerequisite conjunctions | `PS2_MTVU` | per-site latches | PS2_MTVU_DEFER = own-default-ON && MtvuEnabled() (vu1_async.cpp:1657-59); PS2_EE_PACE = requested && DEFER && MTVU, demotes with warning (ee_pace.h:168-183); PS2_CDVD_BUDGET_KB 'active only while PS2_CDVD_INSTANT is on' (ps2_vblank.h:168-170) |
| gate-announce idiom (NOT a fallback) | `PS2_PERF` | ~15 latches re-read it | inside the latch of MTVU (vu1_async.cpp:1013), T0_SUBTICK (ee_timers.cpp:116), WAITFIX (timeline.h:56), FMV_DECODE_CAP (mpeg_hle.cpp:1200), VU_AOT_SPANFOLD (vu_aot_programs.cpp:72), MTVU_DEFER (vu1_async.cpp:1660) … — PERF=1 prints each gate's resolved value at first use; verified it never changes the gate's value |
| misc 2-level | various | per-site | PS2_GS_SNAPSHOT_FAST→PS2_GL_FAST (render.h:446), PS2_XFER_FAST→PS2_GL_FAST (:500), PS2_GS_RUNDRAW→PS2_GS_DRAWMERGE (:2024), PS2_GS_MIP_LODREF→PS2_GS_MIP_ALLCLASS (gs_mipmap.h:125), PS2_ARTDET→PS2_ARTDET_FLICKER (artdet_flicker.cpp:254), PS2_SHADOW_ORDERFIX→PS2_SHADOW_CTXLATCH (s8_ctxlatch.h:111), PS2_GL_ALIAS_DEPCOPY→PS2_GL_ALIAS_DEPCOPY2 (opengl_backend.cpp:670), PS2_LOADSPAN chain (loadspan.h:97) |

**Deepest real chain (3 levels + inversion)**: `PS2_CDVD_INSTANT` → `PS2_VBLANK_FREERUN`
→ `PS2_NOVSYNC` → OFF; with `PS2_MPEG_PACE_AUTHORED` = NOT(FREERUN) hanging off level 2.
Note: the scanner's raw count of 26 'latches reading ≥2 vars' includes the PERF
announce idiom; the *behavioral* chains are the rows above (hand-verified).

### 2.3 Mirrors that could disagree
**38 vars have >1 independent latch site.** Because each site
re-implements its own parse, semantic drift is possible (and in one case defaults differ
by design). Key mirrors:

| Var | Latch sites | Risk |
|---|---|---|
| `PS2_PERF` | 21: host_app/gif_arbiter.h:634, host_app/vu1_async.cpp:609, host_app/vu1_async.cpp:1010, host_app/vu1_async.cpp:1039, host_app/vu1_async.cpp:1536 … | 21 sites — each subsystem re-parses `p && atoi(p)!=0`; announce-only (verified §2.2), so drift risk is inconsistent reporting rather than behavior — but 21 copies of one parse is the idiom to kill |
| `PS2_SHADOW8_KTAP` | 16: host_app/vif.cpp:2171, host_app/vif.cpp:2249, host_app/vif.cpp:2467, host_app/vu.cpp:1634, host_app/vu.cpp:1676 … | 16 sites in 7 subsystem files (vif/vu/gif_arbiter/gs_state/dmac/gif) — one diagnostic spread over the whole pipeline; a numeric-vs-bool parse mismatch silently drops one tap |
| `PS2_MUSQ` | 9: host_app/sif_rpc_handlers.cpp:3157, host_app/sif_rpc_handlers.cpp:3472, host_app/sif_rpc_handlers.cpp:3542, host_app/sif_rpc_handlers.cpp:3716, host_app/iop_stubs.cpp:1385 … | 9 sites (sound + sif_rpc + main) — same |
| `PS2_SH5_BIND` | 8: host_app/graphics/opengl/s9_composite_native.inc:853, host_app/graphics/opengl/opengl_backend.cpp:8899, host_app/graphics/opengl/opengl_backend.cpp:14578, host_app/graphics/opengl/opengl_backend.cpp:15255, host_app/graphics/opengl/opengl_backend.cpp:15521 … | 8 sites |
| `PS2_BAKE_DIAG` | 7: host_app/gs_state.cpp:2866, host_app/gs_state.cpp:4177, host_app/gs_state.cpp:4330, host_app/gs_state.cpp:4866, host_app/gs_state.cpp:4952 … | 7 sites |
| `PS2_GL_FAST` | 4: host_app/texture_decoder.h:27, host_app/render.h:446, host_app/render.h:500, host_app/render.h:2486 | 4 sites: 2 are master latches (texture_decoder.h:28, render.h nested), 2 are family-restore checks in render.h:449/503 — same rule, independently coded |
| *(31 more)* | `PS2_MIPCEN`×5, `PS2_SND_KONLOG`×4, `PS2_R197_D2P`×4, `PS2_VIF_ALIGN_LOG`×3, `PS2_S26_DECAL`×3, `PS2_R198_QORDER`×3, `PS2_R198_QORDER_BASE`×3, `PS2_GS_SCALE`×3, `PS2_GS_S9RDR_DBGPIX`×3, `PS2_S13_TBLWATCH`×2, `PS2_R198_XGK_WINDOW`×2, `PS2_MTVU`×2 … | full list in /tmp/s113/a2/latches.json |

**Refactor rule**: one `Config` struct read **once** at startup; every gate becomes a
named field; helpers/mirrors deleted. Latching then happens exactly once by construction.

---
## 3. Logging census

| Channel | Sites | Notes |
|---|---|---|
| `g_logFile <<` | **6,869** | global `std::ofstream` to logs/runtime_log.txt; opens by DEFAULT (PS2_LOG unset ⇒ open, main.cpp:4905-4908); PS2_LOG=0 skips opening (main.cpp:4899-4920) |
| `fprintf(stderr,…)` | **855** | survives PS2_LOG=0; used for PERF lines, probes, mirrors of important g_logFile lines (e.g. sif_rpc_handlers.cpp:3154 '[S48-MUSQ7] stderr mirror (PS2_LOG=0 hides g_logFile)') |
| `std::cout <<` | 759 | mostly recompiler_tool + early-boot |
| `std::cerr <<` | 180 | boot/config errors |
| `printf` | 19 | stragglers |
| ring log | ring_log.cpp (596 lines, 4 env) | crash-scoped in-memory ring, dumped by bug_report; schema docs/RING_LOG_SCHEMA.md |

### 3.1 The 4 gating layers (all retrofits)
1. **PS2_LOG master**: log simply not opened when =0 — but sites still *format* into a
   closed stream unless individually gated (cost remains).
2. **TagFilterBuf + [M3A] PS2_LOG_FASTFILTER** (main.cpp:157-175): a streambuf that
   drops lines by `[TAG` prefix classification, with a fast-path scanner (default ON).
3. **[M4A] PS2_LOG_ELIDE_DROPPED** (main.cpp:319-331, hook hw_verbose.h:32-47): emit
   sites pre-ask `hwlog::PrefixDropped(tag)` and skip *formatting* lines the filter
   would discard; 'kept-log output byte-identical in every arm'.
4. **hwlog::VerboseOn() / PS2_VERBOSE_HW** (hw_verbose.h:24-30): per-EVENT hardware
   narration (per-DMA-tag, per-FlushBatch, per-DIRECT boxes) default OFF after the
   2.8 GB-log incident — 'a 2.8 GB log whose tail was ~56% GS-STATE-DUMP box lines'
   (hw_verbose.h:1-11). Standing rule (ibid.): hot-path diagnostics env-gated
   DEFAULT-OFF **and** log-open-gated; per-frame+ paths in-memory census only.

### 3.2 Per-site gating patterns observed
- latched bool + `if (on && g_logFile.is_open())` — the post-W10 convention
- naked `g_logFile <<` (pre-convention; pays even in default runs)
- `if (hwlog::VerboseOn())` blocks
- budgeted probes: `static std::atomic<int> lines; if (lines++ < kBudget)` (e.g. SEQ54 main.cpp:1551)
- stderr twins for PS2_LOG=0 visibility (memory.cpp:1602, hle_heap.cpp:1660)

### 3.3 Hot-path sites (inside per-draw / per-instruction / per-MMIO loops)
Gating ratio = sites with a gate within 6 preceding lines (proxy; hand-verified samples):

| File | log sites | gated nearby | Hot-loop exposure |
|---|---|---|---|
| host_app/memory.cpp | 295 | 35 | MMIO read/write handlers — per-access lines like `VIF0_FBRST Read` (memory.cpp:557-566) are UNGATED |
| host_app/vu.cpp | 283 | 54 | VU1 exec + flag pipeline — e.g. [VUFLAG] (vu.cpp:848) has NO env gate: a hardcoded frame-window 1100-1180 + 3000-line budget (vu.cpp:844-846) — a leftover cutscene forensic that still costs a branch per flag-op |
| host_app/dmac.cpp | 302 | 106 | per-tag chain walkers (hwlog-gated post-R188) |
| host_app/gif.cpp | 177 | 95 | per-GIFtag / per-primitive paths |
| host_app/gs_state.cpp | 199 | 81 | per-FlushBatch state dumps (hwlog-gated) |
| host_app/graphics/opengl/opengl_backend.cpp | 279 | 81 | per-draw/per-texture-upload paths |
| host_app/vif.cpp | 99 | 68 | per-UNPACK command paths |
| host_app/graphics_thread.cpp | 83 | 30 | per-RenderJob; many stderr |

### 3.4 What the replacement logger must cover
A leveled, compile-time-category logger (`LOG(cat, level, fmt, ...)`) needs:
- **Categories** = today's tag prefixes; the TagFilterBuf allow/deny list in main.cpp is
  the de-facto category registry — harvest it. ~30 categories cover the 6,869 sites
  (VIF, GIF, GS, VU, DMAC, MEM, SIF, SND, PAD, MPEG, SCHED, LOADER, HEAP, PERF, …).
- **Compile-time category kill** (release build strips diagnostics categories to nothing
  — replaces PS2_LOG_ELIDE_DROPPED's runtime elision with zero-cost).
- **Levels**: error / warn / info / narrate (narrate ≙ today's PS2_VERBOSE_HW layer).
- **Sinks**: file (runtime_log), stderr (PERF+errors), in-memory ring (crash forensics)
  — matching today's three real sinks so bug_report keeps working.
- **Budgets**: first-N / every-Nth / frame-window as library primitives (SEQ54-style
  probes reimplement this by hand ~dozens of times).
- **No iostream formatting on disabled paths** (the M4A lesson, enforced structurally).

---
## 4. Diagnostics census

Dedicated diagnostic/tooling files found (existence-verified, line counts exact):

| File | Lines | Env vars | What it is | Keep? |
|---|---|---|---|---|
| host_app/artdet_flicker.cpp | 339 | PS2_ARTDET,PS2_ARTDET_FLICKER,PS2_ARTDET_TEST,PS2_GS_INTERP… | ARTDET: automated artifact detector — flicker class, PS2_ARTDET gate (artdet_flicker.cpp:254) | KEEP (diag build) — regression tripwire |
| host_app/artdet_flicker.h | 48 |  |  | review |
| host_app/bench.h | 714 |  | --bench-frames harness: deterministic frame-count run | KEEP — used by run_bench.sh + CI |
| host_app/bench_report.h | 782 |  | bench_report.json writer (fps percentiles etc.) | KEEP |
| host_app/bug_report.cpp | 565 | PS2_BUGREPORT,PS2_BUGREPORT_SLOWSPOT,PS2_BUGREPORT_SLOWSPOT_MAX | crash handler: ring-log dump + env capture + savestate on crash | KEEP — essential |
| host_app/bug_report.h | 460 |  |  | review |
| host_app/bullseye_trace.cpp | 133 | PS2_BTRACE | bullseye text-dump tracer (one screen's bug) | one-off — delete |
| host_app/bullseye_trace.h | 87 |  |  | review |
| host_app/call_census.cpp | 492 | PS2_CALL_CENSUS,PS2_CALL_CENSUS_ROOT,PS2_CALL_CENSUS_SLOWSEG | per-function call counters (recomp coverage census) | KEEP (diag build) |
| host_app/call_census.h | 100 |  |  | review |
| host_app/cuelag_probe.h | 201 | PS2_CUELAG,PS2_CUELAG_WIN_TICKS | sound cue-lag probe | one-off |
| host_app/debug_budget.h | 70 |  | shared budget helper for probes | KEEP — becomes the budget primitive of §3.4 |
| host_app/foglane_live.h | 86 | PS2_GL_FOGLANE_LIVE,PS2_S107_LOG | fog-lane live tuner (CAVERN_FOG) | one-off |
| host_app/gs_vram_census.h | 363 | PS2_GS_READER_CENSUS | VRAM page/format census | KEEP (diag build) |
| host_app/hw_verbose.h | 48 | PS2_VERBOSE_HW | hwlog narration gate + PrefixDropped hook | absorbed by new logger |
| host_app/loader_prof.h | 348 | PS2_LOADER_PROF | loader profiling | review — LOADTIME sessions concluded |
| host_app/loadspan.h | 329 | PS2_LOADSPAN,PS2_LOADSPAN_DRAWMAX | hands-off load-span detector (feeds budget_lift) | KEEP — feature-adjacent (budget lift depends on it) |
| host_app/loop_hist.h | 150 | PS2_LOOPHIST | loop histogram probe | one-off |
| host_app/mid_census.h | 426 | PS2_MID_CENSUS,PS2_MID_CENSUS_FROM | mid-frame census | review |
| host_app/mipsrc_diag.cpp | 278 | PS2_MIPSRC_DIAG,PS2_MIPSRC_DUMP,PS2_MIPSRC_RAMDUMP,PS2_MIPWR | mip-source diagnostics (CAVERN_MIPSRC) | review — likely one-off |
| host_app/mipsrc_diag.h | 53 |  |  | with mipsrc_diag.cpp |
| host_app/perf_profiler.h | 611 | PS2_PERF | PERF buckets: per-window ms/frame attribution per subsystem; the [PERF] stderr lines | KEEP — core perf tooling |
| host_app/ring_log.cpp | 596 | PS2_CRASHRING,PS2_LOG_CHANNELS,PS2_LOG_RING_EVENTS,PS2_LOG_VERBOSE | in-memory ring log w/ schema (docs/RING_LOG_SCHEMA.md) | KEEP — bug_report input |
| host_app/ring_log.h | 233 |  |  | review |
| host_app/slowspot.cpp | 307 | PS2_SLOWSPOT,PS2_SLOWSPOT_DEDUPE | PS2_SLOWSPOT: auto-savestate + PERF context on slow frame; perf corpus builder | KEEP |
| host_app/slowspot.h | 62 |  |  | review |
| host_app/hle/drawlist_record.cpp | 404 | PS2_DRAWLIST_RECORD,PS2_DRAWLIST_RECORD_FRAMES,PS2_DRAWLIST_RECORD_PIXELS,PS2_DRAWLIST_RECORD_PPM… | PS2_DRAWLIST_RECORD: typed GS draw-list recorder — input spec for future HLE GS (docs/hle_harness.md §2) | KEEP — verification asset |
| host_app/hle/drawlist_record.h | 242 |  |  | review |
| host_app/pacebrk.h | 197 | PS2_PACEBRK | pace-break probe | one-off |
| host_app/quantum.h | 172 | PS2_QUANTUM | scheduler quantum knob | feature knob |
| host_app/ee_pace.h | 368 | PS2_EE_PACE,PS2_EE_RUNAHEAD,PS2_MTVU,PS2_MTVU_DEFER… | EE pacing model | feature |
| host_app/present_pace.h | 1147 | PS2_CAP_STEPDOWN,PS2_CAP_TRIPFIT,PS2_CAP_TRIPFIT_PCT,PS2_CAP_UPHOLD_MS… | present pacing (1,147 lines, 9 env vars!) | feature — but needs the 9-var surface collapsed |
| host_app/turbo.h | 503 | PS2_LOAD_TURBO,PS2_TURBO,PS2_TURBO_DUCK | PS2_LOAD_TURBO load fast-forward | feature (default-off setting) |
| host_app/budget_lift.h | 224 | PS2_LOAD_BUDGET_LIFT | PS2_LOAD_BUDGET_LIFT CD-budget lift | feature (default-on) |
| host_app/budget_math.h | 157 |  | CD budget math | feature |
| host_app/idx_epoch_guard.h | 116 | PS2_GL_IDXEPOCH | index-epoch guard | KEEP — correctness |
| host_app/gs_page_tracker.h | 518 | PS2_GS_RESIDENT,PS2_GS_RESIDENT_PBO | GS page write-epoch tracker [M1A] | KEEP — NOT a diagnostic: correctness infrastructure for texture cache freshness |

**Forensic hooks for specific addresses**: main.cpp contains 208 `install_dual_hook`/
probe installs. Exemplar: **[SEQ54] sail-node tick-ordering probe** (main.cpp:1530-1650):
hardcoded node addresses `0xd8c5d0/0xd8c1a0/0xd8c130/0xd8c050` (R53 *host* addresses,
valid only for one savestate), frame window ≥1180, 400-line budget, plus a companion
[SEQ54-HEAD] writer probe (main.cpp:1608). The R53/R54 investigation is concluded —
the probe is dead weight. Same class: [WORLDSM] hook (main.cpp:1525), HEADWATCH
(address-parametrized watch, pad_input.cpp:594-644) / EULERTRACE (presence-gated:
ANY value enables, even 0 — run_play.sh:81-84), bullseye_trace, PS2_R198_RTPROBE/
XGKPROBE, PS2_S103_DRAWLIM, etc.

**Essential-to-keep (the diagnostics module)**: perf_profiler (PERF buckets), bench +
bench_report, slowspot, bug_report + ring_log, drawlist_record, vutrace collector
(vu.cpp:135-155 — feeds vutrace_collect/ and the HLE oracle corpus), recomp_corpus
recorder, call_census, artdet, gs_vram_census, debug_budget, the generic hook installer.
These are *durable verification assets* — several are the planned equivalence-harness
inputs. **One-off** (delete with their sessions): SEQ54/WORLDSM/HEADWATCH/EULERTRACE,
bullseye_trace, cuelag_probe, foglane_live, loop_hist, pacebrk, mipsrc_diag, and every
`PS2_R1xx_*PROBE/LOG/DUMP` (≈70 one-off diagnostic vars in the §1 registry).

---
## 5. Session-history-in-code

- **[TAG] comment mentions: 7,469** across scanned sources (excl. third_party),
  **1,374 unique tags**. Top: [W16R1] 112, [R158-GIFFAST] 111, [JALR-MISS-HASHMAP] 105,
  [GE1] 90, [R169-GLRUN] 73, [S82-MIP] 71, [E2-SAVE] 70, [R119-GS-FAST] 70,
  [R168-DEFER] 68, [S43] 61, [E2-LOG] 59, [W13R3-E3] 56. (ImGui's structural
  [SECTION] convention in third_party is excluded.)
- **Session-named namespaces: 36**, with real use counts — they are load-bearing:

| Namespace | Uses | Declared in |
|---|---|---|
| `r120gl::` | 1041 | host_app/graphics/opengl/gl_state_cache.h |
| `r147::` | 232 | host_app/graphics/opengl/opengl_backend.cpp, host_app/render.h |
| `r122::` | 206 | host_app/graphics/opengl/opengl_backend.cpp |
| `r169::` | 168 | host_app/graphics/opengl/gl_state_cache.h |
| `r164::` | 105 | host_app/render.h |
| `zk::` | 94 | host_app/hle/vu1_hle_programs.cpp |
| `r172::` | 92 | host_app/graphics/opengl/gl_state_cache.h, host_app/graphics/opengl/opengl_backend.cpp, host_app/graphics/opengl/opengl_backend.h |
| `r162::` | 71 | host_app/graphics/opengl/opengl_backend.cpp, host_app/graphics/opengl/opengl_backend.h, host_app/render.h |
| `r118::` | 58 | host_app/texture_decoder.h |
| `r159::` | 50 | host_app/graphics/opengl/opengl_backend.cpp, host_app/render.h |
| `r121::` | 44 | host_app/render.h |
| `r119::` | 36 | host_app/render.h |
| `r125::` | 36 | host_app/render.h |
| `r160::` | 33 | host_app/render.h |
| *(+22 more)* | | m4a::29, r120::27, skinrdr::22, r148::19, r154::18, r124::17, r123::15, g8par::6, sh8::6 … full list in /tmp/s113/a2/session_ns.json |

- **Comment blocks >15 consecutive `//` lines: 797** (excl. third_party). The biggest
  are 70-97-line file headers narrating session verdicts (turbo.h:2 97 lines,
  hle_heap.h:347 94, particles_native.inc:1 94, hle_meta_arena.h:1 84,
  rt_neverstale.h:2 84, libc_hle.cpp:1 79 …).

### Durable 'why' vs droppable history — the test
KEEP a comment iff removing it makes a future correct-looking change likely wrong:
- **Hardware invariants**: XGKICK deferral timing (VU skill/GameDB), COLCLAMP=0 wrap
  blending math (PS2_GS_WRAPBLEND rationale), GS half-pixel sprite rounding, VU flag
  pipeline delays, PS2 alpha 0-128 semantics, page/block swizzle layouts. → keep,
  rewritten as short invariant notes at the code they protect.
- **Cross-thread contracts**: e.g. display_settings.h:1-15 atomics convention; hwlog
  standing rule. → keep.
- **DROP**: fps A/B numbers, 'probe legs', acceptance-battery recaps, 'this used to be'
  narratives, R-number cross-references, tag-filter dodging notes (SEQ54 main.cpp:1544),
  per-session re-target stories (the 26-line comment inside r118::MasterOn,
  texture_decoder.h:31-47, is a doc, not code). The narrative already lives in
  **283 docs/loop_state/*.md records** — link, don't inline. Estimated ≥90% of the
  7,469 tag mentions and ≥85% of long blocks are droppable on migration; each module's
  port carries over only its invariant notes.
- **Namespaces**: rename by function on migration (r118/r120/r120gl → `texcache`,
  `glfast`, `glstate`; zk → its actual subsystem), since 691 .cpp/.h line-uses of
  `r120gl::` (1,110 incl. .inc; same counting scope as finding 9) mean mechanical
  rename, not deletion.

---
## 6. Real user config surface & unified Config schema

Sources: scripts/run_play.sh CANON (lines 141-176) + documented user recipe (:126-133);
scripts/run_bench.sh CANON (:92-100); GUI (gui/gui.cpp — input device combo :483,
kbm-pad checkbox :529, scaling mode/aspect :565-579, scale/offset sliders :585-591,
sound section, savestate browser); display.ini (display_settings.h: mode, aspect,
custom_scale, custom_offset_x/y; persisted <exe>/display.ini); vu_aot.ini
(corpus_compile, collection_enabled, collection_per_image — build-time);
recomp_overrides.ini (`pin` = legacy-body pin list for v2 engine, generation-time).

Proposed unified `Config` (single struct, read once at startup, one ini/TOML file +
env/CLI overrides; GUI writes back):

```
[video]   scale=2            # PS2_GS_SCALE (int 1..4)
          interp=true        # PS2_GS_INTERP
          vsync=false        # !PS2_NOVSYNC
          fps_cap=60         # PS2_FPS_CAP
          fit_mode=stretch   # display.ini mode (stretch|fit|fill|integer|custom)
          aspect=auto        # display.ini aspect (auto|4:3|16:9|1:1)
          custom_scale_pct=100, custom_offset_x=0, custom_offset_y=0
[audio]   enabled=true       # PS2_SOUND
          volume=100         # GUI sound section
[input]   device=auto, kbm_pad=false   # + input_device.ini bindings
[loads]   instant_cdvd=true  # PS2_CDVD_INSTANT/PS2_DVD_LATENCY_SCALE/PS2_MC_LATENCY
          load_turbo=false   # PS2_LOAD_TURBO (user disliked; default off)
[engine]  vu1_mode=hle       # hle|cached|interp  (PS2_VU1_HLE/PS2_VU_MODE collapsed)
          mtvu=true          # PS2_MTVU
          mtvu_depth=2       # PS2_MTVU_MAX_FRAMES
          pacing=realtime    # realtime|freerun  (PS2_VBLANK_FREERUN/NOVSYNC pair)
          shutdown_wait_s=10 # PS2_SHUTDOWN_WAIT
[dev]     gui_open=false     # PS2_GUI_OPEN
          perf_hud=false     # PS2_PERF (pure reporting; also retires the 15+ per-gate announce latches)
          log_level=warn     # replaces PS2_LOG/PS2_VERBOSE_HW/filter layers
          slowspot_capture=false      # PS2_SLOWSPOT (+_MS, _COOLDOWN_S)
[recomp]  overrides_file=recomp_overrides.ini   # pin= list (gen-time)
          vu_aot=none        # vu_aot.ini corpus_compile (build-time)
          warp_to=0          # PS2_WARP_TO (bench/dev)
```
~30 fields replace the 496-var surface; everything else is either deleted (§1
dispositions) or moves behind the diagnostics build flag.

---
## 7. Notes for the migration gate-purge procedure

1. Purge per family, not per var: a §2.2 master and its sub-gates go together.
2. For each `delete-losing-arm-keep-winner`: confirm the winner via (a) the code default,
   (b) run_play.sh CANON, (c) the introducing docs/loop_state record (§1 CSV
   `introduced_by`); then inline the winning branch, delete the latch + losing branch.
3. For `delete-both` probes: delete the whole gated block; if it references a fix,
   the fix is already unconditional elsewhere — verify by reading the record.
4. **Generator-injected gates** (PS2_SHADOW_DIAG recompiler.cpp:2412/3198/3247/3312,
   PS2_EE_RAWDISP :25786) are purged in recompiler_tool, then code regenerated.
5. 64 undocumented vars (§1) need a code-comment read before deletion — no record exists.
6. Equivalence harness runs before/after each family purge (A2 acceptance: byte-identical
   drawlist_record + bench_report on the anchor savestates with defaults unchanged).

---
*Generated by audit stage A2. Scan scripts preserved in /tmp/s113/a2/ 
(envvars.json, latches.json, patterns.json, var_defaults.json, var_catalog.json,
hotlog.json, diag_census.json, session_ns.json, tag_families.json, final_rows.json).*

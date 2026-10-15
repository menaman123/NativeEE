# A5 — VERIFICATION ASSETS AND EQUIVALENCE-STRATEGY INPUTS

Audit stage A5 of the the title PS2 static-recomp runtime at
`$PS2R_OLD_ROOT` (read-only; no builds, no runs).
Date: 2026-09-22. Method: static inspection of `tests/CMakeLists.txt`, the
generated ctest registries in `build-v2/` and `build-v2-nog/`, corpus
directories, tool sources, and the `docs/loop_state/` evidence chain.
Everything quoted below was read from the repo; nothing was executed.

Purpose: inventory every asset that can PROVE two builds (current vs
refactored) behave identically, characterize what each asset actually pins,
and derive what is missing for a frame-by-frame differential harness.
This document feeds `docs/design/EQUIVALENCE.md`.

---

## 0. Executive coverage matrix

Verdict scale: **STRONG** = bit-exact machine gate exists and is exercised;
**PARTIAL** = numeric/census-level gate or manual protocol; **WEAK** =
diagnostic exists but no gate; **NONE** = no asset.

| Behavior surface | Asset(s) | Gate type | Verdict |
|---|---|---|---|
| EE recompiled code semantics | recompiler-v2 tiered oracle (`tests/recompiler_v2/`): selftest + planted-bugs + 600 s differential sweep (2 analytic anchors, 13 real-ELF fixtures, 50-function fixed-seed random sample vs interpreter) + 12 convention-enforcer gates + regen-determinism gates + `--audit-corpus` vs real ELF | bit-exact per-seed state compare | STRONG (sampled; not whole-program) |
| VU1 microprogram execution (interp vs cached tier) | 126 × `Vu1CachedCompare_*` (`vu1_replay --compare`, `PS2_VU_AOT=0`): byte-diff of post-run VU1 data memory + final VUregs incl. TPC, cold cache, fresh state per run | bit-exact | STRONG |
| VU1 HLE (native lift) vs interpreter | 126 × `Vu1Hle_*` (`vu1_replay --hle --skip-unregistered`): XGKICK GIF-packet decode + diff (`gsp::Decode/Diff`) at the DEFAULT tolerance — ±1 LSB per field (xy/z/rgba/fog/uv = 1 unit, ST 1e-5 rel), `ignore_nop_state=true`; a `--exact` (all-zero tol + strict packets) mode exists and is used by the 8 × `Vu1HleRef_*` harness self-checks; `Vu1HleCatalog` coverage table | packet-level, ±1 LSB | STRONG (near-exact, not bit-exact) |
| VIF1 unpack / MTVU submit | 8 × `VifReplay*` incl. 4 MTVU-equivalence arms (`--mtvu 20 [--vif-fast]`, ± `PS2_VU1_EXEC_ASYNC=1`) + bulk-unpack equivalence | bit-exact on VU1 data memory | STRONG |
| GS draw-stream replay determinism | `gs_corpus/` 19 `.gsdl` (~16 GB, ~4.5 k frames, ~1.06 M draws) + frozen `baseline/*.gsrep` + `ALLOWLIST.txt`; protocol: `REPLAY_HASH` equality, else `gs_replay diff` (per-frame FNV + 32×32 box-average planes) | hash/byte-exact replay-vs-replay | STRONG for replay path; **manual protocol, not ctest** |
| GS replay harness self-integrity | ctest `GsReplayCensus` + `GsReplayDeterminism` (synthetic fixture through the REAL recorder; 3 separate processes; byte-identical `.gsrep`; exit 77 = skip ≠ pass) | exact | STRONG (fixture-level only) |
| GS renderer unit behavior | 110 unit tests + 7 rapidcheck property suites (pixel formats, TFX, blending, rasterization, dirty tracking, sampling, magnification) + 12 in-process A/B equivalence tests (old-path vs new-path: BlockDecode, Vconv, ZTex, IdxTex, ResMemo, VsUv±, UnifPack, GlStateCache, GsDrawIR, VuSpanfold2, GifFast) | exact / property | STRONG |
| Live pixels on screen (present path) | `coldboot_gate.py` (mean-luma ≥ 10 by frame ≤ 900 on cold boot, via `PS2_BB_SNAP` PPMs; exit 0 = luma gate AND 0 FATAL) — created 2026-09-22 after the black-boot incident; `PS2_BB_SNAP`/`PS2_SNAP` samplers; `tools/flicker/` analyzers; `PS2_ARTDET` in-app detectors | threshold gate (cold boot only) | WEAK→PARTIAL (one scene, one metric, brand-new) |
| Scene content vs real hardware | census contract (live census == replay census, string-equal, 5 summary blocks); PCSX2 ground-truth `.gs.zst` dumps + `pcsx2_video_groundtruth/`; `.p2s` PCSX2 states for breakpoint-walk differential traces (e.g. POOL-OP) | census / manual forensics | PARTIAL |
| Audio output | none (unit tests for VAG decoder, voices, ring, state; `PS2_SND_CENSUS` traffic census; `PS2_SND_STATEHASH` = savestate section round-trip only) | — | NONE |
| Input timing / replay | `PS2_PAD_SCRIPT` frame-indexed scripted input (playback only) | — | WEAK (no recorder) |
| Load screens / streaming | `load_transition.gsdl` (208 f, truncated), loadspan/cuelag/LOADTIME probes, `loader_oracle_check.py` (S57 ordering invariants on `PS2_LOADER_ORACLE=capture` traces) | invariant checker + census | PARTIAL |
| FMV | `fmv_attract.gsdl` (401 f, blit draws), `FmvPaceDeadline` unit test | replay + pace bound | PARTIAL (no decoded-pixel pin) |
| Memory card / save flows | `LibMcSaveFlow`, savestate format/atomic/writer/browser unit tests; `.ps2st` schema CRC-gated | unit | PARTIAL (E2_SAVE_REVIEW documents unresolved nondeterministic deep-state divergence) |
| Long-run stability | none (all legs bounded 100–1500 frames; bounded kills by design) | — | NONE |
| Timing/pacing semantics | `TimeDomain`, `EePaceBound`, `PresentPace`, `FmvPaceDeadline`, `RunaheadGuard` unit gates; `PS2_TIME_EMU` emulated-time domain (quantum REGIME) | unit bounds | PARTIAL |
| Savestate round-trip | format/atomic/writer unit tests + per-section CRC + `PS2_SND_STATEHASH` re-serialize check | exact (format level) | STRONG (format), PARTIAL (semantic) |

---

## 1. The ctest suite

### 1.1 Count and where it lives

- `tests/CMakeLists.txt` (152 KB) declares 166 `add_test(` sites; the
  generated registries `build-v2/tests/CTestTestfile.cmake` and
  `build-v2-nog/tests/CTestTestfile.cmake` each register **407 tests with
  identical names** (the per-case VU tests are generated in a loop).
- The task brief said 411. The delta is engine-gating: e.g.
  `RecompV2OracleDiffLegacy` registers only when `RECOMP_ENGINE != "v2"`
  (`tests/CMakeLists.txt`), and both present build trees are v2. 407 is the
  current registered truth; a legacy-engine configure adds the remainder.
- Full name-by-name inventory generated during this audit:
  `/tmp/s113/a5/ctest_registry.txt` and `/tmp/s113/a5/test_families.txt`.

### 1.2 Families — what each group pins

| # | Family | What it proves | Notes |
|---|---|---|---|
| 126 | `Vu1CachedCompare_<case>` | interpreter vs cached-recompiler tier bit-exactness per captured `Vu1Case`: fresh VU1 + fresh memories per run, cold cache, byte-diff of post-run data memory + final VUregs incl. TPC. Exit 0 iff ZERO differing bytes | env `PS2_VU_AOT=0` pinned on every one (AOT tier excluded so the gate tests cached, not AOT) |
| 126 | `Vu1Hle_<case>` | native VU1 HLE handler vs interpreter: both run the same case; XGKICK packet streams decoded (`gsp::Decode`) and diffed at the default `gsp::Tolerance` (±1 LSB: xy/z/rgba/fog/uv = 1 unit, ST 1e-5 rel, consecutive identical state rewrites ignored). Verified cmd: `vu1_replay --hle --skip-unregistered <case>` | `--skip-unregistered` → exit 77 honest-skip for unlifted programs; NOT `--exact` — the refactor's equivalence plan must not assume bit-exact packets here |
| 8 | `Vu1HleRef_<case>` | harness self-check: interpreter registered AS the HLE (`--hle --hle-ref --exact`) must diff clean against itself at ZERO tolerance + strict packet boundaries | guards the diff machinery; the only `--exact` users in the suite |
| 3 | `Vu1ReplaySelfTest`, `Vu1ReplayCachedCompare`, `Vu1HleCatalog` | trace determinism selftest (fabricated case, 2 runs, traces identical + non-empty); one pinned `vutrace_collect` case; catalog over `vutrace_cases2` (per-image hash/dispatch/coverage table) | catalog is the coverage census |
| 12 | `*Equivalence` | in-process A/B: legacy code path vs optimized path on identical inputs (BlockDecode, Vconv, ZTex, IdxTex, ResMemo, VsUv + Alt16Off arm, UnifPack, GlStateCache, GsDrawIR, VuSpanfold2, GifFast) | this is the repo's standing pattern for "fast path == slow path" |
| 8 | `VifReplay*` | VIF1 chain replay vs ground truth; MTVU equivalence: `--mtvu 20` submits via the MTVU worker and asserts identical VU1 data memory vs synchronous, ×{plain, `--vif-fast`} × {`PS2_VU1_EXEC_ASYNC=1` on/off} | the only MTVU determinism gate in the suite |
| 7 | `*Properties` | rapidcheck property suites over GS pixel pipeline math | vendored rapidcheck via `_deps` |
| 4* | `RecompV2Oracle*` | recompiler-v2 semantic oracle: selftest, planted-bugs (mutation kill check), differential sweep V2 (TIMEOUT 600 — the only long test), legacy sweep (gated off in v2 trees) | fixed-seed xorshift → same 50-function sample both engines |
| 2 | `R141CallCensus`, `RecompCorpusAudit` | corpus audit: `recompiler_tool_v2 --audit-corpus tests/recompiler_v2/corpus_observed_p4.txt` against the real `SLUS_123.45` ELF | pins discovery against observed-at-runtime function set |
| 2 | `GsReplayCensus`, `GsReplayDeterminism` | gs_replay harness gates on a synthetic fixture built through the REAL recorder: census equals reader ground truth exactly; 3 separate-process replays hash-identical + byte-identical `.gsrep` + diff reports 0 | **does NOT touch the 16 GB corpus** — corpus sweeps are the manual BASELINE.md protocol |
| 110 | unit/other | savestate format/atomic/writer/browser, sound (VAG/voice/ring/state/wire-governor), IOP (sysmem heap, sysclib map), pace/timing (TimeDomain, EePaceBound, PresentPace, FmvPace, RunaheadGuard), ring log (+TSAN arm), GIF drain thread (+TSAN arm), input (InputMapper, PadBuffer), display/viewport, bug-report, recompiler enforcement gates ×12 (delay-slot call/branch, fcf-internal, switch-table, probe-silent, lint × legacy/v2), regen determinism ×2, override pins, `Ps2Argv`, `ShutdownPolicy`, etc. | includes pins like `ShadowOffsetPin`, `NewGameSwitchPin` freezing specific game behaviors |

Cross-cutting counts: 143 tests carry an `ENVIRONMENT` property; 17 distinct
`PS2_*` pins are used (126 × `PS2_VU_AOT=0`; 3 × `PS2_GS_DEPOSIT_EEAPPLY=0`;
2 × `PS2_VU1_EXEC_ASYNC=1`; 2 × `PS2_LOG_RING_EVENTS=1024`; 1 each of
`PS2_GS_ALT16UV=0`, `PS2_VU_SUPERBLOCK=0`, `PS2_VU_STALLFOLD=0`,
`PS2_VU_RUNFLAT=1`, `PS2_VU_SPANFOLD=0`, `PS2_VU_SPANFOLD2=1`,
`PS2_VU_FASTPACK=0`, `PS2_MTVU_MAX_FRAMES=2`, `PS2_MTVU_MAX_FRAMES=0`,
`PS2_EE_RAWDISP=1`, `PS2_GL_BLEND_SUB=1`, `PS2_GL_BLEND_SUB=0`,
`PS2_GL_L2_SU=0`). These tests are hatch-COUPLED: a refactor that purges a
gate must simultaneously retire the test arm that pins its off/on state.
(For scale: host_app+tests reference **566 distinct `PS2_*` env vars**.)

### 1.3 Runtime cost — evidence status

No aggregate suite timing is recorded in the repo
(`Testing/Temporary/CTestCostData.txt` is a stale 1-line file; LastTest.log
is an empty Aug-15 header). Quantified points that do exist:

- `RecompV2OracleDiffV2` carries `TIMEOUT 600` — the suite's only long test.
- BASELINE.md: a 7-capture corpus re-replay is "7 replays, ~14 s" — but that
  is the manual gs_replay sweep, not ctest.
- The 252 VU replay tests each load a 33,456-byte case and run one/two VU1
  programs — sub-second each by construction (fixed 32 KB state, bounded
  micro programs); process-spawn dominates.
- `GsReplayDeterminism` spawns 3 GL processes + a diff (needs a GL context;
  exit 77 skip when headless).

The refactored tree should make suite cost a recorded artifact (ctest
`--output-junit` or CostData kept per run).

---

## 2. GS corpus (`gs_corpus/`)

### 2.1 Captures — 19 `.gsdl`, ~16 GB

From `gs_corpus/MANIFEST.md` (all v2 `.gsdl`, verts ON + upload pixels ON =
replay-grade, recorded on the canonical bench env `PS2_VU_MODE=cached
PS2_MTVU=1 PS2_VIF_FAST=1 PS2_GL_FAST=2 PS2_FPS_CAP=35 PS2_NOVSYNC=1
PS2_PERF=1 PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1`, savestates read-only):

| capture | anchor / scene | frames | draws |
|---|---|---|---|
| boot_menu | cold boot: BIOS→title→attract handoff | 401 | 11,927 |
| fmv_attract | cold boot: intro FMVs (sceMpeg→IPU→GS blit) | 401 | 1,348 |
| island_slot7 | gameplay island | 400 | 11,713 |
| slowspot | 20 fps slowspot f7400–7700 | 301 | 23,197 |
| load_transition | long load screen (leg ended early; truncated tail kept) | 208 | 3,667 |
| s57_walk | walk scene w/ `PS2_PAD_SCRIPT="1-999999:LY=0"` | 400 | 26,449 |
| save_select | save-select dialog + 3D scene; Direct-fallback max (16,899) | 400 | 26,069 |
| bad_shadows [SH1/SH5] | FOOTPRINT-SHADOW chain live | 81–83 | 6,306→12,923 |
| crystal [CR3] | crystal cavern; 99% unstamped raw/CPU; fallback worst case | 403 | 208,679 |
| cavern_fog [S79] | fog/tint scene; CT24 target axis; T8H fog composite | 401 | 184,066 |
| cavern_f13611 / f19112 / f20442 [CS11] | post-fix cavern slowspots | 401/401/400 | 182k/208k/85k |
| cavern_mipmap [S82] | far-corrupt mip window; TEX1 MMIN=4 MXL=2..4 + MIPTBP chains live | 400 | 140,182 |
| cavern_checker [S83] | depth-keyed fog checkerboard + gas particles | 141 | ~21 k |
| cavern_gas [D6] | gas slowspot with gate path native (first complete-geometry capture) | 403 | 170,971 |
| cavern_flicker_f37152 [FL5] | fire scene where INTERP present-rate shimmer was root-caused | 163 | 49,338 |
| cavern_wallnoise_f39807 [WN4] | dark-wall CT16 blit + fog lanes + shadow accumulator | ~100 leg | 451/f |
| cavern_streaks | (baseline pair present; capture in corpus) | — | — |

Census artifacts: `census/<cap>.txt` (12 files, `gs_replay census --configs`
output) + `CENSUS_MASTER.md` (merged master: class share table over 104,370
GA3 draws — Direct-fallback 28.95%, S9 shadow composite 24.55%, world
tristrip 24.11%, … — and the **58-distinct-draw-config master table**, the
corpus-containment taxonomy).

### 2.2 Baselines, ALLOWLIST, and the re-freeze history

`baseline/` holds per capture: `<cap>.gsrep` (header + per-frame full-res
FNV + 32×32 box-averaged RGBA downsample) and `<cap>.replay.txt` (per-frame
`f= vf= draws= verts= w= h= fb= ds=` checksum lines + census block +
`REPLAY_HASH`). Provenance stamped: build, GL string (`OpenGL 4.1 Metal -
90.5 / Apple M4`), and a **clean-env fingerprint**
(`env_ps2{n=0 hash=…}` in every replay header).

`ALLOWLIST.txt`: exactly **one** permanent adjudicated alternate hash —
slowspot `0x99b108280b0c4799`, the [GB2] warmup-frame ledger (300/301
identical, single diff frame f=2, max Δ=9), carried since GB2 and
re-confirmed at every sweep. Sweep contract: accept `replay.txt` hash OR any
ALLOWLIST hash. Adding entries requires a full frame-level adjudication in a
loop_state doc — explicitly "NOT a dumping ground".

Freeze/re-freeze ledger (from `BASELINE.md`) — the honest-adjudication
record the task asked about:

| Event | Date | Captures | Nature |
|---|---|---|---|
| GA4 initial freeze | 09-07 | 7 | 3× replay per capture, separate processes: hash-identical, `.gsrep` byte-identical, live census EXACT (string-equal) |
| [SH1] extension | 09-09 | +bad_shadows | new capture frozen |
| [SH5] **re-freeze** | 09-09 | bad_shadows | honest re-adjudication: prior baseline was frozen in the zero-shadow-collapse era. Documents (a) live recordings of this scene are NOT run-to-run content-identical (draw list depends on real-time pacing), so the baseline pins REPLAY DETERMINISM, not visual health; (b) a proven replay-fidelity gap: the T8H composite's CLUT page (cbp 0x380000) is all-zeros in replay vs a real palette live — "replay output of this chain is NOT live-faithful until the recorder carries that CLUT (follow-up owed)" |
| [CR3] extension | 09-12 | +crystal | frozen from the HATCH-OFF replay; other 8 re-verified byte-identical |
| [S83/CB10] | 09-13 | — | ALLOWLIST created (GB2 entry) |
| [WN4-CT16Q] **re-freeze** | 09-16 | 7 | intentional change (retail CT16 5-bit quantization, default ON). Adjudication: every diff frame's max channel delta ≤ 9 = one 5-bit step + rounding; 2× determinism re-proven; other 11 captures byte-identical |
| [S107-FOGLANE-LIVE] **re-freeze** | 09-20 | 8 | intentional fix. Adjudication: hatch-off replay reproduced ALL 8 prior hashes exactly (delta isolated to the fix); changed set == exactly the captures engaging the fog-composite serve; 3× determinism |
| [S111-READVIEW] **re-freeze** | 09-21 | 9 | intentional fix (RT-serve read-view mapping). Same delta-isolation proof: hatch-off byte-identical to prior baselines; magnitudes quantified per capture; 3× determinism |

Reading: **zero unexplained drift events**. Every re-freeze is an
intentional-change adjudication with a delta-isolation proof (turn the new
hatch off → byte-identical to the old baseline), and the single standing
alternate hash is a documented warmup artifact. This adjudication protocol
(hatch-off reproduction + bounded-magnitude quantification + N× determinism
re-proof) is the strongest procedural asset in the repo and must be carried
into the refactor's equivalence plan. Note the flip side: the protocol
depends on the hatches existing — a gate-purged refactor needs a different
delta-isolation mechanism (two binaries instead of one binary × two envs).

### 2.3 `gs_replay` capabilities (`tests/gs_replay.cpp`)

```
gs_replay replay   <in.gsdl> [--out f.gsrep] [--ds N] [--frames a-b] [--quiet] [--skip SPEC]   (exit 77 = no GL)
gs_replay census   <in.gsdl> [--frames a-b] [--configs]                                        (CPU-only)
gs_replay diff     <a.gsrep> <b.gsrep> [--tol N] [--pct P] [--max N]                           (exit 3 = DIFF)
gs_replay classdiff <a.gsdl> <b.gsdl> [--frames a-b] [--seq]                                   (CPU-only numeric; exit 3 = DIFF)
```

- `--skip SPEC` = [S105] ablation filter (drop draw classes during replay —
  bisection tool).
- `--seq` on classdiff = ordered sequence compare (vs multiset).
- `--configs` on census = the 58-row master config table.
- `PS2_REPLAY_LOG=<path>` re-arms the logFile-gated backend diagnostics
  inside replay (off by default; no rendering-byte effect).
- Truncated recordings load with the tail frame kept (bounded-kill captures
  stay valid).
- Downsample grid default `--ds 32`, clamped 4..256.

### 2.4 What `.gsdl` records — and what it does NOT

Format: `host_app/hle/drawlist_record.h`, magic "2DLR", version 2. Records
every RenderJob pushed to the graphics queue in EE program order, taken at
the **EE-side producer tap** (so captures are present-interp-free by
construction — the [GE2] contract cited in MANIFEST for cavern_flicker).

Recorded per draw (`DrawRec`): prim (type/gouraud/fog/fst/abe/scanmsk/
shuffle), texture (tbp/tbw/tpsm/tw/th, cbp/cpsm/csm/csa/cld, mmag/mmin,
wms/wmt + min/max UV, TEXA), blend (colclamp/pabe/fba/A/B/C/D/FIX, FOGCOL),
frame (fbp/fbw/fbmsk/psm), zbuf (psm/zmsk/zbp), scissor, full TEST row
(ate/atst/aref/afail/date/datm/zte/ztst), vertex array (x,y,z,s,t,q,uv,
rgba,fog,draw_kick) or hash+bbox only when `_VERTS=0`, and the v2
draw-identity stamp: `hle_handler`, `hle_frame_inst`, `hle_image`,
`hle_kick_pc`, `hle_kick_ord`, decoded XYOFFSET, active GS context.
Non-draw jobs (`JobRec`): type/seq/src_frame/args/vram_epoch + upload
payload (bytes included iff `_PIXELS=1`, else hash only). Job types cover
DrawBatch, scissor/blend/depth state, DISPFB/DISPLAY/BGCOLOR, VSync,
InvalidateTextures, UploadVRAMTexture, SyncAllRTsToVRAM.

**Not recorded — the live-vs-replay divergence classes:**

1. **VRAM/CLUT texture-window bytes** — only FNV hashes
   (`vram_snapshot_hash`, `clut_snapshot_hash`). VRAM-decoded texels replay
   from the zero mirror. Consequences documented per capture:
   savestate-resident CLUTs make replay content diverge from live ([S79],
   [CS11], [D6], [S83] notes); the [SH5] bad_shadows T8H CLUT gap; the
   cavern_checker idx-serve decline (replay's zero-VRAM CLUT declines the
   fmt=5 GPU serve → replay exercises the legacy CPU chain). Census/order/
   hash gates remain valid; pixel content for those windows is not
   live-faithful.
2. **Mip chains** — `DrawRec` carries no MIPTBP1/2 and no TEX1
   MXL/LCM/K/L; MANIFEST [S82]: "replays of this capture exercise the
   legacy level-0 texture path (that invariance is what keeps every pre-S82
   baseline byte-identical); the mip sampling path is proven live-side."
3. **DTHE/DIMX dither state** — no fields (grep-confirmed); the [WN4]
   CT16-quant write rule is DTHE-gated live but replay applies the hatch
   default. Baselines were re-frozen around it, so this is a pinned,
   documented approximation.
4. **GS deposits / EE-visible readback events** — no record kind; the
   deposit lane (`PS2_GS_DEPOSIT_EEAPPLY`) and fog-lane serves are
   reconstructed by the replay pipeline, not replayed from capture (this is
   exactly why S107/S111 changed replay output and forced re-freezes).
5. **Hash-function caveats**: v2 uses word-stride FNV (`Fnv1aFast`) and a
   sampled fingerprint for >16 KB payloads (size + 4 KB stripes) — content
   identity within one recording/build only; cross-run content hashes are
   documented as inherently nondeterministic.
6. **Upstream stages** — the tap is post-VU1/post-VIF/post-GIF: nothing in
   `.gsdl` can catch a VU1/VIF/GIF regression that changes the produced
   batches (that's the VU/VIF corpora's job), nor scanout/present policy
   (explicitly outside the oracle boundary per BASELINE.md).

### 2.5 The recorder and its extras

Gate: `PS2_DRAWLIST_RECORD=<path>` (latched at first use);
`PS2_DRAWLIST_RECORD_FRAMES=a-b`, `_VERTS=0` (~50× smaller), `_PIXELS=1`
(upload payloads), `_PPM=1` (also write the presented backbuffer per frame
to `<path>.frames/f%06u.ppm`). Off-cost: one null-pointer test per push.
Reader: `hle/drawlist_reader.h`; offline tool `tests/drawlist_tool.cpp`
(`stats` census / `dump [--frame N] [--verts]` / `diff <a> <b> [--frame N]
[--offset K] [--tol-xy F] [--tol-z F]` with frame alignment offset).

---

## 3. VU corpus

### 3.1 Case inventory

| Directory | Count | Role |
|---|---|---|
| `vutrace_collect/` | **3,293** `.bin` | raw auto-capture pool (bulk sweeps; 1 case pinned in ctest; the rest exercised via `--catalog` / manual sweeps — `run_bench.sh` comments show `vu1_replay --catalog vutrace_collect vutrace_cases2`) |
| `vutrace_cases2/` | **126** `.bin` | curated ctest corpus (each = 2 gates: CachedCompare + Hle). Scene families: `0000-0007_1748` boot, `crys_*` + `crysnc00-18_8980533e` crystal cavern, `w12isl_*` island, `w12s7/s9`, `w13hub`, `w14hub_0000-0038` + `w14bkstp` (backstop-generated via `scripts/w14_make_backstop_cases.py`), `w2*`, `vu1_full_*` (22 whole-program images) |
| `vutrace_cases/` | 62 `.bin` | legacy pool (pre-cases2) |

Every case is **exactly 33,456 bytes**: the `Vu1Case` POD
(`host_app/vu_trace_case.h`) = full VUregs + 16 KB micro memory + 16 KB data
memory + VIF1 TOP/TOPS/ITOP/ITOPS + start pc. Filename encodes
`case_<ordinal>_<entrypc>_<imagehash>`.

### 3.2 Capture mechanism

`host_app/vu.cpp` `[VUTRACE]` block (docs/vu1_trace_harness.md): pure
observation — with no `PS2_VUTRACE*` env set, the hot path is a single
cached bool. Capture at `VU1::Execute` entry (vu_id==1), i.e. program start,
pipeline assumed drained. Env: `PS2_VUTRACE=1` (per-bundle trace →
`PS2_VUTRACE_FILE`, changed-registers-only lines, raw IEEE bit patterns),
`PS2_VUTRACE_MAX`, `PS2_VUTRACE_CAPTURE=N` (dump first N entries as cases),
`PS2_VUTRACE_PC=hex` (filter by entry pc), `PS2_VUTRACE_DIR`.
Offline trace diff: `tools/vu1_trace_diff.py` (first divergent bundle with
context; also usable against PCSX2-derived traces).

### 3.3 `vu1_replay` modes (tests/vu1_replay.cpp)

- legacy: `vu1_replay <case> [trace_out] [postdata_out]` — interp + VUT
  trace (+ `.regs` raw final VUregs incl. TPC).
- `--selftest` — fabricate, replay twice, traces identical + non-empty.
- `--mode interp|cached <case>` — single run; cached mode is probe-free by
  contract (arming a trace would silently fall back to interp — the harness
  explicitly unsets trace envs in compare/bench so the gate can never
  degenerate to interp-vs-interp).
- `--compare <case | --fabricate>` — THE bit-exactness gate (see §1.2);
  `--expect-aot` (honest SKIP 77 when the build's AOT registry is
  intentionally empty — double-keyed on registry emptiness AND
  `vu_aot_manifest.txt mode=none` so a generation defect still FAILS),
  `--expect-noaot`, `--compare-cycle`.
- `--bench N` / `--bench-cached` — wall-ms per mode.
- `--dump-kicks` — decode every XGKICKed GIF packet (state writes +
  vertices) — the lift-authoring view.
- `--hle [--hle-ref] [--exact | --tol-xy/--tol-z/--tol-rgba/--tol-st]
  [--strict-packets] [--skip-unregistered] [--max-lines]` — HLE vs
  interpreter packet diff via `gsp::Decode/Diff`.
- `--catalog <dir|cases…>` — per-image table: image/variant hashes, dispatch
  chain, kicks/verts/bundles, HLE coverage.

### 3.4 One stage upstream: `vif_replay`

`tests/vif_replay.cpp` links the REAL `vif.cpp` against inert stubs and
re-feeds a VIF1 DMA chain from an EE memory snapshot exactly as `dmac.cpp`
does; deterministic offline reproduction of UNPACK lane placement in VU1
data memory, byte-diffable against live-PCSX2 ground truth; plus the
`--mtvu` equivalence selftest (R121 recorder intercept: MTVU-submitted vs
synchronous unpack must produce identical memory).

---

## 4. Diagnostics, pixel probes, and anchor tooling

| Tool / gate | Location | What it does |
|---|---|---|
| Drawlist recorder + PPM | `hle/drawlist_record.*` | §2.5; `_PPM=1` writes presented backbuffer per frame |
| `PS2_BB_SNAP=START:END[:STRIDE]` | `graphics_thread.cpp` | post-present backbuffer PPM sampler → `logs/bb_frame_<N>.ppm` (window drawable: scanout letterbox + GUI = exactly what SwapWindow shows) |
| `PS2_SNAP=S:E[:K]` + `_DISP_ONLY` + `_DIR` | GL backend | target-FBO BMPs (`disp_F<N>.bmp`, native res, pre-scanout — bypasses SyncRTToVRAM→gs_vram→Scanout mirror) |
| `PS2_ARTDET=1` (+`_FLICKER`, `_TEST`) | `artdet_flicker.h` | in-app left-zone period-2 (ABAB) flicker detector + [R199-DET] spatial censuses; `save_states/artdet_f*.{ps2st,bmp}` pairs are its captured evidence anchors |
| `tools/flicker/` | tdiff, pixflip, chromaflip, greensig, spatsig, bbdiff | offline numeric analyzers over BB_SNAP/SNAP frame dirs (band tables, phase-flip, chroma ABAB, bb-vs-fbo pairing); tables only, no image viewing |
| `PS2_PERF=1` + `scripts/run_bench.sh` | R127 | canonical bench: per-window `[PERF]` attribution; `bench_report.json` (per-frame times over analysis window, fps stats, resolved gates, subsystem means, GL census finals, rss); `--frames/--report/--window-start/WARP=N` (warp = freerun + skipped presents to frame N, window forbidden to overlap) |
| cuelag / loadspan | `loadspan.h`, `cuelag_probe.h` | load-span cue-lag probes at vblank OnTick; queue-streak counters |
| INLAT | `pad_input.h`, `input/input_latency.h` | [R183] input-latency instrumentation: press-latch consumption handshake + T5 display-relative probe |
| LOADTIME | `memory.cpp`, loadspan, LOADTIME_IMPL docs | load-time measurement + wedge watches (ctrl-latch event-word) |
| `tools/coldboot_gate.py` | [S112-BLACKBOOT] | cold-boot PIXEL gate: runs binary cold under the canonical user env + `PS2_BB_SNAP`, parses PPMs, asserts mean luma > `--min-luma` (10.0) by `--by-frame` (900); exit 0 = luma gate AND 0 FATAL. Pure stdlib |
| `tools/loader_oracle_check.py` | S57 | machine-checks 4 ordering invariants on `PS2_LOADER_ORACLE=capture` traces (baseline vs HLE legs) |
| `scripts/run_play.sh` | play anchor | real-time 60 Hz canon env (dev defaults documented inline: PERF on, LOG off, cached VU + HLE route, MTVU, TIME/BUDGET emu domains, instant loads); accepts KEY=VAL overrides |
| `tools/run_bounded.sh` | leg bounding | bounded bg+sleep+pkill invocation (the corpus-capture pattern; truncated-tail-safe) |
| `scripts/check_vu_aot_optimized.sh`, `w14_make_backstop_cases.py` | VU AOT/backstop | AOT build check; synthesizes the w14 backstop cases |
| `tools/gs_dump_parser.py`, `summarize_gsdump.py`, `parse_gsdump.py` | PCSX2 side | parse PCSX2 `.gs.zst` dumps (ground-truth captures in repo root + `pcsx2_video_groundtruth/`) |

**The anchor lesson (verbatim evidence, BLACKBOOT_IMPL.md):** the [S96]
budget-emu base (landed 2026-09-18/19) starved the cold-boot loader; the
result was a 100% black screen with **0 FATAL, 516 presents, 4,321 draws** —
"every prior cold-boot anchor ('reach title', 'COLD 600f exit=0 FATAL=0')
checked only exit codes and FATAL lines — a 100% BLACK screen passed them
all." Discovered s112 (2026-09-22) — the regression lived ~4 days behind
passing anchors. `coldboot_gate.py` (same day) is the corrective: assert
PRESENTED pixels. It currently covers exactly one scenario (cold boot, mean
luma). No per-scene pixel anchors exist yet.

---

## 5. Savestates

### 5.1 Host-format states (`save_states/`, `.ps2st`)

87 directory entries: ≈40 `.ps2st` states plus paired `.bmp`/`.ppm`
thumbnails and `_logs` dirs (the slowspot auto-capture bundles). Scene
coverage (names are self-describing): `artdet_f24488..f37160` (fire/flicker
scenes), `ps2state_slot0_f36935..f59991` (incl. `f46203_red_crystal`,
`f46949_yellow_crystal`, wallnoise `f39807`), `slowspot_f*_s*` (auto-saved
slowspot states f72..f64266 with logs+PPM), plus the MANIFEST-referenced
anchors (`ps2state_slot7_f36230`, `slowspot_f7260`,
`ps2state_really_long_load_screen`, `ps2state_game_crashes`,
`ps2state_load_game_select_save_issue`, `ps2state_bad_shadows`,
`slowspot_f17425_crystals`, `ps2state_slot0_f13881`,
`slowspot_f13611/f19112/f20442/f29548`, `weird_pixelized_glithcy_filter`).
Every GS corpus capture is anchored on one of these (or cold boot), so the
state set IS the scene-selection mechanism for the differential harness.

Format (docs/SAVESTATE_SCHEMA.md, `savestate_format.*`): "PS2SAVE1" v2, 28
sections, per-section CRC32 + trailer CRC, REQUIRED-flag policy for forward
compat; sections cover RAM/SPAD/META/VRAM/VU memories, THRD (full thread
records + CpuContext + call chains), SEMA/EVFL/INTC/DMAC, GSPR/GSST (v2
serializes the partially-assembled vertex queue/draw buffer — required for
deep-scene saves), VIFS/GIFS/PADS/FILE (CDVD handle table)/CNTR (frame
counters)/MISC/HEAP (allocation identity preserved). Load path:
`host_app --load-state PATH`; save: `--save-state-at N`, F-keys, slots.
Unit-gated by `SavestateFormat/Atomic/Writer/Browser` + reader-policy tests.

### 5.2 PCSX2 states (`crash_tt_save_states/`, `.p2s`) — cross-engine reality

27 entries, all PCSX2 savestates (`.p2s`): splash/start-button/FMV1/FMV2/
iceberg/bullseye/cutscene/load-screen-begin/-transition/shadow-in-game/
slot6-hunt/long, plus `gs_dump/`, `logs/`, `moments/`. **There is no
cross-engine loader**: no `.p2s` reader exists in host_app (grep: the only
mention is a comment). Their role is oracle-side: they anchor PCSX2
breakpoint walks and dumps that are diffed against host traces — e.g. the
[POOL-OP] differential pool-op trace in `main.cpp` ("The same sequence is
captured on PCSX2 … from load-screen-begin.p2s; first diverging
(op,args,result) pins the mistranslated function"), the `.gs.zst` GS dumps,
and `pcsx2_debug_mcp`/`pcsx2-mcp` tooling. Cross-engine equivalence is
therefore a MANUAL forensic workflow, not an automated gate — and `.ps2st`
states are host-version-coupled (REQUIRED-section policy), which the
refactor must respect or re-anchor.

---

## 6. What is NOT covered today (gaps, with evidence)

1. **Present-path pixels** — the black-boot incident (§4) proved anchors
   were exit-code/FATAL-only for ~4 days. `coldboot_gate.py` covers cold
   boot only. No per-scene, per-frame pixel gate exists; graphics_thread
   presentation policies (stale-composite drops, run collector, pacing) are
   explicitly OUTSIDE the frozen oracle boundary (BASELINE.md fidelity
   contract).
2. **Audio output equivalence** — nothing hashes the mixed output stream.
   Assets stop at unit tests (VAG/voice/ring/state/wire-governor),
   `PS2_SND_CENSUS` (traffic counts), and `PS2_SND_STATEHASH` (savestate
   section round-trip only). A refactor could change audible output with
   zero gate failures.
3. **Input timing / record-replay** — `PS2_PAD_SCRIPT` provides
   frame-indexed scripted input (playback), used for capture legs
   (s57_walk, cavern_mipmap). There is no input RECORDER and no
   input-latency equivalence gate (INLAT is instrumentation only).
4. **Load screens** — one truncated 208-frame capture + invariant checker;
   the save-select wedge and LOADTIME machinery are probe-based. Load
   timing is documented as nondeterministic across sessions
   (E2_LOG_REVIEW).
5. **FMV** — draw-level only (fmv_attract blits); no decoded-IPU-pixel pin;
   pace bounded by a unit test.
6. **Memory card / deep-state saves** — E2_SAVE_REVIEW records an OPEN
   defect class: "deep-state loads still pop the unfixed uncommanded save
   menu and diverge nondeterministically across fresh processes (80% pixel
   diff)" — i.e., the flow is not even self-consistent yet, so it cannot be
   an equivalence anchor.
7. **Long-run stability** — every leg is bounded (100–1500 frames; bounded
   kills by design; 1M-bundle watchdog truncation incidents documented in
   [D6]). No soak asset.
8. **GS corpus sweeps are manual** — ctest carries only the synthetic
   fixture gates; the 19-capture/16 GB sweep + ALLOWLIST protocol lives in
   BASELINE.md discipline, not automation.
9. **EE whole-program coverage** — the oracle sweeps 2 anchors + 13
   fixtures + a 50-function fixed-seed sample + planted bugs + corpus
   audit; the rest of the ~1 MiB text is pinned only transitively (by the
   game running).
10. **`.gsdl` fidelity gaps** — §2.4: CLUT/VRAM bytes (hash only), mip
    chains, DTHE/DIMX, deposits. Replay ≠ live for those windows; the
    corpus proves replay-determinism and census identity, not
    live-pixel identity.
11. **Cross-build float/GL variance** — baselines are pinned to one machine
    (`OpenGL 4.1 Metal / Apple M4` stamped); nothing quantifies drift on
    other GPUs/drivers (BASELINE.md's "machine-drift control" is a same-
    machine day-2 recheck).

---

## 7. Determinism properties and the deterministic differential harness

### 7.1 What is deterministic today (proven)

- **gs_replay replay-vs-replay**: strict — hash-equal AND byte-equal across
  separate processes, re-proven 2–3× at every freeze (19/19 captures).
- **vu1_replay / vif_replay**: fresh state per run; selftest asserts
  trace identity; 252 ctest gates rest on it.
- **Recorder tap**: `.gsdl` captures at the EE-side producer boundary —
  present-phase interpolation cannot contaminate a capture ([GE2]).
- **Recompiler**: regen-determinism gates (`RecompRegenDeterminism[V2]`)
  pin tool output; oracle uses fixed-seed xorshift.
- **Env fingerprint**: `env_ps2{n,hash}` stamped in replay headers — env
  drift is detectable.

### 7.2 What is NOT deterministic live (documented)

- **Wall-clock vblank ticker**: 60 Hz `sleep_until`; per-boundary tick gap
  oscillates 1↔2↔3 with host noise (time_domain.h) — mitigated by
  `PS2_TIME_EMU` (default ON): game-visible clocks count an EMULATED
  quantum (REGIME 1..5 via hysteresis + lead governor), but REGIME itself
  adapts to sustained wall rate → still host-dependent across machines.
- **MTVU**: VU1 on a worker thread with runahead
  (`PS2_MTVU_MAX_FRAMES`); equivalence is gated at the VIF-submit level
  (§1.2) but live scheduling interleave is wall-driven.
- **Live draw content depends on pacing**: [SH5] — same scene, different
  runs, different draw lists (per-frame blob/seagull draws present or not);
  TILEPATCH "cross-run settle nondeterminism"; pose regime ±4% band
  (CAVERN_MOTION); load timing across sessions (E2_LOG_REVIEW); deep-state
  load divergence (E2_SAVE_REVIEW).
- **Present phase**: INTERP=1 present-rate shimmer (root-caused R199-DET);
  stale-composite drops; GUI overlay in the backbuffer.
- **IOP deferred completions**: FIX-C events on `steady_clock` due-times
  (CD ~3000 µs, SPU ~300 µs) — wall latency models (TIMEDOMAIN table #9).
- **Cross-run content hashes**: "inherently nondeterministic anyway"
  (drawlist_record.h, citing VU1_HLE_R2 R4 + PCSX2 precedent).

### 7.3 Requirements for `record inputs + savestate → frame-hash sequence`

The building blocks that already exist:

| Need | Existing asset |
|---|---|
| Scene anchor | `--load-state` + ~40 `.ps2st` states (§5.1) |
| Bounded run | `--bench-frames N` (bench.h), `--warp-to-frame` |
| Scripted input | `PS2_PAD_SCRIPT "F1-F2:BTN/axis=val"` (frame-indexed) |
| Emulated time | `PS2_TIME_EMU` + `PS2_TIME_EMU_REGIME=N` (pin the quantum — removes the adaptive detector's host dependence) |
| Tick-clocked heartbeat | `PS2_HB_TICKCLOCK` (STREAM.IRX beats per generator tick) |
| Deterministic loads | `PS2_CDVD_INSTANT`, `PS2_DVD_LATENCY_SCALE=0`, `PS2_MC_LATENCY=0` |
| Free-running ticks | `PS2_NOVSYNC=1` → `PS2_VBLANK_FREERUN` (ticks fire as pipeline completes — decouples from wall) |
| MTVU off | `PS2_MTVU=0` (equivalence to on-path already gated) |
| Pixel tap | `PS2_BB_SNAP` (presented backbuffer) / `PS2_SNAP _DISP_ONLY` (pre-scanout FBO); GUI removable via `PS2_GUI_OPEN=0` |
| Env pinning | `env_ps2` hash stamping precedent |

What is missing (the build list for the EQUIVALENCE plan):

1. **A frame-hash mode** — BB_SNAP writes PPM files (0.9 MB/frame); the
   harness needs an in-process per-present hash (FNV of the drawable or of
   the pre-scanout FBO at fixed scale) appended to a log — cheap enough
   for every frame of a long run. (All pieces exist: the PPM reader path +
   Fnv1aFast.)
2. **An input recorder** — capture live pad state per vsync frame into the
   `PS2_PAD_SCRIPT` grammar (or a binary sibling), so human play can be
   replayed. Playback exists; recording does not.
3. **A fixed-quantum profile** — one blessed env set:
   `PS2_TIME_EMU=1 PS2_TIME_EMU_REGIME=<pin> PS2_VBLANK_FREERUN=1
   PS2_MTVU=0 PS2_HB_TICKCLOCK=1 PS2_CDVD_INSTANT=1 PS2_DVD_LATENCY_SCALE=0
   PS2_MC_LATENCY=0 PS2_GUI_OPEN=0 PS2_FPS_CAP=0 PS2_GS_INTERP=0` — plus
   converting the remaining `steady_clock` game-visible inputs (FIX-C
   deferred completions, TIMEDOMAIN rows #1/#2/#4/#7/#9) to the tick
   domain under that profile. The TIMEDOMAIN inventory is the complete
   checklist of what must be quantum-locked.
4. **Headless present** — GL context without a visible window (gs_replay
   already runs GL offscreen; host_app needs the same for CI soak).
5. **Same-machine discipline** — baselines pin GL renderer strings;
   cross-binary comparison (old vs new build) on ONE machine sidesteps
   GPU variance; the harness should stamp and assert the GL string +
   env hash like `.gsrep` headers do.
6. **Divergence localization** — on hash mismatch at frame N: auto-save
   state at N-k, re-run both binaries with `PS2_DRAWLIST_RECORD` over the
   window, then `gs_replay classdiff --seq` + `drawlist_tool diff` to pin
   the first differing draw; VU/VIF cases capturable at the same window
   (`PS2_VUTRACE_CAPTURE`). All of this exists; the plan only has to wire
   the escalation path.
7. **Audio lane** — add a mixed-output ring hash per N ms (nothing exists).
8. **Savestate as crossing point** — `.ps2st` REQUIRED-section policy means
   the refactored binary must keep reader compatibility (or provide a
   converter) for states to serve as shared anchors between old/new builds.
   This is a hard constraint on the refactor's state layout.

### 7.4 Assessment for the equivalence strategy

The repo's strengths are exactly the inputs a differential harness needs:
a frozen, adjudicated, replay-deterministic GS oracle with a documented
re-freeze protocol; a 3,293-case VU pool with bit-exact replay gates; an
in-process A/B equivalence idiom; and a scripted-input + savestate + warp +
bench substrate. The two structural weaknesses are (a) everything
pixel/present/audio-side is diagnostic, not gated — the black-boot incident
is the canonical proof — and (b) the strongest protocols (corpus sweeps,
hatch-off delta isolation) are manual and hatch-dependent, so a gate-purged
refactor must replace "one binary × two envs" adjudication with "two
binaries × one profile" comparison, which is precisely the deterministic
harness above.

---

## Appendix A — where-is-it index

| Asset | Path |
|---|---|
| ctest source / registries | `tests/CMakeLists.txt`; `build-v2{,-nog}/tests/CTestTestfile.cmake` |
| GS corpus + census | `gs_corpus/*.gsdl`, `gs_corpus/census/`, `CENSUS_MASTER.md`, `MANIFEST.md`, `census_merge.py` |
| GS baselines | `gs_corpus/baseline/{*.gsrep,*.replay.txt,ALLOWLIST.txt,BASELINE.md}` |
| gs_replay / drawlist tools | `tests/gs_replay.cpp`, `tests/drawlist_tool.cpp`, `tests/test_gs_replay.cpp` |
| .gsdl format / recorder | `host_app/hle/drawlist_record.{h,cpp}`, `hle/drawlist_reader.h` |
| VU cases + replayer | `vutrace_collect/`, `vutrace_cases2/`, `vutrace_cases/`, `tests/vu1_replay.cpp`, `host_app/vu_trace_case.h`, `host_app/vu.cpp` [VUTRACE], `docs/vu1_trace_harness.md`, `docs/hle_harness.md`, `tools/vu1_trace_diff.py` |
| VIF replay | `tests/vif_replay.cpp` |
| Recomp oracle | `tests/recompiler_v2/` (oracle_*, fixtures_*, corpus_observed_p4.txt), `tests/recompiler_verify/` |
| Anchors / bench | `scripts/run_play.sh`, `scripts/run_bench.sh`, `tools/run_bounded.sh`, `tools/coldboot_gate.py`, `bench_report.json` |
| Pixel probes | `PS2_BB_SNAP` (graphics_thread.cpp), `PS2_SNAP` (GL backend), `host_app/artdet_flicker.h`, `tools/flicker/` |
| Load/input probes | `host_app/loadspan.h`, `cuelag_probe.h`, `pad_input.{h,cpp}` (PS2_PAD_SCRIPT, INLAT), `tools/loader_oracle_check.py` |
| Savestates | `save_states/` (.ps2st), `crash_tt_save_states/` (.p2s, PCSX2), `docs/SAVESTATE_SCHEMA.md`, `host_app/savestate*.{h,cpp}` |
| Time domain | `host_app/time_domain.h`, `docs/loop_state/CAVERN_TIMEDOMAIN_IMPL.md` |
| Incident record | `docs/loop_state/BLACKBOOT_IMPL.md` (black boot), `CAVERN_BUDGET_IMPL.md` (the regressing change) |
| PCSX2 ground truth | `*.gs.zst` (repo root), `pcsx2_video_groundtruth/`, `tools/{gs_dump_parser,summarize_gsdump,parse_gsdump}.py`, `pcsx2_debug_mcp/`, `pcsx2-mcp/` |
| Audit working files | `/tmp/s113/a5/{ctest_registry,test_families,tests_inventory,sweep}.txt` + scripts |

## Appendix B — numbers at a glance

- ctest: 407 registered (both build trees); 126+126+8+3 VU, 8 VIF, 2 GS,
  4-ish oracle, 12 equivalence, 7 property, 2 corpus-audit, 110 unit;
  143 env-pinned tests; 17 distinct env pins; 566 distinct `PS2_*` vars
  referenced repo-wide.
- GS corpus: 19 captures, ~16 GB, 19 frozen `.gsrep`+`.replay.txt` pairs,
  1 ALLOWLIST entry, 1 initial freeze + 2 extensions + 4 re-freeze events,
  0 unexplained drift.
- VU corpus: 3,293 + 126 + 62 cases × 33,456 B; capture = 1 env var.
- Savestates: ≈40 host `.ps2st` + 27 PCSX2 `.p2s` (no cross-engine load).
- Black-boot window: change 09-18/19 → caught 09-22 (pixel-blind anchors).

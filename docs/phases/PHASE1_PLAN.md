# PHASE1_PLAN — core/ + app shell + harness hookup

Plan for CONSTRUCTION_PLAN §4.1 (session s128, 2026-09-26). Entry criterion: the Phase-0
skeleton is green (configure / build / ctest 6/6 — README "Skeleton proof"). Exit
criterion: §7 below. OLD paths are `$PS2R_OLD_ROOT/host_app/…`
unless prefixed; every OLD file is opened READ-ONLY.

## 1. Goal

A NEW `nativeee` that boots to NOTHING game-visible but already has:
the whole `Config` schema with peak defaults and the `deterministic` preset; the
compile-time-category logger and the always-on ring; the diagnostics MECHANISM behind
`PS2R_DIAGNOSTICS`; the frozen PS2SAVE1 envelope + SectionRegistry; the thread
primitives; hashes; the harness emitters — so that

```
nativeee --profile deterministic --headless --harness-out /tmp/x/new --frames 600
```

writes `frames.fhash`, `audio.ahash`, `mem.mhash`, `run.meta` in the OLD formats
(placeholder lane values, §5) and `verify/diff_legs.py --cross-engine --lanes meta`
passes against every one of the 12 frozen legs. Nothing here is game-visible, so no
lane identity is CLAIMED — the deliverable is the plumbing every later phase reports
through.

## 2. Module list (one primary type per `.h/.cpp` pair; ARCH §13)

### 2.1 `engine/core/config/`

| File pair | Type | Content | OLD source(s) | Flags applied |
|---|---|---|---|---|
| `config.h/.cpp` | `Config` (immutable after Load; passed by `const&`) | the schema in §3 | FLAG_LEDGER SETTINGS table; `display_settings.h` keys (mode/aspect/custom_scale/custom_offset_x/y with their clamps [0,4]/[0,3]/[25,400]/[-8192,8192]); `main.cpp` CLI | all 33 SETTING rows become fields |
| `config_loader.h/.cpp` | `ConfigLoader` | precedence defaults < `ps2r.ini` < `--config <file>` < `--set k=v` (repeatable) < `--profile <name>`; `display.ini` read for `[video]` compatibility (mode/aspect/custom_* keys — the OLD user artifact is honoured, never written back unless `--save-display`); dev builds only: `PS2R_SET` env blob (the ONE getenv site in the tree) | `main.cpp` arg parsing (`--load-state`, `--bench-frames`), `display_settings.cpp` `LoadConfig/SaveConfig` (:134-150) | `PS2_LOG`, `PS2_GUI_OPEN`, `PS2_SAVE_SLOT`, `PS2_SHUTDOWN_WAIT` → fields |
| `config_validator.h/.cpp` | `ConfigValidator` | the explicit dependency DAG: `time.tick_sync ⇒ engine.mtvu=false` (refused otherwise — the OLD hatch refused MTVU=1); `engine.pacing=freerun ⇒ cdvd.latency_model=instant` unless set explicitly; `engine.mtvu_depth` requires `engine.mtvu`; `video.scale ∈ 1..4`; `harness.*` requires `--headless` or an explicit `--harness-window` | the NOVSYNC→FREERUN→CDVD_INSTANT cascade (A2 finding 3), `tick_sync.h` MTVU refusal | cascade dissolved into validation |
| `deterministic_profile.h/.cpp` | `DeterministicProfile` | the preset: every PROFILE.md row as a field value (§3 "det" column); `Config::Fingerprint()` canonical form `key=value;…` sorted, non-default fields only, plus `profile=<name>` | `verify/profile_deterministic.py`, `verify/PROFILE.md` | the 27 profile env vars → field values |

### 2.2 `engine/core/log/`

| File pair | Type | Content | OLD source(s) | Flags applied |
|---|---|---|---|---|
| `log.h` + `log_categories.h` | (macros + enum) | `LOG(cat, level, fmt, ...)`; ~30 compile-time categories harvested from the OLD TagFilterBuf registry (VIF, GIF, GS, VU, DMAC, MEM, SIF, SND, PAD, MPEG, KERNEL, LOADER, HEAP, PERF, IOP, CDVD, TIME, SAVE, HARNESS…); disabled categories compile to nothing | `main.cpp:125` TagFilterBuf tag list, `hw_verbose.h` | `PS2_LOG_FASTFILTER`, `PS2_LOG_ELIDE_DROPPED`, `CT_FULL_LOG` DELETE (the 4 layers do not exist) |
| `logger.h/.cpp` | `Logger` | levels error/warn/info/narrate; sinks file (`logs/runtime_log.txt` path from Config), stderr, ring; `dev.log_level` gate at runtime only for enabled categories | `main.cpp:4905` PS2_LOG master, `gs.cpp` s_scanoutLog, `gs_state` m4a latches | `PS2_LOG` → `dev.log_level` |
| `log_budget.h/.cpp` | `LogBudget` | `first_n`, `every_nth`, `frame_window` — library calls replacing hand-rolled statics | `debug_budget.h` | absorbed |
| `ring_log.h/.cpp` | `RingLog` | per-thread rings, channel masks (normal tier; the verbose tier is `PS2R_DIAGNOSTICS`-only), snapshot request/done handshake, **the extern-C crashring boundary VERBATIM** (`crashring_enabled`, `crashring_record_indirect`, `crashring_fatal_no_jump` — consumed by the generated shards in Phase 3; symbol names frozen) | `ring_log.{h,cpp}` (595+232 ln) | `PS2_LOG_CHANNELS` → `log.channels`; `PS2_LOG_RING_EVENTS` → `log.ring_events`; `PS2_CRASHRING` NORM (always on; alias gone); `PS2_LOG_VERBOSE` DIAG; `WriteBundleTo` env.txt/perf.txt assembly NOT here (→ core/triage 9.2) |
| `ring_format.h`, `ring_tags.h` | (PODs/enums) | record layout + tag ids — FROZEN (RING_LOG_SCHEMA.md) | `ring_log_format.h`, `ring_tags.h` | — |
| `ring_decoder.h/.cpp` | `RingDecoder` | ring → text decode (also used by `tools/diagnostics/ring_decode`) | `ring_log_decode.h` | — |

### 2.3 `engine/core/diag/` (compiled only when `PS2R_DIAGNOSTICS=ON`; Release has no symbols from here)

| File pair | Type | Content | OLD source(s) | Flags applied |
|---|---|---|---|---|
| `diag_registry.h/.cpp` | `DiagRegistry` | named probes/censuses/counters registration + startup config dump (replaces the 21-site `PS2_PERF` announce idiom) | `main.cpp:464` install_dual_hook MECHANISM (address-keyed sites NOT transplanted), `s8cnt`, `BullseyeDiag` | `PS2_PERF` → `diag.perf`; the 195 DIAGNOSTIC hook rows: not written |
| `perf_profiler.h/.cpp` | `PerfProfiler` | scoped timers, buckets, totals snapshot API (later feeds triage `perf_buckets.tsv`) | `perf_profiler.h` profiler half | `PS2_PERF` |
| `bench.h/.cpp`, `bench_report.h/.cpp` | `Bench`, `BenchReport` | PERF/bench anchors + report writer (`bench_report.json` schema kept; written ONLY to `--harness-out`/`--bench-report`, never to cwd) | `bench.h`, `bench_report.h` | the `setenv("PS2_PERF")` write dies |

(`FrameCap` — exit after N boundaries — is NOT diag: it is the harness leg length and lives in `core/verify` (§2.6), compiled in all builds.)

### 2.4 `engine/core/savestate/`

| File pair | Type | Content | OLD source(s) | Flags applied |
|---|---|---|---|---|
| `savestate_format.h/.cpp` | (free functions: `WriteEnvelope`, `ReadEnvelope`, `Crc32`) | PS2SAVE1 v2: magic, version, section_count, vsync_frame, wall_ms, SectionHeader{fourcc, version, flags(REQUIRED), crc32, len}×N, trailer CRC — **FROZEN bit-for-bit** (SAVESTATE_SCHEMA.md) | `savestate_format.{h,cpp}` (234+242 ln) | `PS2_SAVE_ATOMIC` NORM (atomic rename always) |
| `section_registry.h/.cpp` | `SectionRegistry` | `SectionSpec {fourcc, version, required, serialize, restore, validate, owner}` registration; unknown non-required sections skipped, missing required = load error | `savestate.h` + `savestate.cpp` orchestration (the ~30 free functions become registrations by their owning modules in Phases 3–8) | `PS2_W13R7_NORESEED` DELETE |
| `savestate_writer.h/.cpp` | `SavestateWriter` | async writer thread; capture → write → atomic rename | `savestate_writer.{h,cpp}` | — |

### 2.5 `engine/core/threads/`, `engine/core/hash*`

| File pair | Type | Content | OLD source(s) | Flags applied |
|---|---|---|---|---|
| `cmd_ring.h` + `cmd_ring.inl` | `CmdRing<T,N>` (template — `.inl` is the sanctioned sibling) | SPSC ring | `sound/ring.h` | — |
| `park.h/.cpp` | `Park` | condvar + absolute-deadline waits; `notify`/`wait_until`; the ONLY wait primitive (poll+sleep arms are not written) | `timeline.h` `WaitFixOn()` consumers (ps2_vblank / gs_hle / render queue condvar arms) | `PS2_WAITFIX` NORM |
| `hash.h/.cpp` | (free functions `Fnv1a64`, `Xxh64`) | FNV-1a64 with offset `0xcbf29ce484222325`, prime `0x100000001b3` (the harness hash); XXH64 OWN implementation of the published algorithm (no vendored code) | `vu_hash_fnv.h`, `verify/verify_env.cpp`, `render.h` XXH64 (algorithm only) | — |

### 2.6 `engine/core/verify/` + `engine/core/triage/input_log`

| File pair | Type | Content | OLD source(s) | Flags applied |
|---|---|---|---|---|
| `verify_host.h/.cpp` | `VerifyHost` | arming from `Config.harness` (out_dir, headless, record_input, state_every); leg paths; `OnPresent`/`OnBoundary`/`OnPadServe`/`Finalize` entry points the subsystems call | `verify/verify_env.{h,cpp}`, `verify_present.{h,cpp}`, `verify_boundary.{h,cpp}`, `verify_pad.{h,cpp}` | `PS2_VERIFY_OUT/HEADLESS/RECORD_INPUT/STATE_EVERY/TREE/PROFILE` → `harness.*` fields |
| `frame_hasher.h/.cpp` | `FrameHasher` | `u32 nw,nh` LE header + RGB top-down FNV; native downsample rule (top-left texel of each S×S block); hash 0 = no readable scanout | `verify/frame_hasher.{h,cpp}` | — |
| `audio_hasher.h/.cpp` | `AudioHasher` | FNV per 4800-stereo-frame window over raw float bytes; partial tail folds its frame count | `verify/audio_hasher.{h,cpp}` | — |
| `memory_checkpointer.h/.cpp` | `MemoryCheckpointer` | every 60 boundaries: FNV over RAM 32 MB + SPAD 16 KB + meta arena 2 MB (spans supplied by ee/mem in Phase 3; Phase 1 hashes zeroed spans) | `verify/memory_checkpointer.{h,cpp}` | — |
| `run_meta.h/.cpp` | `RunMeta` | keys `build_id, gl_renderer, tree=new, profile, env_ps2=<Config fingerprint>, env_hash=FNV(fingerprint), frames, wall_ms, finalized` (the OLD set — `diff_legs` gate) + NEW-only informational keys `park=` (Phase 3) and `unhandled_mmio=` (Phase 3; CONSTRUCTION_PLAN R17), ignored by the cross-engine gate; early stamp on first present, rewrite every 64 presents + at finalize | `verify/run_meta.{h,cpp}` | `CollectEnv()` over `environ` → `Config::Fingerprint()` |
| `frame_cap.h/.cpp` | `FrameCap` | exit after N frame boundaries (`--frames N` = OLD `--bench-frames`); finalize hooks (RunMeta, bench report) then `_Exit` as OLD does; compiled in ALL builds | `bench.h` frame cap + `main.cpp` exit path + `verify::FinalizeForExit` | — |
| `core/triage/input_log.h/.cpp` | `InputLog` | PS2_PAD_SCRIPT-grammar recorder (range tokens, `#` header) and replayer (comment-skipping, comma-joined; full wire word incl. SELECT/L3/R3/UP/RIGHT/DOWN/LEFT); the D9 single implementation | `verify/input_recorder.{h,cpp}`, `pad_input.cpp` grammar parser + `PS2_PAD_SCRIPT_FILE` loader | `PS2_PAD_SCRIPT`/`_FILE` → `harness.inputs` (TEST channel) |

### 2.7 `engine/platform/`

| File pair | Type | Content | OLD source(s) |
|---|---|---|---|
| `platform.h` | `Platform` (ARCH §16 struct of function pointers + `PlatformCaps`) | full declaration; Phase 1 implements `now_ns`, `thread_create` (+ name, QoS), `cpu_relax`, `open`; window/input/audio/fiber/mmap entries are `unimplemented()` stubs until Phases 3/6/7 | `thread_qos.h`, `platform/proc_stats.h` |
| `macos/platform_macos.h/.cpp` | `PlatformMacos` | the macOS implementation + registration | `thread_qos.h` (QoS), `platform/backtrace_compat.h` |
| `macos/crash_handler.h/.cpp` | `CrashHandler` | signal handlers → ring snapshot + backtrace (the crash-class triage trigger later) | `platform/crash_handler.h`, `platform/macos/crash_handler_macos.cpp` |
| `_template/README.md` | — | checklist only (stub build wiring lands 9.4) | — |

### 2.8 `app/host/`

| File | Content | OLD source(s) |
|---|---|---|
| `main.cpp` (≤ 500 ln, class-free) | parse CLI → `ConfigLoader` → `ConfigValidator` → `Logger` start → `DiagRegistry` (diag builds) → `VerifyHost` arm → `Platform` register → run the Phase-1 idle loop (a `TickSource` stub: one boundary per 16,667 virtual µs, no wall sleep under `pacing=freerun`) → `FrameCap` → `ShutdownLadder` | `main.cpp` shell (:4874-4884 chdir/ISO path resolution is replaced by explicit `--iso`/`Config.paths`) |
| `shutdown_ladder.h/.cpp` | `ShutdownLadder` (`engine.shutdown_wait_s`, default 10, clamp ceiling kept) | `shutdown_policy.h` |
| `build_id.h` (exists) + `cmake/gen_build_id.cmake` (exists) | build id | `host_app/cmake/gen_build_id.cmake` |

### 2.9 `tests/core/` and `verify/`

| Test / script | Proves | OLD source |
|---|---|---|
| `test_config_schema.cpp` | every row of `tests/core/settings_table.tsv` (generated from FLAG_LEDGER SETTINGS by `tools/gen_settings_table.py`) has a field with the stated peak default; `Fingerprint()` is canonical and stable | — |
| `test_config_loader.cpp` | precedence order; `--set` parsing; `display.ini` compatibility (mode/aspect/custom_* incl. clamps); unknown key = error | `tests/test_display_settings.cpp` |
| `test_config_validator.cpp` | the DAG (tick_sync refuses mtvu; freerun defaults instant_cdvd; depth needs mtvu) | `tick_sync.h` refusal |
| `test_deterministic_profile.cpp` | the preset's field values == PROFILE.md rows (table checked in) | `verify/profile_deterministic.py` |
| `test_ring_log.cpp` | emit/snapshot/decode golden; crashring C boundary symbols present and callable | `tests/test_ring_log.cpp` (+ OLD `build/tests/test_ring_log` run bounded as the decode reference) |
| `test_hash.cpp` | FNV vectors == OLD `test_verify_units` vectors; XXH64 == published reference vectors | `tests/test_verify_units.cpp` |
| `test_savestate_format.cpp` | PS2SAVE1 round-trip; **the 12 anchor `.ps2st` under `PS2R_OG_ROOT/save_states/` parse (magic/version/section table/CRCs) and re-serialize byte-identical** (SKIP when the OLD tree is absent) | `tests/test_savestate_format.cpp` |
| `test_savestate_writer.cpp` | async write + atomic rename | `tests/test_savestate_writer.cpp` |
| `test_verify_units.cpp` | downsample rule, audio windows, recorder range emission, run.meta keys | `tests/test_verify_units.cpp` |
| `test_shutdown_ladder.cpp` | wait parse/clamp | `tests/test_shutdown_policy.cpp` |
| `verify/run_leg.py --tree new` | NEW CLI mapping; bounded via `verify/run_bounded.sh` (ours — exists since 2026-09-26; OLD legs already use it and pass `--bench-report` into the leg dir, REFERENCE_FREEZE "Accepted side effects") | — |
| `verify/diff_legs.py --cross-engine --lanes …` | meta gate by profile when trees differ; lane subsets; `--selftest` extended | — |
| `verify/battery.py` | 12 legs × N reps table (ADMISSIBLE / IDENTICAL(lanes) / META-OK) | — |

## 3. The Config schema (Phase-1 form; every field exists from Phase 1)

Peak default = FLAG_LEDGER SETTINGS table (run_play.sh CANON); "det" = the
`deterministic` preset (PROFILE.md). Source var in the last column.

| Field | Type | Peak default | det | Source var |
|---|---|---|---|---|
| `video.scale` | int 0..4 | 1 | 1 | PS2_GS_SCALE (A2, Charter 2: 0 = native window -- the factor follows the window's drawable height in 448-line steps; play mode only, a leg reads 1; applied live from the title's Display page, the targets re-created at the next frame boundary) |
| `video.interp` | bool | on | off | PS2_GS_INTERP |
| `video.capture_wait_ms` | int | 8 | 0 | — (round 5, latency track 2026-10-23: the PROMPT present capture -- the GS consumer waits up to this many ms at the end of a present for the GPU to finish the frame's picture and posts it at once; past the bound the picture is read back at the next present as before; 0 = never wait, the round-4 deferred capture with every picture one present late; run.meta `gs_backend=` prompt=captures/timeouts/ms; host presentation only, fingerprint-excluded) |
| `video.fps_cap` | int | 60 | 0 | PS2_FPS_CAP |
| `video.present_relax` | bool | on | off | — (round 5, latency track 2026-10-23: the presenter's RELAX policy -- the 60 Hz admission train is pulled earlier, at most 4 ms per 32 on-time admissions, while every picture of the window waited for its edge with more than 2 ms to spare, so the edge converges to just after the latest typical arrival instead of staying where the first slow picture anchored it; a late picture still slips; off = the round-3 train that only moves later; run.meta `frame_cap=` relaxed=/relax_ms=; host presentation only, fingerprint-excluded) |
| `video.vsync` | bool | off | off | PS2_NOVSYNC |
| `video.fullscreen` | windowed\|fullscreen\|borderless | windowed | windowed | A2 (Charter 2): the session window's mode, applied live from the title's Display page and persisted in the settings file (the F1 panel's former Fullscreen checkbox). |
| `video.render_rate` | 60\|display | 60 | 60 | A3 (Charter 2): `display` presents at the display's refresh (harness.display_hz when simulated): every presented picture is the previous tick's frame RE-VIEWED at the camera interpolated between the two latest 60 Hz ticks (`engine/render/review/FrameReview` on the GS consumer: the frame's backend commands recorded, re-issued into shadow surfaces with the camera-projected VU1 families re-projected through K = P⁻¹V⁻¹C(φ)P; screen-space / unstamped draws at the tick; one tick of latency). Pin `RootHighFrameRate`: game_hz 60 while render_fps 120, the PRESENTED camera deltas' CV ≤ 0.5 over the beach pan (`present.trace`), hfr_review replays > 0 faults=0 stale=0. The game's logic tick stays 60 Hz (DT-14) in both. |
| `video.frame_cap` | off\|30\|60 | 60 | 60 | The presented-frame CADENCE (which presenter fields show a NEW picture; `app/host/FramePacer`): `60` = a new picture at every 60 Hz field edge it is ready for; `30` = at every OTHER field edge, a steady 33.3 ms train (a game producing 30..60 pictures per second shows every one at a fixed cadence instead of a 1/2/3-field mixture; a picture the cap does not show is superseded by the next -- no game tick is dropped, the logic tick stays 60 Hz by the clock contract); `off` = at the pump it arrives (the consumer's own cadence, no field quantisation). Applied live from the Display page and persisted. The presented-frame record (a title's FrameTimeline) judges it: p99 / gaps > 50 ms of the shown train. |
| `video.backend` | gl\|soft (vulkan/gxm later) | gl | gl | RENDERER_BACKEND (build) |
| `video.fit_mode` | stretch\|fit\|fill\|integer\|custom | fit | fit | display.ini `mode` |
| `video.texture_pack` | str (pack name \| none) | none | none | A4 (Charter 2): `<paths.texture_packs>/<name>/<TextureContentHash>.png` (the C4 layout: `<paths.user_data>/textures` by default) replaces the converted texture with that hash at load, at any resolution (TexturePack); `none` = the converted textures exactly. |
| `video.aspect` | auto\|4:3\|16:9\|1:1 | auto | auto | display.ini `aspect` |
| `video.texture_filter` | title\|point\|bilinear\|trilinear | title | title | A2 (Charter 2): the filtering enhancement over the OWNED textures' samplers (TextureFilterSelection); `title` = the title's own TEX1 choice per draw; `point`/`bilinear` force nearest/linear; `trilinear` forces linear level blending over the owned mip chain with the hardware LOD. Applied live from the in-game Display page. |
| `video.anisotropy` | 1\|2\|4\|8\|16 | 1 | 1 | A2 (Charter 2): anisotropic sampling of the owned textures (clamped to the driver's limit at the draw; 1 = none). |
| `video.custom_scale_pct` | int 25..400 | 100 | 100 | display.ini `custom_scale` |
| `video.custom_offset_x/y` | int −8192..8192 | 0 | 0 | display.ini `custom_offset_x/y` |
| `audio.enabled` | bool | true | true (device suppressed by `time.tick_sync`) | PS2_SOUND |
| `audio.volume` | int 0..100 | GUI default | — | GUI |
| `audio.sdr_block_ms` | int 1..1000 | 8 | 8 (mixer-frame clock: 48 frames/ms) | PS2_SDR_BLOCK_MS |
| `audio.interpolation` | linear\|gaussian | linear | linear (the audio lane's hashes and fixtures are cut with the 2-tap resampler) | — (the SPU2 voice resampler: `gaussian` = the hardware's 4-tap gauss table as PCSX2 Mixer.cpp; opt-in, changes the audio-lane bytes) |
| `input.device`, `input.kbm_pad`, `input.bindings_file` | str/bool/path | GUI defaults, `default_bindings.ini` | scripted only | GUI, input/ |
| `cdvd.latency_model` | instant\|modelled | instant | instant | PS2_CDVD_INSTANT |
| `cdvd.latency_scale` | float | 1.0 | 0 | PS2_DVD_LATENCY_SCALE |
| `cdvd.mc_latency_polls` | int | 4 | 0 | PS2_MC_LATENCY |
| `cdvd.load_turbo` | bool | false | false | PS2_LOAD_TURBO |
| `cdvd.delivery_visibility` | boundary\|poll | poll | poll | — (DP-17 (a) toggle, I8.13-A; default `poll` since gate S4, 2026-10-03 — KEPT by score (DECISIONS_LOG DP-17); `boundary` = the pre-decision model: a completed host-served read visible at the poll that asks once the modelled drive is done) |
| `cdvd.poll_latency_scale` | float > 0 | 1.0 | 1.0 (not pinned: the profile's `latency_scale=0` is boundary mode's "no drive time") | — (DP-17 (a) attempt 2, I8.14-A: the virtual drive's time under `delivery_visibility=poll`) |
| `engine.vu1_mode` | hle\|cached\|interp\|block\|aot | cached (HLE→cached ladder is NORM) | cached | PS2_VU_MODE (`block` = the pre-resolved block tier; `aot` = the generic run-time VU1 recompiler, engine/vu/vu1_aot/: a verified image on its specialised block table, the rest on `engine.vu1_aot_fallback`) |
| `engine.vu1_aot_corpus` | str | "" | — | — (the AOT verifier's corpus directory of Vu1Case files read at start; "" = the corpus collected under `<user_data>/vu1_aot/corpus` only; a path to oracle data, excluded from the fingerprint like `paths.*`) |
| `engine.vu1_aot_verify` | bool | on | — | — (off = every image runs native unverified: the harness / bench knob, never the play default) |
| `engine.vu1_aot_fallback` | block\|interp | block | — | — (the tier an unverified image runs on; block == interp by VuCachedVsInterpCorpus) |
| `engine.vu1_aot_codegen` | bool | on | — | — (the generated-code step above the specialised tier: a verified image's blocks emitted as C++ by `Vu1AotEmitter`, compiled by the host toolchain at run time on a background thread, loaded and RE-VERIFIED on the corpus before the block table swaps to the generated functions; no compiler on the host = one notice, the specialised tier continues; excluded from the fingerprint: the code is proven the identity before it runs) |
| `engine.vu1_aot_compiler` | str | "" | — | — (the C++ compiler the builder runs; "" = `/usr/bin/clang++`, then `c++`, `g++`, `clang++` on the PATH through the platform's spawn; excluded from the fingerprint) |
| `engine.vu1_aot_prelude` | str | "" | — | — (the staged include root of the generated TU's prelude, `engine/vu/vu1_aot/vu1_aot_prelude.h` and its closure; "" = the build's `<build>/vu1_aot_prelude`; copied under the cache so it compiles outside the tree; excluded from the fingerprint) |
| `engine.vu1_aot_opt` | int | 2 | — | — (the generated TU's -O level, 2 or 1: the bodies are already monomorphised templates; -O2 compiles in about the CPU time of -O1 (~7-8 s per image on an idle M4), emits a 12 % smaller library and runs 5-10 % faster in game; excluded from the fingerprint) |
| `engine.vu1_aot_pack` | str | "" | — | — (the pre-built VU1 library pack the loader tries first, `app/host/vu1_aot_pack.h`: `pack.txt` + one library per image, built by `tools/vu1_aot_pack` from the title's corpus; "" = `<executable dir>/vu1_aot_pack`; accepted when its ABI key and platform match, every library re-verified on the corpus before it runs; excluded from the fingerprint) |
| `engine.vu1_aot_region_blocks` | int | 1 | — | — (the generator's region policy, `Vu1AotBlockGen::Policy::block_cap`: blocks one region function holds at most -- the head block plus the blocks it reaches over fall-through / static targets; 1 = the block and its self-loop only (nothing duplicated). Generation-time shape of the verified code; excluded from the fingerprint) |
| `engine.vu1_aot_region_ops` | int | 48 | — | — (`Policy::op_cap`: ops a region holds at most, the head block always; excluded from the fingerprint) |
| `engine.vu1_aot_region_taken` | bool | on | — | — (`Policy::taken_targets`: a region grows over static branch targets too, not only the fall-through chain; excluded from the fingerprint) |
| `engine.vu1_aot_chain` | bool | on | — | — (`Policy::chain`, vuworker track 2026-10-22: a block function whose successor is another block of the same library hands over with a guaranteed tail call (`[[clang::musttail]]`) instead of returning to the dispatcher -- the registers stored back and the stall model made whole exactly as an exit would leave them, `done` carried for the budget check at the successor's head; a NOP-pair block, an E-bit end, a JR / JALR, a wrapped delay slot and a block outside the library still return to the loop. Needs clang or gcc >= 15 for the TU (the generated TU #errors otherwise; the run-time builder generates the unchained shape for any other compiler and says so in its note). Default on since the round-4 vuworker pairs: Paris walk W 6.05 vs 8.33 ms/frame, 5.00 vs 6.97 ns/bundle, region calls 181 k -> 18 k a frame, identity gates equal. Excluded from the fingerprint: the library is verified the identity before it runs) |
| `engine.vu1_aot_mac_acc` | bool | off | — | — (`Policy::mac_acc`, vuworker track 2026-10-22: a block function folds the MAC results it retires for sure at every point that could observe them into a register-resident `FlagPipeline::LazyAcc` as they are produced (the same Z/S/U/O lane sets OR-ed in registers) and delivers the accumulator once per observation point (`FoldLazyBatch`, the newest as the MAC word), instead of keeping every result live to the block's edge for its own load / OR / store fold; the results an XTOP yield or a GENERIC word could push keep the per-result form. Lazy ring only (the eager one is unchanged). Off until the vuworker gates are green. Excluded from the fingerprint: the library is verified the identity before it runs) |
| `engine.mtvu` | bool | on | false | PS2_MTVU (NORM-with-field) |
| `engine.mtvu_depth` | int | 2 | — | PS2_MTVU_MAX_FRAMES |
| `engine.mtvu_depth_hold` | int | 0 | 0 | — (round 5, latency track 2026-10-23: the HOLD -- a second runahead arm on the VU1 WORKER's lag in frames (frame marks pushed at the frame syncs minus marks the worker passed): the EE parks while more than this many COMPLETE frames are queued ahead of the worker (1 = the double-buffered console's own depth: a button's picture waits at most one frame in the worker's queue); the park never starves the worker and leaves `engine.mtvu_depth`'s declared/presented cap untouched; released by the worker passing a mark, an i-bit stall, or the 100 ms valve; 0 = off; run.meta `mtvu=` hold=cap:parks/valves/lag_max; host scheduling, fingerprint-excluded) |
| `engine.mtvu_early_finish` | bool | on | off | — (E425/E426/E427, finish track 2026-10-20: under MTVU the GS FINISH is raised when the EE has STAGED the packet carrying it, and in the paced play composition the boundary is deferred (no terminal sync / VSync drain fence; GIF_STAT and the sceGsSyncPath VIF1_STAT poll answered from the EE's view) — frame = max(worker, logic) instead of their sum; run.meta `mtvu=` early_finish/defer/vif1_idle_view/pre_raised/consumed. The deterministic profile pins it off (the parser-raised FINISH, the oracle lanes' schedule). Excluded from the fingerprint: images are captured quiesced) |
| `engine.mtvu_wait_probe` | bool | on | off | — (E430, waitsites track 2026-10-22: under MTVU the kernel-wait observation point asks `SyscallTable::WouldBlockNow` before idling on the worker; a wait that would return at once -- the semaphore the kick's FINISH handler signalled at this entry -- skips the idle loop, so the logic tick overlaps the worker's replay of the chain. run.meta `mtvu_waits=` idle_nonblocking is the census. Fingerprint-excluded: host scheduling only) |
| `engine.mtvu_kick_drop` | bool | on | — | — (DT-14 clock contract, Mecha-Bandicoot 2026-10-13: in play mode the game thread never parks on the MTVU worker — a frame the worker is > 2 frames behind runs its VIF stream without its VU1 programs, run.meta `mtvu=` kick_drop/drop/kicks_dropped; off = the bytes/frames park, the red-before) |
| `engine.ee_fast_dispatch` | bool | on | — | — (eecode round 4: a guest call to a generated function whose table entry still holds its init-time body skips the double ObservedBody / std::function chain and runs `recomp_raw_dispatch` (shadow-frame push, body, pop); every DispatchTable::Install keeps the full chain for its id. Host dispatch mechanics only — excluded from the fingerprint and not pinned by the deterministic profile (the <=294 cold-boot oracle and the Paris-600 lockstep census are byte-equal either way). Default on since 2026-10-22: the logic thread's dispatch+guard+TLS leaf share fell from ~32 % to ~7 % of its busy samples) |
| `engine.cop0_count` | bool | off | off | — (round 5, cutscene track 2026-10-23: the COP0 Count register (mfc0 $9) advances with the emu tick -- DerivedClocks::Cop0Count, 4915200 counts per field, seated at every syscall return and yield trip, a guest mtc0 reseats it. Off = the register never moves (the pre-round-5 state): a title's cycle clock reads a constant -- Sly 2's frame dt clamps to its 1/60 floor and runs, but the lgAud dialog timeline that paces the binocucom typewriter / lip-sync to the voice stream never advances. Guest-observable (RAM hashes): the deterministic profile pins it off; fingerprint-excluded (no image carries it, a restore reseats). Off by default until its gates are green) |
| `engine.ee_runahead` | int | 2 | 2 | PS2_EE_RUNAHEAD |
| `engine.ee_yield_spin_gate` | bool | on | off | — (E432, waitsites track 2026-10-22: the back-edge yield point's spin gate -- a trip passes a field of its own only when its thread passed no scheduling point (run-token acquire, boundary) since the previous trip; the 2^18th back-edge of code that schedules fires only the wall fields the pacer already noted, no park. Off = every trip a field: ordinary code trips once per 2^18 back-edges (Paris every ~9 frames), each a wall field under Play: 670 VBON per 600 boundaries, 54 pictures/s from a 60 Hz logic. run.meta `ee_yield=` spin_trips/sched_trips/due_fields. The deterministic profile pins it off (the frozen oracles' field schedule). Fingerprint-excluded: host state, not in any image) |
| `engine.pacing` | realtime\|freerun | realtime | freerun | PS2_VBLANK_FREERUN / PS2_NOVSYNC bench env |
| `engine.time_regime_pin` | int? | none | 1 | PS2_TIME_EMU_REGIME |
| `engine.turbo` | int | 0 | 0 | (surviving knob, ARCH §4.7) |
| `engine.shutdown_wait_s` | int 0..600 | 10 | 10 | PS2_SHUTDOWN_WAIT |
| `time.tick_sync` | bool | false | true | PS2_TICK_SYNC |
| `time.tick_deterministic` | bool | false | true | PS2_TICK_DETERMINISTIC |
| `time.t0_tickpaced` | bool | false | true | PS2_T0_TICKPACED |
| `time.maint_eepaced` | bool | false | true | PS2_MAINT_EEPACED |
| `iop.dispatch` | realtime\|frame | realtime | frame | PS2_IOP_DISPATCH |
| `loader.ring_cache` | bool | on | on | — (DP-5 (b) toggle, P8.29-A: the title's windowed reader serves records from a 128 KB callback-stream ring filled once per window (`on`, the retail `FUN_002aac80` rule) instead of per record (`off`, the pre-decision seam); default `on` since P8.31-A, 2026-10-06 — KEPT by the DT-7 score (DECISIONS_LOG DP-5 (b) attempt 2: unbooked `(0, 20064977)` vs S11 `(0, 20253916)`)) |
| `hle.libmc_format_in_t0` | bool | off | off | — (BUG-089 toggle, P8.33-A: libmc's `sceMcGetInfo(port, slot, *type, *free, *format)` — `on` reads the fifth argument from t0 as the EE calling convention passes it (the hardware); `off` keeps the inherited `sp+0x10` read, the caller's spilled register — in this title the MC manager's header word, whose slot-count nibble the `format = 1` store collapses from 4 to 1 (8 of the console's 28 type-registry entries, 114 of 126 class-22 objects) — the frozen baselines' observable; default `off`: under `on` the title's boot flow does not reach the FMV within 400 frames on NEW's libmc model (six Root pins red, P8.33-A), to be scored once that second gap is attributed) |
| `hle.meta_arena` | bool | on | on | — (DP-31 toggle, P8.32-A: the META-ARENA hook (HOOK row `0x181560`): `on` routes the title's handle-heap descriptors / compaction job / deferred-op nodes (`FUN_00181560(size ≤ 0x20)` from the pool-allocator code) to the host arena at `0x02000000` (the frozen baselines' observable); `off` lets `FUN_00181560`/`FUN_00181590` run as recompiled — the page pool holds them as the console does (PCSX2 `rec159`: 385 descriptors in slab class 7/32 B) — while the arena's storage and its free routing stay for restored images that already hold nodes there; default `on`: DP-31 REVERTED on the first 13-leg gate vs S12 (P8.32-A review loop 1; the residual is the −3-page shift of the objects NEW never builds, BUG-089) — to be re-scored after that gap is modelled) |
| `hle.math_helpers` | bool | off | off | DT-12: select checked MathLibrary, CollisionMath, TrigMath, AnimationMath, BoundsMath and ClippingMath boundaries; off retains the previously accepted point transform and counted originals for the remaining entries. |
| `hle.culling` | bool | off | off | DT-12: select the eleven complete chunk-view culling producers/readers and required CULL checkpoint owner. Off preserves and counts captured originals; selected state requires CULL, with no implicit legacy migration. |
| `hle.movie_lifecycle` | bool | off | off | DT-12: select owned movie initialise/start/finished/close and remove the old abort-hold wrapper for that arm. |
| `hle.movie_step` | bool | off | off | DT-12: checked Movie player Step with the same bounded queue/decode/feed/frame behavior, without scratchpad stack relocation; requires movie_lifecycle and graphics_movie. Start's two priming wrappers and presentation transport remain separate. |
| `hle.movie_start` | bool | off | off | DT-12: checked source Movie Start and two owned primes, without scratchpad relocation; requires movie_lifecycle, graphics_movie and music_buffers. Retained presentation registration remains explicit. |
| `hle.movie_wait` | bool | off | off | DT-12: complete Movie WaitFrame refill/presentation service; requires movie_start. Compares the actual NEW dispatch wrapper, keeps decoder pacing, and refuses the dormant spin continuation. Game presentation transport remains with M7. |
| `hle.movie_stop` | bool | off | off | DT-12: paired direct Start presentation and complete Stop; requires movie_wait and movie_step. Removes Movie VBlank registration while preserving Close, sound reclamation and source renderer recovery. M7 still owns transport replacement. |
| `hle.sound_bank_headers` | bool | on | on | DT-12: select the previously enabled sound-bank metadata reader; off retains the original reader for same-input comparison. |
| `hle.sound_samples` | bool | off | off | DT-12: select owned bank delivery, sample commands and common-mixer voice output; also enables StreamLibrary. Pending audio ownership refuses checkpoints. |
| `hle.music` | bool | off | off | DT-12: add owned music delivery and paired voices to the same audio provider; includes the sample/file service composition. Preload timing and game equivalence remain under review. |
| `hle.music_buffers` | bool | off | off | DT-12: own music storage and both Movie buffer loans with host file lookup; requires music and movie_lifecycle. Pending full application acceptance and restore migration. |
| `hle.graphics_movie` | bool | off | off | DT-12: owned Movie images and draws through the shared Graphics output; requires movie_lifecycle and the GL backend. Presentation transport and composed acceptance remain open. |
| `hle.graphics_distortion` | bool | off | off | DT-12: owned bucket-23 distortion application (DSTN owner: block-list observers, settings version at the source settings call, DrawHexagonParticleBlock producer; bucket 23 capture, ordered render-owner execution) over the shared Graphics scene; requires the GL backend. Off delegates the captured originals. Application gate open; selection also awaits the M6/M7 bucket-23 frame/environment capture. |
| `hle.graphics_screen` | bool | on | on | DT-12: owned bucket-4 screen effects (DrawScreenEffects 0x1b92d8: BlurFarPixels, FilterColours as a depth-byte palette lerp of the far pixels, WaveFrame) over the shared Graphics scene; requires the GL backend. Off forwards the captured original (the model census). A call that cannot be proved before commitment refuses to the model (`graphics_screen_effects refused=`). |
| `hle.graphics_shadows` | bool | off | off | DT-12: owned shadow application (SHDW owner, bucket 21 capture, ordered render-owner execution) over the shared Graphics scene; requires the GL backend. Off delegates the captured originals. Application gate open. |
| `hle.graphics_sky` | bool | off | off | DT-12: owned Sky producer through shared Graphics output; requires the GL backend. Pending composed acceptance. |
| `hle.graphics_textures` | bool | on | on | Textures at load (track 1c-2): every texture record converts at TextureRead (0x1bc920) and the title's per-frame slot upload (WriteTextureUpload 0x1c0880) binds the converted image to the GS slot in stream order (`TextureUploads` -> `OwnedTextureSlots`) instead of sending the storage image over PATH2; the resolver serves the bind. Off: only the UI pairs convert; the model uploads. Census `graphics_texture_uploads`, `texres_owned`. |
| `hle.graphics_meshes` | bool | on | on | M8 slice B: the owned mesh bucket for the type-01 rigid/placed family (evidence/MESH.md, INTERFACES.md section 15). The certified materials' (MaterialCertificate) instances are collected at SetRigidModelDma_ (0x1bd000) / SetPlacedModelDMA (0x1be7f0), the geometry comes from the model asset converted at ModelRead (0x1bcb58), the material is captured at FlushMaterial (0x1ba3a8) and the draws are recorded at the bucket's slot of the ordered output -- no VIF/VU1/GIF transport for those materials. Requires `hle.graphics_textures` and `video.backend=gl`. Off: the model writers run. Census `graphics_meshes`, `graphics_mesh_materials`; retirement counter `gs_sink_path1_by_bucket` 2/3. |
| `hle.graphics_sprites` | bool | off | off | DT-12: owned Sprite producer and texture resources through shared Graphics output; requires the GL backend. Pending composed acceptance. |
| `hle.graphics_text` | bool | off | off | DT-12: owned Text producer and texture resources through shared Graphics output; requires the GL backend. Pending composed acceptance. |
| `hle.input_service` | bool | off | off | DT-12: select Input reports, readiness and host rumble at the existing controller interface. Root acceptance remains open; ordinary GUI exposes only input preferences. |
| `hle.save_storage` | bool | off | off | DT-12: select durable SaveStorage through complete save-manager interfaces. Persistent game saves stay separate from reproduction images; root acceptance remains open. |
| `hle.audio_mix` | bool | off | off | DT-12: owned ordered channel and Movie PCM output with composed gain state; requires music, movie_lifecycle and frame-dispatched remaining services. Source effects/transport closure and full game acceptance remain open. |
| `hle.service_time` | bool | off | off | DT-12: owned accepted deadlines and blocking-wait settlement shared by file reads and loader headroom. Requires selected files and logical remaining services; no IOP clock writes or synthetic call cost. |
| `hle.startup` | enum(retained/native) | native | retained | M11 service-only startup (Herschel 3a): `native` never boots the IOP -- the eight sceSifLoadModule requests, the IOP heap and the STREAM/libsd SIF surface are answered by the owned services; `retained` boots STREAM.IRX under the IOP kernel (the oracle arm the deterministic profile pins). Requires every service owner selected and iop.dispatch=frame. |
| `hle.stream_files` | bool | off | off | DT-12: select StreamLibrary ordinary-file completion ownership; audio/sample/module paths and runtime checkpoint migration remain open. |
| `hle.files` | bool | off | off | DT-12: synchronous disc file SDK intents, owned descriptors/cursors and required FILS state. Independent of asynchronous STREAM selection. |
| `hle.graphics_strips` | enum(model/plain/all) | plain | plain | DT-12: plain preserves the checked default Strip; all selects four flat variants; model retains generated producers for the same-input oracle. |
| `hle.graphics_sprites` | bool | off | off | DT-12: select source UI sprites with owned converted textures and shared scene targets. |
| `hle.graphics_text` | bool | off | off | DT-12: select checked Crash font pages, material groups and their owned resources. |
| `hle.graphics_sky` | bool | off | off | DT-12: select Start/Draw/FinishSky through the shared scene and persistent converted assets. |
| `hle.graphics_movie` | bool | off | off | DT-12: select Movie upload/draw/presentation; requires movie_lifecycle and the live shared GL scene. |
| `hle.graphics_frame` | bool | off | off | DT-12: select the source frame-head display conversion and its two send boundaries; source dither differs from the retained model and needs attributed acceptance. |
| `iop.restored_timer_due` | one_period\|at_restore | one_period | one_period | — (DP-20 (c) toggle, I8.13-A: a restored RUNNING hard timer's first due) |
| `game.rtc_fixed` | bool | false | true | PS2_RTC_FIXED |
| `gs.queue` | inline\|thread\|pipelined | inline | inline | — (DP-25 toggle, P8.12-A: where the GS DrawIR consumer runs — inline on the EE thread at the kick, `thread` = its own consumer thread in lockstep (byte-identical, pinned by `RootGsQueueThreadIdentical`), `pipelined` = measurement only, identity not guaranteed; default `inline` until the orchestrator flips it on throughput) |
| `save.slot` | int | 0 | 0 | PS2_SAVE_SLOT |
| `save.restore_mode` | enum(live/relaunch) | live | live | F8 restore route (Bernoulli 3a): `live` restores the newest slot IN the running process -- the game thread quiesced at an admitted boundary, Graphics settled (the capture settle), every owner's restore on the live composition, presenter/audio re-armed, no window or GL device recreated; `relaunch` closes the session and re-executes the image with `--load-state <slot>`. Live falls back to the relaunch, naming why (run.meta `f8=`), when the composition retains IOP state (StartupOwnership). |
| `ui.gui_open` | bool | dev on | false | PS2_GUI_OPEN |
| `ui.menu_pages` | bool | true | true | CHARTER 2 track A (Bernoulli): the owned menu extension -- the 'Display' and 'Controls' pages added to the title's options menu at the title's own Options page construction (MenuExtension, NTSC 0x1646c0). Installed in PLAY mode only (headless / tick-synced legs never build the pages: the deterministic lanes are unchanged by construction); `off` = the title's menu as shipped. |
| `ui.perf_overlay` | bool | false | false | CHARTER 2 A2 (Bernoulli): the game Hz / render fps overlay over the presented frame; the first option of the title's added 'Display' menu page (MenuExtension) -- the page's toggle and this key are one value, applied live, persisted in the settings file. |
| `input.rumble` | bool | true | true | CHARTER 2 A5 (Bernoulli): the title's actuator requests reach the game controller; off = no rumble queued (the Controls page toggle). |
| `launcher.show` | bool | true | true | CHARTER 2 C2 (Bernoulli): the first-run launcher / settings screen (ImGui, the same binary) shown before the game on a plain start; a harness command line (--game-files, --profile, --set, --iso, --headless, ...) bypasses it; `--launcher` forces it. |
| `launcher.elf_sha256` | str | "" | "" | CHARTER 2 C2 (Bernoulli): the SHA-256 the disc's SLUS_123.45 must carry to be extracted; "" = the ELF this build recompiled (TitleProfile::elf_sha256). RootFirstRunExtract injects its synthetic ELF's hash. |
| `qol.skip_movies` | bool | true | true | CHARTER 2 A5 (Bernoulli): START/CROSS during a movie skips it -- the title's own skip where the movie is skippable, else the owned MovieDecoder reports it finished (play mode only). |
| `qol.fast_forward` | int | 1 | 1 | CHARTER 2 A5 (Bernoulli): cutscene fast-forward factor while R2 is held and the title's video controller (0x30973C, state +0x8 == 3 Playing) plays an in-engine cutscene: the wall-locked boundary delivers N fields instead of one (title CutsceneFastForward -> BoundarySequencer::SetExtraFields; the late-frame form of DT-14 rule 3), the boundary keeps its 60 Hz wall edge (game_hz unchanged); 1 = off; play mode + live pad only. Census `fast_forward=`, `fast_forward_fields=`. |
| `fixes.look_rest_seconds`, `fixes.skid_mark_init` | bool | false | false | CHARTER 2 A5 (Bernoulli): RETAIL_BUGS patches (TR docs/RETAIL_BUGS.md: LookController::Construct restSeconds, ConstructSkidMarks lastPoint/colours), owned bodies over the title's constructors (title RetailFixes), default off = the title as shipped; listed on the launcher's 'Fixes' page with the rest as unimplemented. |
| `dev.log_level` | quiet\|error\|warn\|info\|narrate | quiet | quiet | PS2_LOG |
| `log.channels` | list | today's set | today's set | PS2_LOG_CHANNELS |
| `log.ring_events` | int pow2 1024..4M | today's default | same | PS2_LOG_RING_EVENTS |
| `diag.perf` | bool | off | off | PS2_PERF |
| `diag.tick_census` | bool | off | off | CHARTER 3 item 3 (Zeno): the game tick's caller census -- the hot hooks (libc memcpy/memset/strlen, the descriptor allocator) sample the title's shadow call stack and attribute the tick's wall time to the sampled chains; run.meta `pace_long_trace` prints `callers=overrun/total:f2>f1>f0:us:marks,...` per long tick. Off = marks counted only. |
| `diag.vu1_lockstep` | bool | off | off | Round P (the JIT blocker): the VU1 tier A/B in the running game -- every kick the configured tier runs is also run on the interpreter reference on a shadow unit cloned at the kick and the end states diffed (registers, data memory, clock, Q/P latches, bundles, kicked bytes; `VuLockstep`); the first divergences are printed with the bundle index they first part at, all are counted in run.meta `vu1_lockstep=`. The GS sees the tier's output only. Off = no shadow. |
| `triage.enabled`, `triage.fallbacks`, `triage.perf`, `triage.spikes`, `triage.fps_target`, `triage.disk_budget_mb`, `triage.ring_period_s` | per TRIAGE §3 | on / on / on / on / 60 / TRIAGE §3 / TRIAGE §3 | on (small disk budget) | PS2_BUGREPORT*, PS2_SLOWSPOT |
| `harness.out_dir`, `harness.headless`, `harness.frames`, `harness.load_state`, `harness.inputs`, `harness.record_input`, `harness.state_every`, `harness.warp_to_frame` | path/bool/int/path/path/bool/int/int | unset/false/0/—/—/false/0/0 | set by run_leg | PS2_VERIFY_*, PS2_PAD_SCRIPT_FILE, PS2_WARP_TO |
| `harness.legacy_input_save` | bool | off | off | Explicit OLD producer classification for boot-only Input/Save migration. It cannot waive a missing selected section in a marked reproduction or runtime F8. |
| `harness.record_input_path` | path | — | — | Optional destination of the first input recording segment (`--record-input <file>`); empty writes `<out_dir>/input.padlog`. Later restore segments keep their own `output-NNNNNN/input.padlog`. |
| `harness.live_restore` | bool | off | off | Root acceptance opt-in for composed F8 supervision. All owner, clock, pending-work and continuation admission checks still apply. Not shown in the ordinary settings menu. |
| `harness.inject_fault_frame` | int | 0 | 0 | Harness-only fault injection (the TriageBundle acceptance pin `RootTriageBundleOnFault`): the leg throws a synthetic fault at frame boundary N so the crash path (triage bundle, run.meta `park=exception`, the census at exit) is proven on a real process. `--set` only, harness armed only; 0 = off. |
| `harness.display_hz` | int | 0 | 0 | Harness-only SIMULATED DISPLAY rate (pin `RootHighFrameRate`): under `video.render_rate=display` the presenter paces at this rate instead of the window's display mode (0 = the real display). `--set` only, harness armed. |
| `harness.display_sequence` | str | "" | "" | Harness-only SCRIPTED DISPLAY CHANGES (pin `RootScreenModeSwap`, Charter 3 item 4): at frame boundary N a play session submits the Display page's request for `<mode>` exactly as the menu widget does (SessionControls), so the swap runs on the presenter's thread between two presented frames while the game thread keeps ticking at 60. Format `N:<mode>[,M:<mode>]`, mode `windowed\|fullscreen\|borderless\|scale1..scale4\|vsync\|novsync`, frame > 0. `--set` only, a window required; "" = off. |
| `harness.live_pad_probe` | bool | off | off | Harness-only LIVE PAD PROBE (pin `RootScreenModeSwap`, Charter 3 item 4): a play session replaying `--inputs` also opens the live PlatformInput and reads it at every boundary exactly as a live session does (the pad's round trip to the main thread -- the product path a display swap used to block for 560-920 ms), while the served buttons stay the replay's. Census `pad_live=`. `--set` only, a window and `--inputs` required. |
| `harness.inject_key` | str | "" | "" | Harness-only synthetic KEY PRESS through the REAL key route (pin `RootPlayHotkeyF5`): at frame boundary N a play session pushes a real SDL_KEYDOWN/KEYUP pair for the key into SDL's own queue, stamped as SDL stamps a physical press (the keyboard-focus window's id, 0 without focus), so the press travels SDL_PollEvent -> SessionWindow -> SessionGui -> SessionControls exactly as the user's does. Format `<KEY>@N[-M][,...]`: KEY F1..F12 (the GUI hotkey route) or any SDL key name (Space, W, Tab, Up ... -- the pad route, KeyboardMouseBackend's latch; pin `RootKbmPadOnly`), `-M` HOLDS the key through boundary M (frame > 0). `--set` only, a window required (`--harness-window`); "" = off. |
| `harness.close_at_frame` | int | 0 | 0 | Harness-only WINDOW CLOSE (pin `RootExitClean`): at frame boundary N a play session pushes a real SDL_QUIT into SDL's queue, drained by the main pump exactly as the window manager's close is (SessionGui -> SessionControls::Close -> the shutdown ladder). `--set` only, a window required (`--harness-window`); 0 = off. |
| `harness.close_after_ms` | int | 0 | 0 | With `harness.close_at_frame`: MAIN pushes the SDL_QUIT this many ms AFTER boundary N instead of at it -- a close while the game thread is inside a loader with no boundaries (the title's Quit reload; pin `RootExitClean` quit arm). 0 = at N. |
| `harness.virtual_pad` | str | "" | "" | Harness-only VIRTUAL GAMEPAD (pin `RootPadHotplug`): `attach@N[,press@K-L][,detach@M]` -- at boundary N a play session attaches an SDL virtual game controller on the input owner (the real JOYDEVICEADDED route into the pad backend), holds its cross (SDL_CONTROLLER_BUTTON_A) from K through L through real controller events, detaches it at M (JOYDEVICEREMOVED: the keyboard takes over as the active source). `--set` only, a window required; "" = off. |
| `harness.save_at_frame` | int | 0 | 0 | Harness-only savestate trigger (the F5 path without a key press; pin `RootPlaySaveRestore`): from frame boundary N on, the leg captures the play-mode checkpoint at the FIRST admissible boundary into `save.slot` -- the same composition, writer, refusal text and `.meta` sidecar as F5 -- and refuses loudly, naming the owner, after 120 refused boundaries. `--set` only, harness armed (or `paths.save_dir` set); 0 = off. |
| `harness.restore_at_frame` | int | 0 | 0 | Harness-only F8 trigger (the key press without a key; pin `RootPlayRelaunchRestore`): at frame boundary N a play session submits RestoreReproduction exactly as F8 does -- the newest slot or the browser's selection, the same preflight, loud lines and relaunch. `--set` only, harness armed (or `paths.save_dir` set); the relaunch drops it (and `save_at_frame`) from its argv. 0 = off. |
| `paths.save_dir` | path | — (the `--harness-out` dir, else `paths.log_dir`) | same | The savestate root: F5 / `harness.save_at_frame` write `<save_dir>/savestates/slot<N>.ps2st` (+ `.meta`), F8 / `--load-state` read them. |
| `paths.game_files`, `paths.iso`, `paths.mc_dir`, `paths.log_dir`, `paths.texture_packs` | path | — (resolved at Load under `paths.user_data`: `<root>/game_files`, from TitleProfile, `<root>/memcards`, `<root>/logs`, `<root>/textures`; with no root known the former cwd-relative names) | same | main.cpp argv join, GAME_FILES_DIR (`paths.game_files` = the ELF/disc-side files directory, added by the Phase-3 root; CHARTER 2 C4 moved every default under the user data root) |
| `paths.user_data` | path | — (the platform's user data dir: macOS `~/Library/Application Support/NativeEE`, Windows `%APPDATA%/NativeEE`, Linux `$XDG_DATA_HOME/NativeEE`; the app resolves it with SDL_GetPrefPath and hands it to ConfigLoader) | same | CHARTER 2 C4 (Bernoulli): the ONE root of settings (`ps2r.ini`), saves, savestates, texture packs and extracted game files -- never beside the binary. `--set paths.user_data=<dir>` pins it for a leg. |
| `meta.profile`, `meta.source_fingerprint` | str | default / build id | deterministic | — |

Count: the table above has ~59 fields = ARCH §4.8's 38 PRODUCT fields (its 27 base +
7 refactor-introduced + 7 `triage.*` − 1 merged, which already include the 33 SETTING
rows' fields — 30 runtime + 3 build-time: `RENDERER_BACKEND` → `video.backend`,
`GAME_FILES_DIR` → `paths.*`, `PS2_RECOMP_SHARD_COUNT` → build parameter only) + the
harness plumbing (8 `harness.*`), `paths.*` (3), `meta.*` (2) and the display.ini
compatibility keys (`video.fit_mode/aspect/custom_*`, 5) and input/audio GUI fields that
§4.8 folds into its section counts. Neither prose count is the authority: the
`tests/core/settings_table.tsv` + `tests/core/test_config_schema.cpp` pair is
(CONSTRUCTION_PLAN R14).

## 4. Flags consumed by Phase 1 (68 rows; `docs/filemap/FLAG_PHASE_COUNTS.txt` P1 block, generated by `tools/flag_phase_counts.py`)

- NORM (11): `PS2_WAITFIX` (Park is the only wait), `PS2_CRASHRING` (ring always on),
  `PS2_SAVE_ATOMIC`, `PS2_EE_RUNTOKEN` (schema note only — consumer Phase 3),
  `PS2_NO_QOS`, `PS2_SLOWSPOT_ASYNC`, `PS2_SLOWSPOT_{MS,GF_MS,COOLDOWN_S}` (constants),
  `PS2_SLOWSPOT_DEDUPE`, `PS2_SLOWSPOT_SUFFIX` (triage constants — mechanism 9.2, values
  fixed now).
- SETTING (11): `PS2_LOG`, `PS2_LOG_CHANNELS`, `PS2_LOG_RING_EVENTS`, `PS2_GUI_OPEN`,
  `PS2_SAVE_SLOT`, `PS2_SHUTDOWN_WAIT`, `PS2_MAINT_EEPACED`, `PS2_SLOWSPOT`,
  `PS2_BUGREPORT`, `PS2_BUGREPORT_SLOWSPOT`, `PS2_BUGREPORT_SLOWSPOT_MAX`.
- DIAG (19): the census/probe mechanisms (`PS2_CALL_CENSUS`, `PS2_CALL_CENSUS_ROOT`,
  `PS2_CALL_CENSUS_SLOWSEG`, `PS2_MID_CENSUS`, `PS2_MID_CENSUS_FROM`, `PS2_EESTALL_CENSUS`,
  `PS2_EULERTRACE`, `PS2_LOG_VERBOSE`, `PS2_VERBOSE_HW`, `PS2_EDGEPROBE`, `PS2_POPUP_DIAG`,
  `PS2_PROJ_DIAG`, `PS2_SAVE_PROBE`, `PS2_SAVE_WATCH`, `PS2_SITEB_DIAG`, `PS2_SMWATCH`,
  `PS2_SND_BANKTRACE`, `PS2_SND_STATEHASH`, `PS2_TICKWATCH`) — the REGISTRY lands now;
  individual census bodies land with their subsystems if their FILEMAP row keeps them.
- TEST (7): `PS2_RING_TEST_{CRASH,HOTKEY,BUNDLE}`, `PS2_BUGREPORT_TEST_MANUAL(_STATE)`,
  `PS2_GUI_TEST_{LOAD,SAVE}` → `tests/` arguments.
- DELETE (19): `PS2_LOG_FASTFILTER`, `PS2_LOG_ELIDE_DROPPED`, `CT_FULL_LOG`, the S8/S16/S26
  tracers (`PS2_S16_CHAIN(_CAP)`, `PS2_S26_{DECAL,DECAL_CAP,KILL}`, `PS2_S8_{FORCECLAIM,
  REGHOOK,REGHOOK_BATCAP,REGHOOK_CAP}`), `PS2_W13R7_*` (4), `PS2_B57Q`, `PS2_U3_VIEWPORT`,
  `PS2_VU_AOT_PROFILE` — not written.
- NEW (1): `PS2R_DIAGNOSTICS` (CMake option, exists in the skeleton).

(Moved out of the first draft's Phase-1 block by the regenerated tally: the `input/`
device-backend rows → Phase 7, `artdet` rows → Phase 6, VU-AOT rows → Phase 5,
`stick_trace`/`loadspan`/`snd_census` → Phase 4 with their subsystems.)

## 5. Harness hookup — what the NEW binary emits in Phase 1

| Artifact | Phase-1 content | Comparable? |
|---|---|---|
| `frames.fhash` | one `vf,0x0000000000000000` line per virtual boundary (hash 0 = no readable scanout, OLD's own convention) | index-aligned; DIVERGENT vs OLD at OLD's first non-zero present — expected, recorded |
| `audio.ahash` | one silence-window hash per 100 ms of virtual time (`AudioHasher` over zeroed 4800-frame windows) | equals OLD's silence windows (coldboot window 0 `0x0a6fb8af536edb25` is the check that the hasher and window size are right) |
| `mem.mhash` | every 60 boundaries: FNV over zeroed 32 MB + 16 KB + 2 MB spans | DIVERGENT (no game) — expected |
| `run.meta` | `build_id=NATIVEEE-BUILD-ID …`, `gl_renderer=none`, `tree=new`, `profile=deterministic`, `env_ps2=<fingerprint>`, `env_hash=…`, `frames=N`, `wall_ms`, `finalized=1` | `--cross-engine` meta gate PASS on all 12 legs |

`verify/run_leg.py --tree new` maps `--anchor S` → `--load-state S` (accepted by the CLI
now, acted on from Phase 3), `--frames N` → `--frames N`, `--inputs` → `--inputs`;
`--state-every K` → `--state-every K`. `verify/battery.py` runs the 12 legs and prints
`META-OK` per leg; Phase 1 claims META only.

## 6. Order of work (each step builds + ctest green before the next)

1. `core/hash` + `tests/core/test_hash` (FNV vectors from OLD `test_verify_units`).
2. `core/log` (categories, Logger, LogBudget, RingLog verbatim, decoder) + `test_ring_log`.
3. `core/config` (schema, loader, validator, preset) + `settings_table.tsv` generator + tests.
4. `core/savestate` (format verbatim, registry, writer) + anchor-parse test over the 12 OLD states (read-only).
5. `core/threads` (`CmdRing`, `Park`).
6. `core/diag` mechanism (registry, profiler, bench, report) under `PS2R_DIAGNOSTICS`.
7. `core/verify` (+ `core/triage/input_log`) with the OLD formats byte-for-byte.
8. `platform/platform.h` + `macos/`.
9. `app/host/main.cpp` composition + `ShutdownLadder` + the idle `TickSource` stub.
10. `verify/run_leg.py --tree new`, `diff_legs.py --cross-engine --lanes`, `battery.py`
    (`run_bounded.sh` already exists); run the 12-leg META battery; write `PHASE1_IMPL.md`.

## 7. Proof gate / definition of done

- `cmake --preset macos-dev && cmake --build --preset macos-dev && ctest --preset macos-dev`
  100 % (lints + `tests/core/*` + `VerifyDiffLegsSelftest`); `macos-diag` preset also
  builds and passes.
- `python3 tests/lint/lint_one_class.py --root .` → 0 findings over every new pair;
  `python3 tests/lint/lint_engine_purity.py --root .` → 0 tokens.
- `grep -rn getenv engine app` → exactly one site (`core/config/config_loader.cpp`, dev
  builds, `PS2R_SET`).
- `verify/battery.py --binary build/app/host/nativeee --lanes meta` → 12/12 `META-OK`
  (quoted in PHASE1_IMPL.md), and the coldboot leg's `audio.ahash` window 0 equals the
  OLD baseline's `0x0a6fb8af536edb25` (hasher/window proof).
- The 12 OLD anchor `.ps2st` files parse and re-serialize byte-identical through
  `core/savestate` (envelope only — section payload restore is Phase 3).
- Every new directory's README rows filled (OLD sources, flags applied).
- Nothing game-visible exists; no behaviour claim is made or accepted.

## 8. Risks specific to Phase 1

| Risk | Mitigation |
|---|---|
| The crashring extern-C symbols drift from what the Phase-3 generated shards expect | copy the declarations from OLD `ring_log.h` verbatim and add `tests/core/test_ring_log` link checks for `crashring_enabled` / `crashring_record_indirect` / `crashring_fatal_no_jump`; the shard link in Phase 3 is the final proof |
| `Config` schema mismatch with FLAG_LEDGER | `settings_table.tsv` generated from the ledger by script; schema test enumerates it |
| Cross-engine meta gate weaker than env_hash | NEW's fingerprint is asserted equal to the preset's canonical fingerprint by `battery.py` on every leg (CONSTRUCTION_PLAN R6) |
| Own XXH64 differs from the vendored one used by OLD texture keys (Phase 6 consumer) | reference vectors from the published spec in `test_hash`; a Phase-6 test lifts one OLD content key from a `.gsdl` and recomputes it |
| `display.ini` compatibility read changes user-visible geometry | `test_config_loader` ports OLD `test_display_settings` cases verbatim (names, clamps) |

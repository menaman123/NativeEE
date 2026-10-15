# TRIAGE_CAPTURE — the unified play-time capture subsystem

[REV3 2026-09-23 D9] Companion to `ARCHITECTURE.md` (§3 tree, §18), `MIGRATION_PLAN.md`
(triage rows + phase gate), `EQUIVALENCE_PLAN.md` (bundles as harness inputs), and
`../bugs/TRIAGE.md` (bundle-first triage). Binding user decision **D9**: the runtime
CAPTURES, at play time, three classes of event into self-contained bundles that a LATER
session — AI or human, on a different machine — can triage and act on without the
original machine state:

- **(a) HLE FALLBACK** — the game hit something not yet HLE'd. Five populations:
  a VU1 microprogram variant declining to the cached interpreter; an SDK/OS routine
  executing as recompiled code where an HLE binding is expected; an IOP module call
  hitting a stub; a GS draw class declining native to the generic path; a syscall/RPC
  stub. The bundle must let another session **LIFT** it: the exact code, the oracle
  inputs/outputs to prove a lift bit-exact, the decline reason, the frequency, and a
  savestate at the moment.
- **(b) PERF** — sustained poor performance (fps below target for N seconds): PERF
  bucket table, host sample/profile, draw census, HLE fallback counters, GPU timings,
  hot function list, savestate.
- **(c) PERF SPIKE** — frame-time variance/stutter: per-frame time series around the
  spike, what happened in the spike frames, and a savestate from a few seconds BEFORE
  the spike (a rolling savestate ring), so the spike is reproducible.

ALL bundles carry a savestate + the input log since that state, the title profile id,
build id, config, and a `MANIFEST.md` written for an AI session: what happened, what
to do, which tool reproduces it, the acceptance test.

---

## 1. As-is audit — what capture exists today (current tree, read-only; file:line cites)

### 1.1 What EXISTS

| # | Asset | Where (file:line) | What it does today |
|---|---|---|---|
| E1 | **U5 bug_report bundling** | `host_app/bug_report.h:12-21` (layout + crash rule), `host_app/bug_report.cpp:50-52` (`kRoot="bug_reports"`, `kKeepBundles=20`, `kTailLines=500`) | Bundles into `bug_reports/<UTC %Y%m%d-%H%M%S>_<trigger>[-N]/` with trigger ∈ {crash, slowspot, manual}; deterministic `manifest.txt` (build id, trigger, created_utc, os/arch, file list + sizes, absent list + reasons — `GenerateManifest`, `bug_report.h:166-196`); retention keeps newest 20 shape-matched dirs (`RetentionSelectDeletions`, `bug_report.h:362-380`; `PruneRetention`, `bug_report.cpp:175-192`); assembly on detached workers, never the graphics thread (the [FIX-SLOWSTICK] lesson, `bug_report.cpp:20-24`) |
| E2 | **Crash sentinel → next-launch crash bundle** | `bug_report.cpp:270-340` (`CrashScanAtStartup`) | `logs/crash_report.txt` non-empty ⇔ a crash writer fired (handlers open it O_TRUNC per launch); next launch bundles crash_report + fresh-enough `ring_crash.rlog` (+decoded twin) + log tails + **newest** `save_states/*.ps2st` + exe-dir configs. Nothing runs in the signal path (`bug_report.h:18-22`) |
| E3 | **Slowspot watchdog + capture** | `host_app/slowspot.h:5-27` (contract + env gates), `host_app/slowspot_detector.h:31-38` (Config: `threshold_ms=40`, `cooldown_s=30`, `window=60`; O(1) p50-over-threshold hot path, :9-16), `host_app/slowspot.cpp:224` (`RequestSaveNamed`), `:231-232` (snap request), `:239` (`ringlog::WriteBundleTo`), `:259` (`bugreport::SlowspotOnTrigger`) | Sustained-slowdown detector on the graphics thread (window p50 > threshold for a full 60-present window). Captures `save_states/slowspot_f<vf>_s<session>.ps2st/.ppm/_logs` (`slowspot_paths.h:38-58`); `_logs/` gets ring.rlog + ring.txt + env.txt (all `PS2_*` vars) + perf.txt (PERF totals) via `ring_log.cpp:335-400`. Session-unique names ([FIX-C2]); trigger dedup by `vf/600` bucket, 16 slots (`slowspot.cpp:199-207`). Default: detector+ring+stderr ON, capture OFF unless `PS2_SLOWSPOT=1` (`slowspot.h:17-27`) |
| E4 | **Slowspot → bug_report bundle** | `bug_report.cpp:344-405` (`SlowspotWorker`), `:409-455` (pending state machine: probe every 30 presents, expire at 600), `bug_report.h:389-391` (`SlowspotCapAllows`, default **1 per run** via `PS2_BUGREPORT_SLOWSPOT_MAX`) | Copies state + snap + `_logs/*` into a `<UTC>_slowspot/` bundle; adds `perf_window.txt` = PERF-channel ring lines within ±600 frames of the trigger (`FilterPerfLines`, `bug_report.h:314-338`). Live example verified on disk: `bug_reports/20260922-031552_slowspot/manifest.txt` — 44.1 MB `.ps2st`, snap, env/perf/ring/tails, 0 absent |
| E5 | **PS2_ARTDET flicker fire bundles** | `host_app/artdet_flicker.h:24-35` (gates `PS2_ARTDET=1` + `PS2_ARTDET_FLICKER`, 900-frame cooldown, max 8 captures/session), `artdet_flicker.cpp:193-250` (`WriteBundle`) | On an ABAB-flicker fingerprint FIRE: writes flat files `bug_reports/flicker_f<vf>_<epoch>.{ppm,txt,ps2st}` — backbuffer PPM, band metrics + queue depth + scanout counters + env text, async savestate (`RequestSaveNamed`). NOT a directory bundle, no manifest |
| E6 | **vutrace capture (the VU1 oracle corpus feeder)** | `host_app/vu.cpp:74-155` (env registry: `PS2_VUTRACE`, `_FILE`, `_MAX`, `_CAPTURE=N`, `_PC=hex`, `_DIR`, `_MINFRAME`, `_PER_IMAGE=N`, `_IMG_GAP`), `:241-246` (`PS2_VUTRACE_NEWIMG` decode-time capture), `:333-339` (case write `vu1_case_%04d_%03x_%08llx.bin`) | Dumps `Vu1Case` files at VU1::Execute entry — the exact-replay capsule: `vu_trace_case.h:26-77` — magic `VUC1` v1, `start_pc`, full `VUregs` (608 B), 16 KB micro + 16 KB data memory, VIF1 TOP/TOPS/ITOP/ITOPS, defensive Q/P latches, `vu_cycle`; POD, `sizeof==33456` (asserted :64-66). Arming is ENV/at-launch only |
| E7 | **GUI/ini VU collection control** | `vu_aot.ini` (`collection_enabled = off/on`, `collection_per_image = N`), `host_app/main.cpp:4987-5035` ([U6-VUCTRL] startup env-injection), `host_app/gui/vu_microcode_section.cpp:126,283,350-352,427` | Persistent "capture new VU1 microprograms at next launch" toggle; forces interpreter tier while armed; env vars win |
| E8 | **[VU1-HLE-RT] fallback counters + census** | `host_app/hle/vu1_hle_runtime.cpp:222-236` (miss log, first 400, `img=%016llx var=%016llx pc gen` under `PS2_VU1_HLE_DEBUG`), `:252-284` ([CV2] per-(image, entry-pc) decline census, 64 slots, printed at fallback #1 and every 5000), `:410-420` (exit totals: `native runs / fallbacks / direct_kicks / direct_declines / drawir_kicks / drawir_declines`) | Counts and NAMES the residual entries (image hash + pc + n). stderr text only — no case file, no savestate, no bundle |
| E9 | **[GS-CLASSRDR] declined counters** | `host_app/gs_class_renderer.h:289-309` (per-class atomics `fb_norenderer` / `fb_disabled` / `fb_declined`), `:363` (decline bump), `:405-421` (report lines), `host_app/graphics_thread.cpp:4296` (bench routing-counter emitter) | Per-class routing currency for gate G-D1. Counters only — the declined draw itself is not captured |
| E10 | **PERF buckets** | `host_app/perf_profiler.h:7` (`PS2_PERF=1`, default fully disabled), bucket enum + names `:88-137` (31 buckets: ee, vu1, vif_gif, dmac, texdec, gl_submit, readback, present, gif_parse, gs_kick, flush, snap, qpush, gs_xfer, vu0, snap_mat, walk_tag, walk_copy, gl_tex, gl_unif, gl_draw, gl_blendcp, gl_fbo, mtvu_sync, ee_game, file_io, mpeg, dmac_park, dmac_feed, parse_oth, p2copy), `[PERF-SUMMARY]` `:229` | Per-thread RAII wall-clock buckets; totals snapshot feeds `perf.txt` in every ring bundle (`ring_log.cpp:389-397`) |
| E11 | **cuelag / INLAT probes** | `host_app/cuelag_probe.h:1-40` (`PS2_CUELAG=1`, `[CUELAG]` line per 300-tick window: frame vs vblank vs audio-device slopes, lag_win/lag_cum), `host_app/sce_hle/libpad.cpp:405-416` ([S29-A3] `PS2_INLAT_ANALOG`), `input/input_latency.h` ([R183-INLAT] stamp chain) | Timing-drift and input-latency diagnostics; stderr only |
| E12 | **Drawlist recorder (.gsdl)** | `host_app/hle/drawlist_record.h:1-33` (gate `PS2_DRAWLIST_RECORD=<path>`, `_FRAMES=a-b`, `_VERTS=0`, `_PIXELS=1`, `_PPM=1`), format magic `"2DLR"` v2 `:44-45`, kinds `:47-52`, v2 draw-identity stamp {handler id, image hash, kick pc, kick ordinal, instance} `:28-35` | The permanent GS draw-IR recording; read by `tests/drawlist_tool.cpp` (stats/dump/diff), replayed by `tests/gs_replay.cpp` |
| E13 | **Ring bundles (hotkey/programmatic)** | `host_app/ring_log.h:145`, `ring_log.cpp:335-400` (`WriteBundleTo`: default dir `logs/bundle_<ms>_f<frame>/`; ring.rlog + decoded ring.txt + env.txt with every `PS2_*` var + perf.txt with all PERF totals) | The evidence-dir primitive slowspot and the F9 hotkey both ride; live examples: `logs/bundle_1787450081869_f2600/` et al. |
| E14 | **coldboot_gate** | `tools/coldboot_gate.py:1-27` ([S112-BLACKBOOT]: asserts presented mean luma > `--min-luma` by `--by-frame`, runs the binary cold with `PS2_BB_SNAP` sampling, parses `logs/bb_frame_*.ppm`) | The pixel-truth boot gate; a harness input (MIGRATION Phase 0) |
| E15 | **Savestate request/writer path** | `host_app/savestate.h:69-108` (`RequestSaveNamed`, `RequestSaveNamedUser`, `LastResult` surface), `savestate_format.cpp:170` (magic `PS2SAVE1`), `savestate_writer.h:44-92` (async capture writer, PPM copy enqueue) | Boundary-quiesced async named saves — the primitive every capture rides. **Named requests only**; nothing periodic |
| E16 | **Pad script playback + write-order test seam** | `host_app/input/input_system.h:9`, `input/input_system.cpp` (`PS2_PAD_SCRIPT` overlay), `host_app/pad_input.h:74-81` (`pad_input_test::WriteRecorder` — a unit-test mirror of guest pad writes, production nullptr) | Scripted input PLAYBACK exists and is the harness grammar. No live RECORDING to that grammar |
| E17 | **GUI hooks** | `host_app/gui/bug_report_section.cpp:1-38` ("Bug reports" section: manual "Report bug now" + include-state toggle + polled results + slowspot-bundling toggle), `gui/vu_microcode_section.cpp` (VU collection), `gui/savestate_browser.*` | Graphics-thread GUI surfaces over E1/E4/E7 |
| E18 | **Dyn-decode logging (EE)** | `host_app/syscalls.cpp:1040,1079,1085` ("Dynamic interpreter: …" g_logFile lines), `[IOP-STUB]` log census referenced at `host_app/sif_rpc_handlers.cpp:4830` | The EE dynamic interpreter and IOP stubs LOG; neither counts, keys, nor bundles |

### 1.2 What is MISSING vs D9 (the gap list this design closes)

| # | D9 requirement | Today | Gap |
|---|---|---|---|
| M1 | **EE-function fallback capture** (SDK/OS routine running as recompiled/dyn-decoded code where an HLE binding is expected; the code + entry state + memory windows + oracle I/O to lift it) | Does not exist. Dyn-decode logs text (`syscalls.cpp:1040`); sce bindings have no "expected but absent" detector; no `EeFunctionCase` format | Build `FallbackDetector` (EE arm) + `EeFunctionCase` (§5.3) |
| M2 | **IOP/RPC stub capture** (the RPC payload + oracle reply) | `[IOP-STUB]` log lines only (`sif_rpc_handlers.cpp:4830`) | RPC arm of `FallbackDetector` + `RpcCase` (§5.4) |
| M3 | **GS decline capture** (the declined draw itself) | Counters only (`gs_class_renderer.h:299,363`) | GS arm: one-shot `.gsdl` window keyed on first decline per class×reason (§5.5) |
| M4 | **VU1 fallback → case bundle** | Counters + census (`vu1_hle_runtime.cpp:252-284`) and a SEPARATE env-armed vutrace capture (E6) that must be armed before launch | Fallback-triggered `Vu1Case` capture at the declining Execute entry, always-armed, dedup-keyed (§5.2) |
| M5 | **Spike detection** (frame-time variance, high/low) | Slowspot detects sustained p50 only (`slowspot_detector.h:31-38`) | `SpikeDetector` (§4.3) + spike bundle content (§5.8) |
| M6 | **Rolling savestate ring** (a state from BEFORE the event) | No periodic/rolling saves — named requests only (E15); slowspot's state lands AFTER the trigger | `SavestateRing` (§4.4) |
| M7 | **Input log since the savestate** | Playback grammar exists; no recorder (E16; EQUIVALENCE §2 lists the recorder as a required-new harness piece) | `InputLog` recorder writing the `PS2_PAD_SCRIPT` grammar continuously, rotated at ring saves (§4.5) |
| M8 | **AI-ready MANIFEST.md** (what happened / what to do / repro tool / acceptance test) | `manifest.txt` is a file inventory only (`bug_report.h:166-196`) | `BundleManifest` (§6) |
| M9 | **Unified on-disk layout** | Captures scatter: `bug_reports/` dirs + flat flicker files, `save_states/slowspot_*`, `logs/bundle_*`, `vutrace_cases*/` | One root: `games/<title>/triage/{fallbacks,perf,spikes}/` (§7) |
| M10 | **Content-keyed dedup** | Partial, per-mechanism: slowspot vf/600 buckets (`slowspot.cpp:199-207`), artdet cooldown+cap, bugreport 1-per-run cap | `TriageDedup`: same image hash / same hot function / same RPC id ⇒ one bundle + counter (§8) |
| M11 | **PERF bundle completeness** (host sample/profile, draw census, GPU timings, hot list) | Slowspot bundle has PERF totals + ring window (E4) but no host profile, no draw census, no GPU timings, no hot-function list | PERF bundle content spec (§5.7) |
| M12 | **Cost governance** (capture must not itself cause spikes) | Per-mechanism lessons exist ([FIX-SLOWSTICK] worker threads, O(1) detector) but no shared writer/budget | `TriageWriter` async thread + measured savestate cost + self-masking (§9) |

Crash + manual bundles (E1/E2) are ADJACENT to D9 (not one of the three classes) and
are retained: they become two extra trigger kinds of the same `TriageBundler`, so the
U5 machinery has exactly one successor.

---

## 2. The subsystem — `engine/core/triage/` (one class per file, D4)

Title-neutral mechanism (D5): zero game tokens; everything scene/title-specific
(fps target overrides, known-hot-function names) arrives through `TitleProfile` /
Config. Compiled into ALL builds (unlike `core/diag`, which compiles out of
Release): triage capture is a product feature — the whole point is catching
events on end-user/play machines. Each detector's hot path is O(1) and OFF-able.

```
engine/core/triage/
├── triage_host.h/.cpp        # TriageHost: owns the others; boot wiring, Config latch,
│                             #   per-frame Tick fan-out; the ONLY public entry
├── triage_bundler.h/.cpp     # TriageBundler: bundle dir naming/creation, file staging,
│                             #   manifest+counters write, retention (absorbs U5 Builder)
├── fallback_detector.h/.cpp  # FallbackDetector: the five fallback arms' report sinks
│                             #   (VU1 / EE / IOP-RPC / GS-class / syscall) + dedup keys
├── perf_monitor.h/.cpp       # PerfMonitor: sustained-fps detector (generalized slowspot
│                             #   Detector — same O(1) window-p50 core, fps-target-relative)
├── spike_detector.h/.cpp     # SpikeDetector: per-frame time series ring + robust
│                             #   high/low outlier trigger + spike-frame annotation hooks
├── savestate_ring.h/.cpp     # SavestateRing: periodic boundary saves into K rotating
│                             #   slots + input-log rotation + measured save cost
├── input_log.h/.cpp          # InputLog: continuous pad recorder in the PS2_PAD_SCRIPT
│                             #   grammar (the EQUIVALENCE §2 recorder — ONE implementation)
├── bundle_manifest.h/.cpp    # BundleManifest: MANIFEST.md (AI contract, §6) +
│                             #   manifest.txt (file inventory, U5-compatible) writers
├── triage_dedup.h/.cpp       # TriageDedup: content-key index (key → bundle id + count),
│                             #   persisted as index.tsv at the triage root
├── triage_writer.h/.cpp      # TriageWriter: THE single background writer thread —
│                             #   bounded job queue, byte-rate throttle, drop accounting
└── frame_annotator.h/.cpp    # FrameAnnotator: per-frame event tallies (uploads, compiles,
                              #   readbacks, allocs, waits, disk IO, chunk loads) feeding
                              #   SpikeDetector's "what happened in the spike frames"
```

`tools/triage/` (offline, §10) is the consumer CLI. Producers report through one
narrow header (`triage_host.h`); subsystem call sites are one-liners:

| Producer site (new tree) | Reports |
|---|---|
| `vu/hle/` dispatch miss + program DECLINE (today `vu1_hle_runtime.cpp:222-284`) | `OnVu1Fallback(image_hash, variant_hash, pc, reason)` |
| `ee/cpu/` dispatch: dyn-decode entry; `ee/hle/` binding table: call into an address `sce_lib_finder` matched but no HLE row bound (today: nothing) | `OnEeFallback(fn_addr, kind, reason)` |
| `sif/hle/` RPC router stub arm (today `[IOP-STUB]` logs) | `OnRpcStub(module, rpc_id, payload*, len)` |
| `gs/hle/classrdr/` Accept()=false / Dispatch decline (today counters, `gs_class_renderer.h:363`) | `OnGsDecline(cls, reason, draw_stamp)` |
| `ee/hle/syscalls` default arm (today log text, `syscalls.cpp:1040`) | `OnSyscallStub(num)` |
| `gs/hle/present/` per-present (today slowspot's `OnPresent`, `slowspot.h:44-49`) | `OnPresent(now_us, vf, ee_frame, frame_ms)` |
| everywhere `FrameAnnotator` counts (texture upload, pipeline compile, readback, big alloc, thread park, disk read, cdvd chunk load) | `NoteFrameEvent(kind, bytes_or_us)` |

## 3. Triggers + thresholds (Config schema fields)

[REV3 2026-09-23 D9] New `triage` Config section (ARCH §4.8 amended: `dev.
slowspot_capture` retires INTO this section — the slowspot watchdog becomes
PerfMonitor — so the schema goes 32 − 1 + 7 = **38 fields**):

```
triage {
  enabled: bool = true            # master; false compiles detectors to latched no-ops
  fallbacks: bool = true          # class (a): capture HLE-fallback bundles
  perf: bool = true               # class (b): sustained-perf bundles
  spikes: bool = true             # class (c): spike bundles
  fps_target: int = 0             # 0 = auto (video.fps_cap, else 60/NTSC from TitleProfile)
  disk_budget_mb: int = 2048      # hard cap for games/<title>/triage/ (§8 retention)
  ring_period_s: int = 10         # SavestateRing cadence; 0 = ring off (spike bundles
}                                 #   then use at-trigger states, noted in MANIFEST)
```

Derived thresholds (constants in code, not config — the slowspot lesson: defaults
were A/B-derived once and then stable, `slowspot_detector.h:31-38`):

| Trigger | Rule | Basis |
|---|---|---|
| FALLBACK | first occurrence of a NEW dedup key (§8) fires a bundle; subsequent hits increment the key's counter (frequency lands in the manifest at the next flush) | dedup-first: fallback storms are the norm (BUG-023: fallbacks==execs), so per-key one-shot is the only affordable shape |
| PERF | window p50 frame-ms > `1000/fps_target` for a FULL window (60 presents) AND sustain ≥ 10 s (600 presents cumulative over-threshold), cooldown 30 s | generalizes slowspot: same O(1) window-p50 core; 40 ms@25fps-line precedent (`slowspot.h:30-34`); the sustain term separates class (b) from a spike |
| SPIKE-HIGH | frame_ms > max(3 × rolling-median(120), median + 25 ms) — a single frame or a burst ≤ 60 presents; bursts coalesce into one event | robust to slow scenes (multiplicative) and fast scenes (absolute floor); a sustained breach ≥ 10 s reclassifies to PERF |
| SPIKE-LOW | frame_ms < 0.4 × rolling-median(120) while pacing is vsync/cap-bound (a "runaway fast" frame = pacing bug evidence) | catches the INTERP/pacing family (BUG-002/009/027) |
| SPIKE masking | frames where a ring save or a bundle-assembly job ran on ANY thread are annotated `self_inflicted` and never trigger (they still appear in the series) | capture must not report itself (§9) |
| CRASH / MANUAL | unchanged semantics from U5 (sentinel scan at next launch; GUI button) | E1/E2 retained as extra trigger kinds |

Per-class session caps (constants): fallbacks ≤ 32 bundles/run (dedup makes this
rarely binding), perf ≤ 4/run, spikes ≤ 8/run (artdet precedent: 8/session,
`artdet_flicker.h:33-34`), crash 1, manual unbounded (user-driven).

## 4. Component contracts

### 4.1 TriageHost
Constructed by `app/host` after Config load and TitleProfile registration; receives
`{const Config&, const TitleProfile&, build_id}`. Owns the frame hook order:
`OnPresent → PerfMonitor → SpikeDetector → SavestateRing::MaybeSave → TriageWriter
pump-check`. All producer report calls funnel here; when `triage.enabled=false`
every entry is one latched-bool test (the `vu1_hle_runtime.cpp:59-73` latch idiom).

### 4.2 PerfMonitor
The slowspot `Detector` core verbatim (ring of frame intervals, incremental
over-threshold count, exact p50 equivalence `p50>T ⇔ over ≥ n−n/2`,
`slowspot_detector.h:9-16,54-77`) with: threshold derived from `fps_target`;
a sustain accumulator (600 over-threshold presents within a rolling 900) before
FIRE; `Reseed()` on load/warp discontinuities (kept, `:77-81`); game-speed arm
(ee_frame-clocked) kept from `slowspot.h:44-49`.

### 4.3 SpikeDetector
Keeps `FrameSample{vf, frame_ms, annot_tallies[12]}` in a 1,800-present ring
(30 s at 60). Rolling median over 120 via two-heap or P² estimate — O(log n)
worst, amortized O(1). On FIRE: freezes a copy of the ±300-present window (the
series file, §5.8), records which ring-slot savestate is the newest one OLDER
than `spike_start − 1 s` (the repro anchor), and hands the bundle job to
TriageWriter. `FrameAnnotator` tallies per present: draw count, texture-upload
bytes, pipeline/shader compiles, readbacks, GC/big-alloc count+bytes, thread-park
µs, disk-IO bytes (FILE_IO bucket delta), cdvd chunk loads, MPEG decode µs —
each an existing counter or PERF bucket delta (E10), snapshot per present into
the ring slot (fixed array; no allocation).

### 4.4 SavestateRing
Every `ring_period_s`, calls `savestate::RequestSaveNamed` into
`games/<title>/triage/.ring/slot<i>.ps2st` (i rotating over K=4 slots) and asks
InputLog to rotate its file in the SAME boundary. Quiesce refusals retry next
boundary; a slot is valid only when the atomic writer landed (the E4 probe
pattern, `bug_report.cpp:437-452`). Cost governance: the first ring save's wall
cost is measured; if > 8 ms of EE-boundary stall the period auto-doubles (and
the manifest notes the effective period). Spike bundles COPY the chosen slot
(never move — the ring keeps rolling). Slot files live outside the bundle dirs
so retention never eats the live ring.

### 4.5 InputLog
Continuous recorder of the served pad state per game frame, in the
`PS2_PAD_SCRIPT` grammar (playback already exists, `input/input_system.cpp`;
this recorder is the SAME component EQUIVALENCE §2 requires — built at
MIGRATION Phase 0 as the harness recorder, rehomed to `engine/core/triage/`
at Phase 4, one implementation forever). Rotation: on each ring save the current
log closes as `slot<i>.padlog` (paired with `slot<i>.ps2st`); a bundle copies
the pair + the still-open tail up to the event frame, giving "savestate + input
since it" replayability. Format: one line per changed frame
(`F<frame> buttons=<hex16> lx ly rx ry`), header carries start-state frame +
title + build id. Cost: append to a 64 KB user buffer, flushed by TriageWriter.

### 4.6 FallbackDetector
Five report sinks (§2 table). Each maps its arguments to a dedup key (§8),
consults TriageDedup (lock-free hash-set probe on the producing thread; misses
take a leaf mutex), and on a NEW key stages a bundle job carrying the class-
specific payload snapshot (§5) — snapshots are taken ON the producing thread
(the data is only coherent there: the Vu1Case rule, `vu_trace_case.h:5-11`)
into preallocated staging buffers, then handed to TriageWriter. Known-hit keys
cost: hash + probe, no allocation.

### 4.7 TriageBundler
The U5 `Builder` generalized (`bug_report.cpp:99-173`): create
`games/<title>/triage/<class>/<UTC>_<id>/`, stage files, write `counters.json`
+ both manifests (§6), fsync, then run retention (§8). All on the TriageWriter
thread. Collision suffix `-N` and shape-matched retention names carry over
(`bug_report.h:101-146`).

### 4.8 TriageWriter
ONE detached background thread, bounded job queue (depth 8; overflow = drop the
job, bump `triage_dropped_jobs`, one stderr line — never block a producer),
byte-rate throttle 64 MB/s with 4 MB chunks + sleep between chunks (a 44 MB
state copy = ~0.7 s spread out, invisible to the frame loop; measured copy cost
recorded per bundle). Worker-thread lesson inherited: [FIX-SLOWSTICK]
(`bug_report.cpp:20-24`).

---

## 5. Per-class bundle CONTENT SPEC

Every bundle, regardless of class, contains the **common core**:

| File | Format | Source |
|---|---|---|
| `MANIFEST.md` | §6 AI contract | BundleManifest |
| `manifest.txt` | U5 inventory format verbatim (`bug_report.h:166-196`) — tooling continuity | BundleManifest |
| `state.ps2st` | PS2SAVE1 (`savestate_format.cpp:170`) — at-event state (fallback/perf) or the pre-event ring slot (spike) | savestate / SavestateRing |
| `input.padlog` | §4.5 grammar — pad log SINCE `state.ps2st` up to (event + 2 s) | InputLog |
| `config.ini` + `env.txt` | effective 38-field Config dump + every `PS2_*` var (transition period), the `ring_log.cpp:373-385` env capture generalized | TriageBundler |
| `build.txt` | build id (`g_build_id`), title profile id (serial+crc), engine version, os/arch | TriageBundler |
| `counters.json` | dedup key, occurrence count at write, per-class counters snapshot, ring/PERF totals | TriageDedup + producers |
| `ring.rlog` + `ring.txt` | ring dump + decoded twin (`ring_log.cpp:366-370`) | ringlog |

### 5.1 Class (a) FALLBACK — common extra: `frequency` in counters.json (hits of this key so far), `decline_reason` string
### 5.2 VU1 fallback (`fallbacks/…_vu1_<hash8>/`)
| File | Format |
|---|---|
| `case.vu1case` | **Vu1Case verbatim** (`vu_trace_case.h:26-83`: VUC1 v1, start_pc, VUregs, 16K micro + 16K data, VIF1 TOP/TOPS/ITOP/ITOPS, Q/P latches, vu_cycle) — captured at the DECLINING Execute entry, before the interpreter runs |
| `oracle.out` | the interpreter tier's outputs for this exact case: post-run VUregs + data-memory image + the XGKICK byte streams in order (what `vu1_replay --compare` produces today) — the bit-exactness target a lift must match |
| `disasm.txt` | VU micro disassembly of the image (upper/lower pairs), variant region annotated (family signature base 0x1720) |
| `identity.txt` | image_hash, variant_hash, entry pc, family-signature match state, nearest registered library entry + its name (the `vu1_hle_runtime.cpp:222-236` miss line, filed) |
### 5.3 EE-function fallback (`fallbacks/…_ee_<addr>/`) — **NEW format: EeFunctionCase**
```
EeFunctionCase (file case.eecase; magic "EEC1", version 1, little-endian)
├── fn_addr, fn_len              # v2-discovered extent
├── kind                         # dyn_decode | sce_match_unbound | hle_expected
├── code[]                       # the function's bytes FROM GUEST RAM at capture
│                                #   (not the ELF: self-modified/overlaid code truth)
├── entry_ctx                    # full CpuContext GPR/FPU/COP0 slice at entry
├── stack_window                 # 1 KB at $sp (callers' frames for argument recovery)
├── mem_windows[N≤8]             # 256 B around each pointer-looking arg register
│                                #   (a0-a3, t0-t1) that lands in guest RAM
├── ret_ctx                      # CpuContext at return (ra reached) — oracle OUTPUT
├── ret_mem_windows[N]           # the same windows re-read at return — oracle side effects
└── call_meta                    # caller pc, thread id, vf, ee cycle
```
Capture protocol: on a NEW ee key the detector snapshots entry state, lets the
recompiled/dyn-decoded body run, and snapshots the SAME windows at return —
one full oracle I/O pair per bundle, so `triage lift` can scaffold a signature-
bound HLE and prove it against `entry→ret` without the game. Return detection:
for `sce_match_unbound`/`hle_expected` kinds the detector wraps the ONE
dispatch-table call for the armed entry (snapshot → `func_<addr>(ctx)` →
snapshot when the host call returns; a recompiled body returns to its host
caller exactly at guest `jr ra`, so the pair is two bounded memcpys around one
existing call — no guest-side trap); for `dyn_decode` the decoder loop compares
`pc` against the `ra` latched at entry (one armed capture at a time,
re-entrancy-guarded). Either path abandons the pair after a cycle-budget
watchdog and writes the bundle with `oracle: entry_only` in the manifest.
Extra files:
`disasm.txt` (MIPS disassembly via EEAnalyze/rabbitizer), `identity.txt`
(sce_lib_finder nearest-signature report: library, confidence — why we think
this SHOULD be HLE'd).
### 5.4 IOP/RPC stub (`fallbacks/…_rpc_<module>_<id>/`)
`rpc.bin` (the exact request payload + header {module, rpc_id, in_len, out_cap}),
`rpc_reply.bin` (what the stub returned — today's oracle-of-record), `identity.txt`
(module name/version from the TitleProfile IOP table, RPC id, known-API annotation
for the stock Sony set). For STREAM.IRX (the one custom module): the feed-contract
pointers snapshot (recv buffer state) rides in `mem_windows`.
### 5.5 GS class decline (`fallbacks/…_gs_<class>_<reason>/`)
`window.gsdl` (drawlist v2 recording of the declining frame ±1, verts+pixels ON —
the `.gsdl` format verbatim, `drawlist_record.h:44-52`), `draw.txt` (the declined
draw's full register/census tuple in Accept-table vocabulary + which predicate
failed), `frame.ppm` (presented backbuffer). The capture uses the recorder's
existing frame-window arm (`PS2_DRAWLIST_RECORD_FRAMES` mechanism) armed
transiently by the detector for the NEXT frame after first decline (same scene ⇒
same draw recurs — the artdet re-fire assumption: cooldown-then-recapture,
`artdet_flicker.h:32-33`).
### 5.6 Syscall stub (`fallbacks/…_syscall_<num>/`)
**Common core only — no class-specific case file and no oracle file.** Nothing
code-shaped exists to capture: the missing thing is a HANDLER, not a guest
routine. The call's full argument set is recoverable from `state.ps2st` — the
savestate is taken AT the dispatcher's default arm, so its CpuContext holds the
syscall number and the `$a0–$a3` argument registers exactly as passed (plus
guest RAM for pointed-to structures). And there is no oracle output to record
because the stub's current return IS the baseline being replaced — the lift
target is the SDK-documented semantics, proven by `triage repro` (game-visible
behavior), not by a captured I/O pair. Syscall number, SDK name (when in the
documented table) and caller pc land in MANIFEST.md + `counters.json` via the
dedup key and `decline_reason` (§5.1); `triage lift` emits the `ee/hle` syscall
row + manifest note (§10).
### 5.7 Class (b) PERF (`perf/…/`)
| File | Content |
|---|---|
| `perf_buckets.tsv` | full PERF totals snapshot at trigger AND delta over the slow window (all 31 buckets, E10) — requires PerfMonitor to force-enable perf accounting for its window ring even when `PS2_PERF` unset (cheap: bucket RAII only, no I/O) |
| `host_profile.txt` | host sampling profile over 5 s at trigger: mac `/usr/bin/sample <pid>` child process; linux `perf record` if present; windows own-thread EIP sampler — folded by module (the BUG-025 method); absent ⇒ manifest notes why |
| `draw_census.tsv` | per-frame draws by class (classrdr routing counters delta, E9), texture uploads, target switches |
| `fallback_counters.tsv` | the five fallback-arm counters + top dedup keys by hits (a decline storm names itself here — BUG-023) |
| `gpu_timings.tsv` | GL timer-query per-stage ms if the backend exposes them (caps-gated); absent ⇒ noted |
| `hot_functions.tsv` | top-32 recompiled fns by self-sample count from the host profile mapped through the fn table (addr, name-if-adjudicated, %) |
| `perf_window.txt` | PERF-channel ring lines ±600 frames (the E4 `FilterPerfLines` mechanism, `bug_report.h:314-338`) |
| `frame_series.tsv` | the SpikeDetector ring dump for the slow window (same format as §5.8) |
### 5.8 Class (c) SPIKE (`spikes/…/`)
| File | Content |
|---|---|
| `state.ps2st` + `input.padlog` | the ring slot from BEFORE the spike (§4.4) + pad log from that slot through spike+2 s — the repro pair |
| `frame_series.tsv` | per-present rows `vf, frame_ms, draws, upload_bytes, compiles, readbacks, allocs, park_us, disk_bytes, chunk_loads, mpeg_us, self_inflicted` for ±300 presents around the spike (FrameAnnotator ring) |
| `spike.txt` | the spike frames enumerated: which annotator columns DELTA'd vs the surrounding median (the "what happened" attribution, pre-computed so the AI session starts from a ranked cause list) |
| `at_event.ppm` | presented backbuffer at the worst frame |
| `perf_buckets.tsv` | bucket deltas: spike frames vs window baseline |

## 6. MANIFEST.md — the AI contract (written by BundleManifest)

Deterministic section order; every field machine-greppable (`key: value`); prose
budget ≤ 30 lines. Template:

```markdown
# triage bundle <id>
class: fallback|perf|spike        trigger: vu1|ee|rpc|gs|syscall|sustained_fps|spike_high|spike_low
title: SLUS-12345 (0x<elf-crc>)    build: <g_build_id>    engine: <version>
created_utc: …                    dedup_key: …           occurrences_at_write: N
mods: none (asserted)             config: config.ini (profile=<name>)

## What happened
<2-5 lines: the trigger values vs thresholds; for fallbacks the decline reason
and the identity line; for perf the p50/p95 + top-3 buckets; for spikes the
worst frame_ms vs median and the top-3 annotator deltas.>

## What to do
<the per-class playbook line, e.g. fallback/vu1: "lift this variant into the
title Vu1ProgramLibrary; the oracle pair in this bundle is the proof target.">

## Reproduce
    tools/triage/triage repro <bundle-dir>          # headless; expected: <event> re-fires
    (manual: host_app --load-state state.ps2st --pad-script input.padlog)

## Acceptance
    <the exact command that must pass AFTER a fix, e.g.
     vu1_replay --hle case.vu1case  ⇒ exit 0, bit-exact vs oracle.out
     / triage repro ⇒ event does NOT re-fire and harness leg stays hash-identical>

## Files
<table: file, bytes, one-line role — superset of manifest.txt>
```

## 7. On-disk layout — `games/<title>/triage/` (decision + justification)

```
games/<title>/triage/     # gitignored EXCEPT README.md
├── README.md                     # ownership para + this doc's link (checked in)
├── index.tsv                     # TriageDedup persistence: key → bundle id, count, last_vf
├── .ring/                        # slot<0..3>.ps2st + slot<0..3>.padlog (live ring, never retained-away)
├── fallbacks/<UTC>_<kind>_<key8>/…
├── perf/<UTC>_<id>/…
├── spikes/<UTC>_<id>/…
├── crash/<UTC>_<id>/…            # U5 crash trigger, absorbed (§12)
└── manual/<UTC>_<id>/…           # U5 manual trigger, absorbed (§12)
```

Decision: **in-tree under `games/<title>/triage/`, not a user data dir.** Grounds:
(1) bundles are per-title by construction and every consumer of title data
(corpus, savestates, baselines) already resolves under `games/<title>/` (ARCH §3)
— `triage lift` promotes a fixed bundle's case INTO the sibling `corpus/` with a
rename, not a cross-volume copy; (2) the dev loop is the only current consumer —
tools/triage, the harness and CI all run repo-relative (coldboot_gate assumes
repo-root cwd, `tools/coldboot_gate.py:23`); (3) a shipped end-user build
redirects the GAMES ROOT wholesale through `Platform::open` path mapping (ARCH
§16) — the triage root rides that same indirection, so "user data dir" is a
platform-layer concern, not a layout fork. Names stay shape-matched
(`\d{8}-\d{6}_<kind>[_-]…`) so retention never touches foreign files
(`bug_report.h:110-146` rule carried forward).

## 8. Dedup + retention

**Dedup keys** (TriageDedup; key = class tag + 64-bit hash):
| Class | Key |
|---|---|
| vu1 | variant_hash (falls back to image_hash when variant absent) — same variant ⇒ one bundle, counter++ (the E8 census, made durable) |
| ee | fn_addr (+ kind) |
| rpc | module × rpc_id |
| gs | class × failed-predicate id |
| syscall | syscall number |
| perf | top hot function (or top bucket when no profile) × vf/3600 scene bucket — same hot spot re-entered within a minute ⇒ counter++ (slowspot vf/600 precedent widened, `slowspot.cpp:199-207`) |
| spike | top annotator cause × vf/600 bucket |

Index persisted to `index.tsv` on every write and loaded at boot: dedup survives
restarts (the "same image hash ⇒ one bundle + counter" decision). `counters.json`
in the bundle is refreshed lazily: when a known key hits a power-of-two count
(2,4,8,…), TriageWriter rewrites that bundle's counters.json + MANIFEST occurrence
line — frequency lands in the bundle without per-hit I/O.

**Retention** (TriageBundler, after every bundle): per-class keep-newest caps —
fallbacks 64, perf 8, spikes 16, crash 4, manual 20 (U5's `kKeepBundles=20`
precedent, `bug_report.cpp:51`) — THEN the `triage.disk_budget_mb` (default
2048) hard cap prunes oldest-first across classes, except: a bundle whose dedup
key's count is still growing this session is skipped once (evidence in active
use). Deleting a bundle keeps its `index.tsv` row (marked `pruned`) so dedup
does not re-fire and the count history survives. The live `.ring/` is exempt
(bounded: 4 × ~44 MB states + logs ≈ 180 MB, `bug_reports/20260922-031552_slowspot/
manifest.txt` sizing).

## 9. Cost budget — capture must not itself cause spikes

| Cost | Bound | Mechanism |
|---|---|---|
| Detector hot path (per present) | < 1 µs | O(1) window update (slowspot core, `slowspot_detector.h:9-16`); annotator = fixed-array tallies of counters that already exist |
| Fallback known-key hit | hash + lock-free probe | §4.6; no allocation, no I/O |
| Fallback NEW key | snapshot into PREALLOCATED staging (Vu1Case 33,456 B; EeFunctionCase ≤ 8 KB windows) + queue push | producing-thread memcpy ≤ ~50 µs (VU1 case = one 33 KB copy); the write happens on TriageWriter |
| Ring save | savestate quiesce + write, measured | async named-save path (E15) — the boundary stall is the SAVE cost, measured at first ring save; > 8 ms ⇒ period auto-doubles (§4.4); spike frames overlapping a save are `self_inflicted`-masked (§3) |
| Bundle assembly | ≤ 64 MB/s, 4 MB chunks | TriageWriter throttle (§4.8); a 44 MB state copy spreads over ~0.7 s |
| InputLog | ≤ 32 B/frame append into 64 KB buffer | flushed by TriageWriter at ring rotations |
| Queue overflow | drop + count, never block | `triage_dropped_jobs` counter surfaces in the next bundle's counters.json |
| Idle cost (`triage.enabled=false`) | one latched bool per report site | the `vu1_hle_runtime.cpp:59-73` latch idiom |
| Self-measurement | every bundle's counters.json records `capture_cost{stage_us, copy_bytes, copy_ms, ring_save_ms}` | the budget is AUDITED per bundle, not assumed |

---

## 10. Handoff workflow — `tools/triage/` (how a later session consumes a bundle)

One CLI, four subcommands, all headless, all reading ONLY the bundle + the repo
(never the original machine state — the D9 contract). Lives in `engine/tools/`
neutrality (bundle paths are arguments); builds against subsystem libraries +
`verify/` like the replay tools (ARCH §10).

```
triage list [--root games/<t>/triage] [--class …] [--since …] [--key …]
    One row per bundle: id, class, trigger, dedup key, occurrences, size,
    repro-status (from the last `repro` run, cached in the bundle), fixed?
    (index.tsv row state). Reads index.tsv + MANIFEST.md front matter only.

triage repro <bundle>
    Headless re-run: verify/run_leg --binary <exe> --anchor <bundle>/state.ps2st
    --inputs <bundle>/input.padlog --profile deterministic --frames <window>,
    with the matching detector armed. PASS = the event re-fires (same dedup key
    for fallbacks; p50 breach for perf; a masked-comparison spike within ±60
    presents of the recorded offset for spikes — wall-time spikes caused by
    host load legitimately may not reproduce: repro then reports NONDET and the
    manifest's what-to-do escalates to the annotator series instead). Writes
    repro.txt into the bundle (status + observed values).

triage lift <fallback-bundle>
    Scaffolds the fix workspace per trigger kind:
      vu1: emits games/<t>/vu1_library/ entry stub (family/variant keys filled
           from identity.txt), registers case.vu1case + oracle.out as a vutrace
           corpus case, prints the proof command (vu1_replay --hle <case> ⇒
           bit-exact vs oracle.out) — the MIGRATION Appendix A "capture→lift"
           workflow, fed by play instead of env-armed legs.
      ee:  emits an ee/hle (or subsystem hle/) binding stub with the
           sce_lib_finder identity row, plus a ctest oracle test that loads
           case.eecase, runs the HLE against entry_ctx/mem_windows and asserts
           ret_ctx/ret_mem_windows byte-equal.
      rpc: emits the sif/hle handler stub + a payload-replay test asserting
           rpc_reply.bin equivalence (or documenting the intended divergence).
      gs:  emits the RendererProfile Accept-row candidate (draw.txt tuple) +
           a gs_replay case over window.gsdl with the class-routing assertion.
      syscall: emits the ee/hle syscall row + manifest note.

triage perf <bundle>
    The attribution report: merges perf_buckets + host_profile + draw_census +
    fallback_counters + frame_series into one ranked table (bucket share, hot
    fn share, fallback storm score, upload/compile churn score) with the
    TRIAGE.md §5 decision tree applied mechanically — output is a filled-in
    "likely cause: X because Y" section appended to the bundle's MANIFEST.md.
```

## 11. Fixed bundle → regression test + bug-KB entry (the loop closes)

When a bundle's cause is fixed, the SAME commit must (per-class):

1. **Promote the proof to a permanent test at its layer** — vu1: the lifted
   case joins `games/<t>/corpus/vutrace_*` and the entry's `oracle_case_ids`
   (ARCH §14.2 FAIL=0 gate); ee/rpc: the scaffolded oracle ctest lands enabled;
   gs: window.gsdl joins the gs corpus via the EQUIVALENCE §6 freeze ledger;
   perf/spike: the bundle's `state.ps2st + input.padlog` become a named harness
   anchor leg with a frame-time assertion (p50 within target; no unmasked spike
   > threshold) — the leg is the regression pin.
2. **Write/extend the `docs/bugs/` entry** (D8 rule: every root-caused defect
   lands its KB entry in the fix commit) citing the bundle id in the
   scene/savestate field — the bundle IS the entry's repro asset.
3. **Mark the index row `fixed@<commit>`** — `triage list` shows it; a re-fire
   of a fixed key is a REGRESSION and its new bundle's manifest says so loudly.

## 12. Absorption map (what this subsystem replaces) + GUI

| Today | Becomes |
|---|---|
| bug_report.{h,cpp} Builder/manifest/retention/crash-scan (E1/E2) | TriageBundler + BundleManifest + crash trigger kind (crash bundles land in `triage/crash/` — layout §7 gains that dir) |
| slowspot.{h,cpp} + detector + paths (E3/E4) | PerfMonitor (+ common core) — `dev.slowspot_capture` Config field retires into `triage.*` (§3) |
| artdet fire bundles (E5) | a `diag`-build detector that REPORTS through TriageHost (fires a spike-class bundle with its band metrics as an annotator column) — the flat-file writer deletes |
| vutrace env-armed capture (E6/E7) | stays for CORPUS harvesting (bulk PER_IMAGE collection); the fallback-triggered single-case capture is FallbackDetector's vu1 arm reusing the same Vu1Case writer |
| [VU1-HLE-RT] census (E8), [GS-CLASSRDR] counters (E9), syscall/IOP logs (E18) | the counters stay (routing currency); each site gains the one-line report call (§2) |
| ring WriteBundleTo evidence dirs (E13) | the common-core `ring.rlog/ring.txt/env→config` files inside every bundle; the standalone hotkey bundle stays as `manual` trigger |
| GUI bug_report_section (E17) | "Triage" panel: last-result line, per-class counts, manual button, per-class enable toggles reading `triage.*` |

Crash/manual keep their U5 semantics; nothing else in the current tree writes
capture artifacts outside `games/<title>/triage/` after MIGRATION Phase 4.

---
*[REV3 2026-09-23 D9] Companions updated in the same revision: ARCHITECTURE §3
(tree: `engine/core/triage/`, `tools/triage/`, `games/<t>/triage/`) / §4.8
(triage Config section) / §12 (where-is-X row) / §18; MIGRATION §1.4 triage
rows + Phase 4 gate addendum; EQUIVALENCE §2/§5 (bundles as harness inputs);
README; docs/bugs/TRIAGE.md rule zero.*

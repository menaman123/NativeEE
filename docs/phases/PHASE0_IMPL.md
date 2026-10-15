# PHASE0_IMPL — verification harness (implementation record)

Session: 2026-09-23/24. Implements MIGRATION_PLAN Phase 0 / EQUIVALENCE_PLAN
§1–§5 on the OLD tree (`$PS2R_OLD_ROOT`), scripts
in [`verify/`](../../verify/). Everything in-binary is DEFAULT-OFF behind one
switch (`PS2_VERIFY_OUT=<leg dir>`); hatch-off behaviour-neutrality is proven
below.

## 1. Design (as built)

```
host_app (old tree)                          NativeEE/verify/
┌──────────────────────────────────┐         ┌───────────────────────────┐
│ graphics_thread.cpp present seam │ frames  │ run_leg.py  (bounded,     │
│  └ verify::OnPresent ────────────┼─.fhash─▶│   headless, cleaned env,  │
│ gs_hle.cpp SyncV boundary        │ audio   │   profile applied)        │
│  └ verify::OnEeBoundary ─────────┼─.ahash─▶│ diff_legs.py (meta gate → │
│     (audio pump / mem ckpt /     │ mem     │   first divergent frame / │
│      state drops)                ├─.mhash─▶│   window / checkpoint)    │
│ pad_input.cpp serve seam         │ input   │ selftest.py (ADMISSIBLE / │
│  └ verify::OnPadServe ───────────┼─.padlog▶│   NONDET per leg, ×reps)  │
│ run.meta (build/GL/env/frames)   │         │ profile_deterministic.py  │
└──────────────────────────────────┘         └───────────────────────────┘
```

- **Frame lane**: per-present FNV-1a64 of the scanout FBO read by
  `GrabDisplayRGBA` at the present seam (FBO 0 bound, after DrawScanout,
  before GUI/BB-dump/swap — the artdet tap point), i.e. the pre-upscale,
  pre-interp presented scanout. Hash = `u32 nw, u32 nh` LE header + RGB
  top-down. **Downsample rule (stated per §1.2)**: at `PS2_GS_SCALE=S>1`
  native pixel `(x,y) := physical texel (x·S, y·S)` (top-left of each S×S
  block); scale 1 — the profile value — is the §1.1 bit-identity plane and
  hashes every presented pixel. One `vf,hash` line per present (hash 0 =
  present with no readable scanout yet, keeps streams index-aligned).
- **Audio lane**: `sound::MixInto` pumped 800 stereo frames/vsync ON the EE
  frame boundary (both sites: `hle_sceGsSyncV` and its `hle_WaitForVblank`
  mirror) under `PS2_SND_TICKCLOCK=1` (SDL device suppressed);
  FNV-1a64 per 4800-frame (100 ms @48 kHz) window over the raw float bytes;
  partial tail window folds its frame count.
- **Memory lane**: every 60 frame boundaries (internal counter across both
  boundary sites), FNV-1a64 over main RAM (32 MB)
  + scratchpad (16 KB) + HLE meta arena (2 MB, guest-visible allocator
  state) — read directly at the quiescent frame edge (all EE fibers parked;
  cheaper than the savestate section-CRC path, which needs the full save
  quiesce).
- **run.meta**: build id (per-link `g_build_id`), `glGetString(GL_RENDERER)`
  captured at first present, tree/profile passthrough, sorted `PS2_*`
  fingerprint + FNV hash (excluding `PS2_VERIFY_*`/`PS2_PAD_SCRIPT_FILE`),
  presents, wall ms, finalized flag. Rewritten every 64 presents + at exit
  (bench `std::_Exit` rider next to `bench::WriteReportAtExit`).
- **Input recorder** (`PS2_VERIFY_RECORD_INPUT=1`): taps `PadInput_Update`
  AFTER wire inversion + `PS2_PAD_SCRIPT` overlay (exactly the served state),
  writes `input.padlog` as PS2_PAD_SCRIPT-grammar range tokens (one per
  line, `#` header). Round-trip proven: a scripted leg
  (`40400-40460:CROSS,40470-40520:RX=200,40410-40430:SELECT`) recorded back
  byte-equal tokens. Replay: new `PS2_PAD_SCRIPT_FILE=<padlog>` loader
  (comment-skipping, comma-joined, unchanged parser); parser token set
  completed to the full wire word (SELECT/L3/R3/UP/RIGHT/DOWN/LEFT — the
  [S30-SLIDE] extension precedent). This recorder is the TRIAGE_CAPTURE §4.5
  InputLog seed (one implementation, rehomed at Phase 4).
- **Headless** (`PS2_VERIFY_HEADLESS=1`): the boot-hidden SDL window simply
  never gets `SDL_ShowWindow`; GL renders to offscreen FBOs regardless
  (gs_replay precedent).
- **State drops** (`PS2_VERIFY_STATE_EVERY=K`): `savestate::RequestSaveNamed`
  into the leg dir every K boundaries (verified: `state_sv39840.ps2st` etc.).

## 2. Files added / touched in the OLD tree

New (one class per file, ARCH §13; `host_app/verify/`):

| File | Lines | Content |
|---|---|---|
| verify/verify_env.h/.cpp | 79+63 | env latches (Armed/RecordInputArmed inline call-site guards), Fnv1a64, leg paths |
| verify/frame_hasher.h/.cpp | 39+55 | FrameHasher (pure; native-scale downsample rule) |
| verify/audio_hasher.h/.cpp | 45+44 | AudioHasher (pure; 100 ms windows) |
| verify/memory_checkpointer.h/.cpp | 31+28 | MemoryCheckpointer (RAM+SPAD+meta-arena FNV) |
| verify/input_recorder.h/.cpp | 57+118 | InputRecorder (pure; grammar range emission) |
| verify/run_meta.h/.cpp | 46+97 | RunMeta (provenance stamp) |
| verify/verify_present.h/.cpp | 19+59 | present-seam hook (frames.fhash) |
| verify/verify_boundary.h/.cpp | 28+97 | SyncV hook (audio pump, mem ckpt, drops, finalize) |
| verify/verify_pad.h/.cpp | 22+77 | pad-seam hook (input.padlog writer) |
| tests/test_verify_units.cpp | 162 | ctest `VerifyHarnessUnits` (FNV vectors, downsample rule, audio windows, recorder ranges) |

Touched (all default-off; hatch-off = latched-bool early-outs):

| File | Change |
|---|---|
| graphics_thread.cpp | `if (verify::Armed()) verify::OnPresent(...)` at the present seam (+2 includes) |
| gs_hle.cpp, sif_hle.cpp | `verify::OnEeBoundary()` + `iop_stubs::PumpHeartbeatAtBoundary()` at BOTH frame-boundary sites (sceGsSyncV + the WaitForVblank mirror; +includes) |
| pad_input.cpp | recorder tap; `PS2_PAD_SCRIPT_FILE` loader; 7 new grammar tokens |
| sound/sound_core.h/.cpp | `sound::TickClocked()` latch (PS2_SND_TICKCLOCK) |
| sound/mixer.cpp | `MixerStart` suppressed under TickClocked |
| iop_stubs.h/.cpp | `PS2_FIXC_TICKCLOCK` (always-due scheduling + `PumpDeferredEvtsTickClock`), `PS2_HB_BOUNDARY` (boundary-fired heartbeat, kept out of profile v1) |
| ps2_vblank.cpp | hold-to-edge FIX-C delivery before each tick advance |
| syscalls.cpp | include-only residue of the reverted syscall-site pump (behaviour-neutral; kept to avoid a re-link after the acceptance stamps) |
| main.cpp | `SDL_ShowWindow` gated on headless; `verify::FinalizeForExit()` on the exit path |
| host_app/CMakeLists.txt, tests/CMakeLists.txt | source/test registration; `test_pad_buffer` gains the recorder TUs |

## 3. Deterministic profile

See [`verify/PROFILE.md`](../../verify/PROFILE.md) — every var with the
clock it removes, the two NEW Phase-0 pins (`PS2_SND_TICKCLOCK`,
`PS2_FIXC_TICKCLOCK`), the `PS2_HB_BOUNDARY` candidate (measured no effect,
default-off), and the A4 §5.1 nine-clock status table.

## 4. Admissibility self-test (§4: each leg twice; identical ⇒ ADMISSIBLE)

Battery: coldboot 1500 vf + 11 anchors × 600 vf, reps=2, both binaries
(final build ids `2026-09-24 00:52:35` legacy / `00:52:54` v2; an earlier
full pass on the pre-mirror binaries produced the same verdicts with first
divergences within a few frames of these).
Anchor notes: `ps2state_slot7_f36230` and
`ps2state_load_game_select_save_issue` do NOT exist in `save_states/`
(referenced by older docs; the on-disk battery substitutes
`ps2state_slot0_f57292`, `ps2state_slot0_f59991` and `artdet_f37152` as the
"+2 more distinct scenes").

### Legacy binary (`build/host_app/host_app`)

| Leg | Verdict | First divergence (rep1 vs rep2) |
|---|---|---|
| coldboot (1500 vf) | NONDET | frame line 1, vf 1 |
| ps2state_slot0_f40391 | NONDET | frame line 3, vf 40393 |
| ps2state_slot0_f39807 | NONDET | frame line 3, vf 39809 |
| ps2state_slot0_f44111 | NONDET | frame line 3, vf 44113 |
| ps2state_slot0_f46203_red_crystal | NONDET | frame line 3, vf 46205 |
| ps2state_slot0_f46949_yellow_crystal | NONDET | frame line 4, vf 46952 |
| slowspot_f40298_s1789693059 | NONDET | frame line 3, vf 40306 |
| slowspot_f37092_s1789428268 | NONDET | frame line 3, vf 37098 |
| ps2state_slot0_f36935 | NONDET | frame line 3, vf 36937 |
| ps2state_slot0_f57292 | NONDET | frame line 4, vf 57295 |
| ps2state_slot0_f59991 | NONDET | frame line 7, vf 59997 |
| artdet_f37152 | NONDET | frame line 7, vf 37162 |

**0/12 ADMISSIBLE.** Audio window 0 and the first mem checkpoint diverge in
every leg alongside the frames — one shared source (§5.4), not per-lane
noise. Legacy legs: **0 FATAL** across all 24 runs.

### v2 binary (`build-v2/host_app/host_app`)

| Leg | Verdict | First divergence / error |
|---|---|---|
| coldboot (1500 vf) | NONDET | frame line 28, vf 88 |
| ps2state_slot0_f40391 | NONDET | frame line 3, vf 40393 |
| ps2state_slot0_f39807 | NONDET | frame line 3, vf 39809 |
| ps2state_slot0_f44111 | NONDET | frame line 58, vf 44168 |
| ps2state_slot0_f46203_red_crystal | NONDET | AUDIO window 3 first (frames held longer) |
| ps2state_slot0_f46949_yellow_crystal | NONDET | frame line 3, vf 46951 |
| slowspot_f40298_s1789693059 | NONDET | frame line 4, vf 40307 |
| slowspot_f37092_s1789428268 | NONDET | frame line 75, vf 37170 |
| ps2state_slot0_f36935 | NONDET | frame line 3, vf 36937 |
| ps2state_slot0_f57292 | ERROR | rep1 hit the KNOWN v2 open defect P4FIX-F1: `[FATAL-NOJUMP] could-not-jump pc=0x0 vf=57294` (callback slot reads 0) — the MIGRATION Phase-2 root-fix item; not a harness fault; REPRODUCED in both acceptance passes |
| ps2state_slot0_f59991 | ERROR | rep1 wedged (360 s bound; last ring line `EE BOOT_SM_EDGE` — same "v2 loses a race" timing family, Phase-2 backlog); REPRODUCED in both acceptance passes |
| artdet_f37152 | NONDET | frame line 3, vf 37158 |

**0/12 ADMISSIBLE.** The harness surfacing the two v2-correlated open
defects on plain anchor resumes is working-as-designed evidence for the
Phase-2 gate's repro-leg requirement.

### Cross-tree leg (build vs build-v2, f46203_red_crystal, 600 vf)

`diff_legs --allow-build-diff`: **DIVERGENT** at the first content present
(vf 46206, line 4); audio window 0 and the first mem checkpoint differ.
Expected: v2-vs-legacy engine equivalence is the known 99.6%-T1 story
(MIGRATION Phase 2's job), and within-binary NONDET (§5.4) dominates any
cross-binary comparison today — this leg documents the mechanism works
(meta gate passes with `--allow-build-diff`, first divergence localized),
not an equivalence claim.

## 5. Nondeterminism findings (escalation per §5)

1. **SDL audio device clock** (game-visible NAX/ENVX/ENDX readbacks + ring
   pacing advanced by the device callback) → **PINNED**:
   `PS2_SND_TICKCLOCK=1` — device never opens; the SyncV boundary pumps the
   mixer 800 frames/vsync.
2. **FIX-C deferred completions** on `steady_clock` due-times delivered by
   a wall event thread (TIMEDOMAIN #7/#9) → **PINNED**:
   `PS2_FIXC_TICKCLOCK=1` — always-due queue, delivered hold-to-edge on the
   tick generator (everything scheduled during tick k visible by edge k+1);
   poll-retire unchanged. First attempt (delivery at EE syscall/SyncV pump
   points only) DEADLOCKED cold boot — the EE spins in a synchronous RPC
   wait while the IOP worker waits on the CD callback; ticker delivery
   breaks the cycle. Recorded as a design lesson.
3. **Heartbeat thread position** (tick-cadenced but host-scheduled) →
   pin candidate built (`PS2_HB_BOUNDARY=1`, boundary-fired heartbeat);
   measured: does NOT shift the first divergence; kept default-off.
4. **RESIDUAL — free-running IOP guest worker threads** (STREAM.IRX
   `Thread 1` entry `0x64f4` + RPC workers): guest-visible progress is
   host-scheduler-paced. Evidence: first runtime-log divergence between two
   profiled legs is the `[IOP-THREAD] Thread 1 started/calling entry_func`
   lines re-ordering against the EE loader (~line 545); cold boot carries a
   persistent 1-frame phase shift from vf≈69; anchors diverge ~3 presents
   post-load (e.g. f40391 → first divergent vf 40394); `PS2_SOUND=0`
   changes nothing. **Documented runtime nondeterminism → Phase 1+**
   (tick-quantized IOP worker scheduling / hold-to-edge SIF delivery; the
   A5 §7.2 live-nondet family). This single source dominates every NONDET
   verdict in §4.

## 6. Behaviour-neutrality proof (hatch off)

All with `PS2_VERIFY_OUT` unset (the shipped play configuration —
`./build/host_app/host_app`):

| Check | Result |
|---|---|
| GS corpus (19 captures) | **19/19 `.gsrep` byte-identical** to the frozen `gs_corpus/baseline/` (replayed with the rebuilt `build/tests/gs_replay`) |
| ctest `build/` | **412/412 passed (100%)**, 1 pre-existing skip (`Vu1Hle_vu1_case_w14bkstp_mat`) — was 411 + the new `VerifyHarnessUnits` |
| ctest `build-v2/` | **408/408 passed (100%)** — was 407 + `VerifyHarnessUnits` |
| ctest `build-v2-nog/` | **408/408 passed (100%)** — was 407 + `VerifyHarnessUnits`. (One intermediate run under full machine load flaked `VifReplayMtvuEquivalenceExecOnFast` — the in-process MTVU worker equivalence selftest, no touched TU linked; 3/3 pass in isolation and 408/408 on the unloaded rerun.) |
| PageInvariance | Passed (in the 412 set, 0.84 s) |
| coldboot_gate (user env, hatch off) | **PASS** twice — `bright by f100 (<= f900), 0 FATAL` (max luma 241.55 first pass / 194.41 final-binary pass) |
| Anchor battery FATALs (legacy) | **0 FATAL** across all 24 legacy leg runs (final acceptance pass) |
| fps 300f cold bench, user env, hatch OFF vs ON | pair 1: **43.69 / 40.17 fps_mean** (p50 59.26 / 58.13); pair 2 (final binary, idle machine): **39.63 / 39.66 fps_mean** (p50 56.89 / 58.46). Hatch-ON cost is within cold-boot run-to-run variance (≤8% worst pair, ≈0% best pair; p50 within 2% both pairs) — modest, verify-runs-only |

Hatch-off hot-path shape: one inline latched-bool branch per present
(`verify::Armed()`), one per pad pump (`RecordInputArmed()`), one function
call per SyncV frame returning on two latched bools, and one latched bool
in each of MixerStart / FIX-C schedule / heartbeat start / ticker
iteration — the repo's established artdet/bench "single latched-bool
early-out" idiom. No file is opened, no hash computed, no allocation made
with the hatch off.

## 7. Gate verdict

MIGRATION Phase-0 proof gate = "Harness self-test: OLD vs OLD twice on all
anchors ⇒ identical hash streams; corpus sweep green; black-boot class
caught by construction."

| Gate component | Verdict |
|---|---|
| Harness built (emitters + recorder + headless + profile + scripts) | **DELIVERED** — all EQUIVALENCE §2 required-new pieces exist and are exercised end-to-end |
| Corpus sweep green | **PASS** (19/19 byte-identical) |
| Black-boot class caught by construction | **PASS** — coldboot pixel gate wired into the battery (`tools/coldboot_gate.py` PASS quoted above); every leg carries per-present pixel hashes, so an all-black run can never pass silently again |
| Behaviour-neutrality of the harness | **PASS** (section 6) |
| Self-test: identical hash streams on all anchors | **FAIL — 0/24 legs ADMISSIBLE** (section 4) |

**Overall: Phase 0 gate NOT PASSED — harness delivered and proven; the
admissibility half is blocked by ONE root-caused runtime nondeterminism
(free-running IOP guest worker threads, §5.4), which is a property of the
runtime being verified, not of the harness.** The escalation contract was
exercised as designed: two profile gaps found during self-test were fixed
in-tree as default-off pins (`PS2_SND_TICKCLOCK`, `PS2_FIXC_TICKCLOCK`,
plus the `PS2_HB_BOUNDARY` candidate), and the residual is documented with
evidence and a fix class for Phase 1+. Consequence for the plan: the
"Phase 0 green baseline" entry criterion of Phase 1 is NOT yet met; the
IOP-worker quantization fix must land (as a pre-Phase-1 runtime work item
or the first Phase-1 deliverable) and this battery re-run before any purge
wave relies on two-binaries×one-profile adjudication. The harness, corpus
gate, ctest and pixel gates are usable TODAY for regression coverage of
that very fix.

## 8. [IOPK 2026-09-24] Cooperative IOP kernel — §5.4 residual FIXED; battery re-run

The §5.4 residual (free-running IOP guest worker threads) is fixed at the
root in the OLD tree: the OpenGOAL-shaped cooperative IOP kernel
(`host_app/iop_kernel/` — IopKernel/IopCoroutine/IopHostThread/IopClock/
SifRpcBridge/IopModuleTable, one class per file; ARCHITECTURE §3 + §7.1;
REFERENCE_ARCHITECTURES §2.5). Hatch `PS2_IOP_KERNEL` default ON (=0 =
legacy hosting verbatim) [R1B: shipped default OFF, BUG-026 — the realtime
policy's boot stall, §11.7/§12.2; the profile pins `=1`]; dispatch policy `PS2_IOP_DISPATCH` realtime
(play) / frame (this profile — now in `profile_deterministic.py`).

Characterization (Part A.1, `PS2_IOPSCHED_DIAG=1`, f46203 300 vf ×2, hatch
OFF): the CD-worker sema pass (`signal_sema 0` / `wait_sema 0` tid 2)
lands at vf 46203 (rep1) vs 46204 (rep2); frames fork at vf 46205, audio
window 0, mem ckpt 0 — the §5.4 signature, quoted at the event level.
Hatch ON + frame policy: the `[IOPSCHED]` stream is bit-identical between
reps INCLUDING virtual-clock stamps — the IOP timeline itself is now a
pure function of the EE frame stream (kernel unit test
`IopKernelUnits` proves the property in isolation: two FRAME-clock runs
of a scripted workload produce identical effect sequences).

Peeling the IOP source exposed two more wall clocks, pinned as profile
gates found via this battery's escalation contract:
`PS2_RTC_FIXED` (sceScfGetLocalTimefromRTC wall seed in EE RAM) and
`PS2_TICK_DETERMINISTIC` (the freerun tick generator's 16.67 ms
timeout-floor tick source, ~6.4 ±1 wall ticks per EE frame feeding the T0
master clock — BUG-014 family; a NoteTickDemand channel serves the FMV
pace's non-consuming tick waits one tick per wait entry). With all three
pins, a paired f46203 300-vf leg run reached **frame lane 300/300 and
audio lane 50/50 bit-identical** (mem lane residual below) — measured at
change-time; the final binary adds the boot park-loop pump whose
interaction is reported honestly in the table.

### Battery (same contract as §4: coldboot 1500 + 11 anchors × 600, reps=2)

Legacy binary (`build/`), deterministic profile incl. `PS2_IOP_DISPATCH=frame`:

| Leg | Verdict | First divergence (§4 baseline → now) |
|---|---|---|
| coldboot (1500 vf) | ERROR | rep killed at the 720 s bound — FRAME-policy boot is slower than the run_leg timeout model (boot completes at 300 vf in ~5 min; not a wedge — the 21-present wedge found mid-session was fixed by the park-loop pump) |
| f40391 | NONDET | line 3 → line 3 (vf 40393) |
| f39807 | NONDET | line 3 → **line 247** (vf 40053) |
| f44111 | NONDET | line 3 → line 3 |
| f46203_red_crystal | NONDET | line 3 → **line 426** (vf 46628) |
| f46949_yellow_crystal | NONDET | line 4 → line 3 |
| slowspot_f40298 | NONDET | line 3 → **line 38** |
| slowspot_f37092 | NONDET | line 3 → **line 80** |
| f36935 | NONDET | line 3 → **line 260** |
| f57292 | NONDET | line 4 → **line 17** |
| f59991 | NONDET | line 7 → **line 55** |
| artdet_f37152 | NONDET | line 7 → line 3 |

v2 binary (`build-v2/`): coldboot ERROR (same timeout artifact);
f57292 + f59991 ERROR — the two KNOWN v2 open defects (§4: P4FIX-F1 /
BOOT_SM_EDGE), reproduced as before; the 9 other anchors NONDET with the
same deepened-divergence pattern (f39807 line 33, f44111 line 60,
f36935 line 109, slowspot_f37092 line 230).

**Gate verdict: NOT PASSED (0/24 ADMISSIBLE)** — but the §5.4 IOP source
is dead (its event stream is bit-identical), and the battery now measures
the NEXT family: the mem lane diverges at checkpoint 0 in every leg via a
single float in one parked EE thread's saved context (THRD @0x9E8,
frame-fraction-shaped, ~0.267 vs ~1.4e-16) with LSB echoes in VU0D/SPAD,
plus a boot-window interleave between EE-side loader glue and kernel
dispatches pumped from wall-count-coupled spin cadences (the maintenance
thread's wall ISO-read delivery races the spin's exit). Escalation state
and next steps: docs/bugs/BUG-026. The harness, corpus gate, ctest and
pixel gates remain green and usable (behaviour-neutrality of the kernel
itself proven under the play policy — §6-equivalent checks re-run this
session).

OLD-baseline hash streams frozen: `verify/baseline/<leg>/`
(frames.fhash/audio.ahash/mem.mhash/run.meta, rep1 of each of the 12
legacy legs, this battery).

## 9. [MAINT 2026-09-24] Round 2 — sources named, harness model fixed, battery re-run

### 9.1 Harness model (the coldboot ERROR was the timeout, as suspected)

`run_leg.py` hard cap now scales with the leg: `max(600, 300 + frames*2)` s
(FRAME-policy boot measured ~1 s/vf), and `tools/run_bounded.sh` gained an
opt-in NO-PROGRESS watchdog (`RB_PROGRESS_FILE`/`RB_STALL_S`, default 300 s;
frames.fhash flushes per present so it is a live progress signal) — wedges
die fast, honest slow boots run to completion. Proof: coldboot 1500 vf
completed in 86.5 s wall (1405 presents, run.meta finalized=1); the stall
killer was exercised for real by the PS2_FRAME_LOCKSTEP wedge legs
(killed at exactly 300 s of zero fhash growth).

### 9.2 (a) THRD @0x9E8 — writer FUN and source clock (found)

The float is tid 1's saved `fpuRegs.fpr[f11]` (main thread parked inside
sceGsSyncV at pc 0x1A018C): the game's per-frame delta-SECONDS. Writer
chain, named live by the new `[DETW]` write watch (`PS2_DETWATCH`,
memory.cpp — logs every overlapping store with the EE dispatch call
stack): **`FUN_001b74a0`** (game clock feeder, called from the main loop
`FUN_00100d78`, ra=0x100e18) computes it from the T0-reconstructed master
clock and stores it at **`0x3098C4`** (gp-0x7c2c); `FUN_001b6b40`
accumulates it into per-object timers (the `0x3723AC` family = the RAM0
first-diffs; LSB echoes ride transforms into VU0D/SPAD/VRAM).

Source clocks feeding it (two found, both pinned):

1. **The LIVE T0 read path's RAW-WALL interpolation** (memory.cpp
   `0x10000000`): COUNT low bits = wall-ns since the last SyncV.
   `PS2_T0_SUBTICK=0` never covered this path — that gate exists only in
   the default-OFF `PS2_EE_TIM0` timers (a §5.1 nine-clock COVERAGE GAP).
   Pin: **`PS2_T0_TICKPACED=1`** zeroes the returned sub-frame term
   (busy-wait carry promotion untouched).
2. **Wall-raced tick binning**: deliveries from the ~1 kHz WALL
   maintenance loop fire cdvd-classified ticks and retime the per-frame
   tick binning (paired [DETW] traces: delta sequence [3,2,1] vs [2,3,1]
   ticks). Pin: **`PS2_MAINT_EEPACED=1`** (below).

### 9.3 (b) loader-glue spin cadence — mechanism (confirmed)

The 0x2b4738 loader-SM hook pumps the IOP kernel on SPIN-ITERATION
cadences (engage after 4096 same-vf calls, then every 64th call); the
iteration count to spin-exit depends on WHEN the deferred-ISO read
delivers. Delivery was wall-timed twice over: `ProcessDeferredIsoReads`
ran on the CPU maintenance loop at ~1 kHz WALL (each pass decrements the
3-pass `delay_ticks` readiness floor → wall-time elapsing), racing the
EE's own on-demand poll passes. The same loop also ran SIF RPC
end-callbacks (recompiled `FUN_001e6968` writing guest RAM at wall
points) and suspended-DMA resume. Additionally the SDL main loop stamped
the pad slot phase/seq bytes into guest RAM at wall cadence
(`WriteSlotPhaseBytes` ← `PadInput_Update`; measured slot seq 386 vs 397
by mem checkpoint 0 — caught by [DETW] host-backtrace mode).

Pin: **`PS2_MAINT_EEPACED=1`** — the maintenance loop skips all three
guest-visible duties; they run on the guest run token at both EE
frame-boundary sites and both boot/load park-loop pump sites (next to the
IOPK pumps), and the pad stamp moves to the boundary. Delivery points,
cdvd-fired ticks and end-callback execution become pure functions of the
EE stream.

### 9.4 Pins added this round (all deterministic-PROFILE-only, default OFF)

| Var | Ledger | What it removes |
|---|---|---|
| `PS2_MAINT_EEPACED` | SETTING | maintenance-loop wall cadence behind ISO delivery / RPC callbacks / DMA resume + SDL-loop pad stamps |
| `PS2_T0_TICKPACED` | SETTING | the live T0 path's raw-wall COUNT interpolation |
| `PS2_DETWATCH` | DIAG | (diagnostic, not a pin) the writer-naming write watch |
| `PS2_FRAME_LOCKSTEP` | TEST | (hatch, NOT in profile — wedges; see 9.6) |

Measured effect (final binary): **f40391 ADMISSIBLE** — 600 vf
bit-identical on frames + audio + mem (the first fully-identical leg the
battery has ever produced); first divergences deepened across the board
(f44111 3→531, f59991 7→117, f57292 3→48, slowspot_f37092 3→45,
artdet 3→29 — §9.5 table); audio lanes reach full-window identity on a
large fraction of sampled pairs (was: forked at window 0 pre-round-2).
Full 24/24 bit-identity blocked by the named §9.6 interleave sources.

### 9.5 Battery (contract of §4: coldboot 1500 + 11 anchors × 600, reps=2)

Final binary (order-inversion review fix in — the pin arm no longer swaps
the RPC queue on the maintenance thread). Legacy binary (`build/`) — **all
12 legs RUN TO COMPLETION (0 ERROR), first ADMISSIBLE leg in the
project's history**:

| Leg | Verdict | First divergence |
|---|---|---|
| coldboot (1500 vf) | NONDET | frames line 27 (vf 87 — identical through vf 86; the boot-window interleave family) |
| f40391 | **ADMISSIBLE** | — (600 vf bit-identical on frames + audio + mem) |
| f39807 | NONDET | frames line 3 |
| f44111 | NONDET | frames line 531 (vf 44641 — was line 3 in §8) |
| f46203_red_crystal | NONDET | frames line 3 (stochastic: 300-vf samples of the same binary also produced fully-identical pairs) |
| f46949_yellow_crystal | NONDET | frames line 77 (was line 3/4) |
| slowspot_f40298 | NONDET | frames line 38 |
| slowspot_f37092 | NONDET | frames line 45 (was line 3) |
| f36935 | NONDET | frames line 3 |
| f57292 | NONDET | frames line 48 (was line 3) |
| f59991 | NONDET | frames line 117 (was line 7) |
| artdet_f37152 | NONDET | frames line 29 (was line 3) |

v2 binary (`build-v2/`): coldboot NONDET line 27 (completes now — was
ERROR); f57292 + f59991 **ERROR** = the two KNOWN v2 open defects
(§4 P4FIX-F1 / BOOT_SM_EDGE, `[FATAL-NOJUMP] pc=0x0` reproduced as-is,
also reproduced under the play env with 5 FATALs each); the 9 other
anchors NONDET at line 3.

**1/24 ADMISSIBLE (build/ 1/12, v2 0/12)** — zero non-defect ERRORs (the
§8 coldboot ERROR was the harness timeout model, fixed and proven), the
§8 mem-lane mystery is root-caused with its writers named and two of its
clocks pinned, and one full 600-vf leg is now bit-identical on all three
lanes.

### 9.6 Residual NONDET — named sources, fix items (Phase 1)

1. **EE↔gfx drain-depth race** (`gs_hle` boundary park,
   `MAX_FRAME_LAG=1` against wall-paced `g_vsync_frame`): whether a
   boundary parks — and the frame count every frame-keyed consumer reads
   (PS2_PAD_SCRIPT windows, the game's Δsyncv slip cadence and hence the
   delta-seconds ORDER) — follows the graphics thread's wall drain speed.
   `PS2_FRAME_LOCKSTEP` hatch added (rendezvous on the corrected R170
   push/processed pair) but WEDGES ~1 present in: present-skip/coalesce
   paths leave pushed VSyncs unmatched by arg1==1 pops even after
   rebasing the CNTR-restored counter slack. Fix item: finish the
   gfx-side pop accounting, then pin.
2. **Free-running guest EE threads** (`ps2_threading`:
   std::thread-per-guest-thread, NO run token): tid 1/tid 2/RPC-callback
   contexts genuinely interleave on host cores. Fix item: the EE-side
   dual of the IOPK cooperative kernel (ARCHITECTURE §3).
3. **Ticker-thread tick-body asynchrony**: the tick body (count++,
   sema 1/3 signals, INTC raises) executes on the vblank worker thread
   concurrently with the running EE even when its release trigger is
   EE-derived — the landing point in the EE stream is wall-latency-shaped.
   Fix item: fire tick bodies synchronously at EE park/boundary sites
   under the frame policy.

### 9.7 Behaviour-neutrality (everything default-ON; user env
`PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60 PS2_MTVU=1 PS2_GL_FAST=1
PS2_GL_FAST2=1 PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1`)

All new pins are env-gated default-OFF (normal play executes the legacy
paths verbatim: maintenance loop restructure preserves the exact legacy
duty bodies; [DETW] is one relaxed load per slow-path write, fast path
untouched; the T0/lockstep hatches are dead branches unless armed).

| Check | Result |
|---|---|
| coldboot_gate `build/` | **PASS** twice — bright by f100 (≤ f900), 0 FATAL (max luma 246.00 mid-session / **245.36 final binary**); FMV window bright (f425-f600 series quoted in log) |
| coldboot_gate `build-v2/` | **PASS** — bright by f100, max luma 243.01, 0 FATAL |
| Cold 900f, kernel ON vs OFF | fps_mean **36.18 vs 36.20** (Δ 0.06%); both 900 presents in ~25 s (load-time proxy equal); first bright f100 both. **[Record fix 2026-09-26 — measured under the 2026-09-24 pre-round-1A binary (s120 `build10`, 18:27), user env, and the "ON" arm did NOT run the kernel:** `/tmp/s120/neut_coldON.log` (and `neut_f40391_ON.log` / `neut_slow_ON.log`) carry the legacy-hosting marker `StartHardTimer … (tick-clocked)` and NO `[IOPK] kernel up` line, while the same day's profile legs (`/tmp/s120/battery2/legacy/*.log`, `PS2_IOP_KERNEL=1`) print `[IOPK] kernel up: policy=frame` + `(iop-kernel timer)`. The ON arm relied on the documented-but-false "default ON" (the shipped `Enabled()` default was already OFF), so this row is legacy-vs-legacy: it recorded neither a kernel-ON PASS nor the FAIL. Actual 2026-09-24 kernel-ON REALTIME cold boots exist in the IOPK session (`/tmp/s119/pb*.log`, `policy=realtime`, 900f): 5 of 17 reached `[FRAME 900]`, 12 stalled at `[FRAME 60]` after the boot-SM edge 0→1 at vf 61 — the §12.2 signature, pre-dating round 1A. See §12.4 (corrected) and BUG-026.]** |
| cuelag drift ON vs OFF | lag_cum **+12.99 s vs +13.01 s** at the same window (Δ 0.15%); audio **underruns 0 / 0** (24/24 SND-PERF lines each) |
| FMV frames+audio | present (coldboot_gate luma series through the FMV window; fmv_q/fmv_c streaming counters live; underruns 0) |
| fps 300f f40391 ON vs OFF | fps_mean 50.91 vs 53.75, **p50 60.03 vs 59.68** (cap-bound; p5 52.34 vs 48.77 — ON better) |
| fps 300f slowspot_f40298 ON vs OFF | fps_mean 53.70 vs 53.55, p50 59.98 vs 59.94, p5 52.85 vs 46.66 |
| Anchors 0 FATAL | `build/`: **0 FATAL** across all 11 anchors (user env, 300f). `build-v2/`: 0 FATAL on 9; f57292/f59991 = the two KNOWN defects (`FATAL-NOJUMP pc=0x0`), reproduced as-is |
| GS corpus | **19/19 byte-identical** on `build/` AND `build-v2/` (final full-tree builds, rebuilt gs_replay) |
| ctest `build/` | **413/413 passed (100%)** on the final full-tree build (one MTVU-equivalence flake seen only under full battery load mid-session, 3/3 isolated — the §6-documented load-flaky pair), incl. PageInvariance, IopKernelUnits, VerifyHarnessUnits all Passed |
| ctest `build-v2/` | **409/409 passed (100%)** (final full-tree build) |
| ctest `build-v2-nog/` | **409/409 passed (100%)** (final full-tree build; PageInvariance, IopKernelUnits, VerifyHarnessUnits quoted Passed) |

### 9.8 Gate verdict

**NOT PASSED (1/24 ADMISSIBLE — needs 24/24 build/ + all non-defect v2
legs)** — but materially advanced from §8's 0/24: the harness ERROR class
is eliminated (24/24 legs complete; the two v2 ERRORs are the KNOWN
P4FIX-F1/BOOT_SM_EDGE defects, not harness artifacts), the §8 mem-lane
residual is fully root-caused (writer FUN + both source clocks named and
pinned), the first-ever fully bit-identical 600-vf leg exists (f40391:
frames + audio + mem), and every remaining NONDET has a named source with
a concrete Phase-1 fix item (9.6, BUG-033). The Phase-1 entry criterion
still requires the EE-side interleave fixes (lockstep pop accounting +
guest-thread run token + tick-body sync) before the battery can
adjudicate purge waves. Baselines re-frozen from this battery's rep1
(now including a COMPLETE 1500-vf coldboot stream — the previous freeze
had none).

## 10. [P0-EETOK 2026-09-25] Incidents (moved from the OLD tree's stray root PHASE0_IMPL.md)

### 10.1 Drain loop incident (BUG-035)

* **What happened.** After the [P0-EETOK] run token landed,
  `tests/r139_threads/test_r139_threads` stopped exiting: a single thread
  spun at ~98% CPU inside `EeEventQueue::DrainOnToken`. A previous stage ran
  the test bare and hung for about an hour before anyone noticed.
* **Why.** `DrainQueuedEventsIfPending()` had no holder check, and
  `ApplyEeEvent` applied events through the public mutators, whose
  `tokRouteWake` re-queues any call made off-token. A non-holder drain
  therefore popped and re-enqueued the same event forever. Details, the
  invariant, and the mutator audit: `docs/bugs/BUG-035.md` (OLD tree: $PS2R_OLD_ROOT/docs/bugs/BUG-035.md; indexed in this project's docs/bugs/INDEX.md).
* **Fix (two-part invariant).** (1) Only the token holder may drain — the
  guard lives inside `DrainQueuedEventsIfPending()`, non-holder requests are
  counted and refused, events stay queued for the holder's next scheduling
  point. (2) `DrainOnToken` marks the thread as applying
  (`ApplyingOnThisThread()`), and `tokCallerMayMutate()` honours it, so an
  application can never re-route even if (1) is bypassed.
* **Modelling rule.** Threads that model running guest EE threads hold the
  token (`GuestEnter`/`GuestExit`); host-side signalers stay off-token and
  rely on a parked guest holder to drain (park poke -> `BlockEnd` ->
  post-acquire drain hook). Protocols that need synchronous mutator return
  codes (r139 T5's `KE_SEMA_OVF` backpressure) must be guest threads and
  yield via `rotateThreadReadyQueue`.
* **Process lessons.** Every command that runs a test or the game is wrapped
  (`tools/run_bounded.sh <secs> <log> -- <cmd>; echo exit=$?`, or
  `timeout` where available); never pipe test output through `tail`
  without checking `PIPESTATUS[0]`; ctest runs carry an outer bound too.
  Triage tip: a 98% CPU spin in `DrainOnToken` means a non-holder drained.
* **Evidence.** Pre-fix `exit=124` at a 30 s bound; post-fix
  `test_r139_threads` exit 0 in 5.07 s wall, `EeSchedUnits` gained
  `b035a/b/c` invariant tests (all PASS).

## 11. [TICKSYNC 2026-09-25] Round 1A — EE-synchronous ticks + lockstep present; 24/24 non-defect legs ADMISSIBLE-STABLE

Session r1a. Scope: BUG-033 items (1) and (3) — the two wall couplings the
run token (item 2, §10) exposed as the dominant remaining nondeterminism.
Everything in-binary is deterministic-PROFILE-only behind `PS2_TICK_SYNC`
(default OFF; `profile_deterministic.py` sets it); hatch-off neutrality is
proven in 11.7. Evidence directory: `/tmp/s125/` (attrib.out, il/, battery/,
play/, ctest_*.log, corpus_*.out).

### 11.1 Attribution (step 1) — quoted from the s124 paired diag (`/tmp/s124/il/on_r{1,2}.ees`)

Producer identification from source (`ps2_vblank.cpp` tick body,
`graphics_thread.cpp` VSync case): a ticker tick enqueues
`kind=0 a=1` (iSignalSema(1), routed) then the triple `kind=6 a=2 / a=11 /
a=3` (INTC VBON / TIM2 / VBOF) in that order; the graphics thread enqueues a
lone `kind=6 a=2` (VBON) after every processed VSync (present, hold or
coalesce-skip). `/tmp/s125/attrib.py` classifies every `kind=6` enqueue by
that grammar and records the GUEST-event index (non-`enq` line count) at
which it lands:

```
rep1: ticker triples=580 gfx VBON=301 guest-events=5627
rep2: ticker triples=581 gfx VBON=302 guest-events=5636
EE-stream position (guest-event index) of the k-th TICKER triple:
  tick   1: rep1 @   13  rep2 @   13
  tick   2: rep1 @   22  rep2 @   27   <-- differs
  tick   3: rep1 @   46  rep2 @   38   <-- differs
  tick   4: rep1 @   56  rep2 @   53   <-- differs
  ticker triples at a different guest-event position: 574 / 580
EE-stream position of the k-th GFX VBON enqueue:
  gfx   1: rep1 @   30  rep2 @   28   <-- differs
  gfx VBON enqueues at a different guest-event position: 300 / 301
```

Per-kind counts (`grep -c`): `a=11` (TIM2, ticker only) 583 vs 585 —
the tick COUNT itself differs between reps (the freerun generator coalesces
its consumption + vsync-retire triggers by wall timing); `a=2` 881 vs 883 =
583/585 ticker + 298 graphics-thread VBONs. First stream difference (diff
lines 28-30 vs 33-35): the ticker triple `enq kind=6 a=2/a=11/a=3` moved
across the guest events `ready tid=2 / run / sig`. Both threads confirmed:
**ticker** = every `a=11`/`a=3` and the `a=2` that heads a triple;
**graphics** = every lone `a=2`. Every one of their landing points is host
wall latency (574/580 and 300/301 moved), exactly BUG-033 (3) + (1).

### 11.2 Mechanism (step 2) — `PS2_TICK_SYNC=1`, files

New (one class per file, `host_app/ee_timing/`):

| File | Class | Content |
|---|---|---|
| ee_timing/tick_sync.h/.cpp (108+127) | `eetiming::TickSync` | latches `PS2_TICK_SYNC` / `LockstepOn()` (`PS2_TICK_SYNC` or `PS2_FRAME_LOCKSTEP`; both REFUSED with a stderr line when `PS2_MTVU` is on — the deferred MTVU boundary has no ticket); tick-body hook; `FireInline(cause)` (guest EE thread only), `NotePending`/`FirePending` (off-thread demands become `kDeferred` ticks at the next EE fixed point); per-cause counters; concurrent-body tripwire |
| ee_timing/frame_rendezvous.h/.cpp (81+84) | `eetiming::FrameRendezvous` | ticket barrier: EE `Issue()` → stamps `RenderJob::lockstep_ticket` → `WaitFor(ticket, running)`; gfx `Ack(ticket)` at BOTH exits of the VSync case; no wall valve (5 s stall report, 60 s abandon-with-message); `Wake()` on queue stop |
| ee_timing/boundary_sequencer.h/.cpp (94+75) | `eetiming::BoundarySequencer` | the fixed 8-step order below, `Run(fn,user)` stops on a step returning false (savestate LOAD unwind), `InFlight()` for the harness settle |
| tests/test_ee_timing.cpp (210) | ctest `EeTimingUnits` | 35 checks: one body per fire, off-thread demands deferred, ticket release/monotonic/shutdown, documented order, stop-on-unwind, in-flight bracket |

Touched (hatch-off = latched-bool early-outs; legacy bodies verbatim):

| File | Change |
|---|---|
| ps2_vblank.cpp | tick BODY extracted VERBATIM into `FireVblankTickBody()` (diff -w vs the loop body = the one documented `last_fired_tick` return); `SyncTickBody` (census class + cdvd_fired + CSR.VSINT) installed as the TickSync hook at static init; ticker thread exits at once under the hatch; `waitForNextVBlankTick` sync branch = fire one tick + deferred + consume, no park; `NoteTickDemand` / `NotifyCdvdCompletionDelivered` fire in-line on a guest thread, note-pending otherwise; injected CRTC presents get a ticket + rendezvous in sync mode |
| gs_hle.cpp | `Ps2TickSyncBoundary()` (the sequencer's step bodies) + `Ps2LockstepPresentRendezvous()`; ticket issued on the VSync push; the first (wedging) `PS2_FRAME_LOCKSTEP` counter-pair block DELETED, the hatch now routes to the ticket rendezvous; legacy MAX_FRAME_LAG block untouched |
| sif_hle.cpp | `hle_WaitForVblank` mirror: ticket on push; sync mode runs the same fixed sequence |
| graphics_thread.cpp | `Ack(vs_ticket)` + harness `OnVsyncCaseBegin/End` at the case entry and both exits; INTC VBON raise + CSR bit skipped under `TickSync::On()`; `\r[FRAME N]` progress print suppressed under `verify::Armed()` (it spliced onto diag lines); `FrameRendezvous::Wake()` at loop exit |
| mpeg_hle.cpp | FMV pace takes the tick-domain branch under the hatch (`PaceAuthoredOn() \|\| TickSync::On()`, `warp::Active() \|\| TickSync::On()`, demand loop) — pictures pace on in-line demand ticks + injected-present rendezvous instead of the gfx thread's wall present rate |
| render.h | `RenderJob::lockstep_ticket` (POD, 0 = none) |
| ee_sched/ee_run_token.h/.cpp | **BUG-036**: `BeginRelaunch()/EndRelaunch()` (loader self-handoff + held election), `GuestExit` acts only for the entering thread (`t_tid == tid`), `enter` diag logged after the acquire, `DiagPrintf` (one `write(2)` per diag line) |
| ee_sched/ee_event_queue.cpp, iop_stubs.cpp | diag lines via `DiagPrintf` (splice-free) |
| savestate.cpp | `BeginRelaunch()` before / `EndRelaunch()` after the resume-relaunch loop |
| verify/verify_present.h/.cpp, verify_boundary.cpp | end-of-run settle: `WaitPresentSeamIdle(3 s)` + wait for `BoundarySequencer::InFlight()==0` (5 s) before `Finish()` |
| host_app/CMakeLists.txt, tests/CMakeLists.txt | source + test registration |
| verify/profile_deterministic.py | `PS2_TICK_SYNC=1` |

### 11.3 The fixed boundary sequence (documented in `boundary_sequencer.h`)

Under `PS2_TICK_SYNC=1` both EE frame-boundary sites, AFTER pushing the
frame's VSync job (ticket stamped), execute on the boundary thread:

| # | Step | What lands here |
|---|---|---|
| 1 | present rendezvous | park (run token released) until the graphics thread has PROCESSED this boundary's VSync ticket. Replaces MAX_FRAME_LAG + the 100 ms wall valve. FIRST, so every frame-keyed consumer below reads a settled `g_vsync_frame` |
| 2 | tick advance | exactly ONE vblank tick in-line (FIX-C hold-to-edge delivery, `g_vblank_count++`, emu-domain carry, CRTC injector census, sema 1 / sema 3 signals — direct, a readied higher-priority thread preempts and runs HERE — INTC VBON/TIM2/VBOF raised directly, GS_CSR.VSINT), then any `kDeferred` ticks noted off-thread, then the `PS2_EE_TIM0` pump (hatch) |
| 3 | event-queue drain | explicit `DrainQueuedEventsIfPending()`; under the profile no foreign producer remains (0 `enq` lines in 1225) — the documented landing point for any straggler |
| 4 | maint duties | `PS2_HB_BOUNDARY` (hatch); `PS2_MAINT_EEPACED`: deferred-ISO delivery (a delivering pass fires its cdvd tick in-line right there), SIF RPC end-callbacks, suspended-DMA resume, pad slot stamp |
| 5 | IOP dispatch | IOP kernel FRAME policy `PumpAtBoundary()` |
| 6 | EE stream RPC | `ServiceEeStreamRpc` + `PumpAfterEeService` |
| 7 | savestate | probe / pending save / LOAD — a load unwinds the thread: sequence STOPS |
| 8 | verify | harness boundary hook (audio pump, mem checkpoint, state drops) |

Deviation from the orchestrator's suggested order (IOP dispatch → tick →
drain → rendezvous): the rendezvous is placed FIRST because the pad-script
window mask, savestate naming and the loader spin detector all read
`g_vsync_frame` inside the sequence — with the rendezvous last they would
read N-1 or N depending on gfx speed. The IOP dispatch keeps its legacy
position after the maintenance deliveries ("completions delivered at this
edge are visible to this edge's kernel dispatch", §9.3). Tick policy: ONE
tick per boundary (the profile's `PS2_TIME_EMU_REGIME=1` "perfect 60 Hz
PS2"; the legacy freerun generator fired ~2/frame, 583 vs 585 wall-shaped),
plus one per delivering deferred-ISO pass and one per FMV demand — every
tick body executes on a guest EE thread at a fixed point of its own
stream, and the graphics thread mutates NO guest-visible state.

Observed tick body ordering note: in this scene the game's tid 2 sleeps
(`SleepThread`, syscall 0x32) — it was never woken by sema 1 in either the
legacy or the sync stream (its only wakes under the token were the queue
POKES: wake, drain, re-park, zero guest code). The in-line `sig a=1 b=1`
per boundary therefore readies nobody; behaviour-equivalent to before.

### 11.4 Interleave result (step 3) — f46203 300 vf ×2, profile + both diags

`/tmp/s125/interleave_sync.sh` (streams truncated at the bench's
`requesting clean shutdown` line — the run's defined end; extraction
`grep -o '\[EESCHED\] .*'`, splice-proof after DiagPrintf):

| Binary | EESCHED | IOPSCHED | frames | audio | mem | wall |
|---|---|---|---|---|---|---|
| build/ (`sync3`, 19:48 binary) | **BIT-IDENTICAL, 1225 lines, diff = 0** | **BIT-IDENTICAL, 1088 lines, diff = 0** | identical 300/300 | identical 50/50 | identical 5/5 | 8.39 / 8.40 s |
| build-v2/ (`v2sync`) | **BIT-IDENTICAL, 1225 lines** | **BIT-IDENTICAL, 1088 lines** | identical 300 | identical 50 | identical 5 | 8.47 / 8.38 s |
| build/ final binary (`finalA`, `finalB`: 2 pairs = 4 runs) | **BIT-IDENTICAL, 1224 lines, one md5 across all 4 runs** | **BIT-IDENTICAL, 1088 lines** | identical 300 | identical 50 | identical 5 | 8.45-8.56 s |

0 `enq`/`drain` lines (the queue is never used); 299 `sig a=1 b=1` = one
tick per post-load boundary. Two intermediate defects were found and fixed
on the way to diff = 0 (each first measured, then removed):

1. **Savestate relaunch token race → BUG-036** (12 differing lines, all at
   the load instant vb=58626): the loading boundary thread relaunched its
   OWN tid while still holding the token — the new incarnation's
   `GuestEnter` found `owner_ == tid` and ran concurrently, the old
   incarnation's later `GuestExit` then handed the token to tid 2, and which
   resumed thread ran first followed pthread start order
   (`enter tid=1 own=1 / run tid=1 / enter tid=2 / exit tid=1 own=2` vs
   `enter tid=1 own=2 / enter tid=2 own=2 / run tid=2`). Fix:
   `BeginRelaunch()` (self hand-off, elections held) → `MakeReady` all →
   `EndRelaunch()` (one thbase election: `relaunch-end own=2`); `GuestExit`
   only for the entering thread. Guest-visible lanes were identical either
   way (tid 2 re-parks at once), so this was invisible to the battery and
   visible only to the stream contract.
2. **Diag-stream splicing** (`ev=block tid=2551 prio=10 seq=0 255 255`,
   `\r[FRAME 46440][EESCHED] ...`): unbuffered `fprintf(stderr)` writes a
   formatted line in pieces and the progress print has no newline. Fixed
   with `DiagPrintf` (one `write(2)` per line) and the harness-only
   progress-print suppression — an instrument defect, not a scheduling
   difference.
3. **Diag-line ORDER was host wakeup timing** (found on the final-binary
   pairs: a duplicate `ready tid=2 seq=5` printed by the woken waiter's
   idempotent re-ready in `BlockEnd` landed before or after the
   terminator's `block tid=1`): the stream contract is now "every line at
   the guest event, on the causing thread, under the token mutex" —
   block/preempt/rotate/exit print BEFORE the release (the successor's
   `run` can only follow), `ready` prints only from the call that creates
   the READY slot, `enter` after the acquire (`ee_run_token.h` header
   comment). Behaviour unchanged (diag placement only); 4 runs → one md5.

Bonus cross-tree leg (build vs build-v2, same profile, `--allow-build-diff`):
frames **identical 300/300**, EESCHED and IOPSCHED streams **identical**;
audio diverges only at the last window (49), mem at checkpoint 180 (4th of
5). With within-binary NONDET gone this is the first clean measurement of
the genuine v2-vs-legacy engine difference (Phase-2 material; §4 saw it as
"divergent at the first content present").

### 11.5 Battery (step 4) — coldboot 1500 + 11 anchors × 600, both binaries

`selftest.py` deterministic profile (now incl. `PS2_TICK_SYNC=1`),
`/tmp/s125/battery/`. First pass reps=2, then reps=3 on every leg identical
at 2 → ADMISSIBLE-STABLE.

Legacy binary `build/host_app/host_app` (two full 3-rep passes: build id
2026-09-25 19:48:33, and the FINAL binary 21:25:53 after the MTVU-refusal
latch and the diag-order fixes — every leg's frames/audio/mem streams are
byte-equal ACROSS the two builds too, i.e. 6 identical runs per leg):

| Leg | reps=2 | reps=3 | Verdict |
|---|---|---|---|
| coldboot (1500 vf, 1494 presents, 162 audio windows, 17 mem ckpts) | identical | identical | **ADMISSIBLE-STABLE** |
| f40391 | identical | identical | **ADMISSIBLE-STABLE** |
| f39807 | identical | identical | **ADMISSIBLE-STABLE** |
| f44111 | identical | identical | **ADMISSIBLE-STABLE** |
| f46203_red_crystal | identical | identical | **ADMISSIBLE-STABLE** |
| f46949_yellow_crystal | identical | identical | **ADMISSIBLE-STABLE** |
| slowspot_f40298 | identical | identical | **ADMISSIBLE-STABLE** |
| slowspot_f37092 | identical | identical | **ADMISSIBLE-STABLE** |
| f36935 | identical | identical | **ADMISSIBLE-STABLE** |
| f57292 | identical | identical | **ADMISSIBLE-STABLE** |
| f59991 | identical | identical | **ADMISSIBLE-STABLE** |
| artdet_f37152 | identical | identical | **ADMISSIBLE-STABLE** |

**12/12 ADMISSIBLE-STABLE** (was 1/12 in §9.5). Wall: 12 legs ×2 = 9.8 min,
×3 = 14 min.

v2 binary `build-v2/host_app/host_app` (reps=2 pass on the 19:30 binary,
reps=3 on the 19:48 and again on the final 21:26 binary):

| Leg | reps=2 | reps=3 | Verdict |
|---|---|---|---|
| coldboot | identical | identical | **ADMISSIBLE-STABLE** |
| f40391, f39807, f44111, f46203, f46949, slowspot_f40298, slowspot_f37092, f36935, artdet_f37152 | identical | identical | **ADMISSIBLE-STABLE** (9) |
| f57292 | ERROR | — | KNOWN v2 defect P4FIX-F1 `[FATAL-NOJUMP] could-not-jump pc=0x0 vf=57293` — now REPRODUCIBLE: two runs FATAL at the same vf with identical partial streams (frames 2/2, mem 1/1) |
| f59991 | ERROR | — | same defect class, `pc=0x0 vf=60112` — two runs identical up to the FATAL (frames 122/122, audio 20/20, mem 3/3); the §4 "BOOT_SM_EDGE wedge" form did not recur |

**10/10 non-defect legs ADMISSIBLE-STABLE; the 2 known defects are now
deterministic crashes** (the Phase-2 repro-leg requirement met by
construction).

A first reps=2 pass on build/ (before the harness end-of-run settle) gave
9/12 with coldboot / f39807 / artdet NONDET **only in the LAST audio
window** (frames + mem identical): the bench requests the clean shutdown
from inside the VSync case BEFORE that frame's present, and the main
thread's exit path raced the graphics thread's last present/hash line and
the boundary's remaining steps (audio pump). Attribution: harness
end-of-run race, not the runtime. Fix: `FinalizeForExit` waits for the
present seam and the in-flight boundary sequence (11.2). Re-run: 12/12.
No NONDET remained, so the diff_legs → lane → frame → diag escalation was
not needed this round.

### 11.6 Ranked residual sources / risks (nothing measured NONDET; ranked by remaining wall coupling)

1. **Harness/bench exit race (FIXED this round, harness-only)** — the
   settle is bounded (3 s + 5 s) and prints a line if it times out; a leg
   whose last boundary is a load spin could still hit the bound.
2. **Savestate relaunch protocol (BUG-036, FIXED)** — related LATENT
   hazard kept as-is: the old boundary thread's epilogue still runs
   `UnregisterThreadCtx(tid)` / `info->started=false` for the relaunched
   tid after the new incarnation registered (pre-existing; harmless for
   the boundary thread whose ctx is passed explicitly; would matter for a
   save-after-load of a parked relaunched thread).
3. **FMV present content under `mpeg_hle::DecodeActiveRecently()`** — the
   graphics thread consults a WALL window (`NowMs()-t <= 250/500 ms`) at
   4 sites (compfix/stale/bypass decisions) while the FMV decodes. The 3×
   identical 1500-vf coldboot (FMV included) shows it did not bite, but it
   is a wall input to present content; pin candidate for round 1B.
4. **Legacy T0 busy-wait carry promotion** (memory.cpp, >1 s wall
   hysteresis during SyncV-silent loads) — untouched by `PS2_T0_TICKPACED`;
   the coldboot loads passed 3×, so it did not fire in-window.
5. **`kDeferred` ticks** — a demand/delivery from a non-guest host thread
   (legacy IOP hosting's heartbeat thread, kernel OFF) is fired at the
   next boundary: correct but not exercised by the profile (kernel ON).
6. **Other diag emitters still on `fprintf(stderr)`** — only EESCHED /
   IOPSCHED are splice-proof; any future paired-stream diag must use
   `eesched::DiagPrintf`.
7. **Cross-tree (legacy vs v2) engine difference** — not nondeterminism:
   audio window 49 / mem checkpoint 180 on f46203 (11.4), now measurable.

### 11.7 Behaviour-neutrality (hatch OFF = shipped play configuration; default-ON pieces = the BUG-036 relaunch handoff, the GuestExit guard, the diag emitter, the harness settle)

| Check | Result |
|---|---|
| GS corpus (19 captures) | **19/19 byte-identical** on `build/` AND `build-v2/` (`/tmp/s125/corpus_*.out`; re-run 19/19 on the final `build/`) |
| ctest `build/` | **100% (415 tests: 413 passed, 2 designed SKIPs `Vu1Hle_vu1_case_w14bkstp_{mat,rep}` = "no HLE registered for image")**, incl. the new `EeTimingUnits` (35 checks), `EeSchedUnits`, `R139Threads`, `IopKernelUnits`, `VerifyHarnessUnits`, `PageInvariance` — run on both the 20:36 and the final 21:26 builds |
| ctest `build-v2/` | **100% (411)** (both builds) |
| ctest `build-v2-nog/` | **100% (411)** (both builds) |
| coldboot_gate `build/` (user env, hatch off) | **PASS ×3** (20:36 binary ×2, final binary ×1) — bright by f100 (≤ f900), max luma 244.28, 0 FATAL, FMV window bright (f425-f600 series 236-244) |
| coldboot_gate `build-v2/` | **PASS** — bright by f100, max luma 243.01, 0 FATAL |
| Anchors, user env, 300f, `build/` | **0 FATAL on all 11** (fps_mean 41.6-53.2 on a shared host) |
| Anchors, user env, 300f, `build-v2/` | 0 FATAL on 9; f57292 / f59991 = the two KNOWN defects (5 FATAL each, as in §9.7) |
| Savestate load under play env | every anchor run above is a `--load-state` (the BUG-036 handoff path, default ON): 0 FATAL, normal fps |
| **IOP kernel ON, REALTIME (play) cold boot** (`coldboot_gate.py ... PS2_IOP_KERNEL=1`, the kernel-ON gate variant = the gate's env override) | **FAIL ×2: run timed out (300 s)**, last sampled present f75 — the BUG-026 realtime boot race (stall at vf~61) is NOT closed by the token/tick work; `PS2_IOP_KERNEL` default OFF for play is RECONCILED (unchanged, fresh 2/2 evidence). The variant needs no new script: `python3 tools/coldboot_gate.py build/host_app/host_app --frames 1500 PS2_IOP_KERNEL=1` |

### 11.8 Speed (step 5) — `PS2_TICK_SYNC` ON vs OFF, 300f, `--load-state`, user env with `PS2_MTVU=0` in BOTH arms (the hatch refuses MTVU=1)

Host was shared during these runs (load average 3.6-4.9 with this session
idle; an earlier user-env pass on the same anchors ran 48-53 fps_mean, the
later reference arm 35). Back-to-back pairs (`/tmp/s125/play/fps*.out`):

| Leg | OFF fps_mean (p50) ×4 | ON fps_mean (p50) ×4 |
|---|---|---|
| f40391 | 27.34 (29.69), 27.34 (29.86), 30.44 (31.89), 32.38 (31.56) | 21.90 (22.15), 22.30 (23.63), 27.76 (29.73), 27.66 (29.71) |
| slowspot_f40298 | 27.39 (29.89), 32.23 (32.32), 32.45 (32.11), 32.00 (31.67) | 27.65 (29.98), 27.64 (29.88), 27.69 (29.65), 27.66 (29.74) |
| full user env reference (MTVU=1) | f40391 35.46 (33.56), slowspot 35.30 (32.70) | n/a (refused) |

Verdict: **NOT speed-neutral** — ON is ~10-20 % lower fps_mean (the
rendezvous removes the EE/gfx one-frame overlap by construction) and
requires `PS2_MTVU=0`. Kept PROFILE-ONLY (the play-path decision is round
1B's; the numbers above are the input to it). Behaviour: 0 FATAL in every
ON run.

### 11.9 Gate verdict

**Phase-0 self-test: PASSED for every non-defect leg — 24/24 legs
(12 legacy + 10 v2) ADMISSIBLE-STABLE at 3 reps; the remaining 2 v2 legs
are the KNOWN P4FIX-F1 defect, now a deterministic crash.** Harness ERROR
class: 0. Corpus, ctest, pixel gates and the play-env anchor battery stay
green with the hatch off. Phase-1 entry criterion ("Phase 0 green
baseline") is met for the legacy tree under the deterministic profile.
Baselines RE-FROZEN in `verify/baseline/<leg>/` from this battery's legacy
rep1 (final build id 2026-09-25 21:25:53; `README_FREEZE.md` records the freeze;
the §9 freeze is not comparable — tick policy ~2 wall-shaped ticks/frame →
exactly 1/boundary).

## 12. [R1B 2026-09-25] Round 1B — run-token neutrality in normal play, kernel-ON stall signature, Phase-0 GATE VERDICT

Session r1b (bounded ~40 min; every run under `tools/run_bounded.sh`, no
commits, savestates read-only). Evidence directory: `/tmp/s126/`
(`neutral.sh` + `neutral.out` + `play/*.{log,json}`, `analyze.py`,
`iopk/{head5400.txt,tail200.txt,delay_count.txt}`, `iopk_diag.sh`).
Binaries: the FINAL round-1A builds (build id 2026-09-25 21:25:53 legacy,
21:26 v2) — no runtime source was rebuilt this round (see 12.5).

### 12.1 Neutrality of the default-ON EE run token in NORMAL PLAY (user env, hatch OFF)

User env in every arm: `PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60
PS2_MTVU=1 PS2_GL_FAST=1 PS2_GL_FAST2=1 PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1`
(+ `PS2_GUI_OPEN=0 PS2_LOG=0 PS2_VERIFY_HEADLESS=1` for headless). ON =
shipped default; OFF = `PS2_EE_RUNTOKEN=0` (legacy free-running pthread
hosting). Arms interleaved ON/OFF/ON/OFF back-to-back on a shared host (load
average 3.5-4.1 throughout).

| Check | ON (default) | OFF (`PS2_EE_RUNTOKEN=0`) | Verdict |
|---|---|---|---|
| fps f40391 300f `build/` — mean (p50 / p5) ×2 | 45.66¹ (57.12 / 34.34), 49.94 (57.12 / 32.08) | 51.85 (57.74 / 38.24), 50.11 (56.67 / 33.03) | p50 within 0.6 of OFF; steady-state fps (frames 20-300) 56.78 / 55.26 vs 57.50 / 56.30 — **no measurable regression beyond rep spread (n=2, shared host; ON arm consistently ~0.4-1.6 fps lower in steady state)** |
| fps slowspot_f40298 300f `build/` — mean (p50 / p5) ×2 | 47.89 (54.49 / 31.97), 49.93 (56.62 / 32.43) | 48.91 (55.49 / 32.51), 51.37 (57.91 / 35.45) | steady-state 52.98 / 56.57 vs 54.55 / 57.15; OFF rep spread 2.6 fps > ON−OFF gap 1.1 — **no measurable regression beyond rep spread (n=2, shared host; ON arm consistently ~0.4-1.6 fps lower in steady state)** |
| fps `build-v2/` f40391 / slowspot 300f ×1 | 47.22 (56.51) / **50.81 (58.01)** | 50.57 (57.08) / 49.00 (56.61) | one leg each direction — **no systematic cost** |
| music sync — `[CUELAG]` f40391 900f, 120-tick windows (first 2 skipped) | fps 54.63, slope_wall/tick/aud **+0.0886 / +0.0887 / +0.0887**, tick_hz 59.95-60.04 | fps 55.74, slope **+0.0700 / +0.0699 / +0.0699**, tick_hz 59.95-60.03 | all three domains agree to 1e-4 in BOTH arms (no inter-domain drift); slope = 1−fps/59.94 by construction, tracks the window's fps only — **neutral** |
| audio underruns / drops — `[SND-PERF]` 16 windows | **0 / 0** (kon 107) | **0 / 0** (kon 110) | **neutral** |
| input latency — `[INLAT-T2]` wire → `[INLAT-T3]` scePadRead served, 5 scripted CROSS taps = 10 edges | served in the SAME syncv frame 1/10, NEXT frame 9/10; µs mean 14.4 ms (min 0.7, max 32.7 — one outlier) | SAME 1/10, NEXT 9/10; µs mean 13.1 ms (min 6.3, max 17.2) | **identical frame-latency distribution** (the game samples the pad once per frame; µs spread is host wake-up jitter) |
| load time — `--load-state f40391`, first frames after restore (f1 / f2 / f1..5 ms) ×2 | 384 / 269 / 752; 353 / 239 / 662 | 365 / 233 / 691; 350 / 226 / 683 | ON rep 2 < both OFF reps — **within spread** |
| FMV — one cold boot 900f, `build/` | 900/900 frames, 465 injected presents, presents_per_s_fmv 29.4, FMV audio consumed 723,200 frames (rising from window 9, 0 dropped), 0 underruns, 0 FATAL | (see 11.7 coldboot gates; the default arm IS the shipped path) | **frames + audio non-empty** |
| coldboot_gate `build/` (user env) | **PASS** — bright by f100, max luma 245.36, 0 FATAL | — | re-run this round |
| coldboot_gate `build-v2/` (user env) | **PASS** — bright by f100, max luma 243.01, 0 FATAL | — | re-run this round |
| scripted boot-to-gameplay, cold 3000f, `PS2_PAD_SCRIPT=1450-1458:START,1700-1708:CROSS,1950-1958:CROSS,2200-2208:CROSS` | rc 0, 0 FATAL, 3000/3000 frames at fps_mean 59.90; `[BOOT-SM-EDGE]` cases 0→6 by vf 979 (FMV + title), START seen on the wire (`btn=f7ff`), no 6→7 edge | see 12.6 (OFF arm) | **NOT neutral: OFF reaches the title (case 6→7 at vf 2629), ON never does (3000 and 4500 vf) — BUG-038, see 12.6** |

¹ first run of the session: f1 = 957 ms cold page-in (all other reps f1 ≈ 350 ms); its p50 is identical to rep 2.

**Speed mechanism (why a run token costs ~nothing here).** Reading of the
fps rows [record fix 2026-09-26]: no measurable regression beyond rep spread
(n=2 per arm, shared host) — but the ON arm's steady-state fps is
consistently ~0.4-1.6 fps lower in all four `build/` pairs (and in the
reviewer's independent r1r pair: 56.76 / 55.80 vs 58.35 / 56.23), so the
honest statement is "small, below the resolution of n=2", not "zero". Under
`PS2_FPS_CAP=60` the EE frame loop is present-paced: every arm sits at the
cap (p50 56.5-58.0 in all 12 runs) and the token only changes hands at
thbase scheduling points that were ALREADY parks/blocks in the legacy
hosting (`SleepThread`/`WaitSema`/boundary park); no arm spins on the
token. The one measurable difference is the diag-free `GuestEnter`/
`GuestExit` bracket per scheduling event, below the run-to-run noise of a
shared host. The §11.8 fps loss belongs to `PS2_TICK_SYNC` (rendezvous
removes the EE/gfx one-frame overlap), which stays PROFILE-ONLY — round-1B
play-path decision: **`PS2_TICK_SYNC` default OFF is confirmed; no play
default changes this round.**

### 12.2 IOP kernel ON, REALTIME cold boot — the f75 stall signature (BUG-026 fix item)

One bounded run: `PS2_IOP_KERNEL=1 PS2_IOPSCHED_DIAG=1 PS2_EESCHED_DIAG=1`
+ user env, `--bench-frames 300`, 90 s bound → `rc=124` (timeout), last
progress print `[FRAME 60]`, last sampled scanout frame 91; 64,800,495 log
lines (5.4 GB, deleted after slicing; `iopk/head5400.txt` = everything up
to the spin start, `iopk/tail200.txt` = the end).

| Side | Signature |
|---|---|
| IOP — which thread waits on what | **tid 1 = the STREAM.IRX RPC-dispatcher thread (entry 0x64F4, prio 30)** enters at vf 91 (kernel clock t=1,986,390 µs) a `DelayThread(20 µs)` poll loop at **ra=0x8c94**, re-polling the module state words `[0xe50]=4 [0xe54]=1 [0xf34]=273 [0xf1c]=0 [0xf5c]=0x1` — none changes for the rest of the run: **64,768,556 identical `delay tid=1 ra=0x8c94 us=20` iterations in ~88 s (≈736 k/s: the realtime host thread honours the 20 µs delay as ~1.4 µs, a hot spin)**. Just before it, tid 2 (CD worker, entry 0x30D4, prio 40) did its last `signal_sema 0 / wait_sema 0` handoffs and `sleep 1 / sleep_woke 1` pairs (vf 91); no CD delivery follows. |
| EE — which loop spins | last EE scheduling events, vb=116: `ready tid=1 prio=10 seq=1742` → `block tid=2 prio=9` → **`run tid=1 prio=10 own=1`** (the MAIN thread takes the token) → `ready tid=-2 prio=-1 seq=1743` (host-side RPC-callback context marked ready) — then **NOT ONE ready/run/block/drain line for the remaining ~88 s** while the vblank ticker keeps enqueuing (`vb` 117→5391 = 5,275 ticks at 60 Hz; every `enq kind=0 a=1 / kind=6 a=2,a=11,a=3` stays queued — no holder ever drains). I.e. EE tid 1 holds the run token and spins WITHOUT reaching a kernel scheduling point: the vf~61 boot loader wait (the 0x2b4738 loader-SM poll named in BUG-026 §"exposed (b)"), waiting for the IOP completion that only the spinning dispatcher could produce. Scanout side: `[GS-SCANOUT-NONNATIVE] mirror-path present … frame=75-85` (black/hold presents), `[S94-TIMEDOM] regime 1 -> 2` at f75. |
| Class | **mutual wait across the two hosts** — IOP dispatcher poll ⇄ EE loader spin; neither side is at a scheduling point the other can wake. Under the FRAME policy (profile) the EE thread itself dispatches the IOP at boundaries/park sites, so the poll is serviced in EE order and the race does not exist (12/12 STABLE battery). |

Fix item (Phase 1, BUG-026 record): make the realtime dispatcher's
`DelayThread` poll yield to the EE completion path (or route the EE loader
wait through a kernel scheduling point so the queued events drain), then
re-run `python3 tools/coldboot_gate.py build/host_app/host_app --iop-kernel on`
(new flag this round; the env-override form `PS2_IOP_KERNEL=1` still works).
Shipped default reconciled to **OFF for play** in the code (`Enabled()` was
already OFF; the `iop_module_table.h` hatch comment and `Enabled()`
declaration comment said "default ON" — corrected), in BUG-026's header,
in the flag ledger row and in §9.7 wording (see 12.4).

### 12.3 GATE VERDICT — Phase 0

**PASSED on the legacy binary `build/host_app/host_app`: 12/12 legs
ADMISSIBLE-STABLE at 3 reps under the deterministic profile (§11.5),
baselines frozen (§11.9); the default-ON machinery is proven behaviour- AND
speed-neutral in play (12.1: fps, music sync, input latency, savestate load
time, FMV, cold-boot luma gates ×2 binaries) with ONE open exception — the
cold-boot title load under the run token (BUG-038, 12.6), which is a play
defect outside the profile's evidence and does not touch the battery.** The v2
binary: 10/10 non-defect legs ADMISSIBLE-STABLE; its two remaining legs are
the KNOWN v2 defect P4FIX-F1, now DETERMINISTIC crashes and therefore
Phase-2 ENTRY WORK with reproducible legs:

| v2 defect | Repro leg (deterministic profile, `verify/run_leg.py`) | Signature |
|---|---|---|
| P4FIX-F1 (a) | `--anchor save_states/ps2state_slot0_f57292.ps2st --frames 600` | `[FATAL-NOJUMP] could-not-jump pc=0x0 vf=57293`, identical partial streams ×2 (frames 2/2, mem 1/1) |
| P4FIX-F1 (b) | `--anchor save_states/ps2state_slot0_f59991.ps2st --frames 600` | same class, `pc=0x0 vf=60112`, identical up to the FATAL ×2 (frames 122/122, audio 20/20, mem 3/3) |

Not part of the verdict (recorded, not gating): `PS2_TICK_SYNC` is
profile-only (speed, §11.8); `PS2_IOP_KERNEL` realtime boot stall (12.2)
keeps that hatch OFF for play — the deterministic profile does not depend on
the realtime policy; BUG-038 (12.6) is the first Phase-1 runtime fix item
(play fallback `PS2_EE_RUNTOKEN=0` until then). **Phase-1 entry criterion ("Phase 0 green baseline
captured") — MET for the legacy tree.** Phase 1 (dead-code + hygiene purge)
may start; its proof gate is bit-identity against the frozen
`verify/baseline/`.

### 12.4 Reconciliations and small items landed this round (no runtime rebuild)

| Item | Disposition |
|---|---|
| `tools/coldboot_gate.py` | `--iop-kernel on\|off` added (sets `PS2_IOP_KERNEL=1/0` on top of the user env; an explicit `ENV=VAL` override wins) + `--print-env` parser self-check (exit 0, no run). Verified: on/off/`=off`/bad-value/override precedence. |
| `host_app/iop_kernel/iop_module_table.h` | hatch comment "default ON" → default OFF for play with the BUG-026 reason; `Enabled()` comment fixed. Comment-only. |
| `host_app/iop_stubs.h` — `ChangeThreadPriority` | **documented NO-OP in both hosting modes** (returns KE_OK, changes nothing). Call-site evidence: STREAM.IRX has exactly ONE call (`build/iop_recompiled.cpp:60482`, guest pc 0x1115c: thid = the tid stored at `[0x2f14]`, guarded non-zero; prio = caller's a0). NOT routed to `IopKernel::ChangePriority` this round on purpose: routing changes the IOP dispatch order under the frozen profile, so it must land together with a battery re-freeze — Phase 1 fix item under BUG-026. |
| `REFACTOR.md` (code-repo root) ⇄ refactor `README.md` cross-link | both written: where the plan/audits/bug KB/Phase records/harness live vs which program pieces live in the code tree. |
| §9.7 wording | **[Corrected 2026-09-26]** The original row here claimed §9.7 "already recorded" the kernel-ON realtime FAIL — it did not: §9.7's "Cold 900f, kernel ON vs OFF" row (2026-09-24, pre-round-1A binary) reports 900 presents in both arms, and the s120 logs show its ON arm ran the LEGACY hosting (`(tick-clocked)`, no `[IOPK] kernel up`), so §9.7 recorded neither a kernel-ON PASS nor a FAIL (row annotated in place). The FAIL was first recorded in §11.7 (round 1A, `/tmp/s125/coldgate_iopk_{1,2}.log`). Regression question settled by evidence, not by re-running the 2026-09-24 binary (which no longer exists — `build/`, `build-v2/`, `build-v2-nog/` all hold 2026-09-25 21:25-21:26 binaries): (i) the 2026-09-24 IOPK session's own realtime boots already stalled 12/17 at `[FRAME 60]` (`/tmp/s119/pb*.log`, same edge 0→1 vf 61 signature); (ii) s127 discriminator: kernel ON, realtime, user env, **`PS2_EE_RUNTOKEN=0`** cold 300f → same stall (last `[FRAME 60]`, scanout frame 86, edge 0→1 vf 61, killed at the 100 s bound, rc 124; `/tmp/s127/kern_rt/tokoff.log`). **The realtime stall is NOT a round-1A regression**: it pre-dates the run token and reproduces with the token off; round 1A at most changed its odds (09-24: 5/17 boots completed; 09-25/26: 0/4). 12.2 stays the authoritative signature; the shipped default is OFF. |
| BUG-033 | **CLOSED** (items 1-3 fixed under the profile in round 1A; the default-ON item 2 proven neutral in play here). BUG-026 stays **OPEN** with the 12.2 signature as its fix item. |

### 12.5 Process notes

* Source changes this round are comment-only (`iop_stubs.h`,
  `iop_module_table.h`) plus the Python gate tool — no `-j4` rebuild of the
  three trees was needed (behaviour cannot differ); the neutrality numbers
  were measured on the round-1A final binaries.
* The kernel-ON diag run writes ~60 MB/s of `[IOPSCHED] delay` lines while
  the dispatcher spins; slice with `head`/`tail`, never `grep` the whole file
  more than once, and delete it (5.4 GB in 90 s). A `PS2_IOPSCHED_DIAG`
  rate limiter for repeated identical `delay` lines is a cheap follow-up.
* macOS has no `timeout`; `tools/run_bounded.sh` is the bound everywhere.

### 12.6 Boot-to-gameplay leg — the one NON-neutral observation (BUG-038, OPEN)

The scripted cold boot did NOT reach gameplay in the ON arm, and the OFF arm
shows why the leg matters: it is not the script.

| Arm | `[BOOT-SM-EDGE]` trajectory | Title scene | Render queue (`[QUEUE-BACKPRESSURE]`, `[MAIN-MONITOR] queue=`) |
|---|---|---|---|
| ON, 3000 vf (`boot2game`) | cases 0→6 by vf 979; **no 6→7 edge by vf 3000** | `[VU1-HLE-RT]` 0 lines | depth 72-108, pending_vsync 2 from ~iter 27k (post-FMV) to the end |
| ON, 4500 vf (`boot4500.on`, START only) | cases 0→6 by vf 977; **no 6→7 edge by vf 4500 (75 s)** | 22 lines | depth 0 at the end |
| OFF `PS2_EE_RUNTOKEN=0`, 3000 vf (`boot2game.off`) | cases 0→6 by vf 977, **6→7 at vf 2629** (27.5 s after case 6) | 266 lines (3D title rendering) | depth 0 |

Common to all three: FMV audio complete (`fmv_c` plateau 897,024 in both
arms at the same window), 0 FATAL, presents at the 60 cap (the CRTC injector
keeps presenting, so neither the bench nor the luma gate at f900 can see the
difference — coldboot_gate PASSes on the same binary, 12.1). Recorded as
**BUG-038** (`docs/bugs/BUG-038-coldboot-title-load-runtoken.md`): cold-boot
title load never completes under the default-ON run token; hypothesis =
the legacy-IOP-hosting (kernel OFF) completions routed through
`EeEventQueue` are never drained because the loader poll holder reaches no
scheduling point — the §11.6 item-5 path the profile does not exercise
(profile = kernel ON, frame policy; its coldboot leg is 1500 vf < 2629).

**Consequence for the neutrality claim.** The run token is proven neutral
for everything measured IN PLAY (12.1: fps, music sync, underruns, input
latency, savestate load, FMV, gates) and NOT neutral for the cold-boot title
load — a **blocker for deleting the `PS2_EE_RUNTOKEN=0` arm**, not for the
Phase-0 gate (the gate is the deterministic-profile battery, which runs with
the token ON and the kernel ON and is unaffected — NOTE [record fix
2026-09-26]: at round 1B the profile did NOT pin `PS2_EE_RUNTOKEN`; the
battery and the frozen baselines relied on the binary's shipped default
(ON-unless-0). The profile pins `=1` explicitly since 2026-09-26 and the
baselines' `env_hash` was re-stamped with the streams proven unchanged —
§12.7, `verify/baseline/README_FREEZE.md`). Play fallback until fixed:
`PS2_EE_RUNTOKEN=0`. Fix + regression test (a title-reached assertion in
`coldboot_gate.py`) are BUG-038's items, first thing in round 2 / Phase 1.


### 12.7 [s127 2026-09-26] Record-consistency fixes (no runtime rebuild; no commits)

Six reviewer items on the Phase-0 closure, all record-side; the ONE runtime
artifact touched is the profile preset (`verify/profile_deterministic.py`).

1. **`PS2_EE_RUNTOKEN=1` pinned in the deterministic profile** (+ PROFILE.md
   row). Until now the profile relied on the binary default (ON-unless-0),
   so the frozen baselines' `env_hash` had to follow: re-stamped on all 12
   legs `0x09d513b97f30f7ea → 0x9a0f815a02d994d4`, computed exactly as
   `host_app/verify/run_meta.cpp` `CollectEnv()` (sorted `PS2_*` `K=V`,
   `PS2_VERIFY_*` / `PS2_PAD_SCRIPT_FILE=` skipped, FNV-1a64 per entry + `;`);
   the re-implementation reproduced all 12 recorded fingerprints and the
   r1r diag leg's `0x13bd755cedd46e36` first. Hash streams untouched
   (README_FREEZE). Proof leg under the UPDATED profile — `build/`
   (21:25:53), f46203_red_crystal, **600 vf** (the leg's frame contract; a
   300-vf leg cannot be `IDENTICAL` against a 600-line stream by
   `diff_legs`' length rule), `run_leg.py` → `tools/run_bounded.sh`,
   wall 16.8 s, 0 FATAL, `env_ps2` carries `PS2_EE_RUNTOKEN=1`, the
   binary's own `env_hash=0x9a0f815a02d994d4`:

   ```
   frames.fhash: identical (600 lines)
   audio.ahash: identical (100 lines)
   mem.mhash: identical (10 lines)
   diff_legs: IDENTICAL
   ```
   (`/tmp/s127/proof/diff_legs.out`; env gate passed — same `env_hash`,
   same `build_id`.) Independent corroboration: the reviewer's r1r legs
   (`/tmp/r1r/battery/`, 6 legs × 2 reps under the binary default) are
   `diff_legs: IDENTICAL` against the baseline 12/12 (re-checked here).
   The two sentences that claimed the profile "pins `=1`" at round 1B
   (BUG-038 Fix + hatch, §12.6) now state the dependency truthfully.
2. Stale "`PS2_IOP_KERNEL` default ON" sites annotated `[R1B: shipped
   default OFF, BUG-026]`: §8 (this file), MIGRATION_PLAN.md (IOP runtime
   table header), FILEMAP_host_app_2.md (iop_stubs IOPK update).
3. §9.7 kernel-ON cold-boot row annotated with its binary/config, §12.4's
   "§9.7 already recorded the FAIL" claim corrected, regression question
   settled by evidence (pre-existing, not round 1A) — see §12.4 and the
   BUG-026 fix-item bullet.
4. BUG-033 table `Fix + hatch` cell → closed wording matching its CLOSED
   section.
5. `REFACTOR.md` (code-repo root) bug-KB range `BUG-001…038`; FLAG_LEDGER
   `PS2_EE_RUNTOKEN` row cites BUG-038 and the profile pin.
6. §12.1 fps verdicts: "no measurable regression beyond rep spread (n=2,
   shared host; ON arm consistently ~0.4-1.6 fps lower in steady state)".

Evidence directory: `/tmp/s127/` (`envhash.py`, `restamp.out`,
`proof_leg.sh` + `proof/`, `kern_rt_tokoff.sh` + `kern_rt/`).

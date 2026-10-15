# The `deterministic` profile — every var and the clock it removes

Phase-0 form of EQUIVALENCE_PLAN §3 (env preset until the Config schema
exists, MIGRATION Phase 4). Applied by `run_leg.py` on a CLEANED
environment: inherited `PS2_*` vars are stripped first, so a leg's config
fingerprint is exactly this table plus the harness plumbing
(`PS2_VERIFY_*`, `PS2_PAD_SCRIPT_FILE` — excluded from the `env_hash`
equality gate by construction).

| Var | Value | Clock / noise source it removes |
|---|---|---|
| `PS2_TIME_EMU` | `1` | Game-visible clocks count an EMULATED quantum instead of raw wall vblank gaps (the 1↔2↔3 tick-gap oscillation, `time_domain.h`). |
| `PS2_TIME_EMU_REGIME` | `1` | Pins the emulated-quantum REGIME to 1. The adaptive detector follows sustained wall rate — host-dependent across machines and runs (A5 §7.2). |
| `PS2_NOVSYNC` | `1` | The bench env: present path stops pacing against the display's vsync. |
| `PS2_VBLANK_FREERUN` | `1` | Vblank ticks fire as the pipeline completes, decoupling the tick generator from `sleep_until` wall noise (explicit; also the `PS2_NOVSYNC` default). |
| `PS2_T0_SUBTICK` | `0` | T0 COUNT sub-tick interpolation derives low bits from wall time between ticks ([S40-SUBTICK]); off = whole-tick quantum, pure tick-domain. |
| `PS2_MTVU` | `0` | VU1 worker-thread runahead interleave (wall-scheduled). Equivalence of the on-path is separately gated at VIF-submit (VifReplay arms). |
| `PS2_HB_TICKCLOCK` | `1` | STREAM.IRX 60 Hz heartbeat beats per generator tick instead of a relative wall sleep (default ON already; pinned explicitly). |
| `PS2_CDVD_INSTANT` | `1` | CDVD stream HLE latency model (wall). |
| `PS2_DVD_LATENCY_SCALE` | `0` | DVD read latency model (wall). |
| `PS2_MC_LATENCY` | `0` | Memory-card latency model (wall). |
| `PS2_FPS_CAP` | `0` | Present-phase decimation driven by wall frame pacing. |
| `PS2_GS_INTERP` | `0` | Present interpolation (present-rate shimmer, root-caused R199-DET). |
| `PS2_GUI_OPEN` | `0` | GUI pixels composited into the presented backbuffer. |
| `PS2_GS_SCALE` | `1` | Native scale — the §1.1 bit-identity contract plane; removes resolution-dependent paths. |
| `PS2_SND_TICKCLOCK` | `1` | **[P0 pin]** The SDL audio DEVICE clock. Default: the device callback pulls `MixInto` at hardware rate, so voice positions and the game-polled NAX/ENVX/ENDX readbacks advance on a wall clock. Pinned: the device never opens; the EE SyncV boundary pumps 800 frames/vsync — readbacks become pure functions of the EE frame counter. |
| `PS2_FIXC_TICKCLOCK` | `1` | **[P0 pin]** FIX-C deferred CD/SPU completions on `steady_clock` due-times delivered by a wall-scheduled event thread (TIMEDOMAIN rows #7/#9, the A4 §5.1 residual). Pinned: completions are queued always-due and delivered hold-to-edge on the tick generator (everything scheduled during tick k is visible by edge k+1), with the existing inline poll-retire unchanged. |
| `PS2_LOG` | `0` | Not game-visible (runtime log only); keeps legs fast and the shared `logs/runtime_log.txt` quiet. |
| `PS2_MAINT_EEPACED` | `1` | **[MAINT pin, BUG-026 r2]** The CPU maintenance loop's ~1 kHz WALL cadence behind three guest-visible duties: SIF RPC end-callbacks (recompiled `FUN_001e6968` runs at wall points), deferred-ISO delivery (each pump pass decrements the 3-pass readiness floor and a delivering pass fires a cdvd-classified vblank tick — the wall clock behind the T0 master-clock per-frame binning AND the boot loader-spin exit race), and suspended-DMA resume. Pinned: the loop skips them; both EE frame-boundary sites and both boot/load park-loop pump sites run them on the guest run token. The pad slot stamp (`PadInput_Update`: slot phase/seq counters in guest RAM, measured 386 vs 397 stamps by mem checkpoint 0) moves from the SDL main loop to the boundary too. |
| `PS2_T0_TICKPACED` | `1` | **[MAINT pin, BUG-026 r2]** The LIVE legacy T0 read path (memory.cpp `0x10000000`) interpolates COUNT low bits from RAW WALL ns since the last SyncV — the clock `PS2_T0_SUBTICK=0` does NOT cover (that gate only exists in the default-OFF `PS2_EE_TIM0` path; the nine-clock row was wrong about coverage). Pinned: the returned COUNT zeroes the sub-frame term — the game's reconstructed master clock and its per-frame delta seconds (`FUN_001b74a0` → `0x3098C4`, echoed into the `0x3723AC` accumulator family / parked-ctx `f11` / VU0D/SPAD) advance only at EE frame boundaries. The busy-wait carry promotion (loads) keeps its legacy wall hysteresis. |
| `PS2_TICK_SYNC` | `1` | **[TICKSYNC pin, round 1A, BUG-033 (1)+(3)]** The vblank TICKER thread executed every tick BODY (count++, sema 1/3 signals, INTC VBON/TIM2/VBOF, the CRTC injector) asynchronously and the graphics thread raised INTC VBON after every processed VSync: under the run token these became queued events whose EE-stream landing point was host wall latency (574/580 ticker triples, 300/301 gfx VBONs moved between reps; tick count 583 vs 585). Pinned: NO ticker thread — exactly ONE tick body per EE frame boundary fired in-line by the boundary thread (sema/INTC direct), cdvd-delivery and FMV-demand ticks in-line at their EE-thread trigger sites, off-thread demands deferred to the next boundary, graphics thread mutates no guest state; AND the lockstep present rendezvous (ticket on every EE-pushed VSync, acked at both exits of the gfx VSync case, EE parks until acked — replaces MAX_FRAME_LAG + the 100 ms wall valve). The FMV pace rides the tick domain (2 ticks/picture) with injected presents rendezvous'd. Fixed boundary order: `host_app/ee_timing/boundary_sequencer.h` (rendezvous → tick → drain → maint → IOP dispatch → EE stream RPC → savestate → verify). Requires `PS2_MTVU=0` (refused otherwise). ~10-20 % lower fps than the legacy park (no EE/gfx overlap) — profile-only. |
| `PS2_EE_RUNTOKEN` | `1` | **[EETOK pin, BUG-033 (2); record fix 2026-09-26]** Pins the EE run token on regardless of the binary default; removes guest-thread host interleave (one guest EE thread executes at a time, the token changes hands only at thbase scheduling points — PHASE0_IMPL §10, BUG-034/035/036). Until 2026-09-26 the profile did NOT set this var and relied on the shipped default (ON-unless-0): the round-1A freeze (`baseline/README_FREEZE.md`) was measured under that default. Pinning it makes the frozen baselines independent of the play default (BUG-038 keeps the `=0` play fallback alive); the baselines' `env_hash` was re-stamped for this one var with the hash streams proven unchanged (README_FREEZE). |
| `PS2_VERIFY_HEADLESS` | `1` | (plumbing) hidden SDL window; rendering is offscreen-FBO either way. |
| input | scripted only | Human timing. Recorded padlogs replay through `PS2_PAD_SCRIPT_FILE` (same grammar the recorder writes). |

## Available but NOT in profile v1

| Var | Why not |
|---|---|
| `PS2_HB_BOUNDARY=1` | Boundary-fired heartbeat (handler runs on the EE thread at the SyncV edge instead of a ticker-following host thread). Implemented as a pin candidate; measured no shift in first divergence on the anchor battery (the residual is the free-running IOP workers, below), so it stays default-off until Phase 1+ revisits IOP scheduling. |
| `PS2_FRAME_LOCKSTEP=1` | Lockstep present rendezvous WITHOUT synchronous ticks (attribution leg: separates BUG-033 item (1) from item (3)). Implied by `PS2_TICK_SYNC=1`; its first form (R170 counter pair) wedged and was replaced by the ticket barrier 2026-09-25. Not needed in the profile since `PS2_TICK_SYNC` covers it. |
| `PS2_TICK_DETERMINISTIC=1` | Still in the profile (harmless) but SUPERSEDED under `PS2_TICK_SYNC=1`: there is no ticker thread to apply the timeout-floor rule to. Keep for legs that run the legacy generator (`PS2_TICK_SYNC=0`). |

## Known residual (documented, Phase 1+)

**Free-running IOP guest worker threads.** STREAM.IRX's guest threads
(`Thread 1`, entry `0x64f4`, plus the RPC workers) run on host threads
serialized only by the guest monitor; their guest-visible progress
(module init completion, stream-buffer delivery, SIF-side flag writes) is
host-scheduler-paced, not tick-paced. Evidence: first runtime-log
divergence between two profiled legs is the `[IOP-THREAD] Thread 1
started/calling entry_func` lines interleaving differently against the EE
loader; cold boot shows a persistent one-frame boot phase shift (first
divergent present vf≈69), anchors diverge ~3 presents after load.
`PS2_SOUND=0` does not remove it (not the audio lane). Fix class:
tick-quantized IOP worker scheduling / hold-to-edge SIF delivery —
MIGRATION Phase 1+ (the A5 §7.2 live-nondet family).

## A4 §5.1 nine-clock inventory — status under this profile

| Clock | Status |
|---|---|
| Wall vblank ticker | **PINNED (TICKSYNC 2026-09-25): no ticker thread at all — one tick per EE boundary + in-line cdvd/demand ticks, bodies on the EE thread (`PS2_TICK_SYNC`)**; the earlier PS2_TICK_DETERMINISTIC rule (no timeout-floor ticks) is subsumed |
| Adaptive REGIME | pinned =1 |
| Timer sub-tick | off |
| MTVU worker | off |
| CDVD/DVD/MC latency models | zeroed |
| IOP heartbeat cadence | **pinned (IOPK): kernel periodic timer fired inside dispatch — no ticker thread** |
| FIX-C deferred completions | **pinned (IOPK): kernel events, FRAME always-due at the next dispatch generation** |
| Audio device clock | **pinned (SND_TICKCLOCK, P0)** |
| IOP worker scheduling | **PINNED (IOPK 2026-09-24): PS2_IOP_KERNEL cooperative kernel + PS2_IOP_DISPATCH=frame — guest threads are coroutines dispatched at EE frame boundaries on a frame-derived clock; the paired-leg [IOPSCHED] trace is bit-identical. The former residual paragraph above is historical.** |
| Wall RTC seed (tenth clock, found by the IOPK self-test) | **pinned (PS2_RTC_FIXED)** |

## Residual after the IOPK pins (2026-09-24)

Frame + audio lanes: bit-identical on paired 300-vf anchor legs. Memory
lane: one float in one parked EE thread's saved context (THRD @0x9E8,
frame-fraction-shaped) with LSB echoes in VU0D/SPAD — behaviour-invisible
over the measured horizon; see docs/bugs/BUG-026 for the escalation state.

## Round 2 (2026-09-24, later session): sources named, two pins added

The THRD @0x9E8 float is `fpuRegs.fpr[f11]` of tid 1 (the main thread,
parked in sceGsSyncV at pc 0x1A018C): the game's per-frame delta-seconds,
computed by `FUN_001b74a0` (called from the main loop `FUN_00100d78`) from
the T0-reconstructed master clock and stored at `0x3098C4`, then
accumulated into per-object timers (`FUN_001b6b40` → `0x3723AC` family).
Two wall inputs fed it: the legacy T0 read path's RAW-WALL sub-frame
interpolation (pinned: `PS2_T0_TICKPACED`) and the per-frame TICK BINNING
noise from wall-raced cdvd-fired ticks + maintenance-thread deliveries
(pinned: `PS2_MAINT_EEPACED`).

Named still-open residuals (Phase 1 runtime fix items, not pinnable
profile-side this round):

1. **EE↔gfx drain-depth race.** The boundary backpressure park
   (gs_hle `MAX_FRAME_LAG=1` on `g_vsync_frame`) lets the EE run 0-1
   frames ahead depending on the graphics thread's WALL drain speed;
   every frame-keyed consumer (PS2_PAD_SCRIPT window masks, the game's
   own Δsyncv slip cadence and hence the delta-seconds ORDER) sees it.
   A `PS2_FRAME_LOCKSTEP` hatch exists (default OFF): rendezvous on the
   corrected R170 push/processed pair — it currently WEDGES ~1 present in
   (present-skip/coalesce paths leave pushed VSyncs unmatched by arg1==1
   pops; the CNTR-restored counter slack rebase alone was insufficient).
   Needs the graphics-side pop accounting finished before it can pin.
2. **Free-running guest EE threads.** ps2_threading hosts each guest
   thread on its own std::thread with NO run token: tid 1 and tid 2 (and
   RPC callback contexts) genuinely interleave on host cores. The EE-side
   dual of the IOPK cooperative kernel (ARCHITECTURE §3) is the fix.
3. **Vblank tick body asynchrony.** The ticker THREAD executes the tick
   body (count increment, sema 1/3 signals, INTC raises) concurrently
   with the running EE even when the release trigger is EE-derived; the
   landing point in the EE stream is wall-latency-shaped.


## Round 1A (2026-09-25): EE-synchronous ticks + lockstep present — 24/24 non-defect legs ADMISSIBLE-STABLE

`PS2_TICK_SYNC=1` added (row above). The three round-2 residuals are closed
under the profile: (2) by the run token (PHASE0_IMPL §10, BUG-034/035),
(3) by the in-line tick bodies and (1) by the ticket rendezvous. Paired
f46203 300-vf legs: EESCHED 1225 lines and IOPSCHED 1088 lines
bit-identical on both binaries, all three lanes identical. Battery:
`build/` 12/12 and `build-v2/` 10/10 ADMISSIBLE at 3 reps; v2's f57292 /
f59991 are the KNOWN P4FIX-F1 defect, now a deterministic FATAL at the same
vf with identical partial streams. Two instrument/protocol defects found by
the stream contract and fixed on the way: BUG-036 (savestate relaunch token
handoff, default ON) and BUG-037 (harness end-of-run race). Details:
PHASE0_IMPL §11. Baselines re-frozen (`baseline/README_FREEZE.md`).

Residual risks (no measured NONDET; see §11.6): FMV `DecodeActiveRecently`
wall window read by the graphics thread; legacy T0 busy-wait carry
promotion (>1 s wall) in SyncV-silent loads; `kDeferred` ticks only under
legacy IOP hosting; the cross-tree legacy-vs-v2 difference (audio window
49 / mem ckpt 180 on f46203) is now cleanly measurable.

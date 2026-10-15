# BUG-033 — EE-visible interleave still follows host wall scheduling (gfx drain depth, free guest threads, ticker body) — FIXED under the deterministic profile (items 1-3), 2026-09-25

| Field | Content |
|---|---|
| **ID** | BUG-033 |
| **Title** | Three structural wall couplings keep the Phase-0 battery NONDET after the BUG-031/032 pins: the EE↔gfx MAX_FRAME_LAG park, std::thread-per-guest-EE-thread with no run token, and the vblank ticker executing tick bodies asynchronously |
| **User-visible symptom** | None in play. Harness: paired legs fork frames at line 3 on busy anchors (immediately post-restore) and by ~line 40-400 on quiet ones; mem checkpoints 1+ carry delta-order noise; occasional ±1-frame KON timing shifts audio windows. |
| **Scene / savestate** | Whole battery; clearest on f46203 (quiet: forks late) vs f44111/artdet (busy: line 3). |
| **Subsystem** | ee-timing (secondary: gs-present, kernel-hle) |
| **Root cause** | (1) gs_hle's boundary backpressure park lets the EE run 0-1 frames ahead of the graphics thread (`MAX_FRAME_LAG=1` vs wall-paced `g_vsync_frame`, plus a 100 ms wall valve): whether a boundary parks — and the frame count every frame-keyed consumer reads (PS2_PAD_SCRIPT windows, the game's Δsyncv slip cadence and so the ORDER of its delta-seconds) — follows gfx drain speed. (2) ps2_threading hosts every guest EE thread on its own std::thread with no global run token: tid 1/tid 2/RPC callback contexts interleave on host cores. (3) The vblank ticker thread runs each tick's body (count++, sema 1/3 signals, INTC raises) concurrently with the running EE even when the release trigger is EE-derived. |
| **PS2 rule violated** | One EE, one instruction stream: on hardware every one of these events lands at a defined point in the EE's own execution. |
| **Fix + hatch** | **CLOSED 2026-09-25 (round 1B, PHASE0_IMPL §12; details in the CLOSED section below).** Items (1)-(3) fixed under the deterministic profile in round 1A: (1) lockstep present rendezvous (`ee_timing/frame_rendezvous.h`, replaces the `MAX_FRAME_LAG` park + 100 ms valve) and (3) in-line tick bodies (`ee_timing/tick_sync.h`) — both under the PROFILE-ONLY pin `PS2_TICK_SYNC=1` (play default OFF, §11.8 cost); (2) the EE run token `PS2_EE_RUNTOKEN` (default ON; BUG-034/035/036; pinned `=1` in the profile since 2026-09-26), proven behaviour- and speed-neutral in normal play (§12.1). Historical fix items (a)-(c) as originally filed: (a) present-skip/coalesce pop accounting for a lockstep hatch — superseded by the ticket barrier; (b) the EE-side cooperative run token — landed; (c) synchronous tick bodies at boundary sites — landed. The post-FMV cold-boot title load under the run token is a separate entry (BUG-038). |
| **Regression test** | The Phase-0 battery itself (0/24 ADMISSIBLE stands until these land); PHASE0_IMPL §9.5 table is the baseline to beat. |
| **Source record** | PHASE0_IMPL.md §9.6, PROFILE.md round-2 section. |
| **Triage tips** | If paired legs fork immediately on BUSY scenes but late on quiet ones, suspect load-dependent host scheduling (drain depth/backpressure), not a clock: clocks fork uniformly. A lockstep hatch that wedges names the accounting hole for you — chase the unmatched counter, don't widen the valve. |


## Status update 2026-09-25 (round 1A, PHASE0_IMPL §11) — all three items closed under the deterministic profile

| Item | Closure | Evidence |
|---|---|---|
| (2) free guest EE threads | `PS2_EE_RUNTOKEN` (default ON; BUG-034/BUG-035, §10) | paired-run EESCHED streams: the only remaining differences were the queued kIntcRaise positions (items 1+3) |
| (3) ticker tick bodies asynchronous | **`PS2_TICK_SYNC=1`** (`host_app/ee_timing/tick_sync.h`): no ticker thread; exactly ONE tick body per EE boundary fired in-line by the boundary thread (step 2 of the fixed sequence, `boundary_sequencer.h`), cdvd-delivery / FMV-demand ticks in-line at their EE-thread trigger sites, off-thread demands deferred to the next boundary; graphics thread raises no INTC / sets no CSR bit | attribution: 574/580 ticker triples and 300/301 gfx VBON enqueues at a different guest-event index between reps, tick count 583 vs 585 (§11.1); after: 0 `enq` lines, EESCHED 1225 lines bit-identical ×2 on both binaries |
| (1) gfx drain-depth park + 100 ms valve | **lockstep present rendezvous** (`ee_timing/frame_rendezvous.h`): ticket stamped on every EE-pushed VSync (`RenderJob::lockstep_ticket`), acked at BOTH exits of the graphics thread's VSync case, EE parks (token released) until acked — no wall valve. Replaces the MAX_FRAME_LAG park as step 1 of the sequence. The first `PS2_FRAME_LOCKSTEP` hatch (R170 counter pair rebased at the first boundary) WEDGED because the `--load-state` restore happens INSIDE that boundary and rewrites both counters; it now routes to the ticket barrier (attribution mode: rendezvous without sync ticks) | battery 24/24 non-defect legs ADMISSIBLE-STABLE at 3 reps (§11.5) |

Superseded by: `PS2_TICK_SYNC` (profile pin); play path unchanged (legacy
ticker + MAX_FRAME_LAG verbatim, hatch OFF; the ON arm measures ~10-20 %
lower fps_mean and requires `PS2_MTVU=0` — round-1B decision). Related
new entry: BUG-036 (savestate relaunch token handoff, found by the stream
contract). Regression test: the Phase-0 battery (`selftest.py`,
`verify/baseline/` re-frozen 2026-09-25) + ctest `EeTimingUnits`.

## CLOSED 2026-09-25 (round 1B, PHASE0_IMPL §12)

Items (1)-(3) fixed under the deterministic profile (round 1A); the one
default-ON piece — the EE run token (item 2, `PS2_EE_RUNTOKEN`) — proven
behaviour- and speed-neutral in normal play (user env, hatch OFF, ON vs
`=0`, interleaved reps on `build/`; §12.1 table): fps p50 within 0.6-1.3 of
the OFF arm on f40391 and slowspot_f40298 (steady-state within the OFF rep
spread; v2 one leg each direction), `[CUELAG]` wall/tick/audio slopes equal
to 1e-4 in both arms, 0 audio underruns/drops, identical wire→scePadRead
frame-latency distribution (1/10 same frame, 9/10 next), savestate load
frames within spread, FMV frames + audio non-empty, coldboot_gate PASS on
both binaries. `PS2_TICK_SYNC` (items 1+3) stays PROFILE-ONLY — the
round-1B play-path decision (default OFF; §11.8 cost stands). One open
observation from the boot-to-gameplay leg is recorded separately (§12.6):
it concerns the post-FMV title load, not the items of this entry.

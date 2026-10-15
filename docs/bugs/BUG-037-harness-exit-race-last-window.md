# BUG-037 — Verification-harness end-of-run race: the last frames.fhash line and the tail audio window were coin flips (instrument defect, not runtime)

| Field | Content |
|---|---|
| **ID** | BUG-037 |
| **Title** | The bench requests the clean shutdown from INSIDE the graphics thread's VSync case (bench::OnVsyncFrame, before that frame's present) and the main thread's exit path (WriteReport → verify::FinalizeForExit → std::_Exit) raced (a) the graphics thread's last present + hash line and (b) the EE boundary's remaining fixed-sequence steps (audio pump) |
| **User-visible symptom** | None in play. Harness: 299 vs 300 `frames.fhash` lines on a paired leg; 3/12 otherwise-identical legs NONDET only in the LAST audio window (coldboot window 161, f39807 / artdet window 99) |
| **Scene / savestate** | any leg; first seen f46203 300 vf and the 600-vf anchors under `PS2_TICK_SYNC=1` (lockstep changed the exit timing enough to lose the race) |
| **Subsystem** | core-log (harness) — secondary: gs-present |
| **Root cause** | `bench::OnVsyncFrame` fires `request_shutdown_fn` at `tick >= frames_target` at the TOP of the VSync case; `g_emulator_running=false` wakes the main loop within ~1 ms, whose exit riders finalize the hash streams while the present of frame N (verify::OnPresent → frames.fhash) and the boundary steps after the present rendezvous (verify::OnEeBoundary → audio pump / mem checkpoint) are still executing on other threads |
| **PS2 rule violated** | n/a (host instrument) — the artifact must be a pure function of the frame count |
| **Fix + hatch** | `verify::FinalizeForExit` (armed only) waits, bounded, until (1) no VSync case is in flight (`OnVsyncCaseBegin/End` bracket from the graphics thread, 3 s) and (2) `eetiming::BoundarySequencer::InFlight()==0` (5 s); prints a line if either bound is hit. Streams for diag comparison are additionally truncated at the `requesting clean shutdown` line (`/tmp/s125/interleave_sync.sh`). Harness-only; play path untouched |
| **Regression test** | battery 12/12 ×3 reps on `build/`, 10/10 ×3 on `build-v2/` (PHASE0_IMPL §11.5); the first pass without the settle reproduced the 3-leg tail-window fork |
| **Source record** | PHASE0_IMPL.md §11.5 |
| **Triage tips** | A NONDET whose ONLY divergence is the last line of a lane (frame line N of N, audio window last-of-last) is an end-of-run race, not a runtime clock — check the shutdown ordering before chasing game state |

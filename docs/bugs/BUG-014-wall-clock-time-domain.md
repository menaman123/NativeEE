# BUG-014 — Game-visible clocks rode host wall time: the single emulated-time-domain consolidation

| Field | Content |
|---|---|
| **ID** | BUG-014 |
| **Title** | [S94-TIMEDOM] Nine game-observable timing inputs inventoried; T0/vblank/heartbeat rode a 60 Hz WALL ticker, so game logic sampled host jitter (2↔3-tick gaps routine at 28-31 fps) |
| **User-visible symptom** | Timing-derived content decisions jitter with host load (the BUG-013 patchwork's enabling condition); run-to-run nondeterminism in anything reading T0 deltas. |
| **Scene / savestate** | Same cavern anchor; [TD2] 340-boundary census showing dd==df on every row (the old min(gap,3) clamp NEVER fired — gap 2↔3 oscillation was the dominant jitter and passed untouched). |
| **Subsystem** | ee-timing |
| **Root cause** | The vblank ticker was a wall-clock `sleep_until` loop; EE timers (T0-T3 COUNT), the VBlank ISR chain, and the STREAM.IRX heartbeat all derived from it, so the tick count BETWEEN two game frame boundaries varied with host scheduling (2 or 3 ticks routinely at this scene's fps). Every consumer of a per-frame T0 delta therefore saw host jitter as game-visible time. The [TD1] inventory catalogued all 9 inputs and which were quantum-locked. |
| **PS2 rule violated** | On hardware, vblank and the 147.456 MHz-derived timer clocks are phase-locked to the frame the game is producing: a frame boundary always observes a deterministic tick/count relationship. Game-visible time must be a pure function of emulated progress (EmuDomain), not of host wall time. |
| **Fix + hatch** | [TD3]/[TD4] single emulated-time domain (`time_domain.h` timedom): a regime detector (or pin) fixes ticks-per-frame; T0 COUNT and derived clocks become pure functions of the domain; `PS2_TIME_EMU` family default ON. Post-refactor: TickSource → EmuDomain → DerivedClocks → BoundaryProtocol is the codified architecture (ARCH §4.7); the deterministic profile pins the regime (EQUIVALENCE §3). |
| **Regression test** | `test_time_domain`; [TD2]-style boundary census (dd==df) as a unit property; the whole EQUIVALENCE harness depends on this domain — any wall leak shows as anchor-leg hash nondeterminism (the harness self-test catches it). |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_TIMEDOMAIN_IMPL.md` ([TD1]-[TD4]) |
| **Triage tips** | Before chasing any timing-flavored artifact, run the [TD1] question: list every clock the game can observe and ask "pure function of emulated progress?" — one NO explains a family of symptoms. A clamp that "should" bound jitter but never fires (dd==df) means the jitter lives BELOW the clamp's horizon; measure the actual gap distribution first. |

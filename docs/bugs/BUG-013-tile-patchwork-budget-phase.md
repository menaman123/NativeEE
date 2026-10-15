# BUG-013 — Flickering tile patchwork on cavern floor/wall: the frame-budget predicate was a host wall-clock phase lottery

| Field | Content |
|---|---|
| **ID** | BUG-013 |
| **Title** | [S96-BUDGETEMU] The 0x191bd0 frame-budget HLE anchored its 60 Hz sawtooth at a free-running host epoch — the game's detail-overlay decision flipped with host phase, repainting whole tile rectangles in a different shade |
| **User-visible symptom** | Rock floor and wall render as a patchwork of large rectangles, each slightly differently shaded, flipping between two states in sync (31 blocks flip in one frame; hold times 7-74 frames; amplitude up to 10.4 luma). |
| **Scene / savestate** | `save_states/ps2state_slot0_f40391.ps2st`, camera still; flip map per-16x16-block CRC over 480 frames. |
| **Subsystem** | ee-timing (primary), gs-classes (symptom surface) |
| **Root cause** | Chain of three records. [TL4]: the game EMITS a different display list in state B — a game-logic decision, not a render defect. [DP1-DP8]: the decision sites are the engine's detail-overlay passes reading the timer singleton (0x3234B4) and the frame-budget predicate `FUN_00191bd0`. [BH1]: our HLE for 0x191bd0 (`[R145-LOCKSTORM]` hook) computed `in_quantum = (now − s_epoch) mod 1/60` from a FREE-RUNNING host epoch captured at first query — a phase lottery: whether the game "had budget" depended on where host wall time happened to sit in the 60 Hz sawtooth, flipping the detail decision frame to frame. |
| **PS2 rule violated** | The budget the game derives is a function of the EE's OWN frame phase (T0 counts since frame top ≈ 9600/frame). Any game-visible clock must be a pure function of emulated time, phase-anchored at the game's frame boundary — never at a host epoch. |
| **Fix + hatch** | [BH8]/`[S96-BUDGETEMU]`: rebind the budget predicate to the emulated time domain — synthetic base + reads×increment anchored at the game's frame top (`PS2_BUDGET_EMU`, default ON), keeping the `loader_sync::ShouldYieldPollSlice` blocked-read yield. Post-refactor: `DerivedClocks.budget_anchor` in ee/timing (ARCH §4.7); classified HARDWARE/TIMING HLE (HOOK_RECLASSIFICATION §3.1). |
| **Regression test** | Tile flip metric: PRE 294/300 frames over threshold → POST 0/281 (FOGCONTENT battery reuses it); `test_time_domain` unit gates; deterministic profile makes any regression a frame-hash diff. |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_TILEPATCH_IMPL.md` ([TL1]-[TL4]), `CAVERN_DETAILPASS_ANALYSIS.md` ([DP1]-[DP10]), `CAVERN_BUDGET_IMPL.md` ([BH1]-[BH8]) |
| **Triage tips** | Content that flips between exactly TWO whole-region states is a game DECISION flipping, not a texture bug — diff the display lists first (draw counts/classes per frame), then hunt the decision's inputs. Any `clk::now()` that reaches a game-visible value is a suspect; grep for host-clock reads on guest-visible paths before staring at shaders. |

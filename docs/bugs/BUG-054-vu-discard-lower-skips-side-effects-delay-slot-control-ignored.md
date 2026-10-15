# BUG-054 — VU interpreter: `discard_lower` skips the lower op's side effects (LQI VI++), and a branch/E bit inside a delay slot is ignored; TPC not set on redirect

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-054 |
| **Title** | VU interpreter: `discard_lower` skips the lower op's side effects (LQI VI++), and a branch/E bit inside a delay slot is ignored; TPC not set on redirect |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit) — all 3,293 corpus cases and 126 replay cases are identical to OLD WITH these rules, so no title program exercises the arms |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `vu1-hle` (VU1 interpreter tier) |
| **Root cause** | OLD `vu_interpreter_tier`: when the upper op's result discards the lower (`discard_lower`), the WHOLE lower op is skipped — LQI's `VI++` and a load's address side effects included — where hardware still bumps VI (`P5-5B-DISCARD-LOWER-SIDE-EFFECTS`). A branch or E bit inside a delay slot is ignored; an E bit on a taken-branch bundle wins over the branch; TPC is not set on a redirect (`P5-5B-DELAYSLOT-CONTROL`). NEW replicates both (pinned) so the cached tier and the corpus stay OLD-identical. |
| **PS2 rule violated** | VU User's Manual: the lower instruction executes its integer side effects even when the upper's destination overlaps (only the register write-back is arbitrated); a branch in a delay slot is undefined on hardware but E in a delay slot still ends the program after the next instruction; TPC follows every taken branch. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field (also carried with R-VU0-SIZE, the NOP sled and the XTOP yield rows) |
| **Regression test** | ctest `VuInterpSmoke` (pins both observables); `Vu1Replay --compare 126/126`; the `SWEEP cases=3293` long test |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-5B-DISCARD-LOWER-SIDE-EFFECTS / P5-5B-DELAYSLOT-CONTROL`; 5B `:675-676` |
| **Triage tips** | A new VU1 program (a second title) whose address register drifts by one after an LQI paired with a discarding upper, or that misbehaves around a branch-in-delay-slot — run it through `vu1_replay --compare-cycle` against the interpreter first. |

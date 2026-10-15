# BUG-057 — VU1 cached tier: `MaterializeFmac` rebuilds only the last 3 slots — the drained clock can lag the interpreter by 1 after a cap cut

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-057 |
| **Title** | VU1 cached tier: `MaterializeFmac` rebuilds only the last 3 slots — the drained clock can lag the interpreter by 1 after a cap cut |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit) — not observed: both `w14bkstp_*` cap cases and the 15 backstop legs have identical clocks |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `vu1-hle` (cached tier) |
| **Root cause** | `MaterializeFmac` rebuilds from the last 3 slots (OLD `kmax = min(executed, 3)`); a writer 4 back (retire `cycle + 1`) is not re-created. It is dead for every consumer (pruned at the next bundle) EXCEPT a program-end drain immediately after a cap cut inside a span whose last 3 bundles write nothing — then the drained clock could be 1 lower than the interpreter's. Left as OLD so the cached tier stays OLD-identical (`P5-5C-MATERIALIZE-K3`). |
| **PS2 rule violated** | VU pipeline: an FMAC result retires 4 cycles after issue; a program-end drain must account for every in-flight writer, including one issued 4 bundles back. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field; revisit only if `vu1_replay --compare-cycle` ever flags it |
| **Regression test** | `vu1_replay --compare-cycle` over the corpus (cycle-identical 126/126 and the 3,293 sweep); the `w14bkstp_*` cap cases |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-5C-MATERIALIZE-K3`; 5C `:804` |
| **Triage tips** | A one-cycle clock disagreement between tiers reported by `--compare-cycle` right after a cap cut is this row; it is a count, never a result difference. |

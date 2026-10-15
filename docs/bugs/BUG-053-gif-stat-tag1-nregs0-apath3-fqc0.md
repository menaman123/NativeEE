# BUG-053 — GIF status registers: TAG1 reads NREG 0 after RST, APATH always 3 while a tag is in transfer, P1Q/P2Q/P3Q never set, FQC always 0

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-053 |
| **Title** | GIF status registers: TAG1 reads NREG 0 after RST, APATH always 3 while a tag is in transfer, P1Q/P2Q/P3Q never set, FQC always 0 |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit); a title polling `GIF_STAT` for path arbitration or FIFO level would read a constant |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` |
| **Root cause** | OLD composes TAG1 from the POST-Parse nregs; `16 << 28` leaves the 32-bit word and reads 0 — accidentally the hardware encoding — but after RST the tag reads nregs 0, not 16 (`P5-GIF-TAG1-NREG16`). STAT.APATH reads 3 whenever a tag is in transfer regardless of the real path; P1Q/P2Q/P3Q never set; FQC always 0 because nothing fills the FIFO (`P5-GIF-APATH3`). NEW `GifRegs` carries both verbatim. |
| **PS2 rule violated** | EE User's Manual `GIF_STAT`: APATH names the path actually transferring (1/2/3), PnQ flags a path waiting, FQC is the GIF FIFO quadword count (0–16); `GIF_TAG1.NREG` is the tag's NREGS field (0 = 16). |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field |
| **Regression test** | ctest `GifRegsStat` (pins TAG1/STAT observables) |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-GIF-TAG1-NREG16 / P5-GIF-APATH3`; 5A `:96-97` |
| **Triage tips** | A `GIF_STAT` poll loop that never exits (waiting for FQC/PnQ) or always exits (APATH) is this row; the sync functions the title actually uses are HLE'd, so a hang here means a new polling site. |

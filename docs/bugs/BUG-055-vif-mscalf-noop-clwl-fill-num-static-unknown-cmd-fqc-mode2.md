# BUG-055 — VIF: VIF0 MSCALF/MSCNT are no-ops; CL<WL fills from the exhausted source; NUM never counts down; unknown VIFcodes ignored; FQC /16; MODE 2 is `+COL`

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-055 |
| **Title** | VIF: VIF0 MSCALF/MSCNT are no-ops; CL<WL fills from the exhausted source; NUM never counts down; unknown VIFcodes ignored; FQC /16; MODE 2 is `+COL` |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit) (the corpus and the 6 VIF arms are identical to OLD with these rules) |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` (VIF) |
| **Root cause** | OLD `vif.cpp`: on VIF0 only MSCAL kicks VU0 — MSCALF/MSCNT are no-ops and VIF0 UNPACK ignores TOPS (`P5-5E-VIF0-MSCALF`); with CL < WL the filling writes read from the exhausted source instead of the fill rule (`P5-5E-CLWL-FILL`, EE UM 6.3.5); `VifRegs.num` is never updated (`P5-5E-NUM-STATIC`); unknown VIFcodes are ignored without ER1/stall (`P5-5E-UNKNOWN-CMD`); STAT.FQC divides by 16 (`P5-5E-FQC-DIV16`); STMOD MODE 2 is implemented as `+ COL[cycle&3]`, not the difference (offset) mode (`P5-5E-MODE2-COL`, 5E REVIEW §4 (a)). NEW carries each verbatim and tests them. |
| **PS2 rule violated** | EE User's Manual VIF: MSCALF/MSCNT start the VU microprogram (after a flush / from the current address); when CL < WL the write cycles beyond CL take the filling value (ROW/COL per the mask), §6.3.5; NUM decrements per written vector; an invalid VIFcode sets `STAT.ER1` and stalls; FQC is the FIFO quadword count; STMOD MODE 2 adds the row register AND writes the result back to the row (difference mode). |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field |
| **Regression test** | ctests `VifUnpack*` / `VifArms` (`VIFARMS arms=6 same=6 diff=0`), `vif_replay` vs OLD; `Vif*` tests pin the no-op and MODE 2 forms |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-5E-VIF0-MSCALF / P5-5E-CLWL-FILL / P5-5E-NUM-STATIC / P5-5E-UNKNOWN-CMD / P5-5E-FQC-DIV16 / P5-5E-MODE2-COL`; 5E `:1585-1587`, 5E REVIEW §4 (a) |
| **Triage tips** | Vertex streams that step wrongly on a STMOD 2 unpack (deltas accumulate on hardware, not here) or a VU0 program that never starts after MSCALF — both are this row; MODE 2 is the likeliest to bite a second title. |

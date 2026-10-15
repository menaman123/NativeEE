# BUG-056 — DMAC chain bookkeeping: GIF normal mode never sets CIS(2); CHCR.TAG mirror includes the ID bits; a scratchpad offset-0 TADR takes the early return; the chase gate honours the `0x7000` prefix while the walk honours the SPR bit

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-056 |
| **Title** | DMAC chain bookkeeping: GIF normal mode never sets CIS(2); CHCR.TAG mirror includes the ID bits; a scratchpad offset-0 TADR takes the early return; the chase gate honours the `0x7000` prefix while the walk honours the SPR bit |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit) |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` (DMAC) |
| **Root cause** | Four OLD DMAC observables, each transcribed as-is: (1) GIF normal mode clears STR (via `Write`) but never sets `D_STAT.CIS2` (`P5-5F-GIF-NORMAL-NO-CIS`); (2) the CHCR.TAG mirror copies bits 16–31 of tag word 0 INCLUDING the ID field — after an END the channel's CHCR reads `0x7000xxxx` and the tail record stores and resumes with it (`P5-5F-TAG-MIRROR-ID`); (3) a VIF1 chain whose TADR is scratchpad offset 0 (`0x70000000`) reads as TADR 0 through the 28-bit mask and takes the "VIF1 TADR 0" early return — STR clear, no CIS, nothing fed (`P5-5F-SPAD-TADR0`); (4) `IsSaneRewrite` accepts an address-bearing tag whose ADDR carries the `0x7000` prefix without the SPR bit, but the VIF walk then reads RAM at `addr & 0x01FFFFFF` (`P5-5F-CHASE-GATE-VS-WALK`). |
| **PS2 rule violated** | EE User's Manual DMAC: every channel completion sets its `CIS` bit (normal mode included); `CHCR.TAG` mirrors bits 16–31 of the tag (the hardware does include ID/PCE/IRQ there — (2) is faithful but its persistence past END is not); `TADR` with the SPR bit (bit 31 of the 0x7000_0000 form → `ADDR.SPR`) addresses the scratchpad, offset 0 included; tag `ADDR` selects scratchpad by the SPR bit alone. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field |
| **Regression test** | ctests `DmacChainWalk` (final CHCR, no CIS on GIF normal), `DmacDeferredVif1` (record CHCR; TADR-0 early return), the chain-walker accept table |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-5F-GIF-NORMAL-NO-CIS / P5-5F-TAG-MIRROR-ID / P5-5F-SPAD-TADR0 / P5-5F-CHASE-GATE-VS-WALK`; 5F `:1844`, `:2017-2019` |
| **Triage tips** | A VIF1 chain built at the very start of the scratchpad that never runs is (3); a `sceDmaSync`-style poll on `D_STAT.CIS2` after a GIF normal transfer that never returns is (1). |

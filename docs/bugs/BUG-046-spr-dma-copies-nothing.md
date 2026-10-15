# BUG-046 — SPR0/SPR1 (scratchpad) DMA transfers complete without copying a byte

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-046 |
| **Title** | SPR0/SPR1 (scratchpad) DMA transfers complete without copying a byte |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit); a title path that moves data through the scratchpad DMA channels would see stale RAM/SPR contents |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` (primary); `ee-mem` (secondary: scratchpad) |
| **Root cause** | OLD `host_app/dmac.cpp:1562-1576` handles channels 8/9 (SPR FROM / SPR TO) by clearing STR and completing the channel with no data movement — the `qwc` quadwords between MADR and SADR are never read or written. NEW `Dmac::StartChannel` reproduces it ("SPR = complete only", PHASE5_IMPL `:1766`), class `DmacSprStub`-shaped arm, so a `CHCR.STR` on channel 8/9 is a counted no-op. |
| **PS2 rule violated** | EE User's Manual DMAC ch. 8/9: SPR FROM copies `QWC` quadwords from scratchpad `SADR` to main-RAM `MADR` (SPR TO the reverse), in normal or interleave mode (`D_SQWC` skip/transfer counts), then sets `D_STAT.CIS8/9`. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field |
| **Regression test** | ctest `DmacSprStub` (`tests/dmac/test_dmac_spr_stub.cpp`: SPR FROM/TO complete without copying — pins the OLD observable) |
| **Source record** | `docs/phases/PHASE5_IMPL.md` INTEGRATION CHECKLIST (j) row `P5-SPR-STUB`; 5F record `:1766` (`Dmac::StartChannel`), `:1897` (`DmacSprStub`); OLD `host_app/dmac.cpp:1562-1576` |
| **Triage tips** | If a scene reads garbage from a buffer the title fills via `D8_CHCR`/`D9_CHCR` (addresses `0x1000D000`/`0x1000D400`) — check `run.meta` DMAC channel counters for SPR starts with zero bytes moved; a real copy is the fix and an accepted-difference row is required first. |

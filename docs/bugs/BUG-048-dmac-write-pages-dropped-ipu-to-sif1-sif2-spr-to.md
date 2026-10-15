# BUG-048 — MMIO writes to the IPU TO, SIF1, SIF2 and SPR TO DMAC channel pages are dropped — those channels can never start through the register file

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-048 |
| **Title** | MMIO writes to the IPU TO, SIF1, SIF2 and SPR TO DMAC channel pages are dropped — those channels can never start through the register file |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit) (the title starts those channels through the kernel/library HLE bindings, not through raw MMIO) |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` |
| **Root cause** | OLD `GetDmaChannelID` switches on `offset & 0xFF00` with cases `0x8000..0xD000` only; the inner `offset & 0x400/0x800` tests are dead, so MMIO writes to the IPU TO (`0xB400`), SIF1 (`0xC400`), SIF2 (`0xC800`) and SPR TO (`0xD400`) pages are DROPPED — `StartChannel` has no other caller for them (`ProcessSifDma`, the IPU TO arm and the SPR TO stub are unreachable from MMIO). Reads still map them by range. NEW `Dmac::ChannelForWrite` replicates the page rule exactly; the four arms stay reachable through `StartChannel` (kernel HLE / library bindings). |
| **PS2 rule violated** | EE User's Manual DMAC register map: every channel's CHCR/MADR/QWC/TADR/ASR/SADR page (`0x1000B400` IPU TO, `0x1000C400` SIF1, `0x1000C800` SIF2, `0x1000D400` SPR TO) is writable and `CHCR.STR = 1` starts that channel. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field (the FACTS §3.5 page-rule note is corrected by the 5F row) |
| **Regression test** | ctest `DmacRegs` (pins the dropped-write observable per page) |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-5F-WRITE-PAGE-DROP`; 5F `:1841`, `:1766` |
| **Triage tips** | A title-side `sw` to `0x1000B4xx`/`0x1000C4xx`/`0x1000C8xx`/`0x1000D4xx` that seems ignored is this row — confirm with a watchpoint on the page, then route the start through `Dmac::StartChannel`. |

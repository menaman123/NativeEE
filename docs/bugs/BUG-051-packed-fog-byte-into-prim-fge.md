# BUG-051 — GIF PACKED FOG stores the fog byte into PRIM.FGE (any non-zero F enables fog)

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-051 |
| **Title** | GIF PACKED FOG stores the fog byte into PRIM.FGE (any non-zero F enables fog) |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit) (reproduced; recorded so the gsops/mem lanes are explained). A manual-faithful decode would change fog on draws whose PACKED FOG carried F ≠ 0 under PRIM.FGE = 0 |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` (parser → `GsSink::PackedFog`); `gs-classes` (secondary) |
| **Root cause** | OLD's PACKED FOG handler writes `f != 0` into the PRIM fog-enable bool (FACTS §6) instead of only latching the F coefficient. NEW reproduces it (`GsSink::PackedFog` comment, `GsRegisters::PackedFog`) so the Phase-6 `GsState` identity holds; `DrawIrLift::kMaskFOGF` agrees. |
| **PS2 rule violated** | GS User's Manual PACKED mode: register descriptor 0xA (FOG) carries F in bits 100–107 only; PRIM.FGE is set by PRIM/PRMODE and is not touched by a FOG write. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field (`EQUIVALENCE_PLAN.md` §7.1 row P6-FOG-BOOL) |
| **Regression test** | ctest `GsRegistersDecodeVectors` + the G-C1 façade bridge (Phase 6); the Phase-5 parser pin on `PackedFog` |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-GIF-PACKED-FOG-FGE` (`:101`); `docs/phases/PHASE6_IMPL.md` findings row `P6-FOG-BOOL` (`:95`), item 7 L6 |
| **Triage tips** | Fogged geometry where PRIM.FGE should be off (or vice-versa) on a draw fed by PACKED FOG — compare `PRIM.FGE` after the packet against the PRIM write alone. |

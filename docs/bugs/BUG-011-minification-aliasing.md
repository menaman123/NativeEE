# BUG-011 — Block-shade speckle on distant cavern walls: retail LOD on an LCD grid (no CRT low-pass)

| Field | Content |
|---|---|
| **ID** | BUG-011 |
| **Title** | [S91-MINIF] Minification aliasing: the game's authored LOD constant K pins far surfaces to mip levels 0-2 while the true footprint is 2-9 texels/pixel — faithful on a CRT, aliased on our raw pixel grid |
| **User-visible symptom** | Far dark rock wall: adjacent blocks at visibly different shades ("texture fine, then a block lighter than the one next to it"), dense speckle, static when still, crawls when moving; only distant surfaces, only the dark cavern. |
| **Scene / savestate** | `save_states/ps2state_slot0_f39807.ps2st`; wall bands wallL x[16,176) y[144,240), wallR x[272,448) y[128,224). |
| **Subsystem** | gs-texsrc (primary), gs-format |
| **Root cause** | No program bypass, no chain loss ([MN1] census). GS manual 3.4.12: LOD = log2(1/\|Q\|)·2^L + K is a distance function; the game's authored K (−4.3125/−6.25/−9.375) keeps the far-wall band at levels 0-2 while the recorded-vertex census shows 2-9 texels per display pixel there. Level-0/1 bilinear at 2-9 tpp IS minification aliasing — static speckle, crawling under motion. Real hardware makes the same selection but a CRT low-passes it; our LCD-grid presentation does not. |
| **PS2 rule violated** | None — the selection is retail-faithful (GS manual 3.4.12). The violated expectation is presentational: retail output was authored for CRT response. Classified as an ACCEPTED-DIFFERENCE-class enhancement, not a correctness fix. |
| **Fix + hatch** | `[S91-MINIF]`, hatch `PS2_GS_MIP_ALLCLASS` (default ON): an anti-aliasing LOD floor in every retail-mip shader block — `lod_used = max(lod_retail, lambda_footprint)` with lambda from screen-space derivatives at render resolution. Only ever picks a COARSER level than retail, only where the pixel cannot resolve the retail level; magnification and resolvable pixels bit-unchanged; scale-covariant (derivative shrinks by log2(N) at scale N). |
| **Regression test** | Speckle metric before/after on the wall bands; corpus byte-identity at hatch=0 (bit-exact legacy); EQUIVALENCE §7 accepted-difference ledger entry is the standing record (bounded, coarser-only). |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_MINIF_IMPL.md` (§ [MN1]/[MN3]/[MN4]) |
| **Triage tips** | Speckle that is static when still and crawls when moving = sampling below the Nyquist of the texture — measure texels-per-pixel from recorded vertices before blaming decode. When output is "wrong" but matches the GS manual, the fix belongs on the enhancement side with a coarser-only/bounded guarantee, and hatch=0 must stay bit-exact retail. |

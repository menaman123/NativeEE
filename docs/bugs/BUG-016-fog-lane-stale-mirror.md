# BUG-016 — Distance-fog patchwork: fog-lane reads consumed a stale VRAM mirror instead of the live lane

| Field | Content |
|---|---|
| **ID** | BUG-016 |
| **Title** | [S107-FOGLANE-LIVE] The CT16 fog-lane (FBMSK=0x3fff depth-shuffle strips into 0x280000/0x2f0000) was re-read through the `gs_vram` mirror whose lane pages were stale — 15/21 pages wrong vs the hardware-exact reference |
| **User-visible symptom** | Rectangular patchwork in the cavern's distance fog: fog density wrong in page-shaped bands, flickering (PRE 294/300 frames over the flip threshold, blocks with abs-delta>2 up to 66). |
| **Scene / savestate** | `save_states/ps2state_slot0_f40391.ps2st`; frame-P ROI metric + index-space plane comparison vs a hardware-exact derivation (21 pages). |
| **Subsystem** | gs-texsrc (primary), gs-vram |
| **Root cause** | The fog composite writes the Z-derived lane with FBMSK=0x3fff strips and immediately re-reads it as PSMT8H. When the read declined the GPU serve, it decoded the `gs_vram` mirror — but the lane's mirror pages were stale (the strips' deposits hadn't landed / were superseded): the reviewer's negative control shows the raw mirror wrong on 15/21 pages (index deltas 67-105) while the fixed path matches the hardware-exact plane on 21/21 pages (mean index delta ≤0.003). |
| **PS2 rule violated** | Stream order again, specialized: a read of pages whose last stream-order author is a render target must observe that target's CURRENT content — an RT-backed read may never consume stale mirror bytes ("never-stale" rule, rt_neverstale.h family). |
| **Fix + hatch** | [FC3] class-based, decline-only rescue: when a lane-class read (CT16-class FRAME + Z16/Z16S texture + keep16 lane signature, same-frame, last-author) declines the GPU serve, serve the LIVE attachment instead of the mirror — footprint-pair keyed, no addresses or scene constants; hatch=0 verbatim legacy. Post-refactor: `foglane_live.h` generalizes into ResolveRead's freshness/decline→rescue ladder (ARCH §4.5 [S109]); the lane trigger data moves to the title RendererProfile (A6 §3.2). |
| **Regression test** | 300-frame leg: POST 0/281 frames over bound, flip blocks max 66→0; plane-vs-reference 21/21 pages; corpus 19/19 identical; PageInvariance ctest ×3. |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_FOGCONTENT_IMPL.md` ([FC3], [FC3-R] review), `CAVERN_DEPTHPATCH_IMPL.md` (inherited chain) |
| **Triage tips** | Page-shaped wrongness in a value that was JUST rendered = the reader took the mirror path — check the serve-admission census before the decode math. Build the hardware-exact reference for one frame (index space, per page): "which pages differ" localizes the defect to writer vs reader instantly. Rescues must be CLASS-based (shape predicates), never address-based — addresses rot, shapes port. |

# BUG-022 — Checkerboard dark/light blocks: native-pixel aliasing permutations don't commute with resolution scaling

| Field | Content |
|---|---|
| **ID** | BUG-022 |
| **Title** | [S83-IDXSCALE]/[S83-FOGNATIVE] The fmt=3/4/5/6 aliased GPU serves sampled scaled targets as if native — the cross-format bit permutation (PSMZ16-of-PSMZ32, T8H/T8-of-CT16/CT32) landed on the wrong pixels at SCALE=2 |
| **User-visible symptom** | Alternating dark/light block (checkerboard) texture pattern, very visible on the green gas surface; present at scale 2, floor-level at scale 1. |
| **Scene / savestate** | `save_states/slowspot_f29548_s1789326538.ps2st` (gas scene); fog-mask blockflip metric from [S83-TEXBLKDUMP] captures. |
| **Subsystem** | gs-classes (primary), render-backend |
| **Root cause** | GS rule (GS User's Manual page/block/column layouts): the cross-format aliasing chains this game uses are exact NATIVE-pixel bit permutations of the 32-bit word address space — every texel of the reading format aliases exactly one halfword/byte of the written format, defined on native pixels ONLY; the permutation does not commute with resolution scaling. The R163/R164 GPU serve arms (u_depth_fmt=3..6) sampled 2x-scaled target surfaces without mapping coordinates back through native space (read side), and the depth→16-bit aliasing passes rendered their DESTINATION RTs at scale (write side) — both sides scrambled the permutation. |
| **PS2 rule violated** | The native-pixel aliasing permutation above — scale must be unwound around any cross-format aliased access, read and write. |
| **Fix + hatch** | Read side: `[S83-IDXSCALE]` scale covariance for the fmt=3/4/5/6 serve arms (`PS2_GL_IDXSCALE`, default ON) — mask alternation 0.181-0.254 → 0.157-0.181 at scale 2. Write side: `[S83-FOGNATIVE]` ([CB9]) pins the destination RTs of depth→16-bit aliasing passes NATIVE (`PS2_GL_FOGNATIVE`, default ON) — blockflip collapses to the scale-1 floor. Post-refactor: `alias_class` in PsmLayout drives shuffle legality; the shuffle/uv_scale transform is computed once in ResolveRead (ARCH §4.2/§4.5), and scale pins live in target identity ([GE1]/[S83] pins, ARCH §4.4). |
| **Regression test** | Fog-mask blockflip on/off pairs (reviewer's captures are the record); corpus 13/14 byte-identical + documented ledger hash; hatch identity at scale 1 (IDXSCALE on == off). |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_CHECKER_IMPL.md` ([CB1]-[CB9]) |
| **Triage tips** | Checkerboard at exactly block/page periodicity + scale-dependent = an aliasing permutation applied at the wrong resolution. Fix BOTH sides: a read-side fix that improves-but-doesn't-reach the scale-1 floor means the WRITE side is also scaled. The scale-1 arm is your free oracle — always measure the artifact's floor there first. |

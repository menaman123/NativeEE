# BUG-012 — Left-background texture crawls under camera motion: authored mip chains end too early

| Field | Content |
|---|---|
| **ID** | BUG-012 |
| **Title** | [S92-MOTION] The dominant background tristrips (T8 0x3c0000 128x32) exhaust their authored mip chain while still minified — residual crawl under motion beyond what BUG-011's LOD floor can fix |
| **User-visible symptom** | "Move the camera slightly up and down; on the LEFT side the background texture is clearly not done properly" — temporal crawl under motion, persisting after the [S91-MINIF] fix. |
| **Scene / savestate** | `save_states/ps2state_slot0_f40391.ps2st`; pad script RY tilt segments 40421-40600; crawl metric per-16x16 block over 180 motion pairs. |
| **Subsystem** | gs-texsrc |
| **Root cause** | The left background is drawn by HLE id=66a8cd1e (vs_textured_tristrip family) T8 tristrips whose textures author only a short mip chain (128x32, MXL 2-4). At the band's footprint the correct level is BEYOND the last authored level, so even a correct LOD floor clamps to a still-aliased level; bilinear (no trilinear blend between levels) adds level-switch stepping. |
| **PS2 rule violated** | None — the GS samples only authored levels (MIPTBP/MXL) with per-level point/bilinear. Enhancement-class: the CRT low-pass again hid the residual. Accepted-difference class, coarser/blur-only. |
| **Fix + hatch** | `[S92-MOTION]` under the same master hatch `PS2_GS_MIP_ALLCLASS` (default ON; =0 restores the full pre-S91 pipeline): (1) synth chain extension — `UploadMipLevels` box-averages the last authored level's decoded RGBA down to 1x1 as GL levels n+1.., raising GL_TEXTURE_MAX_LEVEL (TexRec.levels still means AUTHORED); (2) trilinear across the extended chain. Measured: crawl −12…−15 % (build), −21…−25 % (build-v2), 2-4× the replicate spread. |
| **Regression test** | Motion-crawl metric (dy4/dy5) table pinned in the record; hatch=0 bit-exact; corpus unchanged (synth levels engage only above authored MXL). |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_MOTION_IMPL.md` (§ [MV1]-[MV5]) |
| **Triage tips** | If aliasing persists after a correct LOD clamp, check whether the authored chain even contains the needed level (dump MXL + per-level dims for the offending draws). Distinguish "wrong level selected" (BUG-011) from "right level doesn't exist" (this bug) — the fixes are different layers. |

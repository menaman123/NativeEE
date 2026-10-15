# BUG-021 — Far textures "pixelized glitchy filter" on zoom-out: authored mip levels were never uploaded

| Field | Content |
|---|---|
| **ID** | BUG-021 |
| **Title** | [S82-MIPMAP] The texture path ignored MIPTBP1/2 — mip draws sampled only level 0 through caches keyed on the base window, so far textures rendered from the wrong (aliased, cache-stale) data and the zoom transition thrashed |
| **User-visible symptom** | Small movement zooms the camera out → far textures go blocky/"weird pixelized glitchy filter"; a nudge zooms back in → clean; the transition drops fps. |
| **Scene / savestate** | `save_states/weird_pixelized_glithcy_filter.ps2st` (vf=13617); scripted zoom legs `13650-13700:LY=0, 13900-13930:RX=255`. |
| **Subsystem** | gs-texsrc (primary), gs-format |
| **Root cause** | The game's world textures author mip chains (TEX1 MXL up to 4, MIPTBP addresses); our serve path sampled level 0 only, and the texture caches (content key, R120 epoch-keyed L1, R172 resolve memo) were keyed/epoched on the BASE window's pages — mip-level bytes were invisible to both sampling and invalidation. Far camera = heavy minification = the missing levels ARE the image. |
| **PS2 rule violated** | GS mipmapping (TEX1/MIPTBP, manual 3.4.12 family): when MXL>0 and MMIN selects mip modes, the GS addresses each level at its own MIPTBP base/width; a faithful serve must decode and key EVERY level, not just level 0. |
| **Fix + hatch** | Mip-aware pipeline: content key extends to `{per-level window bytes}` when mips are present (legacy keys bit-identical otherwise); `OpenGLBackend::UploadMipLevels` decodes each level over its own window (shared CLUT/TEXA) into GL levels + `GL_TEXTURE_MAX_LEVEL`; mip draws bypass the base-window fast paths (epoch coverage) and take the content-key path. Hatch family under the mip stage (default ON). Post-refactor: TexReadView carries the mip chain; DrawIR records miptbp1/2 + tex1 (ARCH §4.5/§4.6 — closes the .gsdl mip gap). |
| **Regression test** | Frozen corpus 12/13 byte-identical + adjudicated slowspot ledger hash (MP6); zoom leg census (uploads/f, l2 hit-rate) bounded; the mip fields now recorded by the corpus prevent silent regression. |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_MIPMAP_IMPL.md` ([MP1]-[MP6]) |
| **Triage tips** | "Bad only when far, clean when near" = mip chain (near draws magnify and never touch the missing levels). Check MXL/MIPTBP in the draw census before suspecting decode. When adding a new sampled dimension (mip levels), audit every CACHE KEY and EPOCH that assumed the old dimensionality — a correct upload behind a stale key still serves garbage. |

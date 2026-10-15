# `engine/gs/format/`

**Phase:** 2  
**Purpose:** THE format layer (ARCH §4.2, PHASE2_PLAN §2): the only GS address/format math in the tree.
Pure functions over bytes; no SDL/GL, no runtime consumer until Phase 6 (`gs/state`, `gs/vram`, `gs/hle`).

## Belongs here (one class per `.h/.cpp` pair; file = snake_case(type))

| Class | Files | Stage | Role |
|---|---|---|---|
| `Psm` / `PsmDescriptor` | `psm.h` | 2A | the 13 PSM field values + per-mode geometry (data only) |
| `PsmLayout` | `psm_layout.{h,cpp}` | 2A | page/block/column tables and the six-step `PixelAddress` (GS_SKILL ch. 8) |
| `Swizzle` | `swizzle.{h,cpp}` | 2B | the ONE rect walker: `LinearToVram` / `VramToLinear` over `PsmLayout` (read side, transfer write side, RT encode side are the same two primitives) |
| `ClutLayout` | `clut_layout.{h,cpp}` | 2B | the ONE CSM1 index reorder, CLUT rectangle positions, `ReadClut32/16` from a VRAM view |
| `TexaExpand` | `texa_expand.{h,cpp}` | 2C | TEXA application, 16→32 bit-replicate expansion |
| `PixelConvert` | `pixel_convert.{h,cpp}` | 2C | pack/unpack per PSM, alpha domain 0–128 ↔ 0–255 |
| `BlockDecode` | `block_decode.{h,cpp}` | 2D | THE texture read: rect of any PSM → RGBA32 through a page LUT GENERATED from `PsmLayout::ElementInPage` at first use (never typed), values via `TexaExpand`/`PixelConvert`, palettes via `ClutLayout`; `DecodeFootprint`/`DecodeRowBand` page windows; scalar + NEON (`PS2R_SIMD`, byte-identical) |
| `MipChain` | `mip_chain.{h,cpp}` | 2D | MIPTBP1/2 field unpacking → per-level `(tbp, tbw)`, level sizes halving to 1, MXL/MTBA from TEX1 (GS_SKILL §3.4.11) — addressing only; LOD policy is Phase 6 |

## Does not belong here

- caches / serve routes (`gs/hle/texsrc`, Phase 6), dither application, shader-side alias remaps
  (Phase 6 `render/gl`, expressed from `PsmLayout`), CSM2 COU/CSA offset loading (scope cut, see below).

## The four original swizzle families this directory replaces (OLD, read-only)

`Swizzle` + `PsmLayout` are the single implementation; the differential oracle
(`tests/oracle/og_gs_format_oracle.cpp`) proves each family against it.

| Family | OLD site (file:line) | Role there | Oracle |
|---|---|---|---|
| F1 decoder | `host_app/texture_decoder.cpp` block tables `:263/:275/:577/:590/:604/:872/:922`, column tables `:285/:616/:883`, T4 column formula `:1017-1025`, `GetAddressPSMCT32 :306`, `Z32 :336`, `CT16 :645`, `Z16 :675`, `Z16S :694`, `T8 :964`, `T4 :996`; page LUT `~:2300` | texture decode read side | 2A: lifted tables (`og_tables.bin`), per PSM |
| F2 deswizzle header | `host_app/gs_deswizzle.h:12-55` tables, `:57 addr_psmct32`, `:78 addr_psmct16`, `:101 addr_psmt8` | snapshot reader (texture inspector) | 2A: `#include`d read-only |
| F3 transfer write side | `host_app/gs_state.cpp` `WriteHWReg` legacy loop + `PS2_XFER_FAST` per-word path (`:5042-5160`), `writePixel`, local→local (23 `GetAddress*` refs; CT16S aliased to CT16 `:4534/:5110/:5362`) | HWREG image upload scatter | 2B: rect model lifted (per-pixel F1 dispatch over the transfer window) |
| F4 render-target encode | `host_app/graphics/opengl/opengl_backend.cpp` `EncodeRTToVRAM :15922` (CT32/CT24/CT16), `SyncRTToVRAM :16272`, `SyncZToVRAM :18157` (Z32/Z24 `:18396`, Z16/Z16S `:18385`) | FBO readback → VRAM | 2B: rect model lifted (per-pixel F1 dispatch over the sync rect) |

Decode leaves replaced by `BlockDecode` (OLD, read-only): `texture_decoder.cpp` `DecodePSMCT32 :365`, `Z32 :460`,
`CT24 :499`, `CT16 :723`, `Z16/Z16S :794`, `T8 :1252`, `T4 :1764`, `T8H :1863`; the `r121blk` page-LUT decoders
`:2291-3006` (`PageLUTs :2293` built from the same tables — NEW generates its LUT from `PsmLayout`); `ComputeDecodeFootprint
:1039` / `ComputeDecodeRowBand :1088` (page math → `DecodeFootprint` / `DecodeRowBand`). Oracle: `tests/oracle/og_block_decode.cpp`
(the port of OLD `tests/test_block_decode_equivalence.cpp`, 1,150 ln — its exact 843-case list replayed differentially).
MIPTBP addressing replaced by `MipChain`: `gs_mipmap.h:158 MiptbpPair`, `:213-228` level loop (sizes `base >> i`, min 1);
the LOD / GL filter half (`:278 SelectGLFilters`, `:126 AafMode`) is Phase 6.

CSM1 index reorder — the three OLD copies + one variant replaced by `ClutLayout::Csm1Index`:
`texture_decoder.cpp:944 CSM1Swizzle`, `graphics/opengl/opengl_backend.cpp:21667 R164Csm1Swizzle`,
`graphics/opengl/s9_composite.h:322 Csm1Swizzle`, `gs_deswizzle.h:140 csm1_unswizzle` (row-order form).
CLUT reads: `texture_decoder.cpp:1146 ReadCLUT32`, `:1217 ReadCLUT16`.

## Scope cuts kept (documented, not silently tolerated)

- CSM2 is read as a linear row of PSMCT16 entries at CBP; TEXCLUT COU/COV/CBW offset loading is not
  modeled (A3 §2 inconsistency 3). The property tests pin only what IS claimed (involution, linear).
- Accepted-difference candidates from this directory's oracles are in `docs/design/EQUIVALENCE_PLAN.md` §7.1:
  P2-CT16S, P2-T4, P2-T8-F2, P2-CLUT16-CSM1, P2-CT24-HWREG-A, P2-T4-HWREG, P2-Z-L2L (2A/2B address rows),
  P2-CT16-TEXA, P2-EXP16-REPL (2C value rows), P2-T8H-CSM1, P2-CLUT32-I4-CSM2 (2D decode rows); P2-BASE-8.5 is the recorded §8.5
  base-reading assumption both trees share. Z reads (`BlockDecode` Z16/Z16S) use the TEXA-less frame form, as OLD does.

## Flags (PHASE2_PLAN §3; all 14 rows applied at transplant time — 11 NORM / 2 DIAG / 1 DELETE)

| Flag | Outcome | Stage | What is written here |
|---|---|---|---|
| `PS2_GL_FAST` | NORM (=1) | 2D | `BlockDecode` has ONE decode path (the page-LUT walk); no legacy per-pixel arm besides the `DecodeGeneric` reference kept for the identity test |
| `PS2_GL_TEXCACHE` | NORM (on) | 2D | `DecodeFootprint` / `DecodeRowBand` exist unconditionally (the cache path's inputs; consumer Phase 6) |
| `PS2_GS_ABIT16` | NORM (on) | 2C | A bit ⇄ 0xFF/0x00 (`Truncate32To16` / `Expand16To32`); CT16S-only frame scope is a Phase-6 predicate |
| `PS2_GS_ALT16UV` | NORM (on) | 2B | no arm: the 16-bit companion view's geometry is `PsmLayout`'s class table (consumer Phase 6) |
| `PS2_GS_CLUTRAW` | NORM (on) | 2B | `ClutLayout::ReadClut*` returns raw PS2 alpha (no prefold); `BlockDecode::ReadPalette` keeps it raw |
| `PS2_GS_CT16_QUANT` | NORM (on) | 2C | `Truncate8To5` is the 5-bit truncation convention (dither itself Phase 6) |
| `PS2_GS_MIPMAP` | NORM (peak) | 2D | `MipChain::Build` is always available (no enable arm); the enable policy is Phase 6 |
| `PS2_GS_MIP_ALLCLASS` | NORM (peak) | 2D | no per-class exclusion arm in `MipChain` |
| `PS2_GS_MIP_TRILINEAR` | NORM (peak) | 2D | nothing here (filter policy, Phase 6 `mip_sampler`); recorded |
| `PS2_GS_RTKEEP` | NORM (on) | 2D | nothing here (serve predicate, Phase 6 `texsrc`); recorded |
| `PS2_GS_ZSRC` | NORM (on) | 2C | `PixelConvert::DepthFromColorWord` (value half); route predicate Phase 6 |
| `PS2_GS_READER_CENSUS` | DIAG | 2D | no census code in this directory; the `core/diag` category lands with `vram_census` (Phase 6) |
| `PS2_MIPCEN` | DIAG | 2D | `MipChain` has no census code (`gs_mipmap.h:187/:326` counters not transplanted); `core/diag` category Phase 6 |
| `PS2_GS_MIP_LODREF` | DELETE | 2D | not written (`gs_mipmap.h:126` reference arm) |

No `getenv`, no Config field, no diag code in `gs/format` (`CoreSingleGetenvSite` = 1; settings table unchanged).

Value leaves replaced by `TexaExpand` / `PixelConvert` (OLD, read-only): `texture_decoder.cpp:502-553 DecodePSMCT24`
(TA0/AEM), `:740-780 DecodePSMCT16` → `:1946 Expand16to32` (TEXA omitted: P2-CT16-TEXA), `texture_decoder.h:263
ScaleAlphaPS2ToGPU`, `opengl_backend.cpp:15992` (GPU→PS2 alpha inverse), `:1620/:2197` (5551 replicate at the RT),
`:1395` ("no TEXA"), `s9_shadow.h:54/:62/:102` (ABIT16 / ZSRC / CT16_QUANT value logic), `gs_state.h GetPixelSize`
(`gs_state.cpp:4731`), `gs_state.cpp:5066/:5332` pixels-per-write (CT24 at 32 bpp + `0x80` upper byte `:5148/:5424`:
P2-CT24-HWREG-A). Oracle case `OgGsFormatOracle_TEXA`.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.2, `docs/phases/PHASE2_PLAN.md`.

# `engine/gs/hle/texsrc/`

**Phase:** 6  
**Purpose:** TextureSource + the ONE ResolveRead (today's 11 serve routes + 3 rescues + 2 caches as exhaustive cases), content key, CLUT source, decode cache, uv bbox, window-in-target, mip sampler.

## Contents (lib `ps2r_gs_hle_texsrc`)

- `texture_source.h` -- `TextureSource` POD: `Owner {kind None|Surface|Decoded|Uploaded, surface, fmt_case 0..6, self_serve, levels, clut, partner}`, `Transform {shuffle, uv_tex_w/h, uv_pre_w/h, uv_native, z_row_offset, depth_fmt, idx_fmt, idx_self, mip_*, draw_scale, region}`, `Sampler`, `Declined`, `enabled`.
- `texture_resolver.h/.cpp` -- `TextureResolver::ResolveRead(const DrawIr&, const Context&)`: MatchTarget (alt16 / scene-accum / indexed fmt 4-5 with the lane-live rescue / colour serve with the `!recent ∧ CPU-dirty` keep test, rescue-then-decline in the read's format, class-mismatch sync, the CT32-of-CT16S exception, the self-read veto; a target at TBP always terminates here) -> MatchDepth (containment + most recent Z writer, fmt 1-3 with the page-row offset, Z sync only on decline) -> DecodeFallback (no snapshot -> nothing bound; L1/L2 -> decode of the composed view, UV-bounded by `UvRegion`, recorded in both caches). Before the lookup the fallback reads ONLY the bytes the key hashes (the level-0 window or its row band, the CLUT page of an indexed read, the authored mip windows) into grow-only scratch members; on a miss alone (`DecodeAndUpload`) the decoder's reach — the level-0 footprint (every page the texture touches), the CLUT page of an indexed read, the authored mip windows — is read into the 4 MiB view scratch at its own offsets, never the whole image (track 1d; `view_bytes` counts those spans). `TextureResolver::Stats()` (`Census`: `lookups`, `l1_hits`, `l2_hits`, `decodes`, `prep_bytes` = bytes read before the lookup, `view_bytes` = composed-view bytes read for decodes) -> `run.meta texres=`; the contract pinned by `Texsrc_DecodeFallbackPrepBytes` is O(texture window) per lookup, O(texture footprint) per decode (BUG-067; the 4 MiB-per-decode read retired in track 1d).
- `content_key.h/.cpp` -- `ContentKey` (address-free decode key; FNV tuple + xxHash64/FNV content steps; sub-rect and mip folds).
- `decode_cache.h/.cpp` -- `DecodeCache` (L1 by key with footprint eviction; L2 by content hash: 16384 entries / 320 MiB, probation pool 2048 / 32 MiB, LRU).
- `clut_source.h/.cpp` -- `ClutSource` (256x2 RGBA8 palette texture: prescaled row + raw row, CSM1 fold once / twice, keyed by CLUT byte hash).
- `clut_heal_cache.h/.cpp` -- `ClutHealCache` (quarantined workaround; exemption = `VramLayout::IsClutHealExempt`).
- `uv_bbox.h/.cpp` -- `UvBbox::Scan` (texel-space vertex bbox, kOk / kBail, rounding-mode normalised).
- `uv_region.h/.cpp` -- `UvRegion` (the decode sub-rect from the bbox: bilinear taps, CLAMP wrap modes, 64-texel quantisation, the half-area rule; eligibility = rect-capable PSM, >= 3 vertices, no mip chain).
- `window_in_target.h/.cpp` -- `WindowInTarget` (PSMT8 window inside a CT16 target: Contains / PackSelf6 / Fmt6Remap / Fmt6IndexFromRgba).
- `texres_trace.h/.cpp` -- `TexresTrace` (the source-composition census of filtered reads: tier / cache outcome, key, the deferred entries flushed, per-page commit state / author / epochs / overlay of the window and CLUT, optional window + CLUT bytes and the bound level 0; rotates per frame like `DrawRouteTrace`; inert without a filter; the harness enables it from `<dump-frames dir>/texres_trace.cfg` in diagnostics builds).
- `mip_sampler.h/.cpp` -- `MipSampler::Capture` (levels 1..MXL from MIPTBP via `gs::MipChain`, LCM/L/K).
- `owned_texture_slots.h/.cpp` -- `OwnedTextureSlots` (track 1c-2, textures at load): the texture slots an owner UPLOADED WITHOUT BYTES. `Bind(base, Texture)` records, in the stream position the title's IMAGE transfer held, the slot base, the footprint the bytes would have covered, the TEX0 tuple the title reads the slot with (per-level offset / 64-px width, the CLUT offset, PSM, log2 size) and the converted top-first RGBA8 levels (GPU alpha, pinned). `Serve(TexReadView, TextureSource*)` serves a read whose tuple names EXACTLY what the bind describes -- level 0 at base+offset with the bound TBW/PSM/size, the CLUT at base+offset in CPSM CT32 / CSM1 / CSA 0, every mip level `MipSampler::Capture` yields at its bound offset/width/size -- the image's backend surface built for that chain depth with the decode fallback's calls (`CreateSurface` + level 0, `UploadLevel` 1..n, `FinishMipChain(n, true)`), cached per (image, depth); anything else over a bound slot is a counted MISS (`miss_tuple/clut/mips`, the tripwire) and falls through to the VRAM tiers. A bind retires like the bytes would: `EvictRange` under a GPU draw (`EvictUnderDraw`) or an EE upload (`OnCpuWrite`), a rebind of the base, `Release(key)` (the owner destroyed the image). Surfaces of images no bind references are LRU-bounded (`kMaxSurfaceBytes` 256 MiB). The resolver consults it first inside `DecodeFallback` (`Census::owned_serves/owned_misses`, `TexresTrace::Outcome::OwnedServe`); `run.meta texres_owned=`. Tests: `tests/gs/texsrc/test_owned_texture_slots.cpp`.
- `texture_dump_sink.h/.cpp` -- `TextureDumpSink` (the texture-oracle observer the resolver owns, `TextureResolver::DumpSink()`; inert until a composition arms it for a directory): `DecodeAndUpload` reports every fresh decode (one per distinct texture content), the sink hashes tuple + footprint bytes + CLUT page, runs ORACLE B (`BlockDecode::DecodeGeneric` per-pixel reference over the same view, no heal) against the fast tier's texels and writes each distinct texture once -- `<hash>.ppm` / `.alpha.pgm` / `.json` (tuple, sizes, sub-rect, hashes, first draw / frame, palette, `oracle_b`) / `.win` (raw footprint, <= 1 MiB) / `.clut` (raw CLUT page), `.ref.ppm` when Oracle B differs; the non-decode tiers (`TraceServe`) record one `serves.tsv` row per (tuple, tier). Tests: `tests/gs/texsrc/test_texture_dump_sink.cpp` (`Texsrc_DumpSinkOracleB`: every PSM x CLUT CPSM / CSM / CSA x TBW x block-granular bases x TEXA x sub-rects; `Texsrc_DumpSinkFiles`). A title's judge (e.g. Sly 2 `verify/judge_textures.py`) compares the dump with PCSX2's per-draw texture dumps.
- `texture_decoder.h/.cpp` -- `TextureDecoder` (Decode / DecodeRect / DecodeLevel / ReadPalette over `gs/format`, GPU alpha; DecodeScanoutBuffer; ParseDispfb / ParseDisplay).

## Belongs here

- TextureSource, ResolveRead, ContentKey, ClutSource, DecodeCache, UvBbox, UvRegion, WindowInTarget, MipSampler

## Does not belong here

- fog-lane / crys trigger data (RendererProfile)

## Transplanted from (OLD, read-only)

- `opengl_backend.cpp serve regions, rt_neverstale.h, foglane_live.h†, texcache_key.h, uv_bbox.{h,cpp}, s8_inrt.h, gs_mipmap.h sampler half, texture_decoder.cpp caches`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

## Status

Stage 6D′ landed every file above; the Review 6D′ fix round re-derived the resolver's tier predicates line by line from OLD R16 `:19605-21611` / R6 `:4337-8287` (`docs/phases/PHASE6_IMPL.md` §6D′ "Review 6D′ fix"), added `UvRegion` and the L2 insert-on-miss; the corpus gate is 6F. The T8-of-CT16 serve (fmt 6) and the PSMT8-window-in-target serve are NOT taken at the peak configuration (their gate follows the OFF upload-deposit gate) -- the geometry is kept for the tests only.

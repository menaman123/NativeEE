# `engine/gs/hle/targets/`

**Phase:** 6  
**Purpose:** RenderTarget four-way split (identity / surface views / readback / coherence), target cache, refresher, scale policy.

## Belongs here

- `VramLayout` (the title's VRAM map as an engine-side struct; every query false when empty), `TargetIdentity`, `SurfaceViews`, `CoherenceState` (+ `LogicalRect`), `RenderTarget` (PrepareDraw / AfterDraw by route incl. the Z-write ordinal `last_z_seq` / alt16 companion), `ReadbackEngine` (colour + Z writeback into the VramImage shadow: linear-span touched pages, per-page EE-newer / rescue-authorship / uploaded-range vetoes, Z saturation, an optional encode-PSM override for reader-driven deposits), `RenderTargetCache` (grow-only, tag carry, rescue deposit with page-aligned stride, colour-only SyncAll at epoch 0, most-recent-drawn), `TargetRefresher` (Uploaded(addr) textures with the 512 / 64 MiB budget, EvictUnderDraw, CPU-dirty pages, UploadRowBand fold helper, RefreshFromVram), `ScalePolicy` (+ `ScaleSettings`)

## Does not belong here

- GL ids (handles only); any VRAM address literal (all of them are `VramLayout` data filled by `games/<title>/renderer_profile/`)

## Transplanted from (OLD, read-only)

- `opengl_backend.{h,cpp} TargetFBO regions, rt_copy_tracker.h, gs_scale.h, idx16_lane.h†`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

Added in 6D″: `alias_page_set.h/.cpp` (`AliasPageSet` — the shared cpu_dirty + aliased page sets),
`pending_rt_sync_ledger.h/.cpp` (`PendingRtSyncLedger` — the deferred 16-bit writebacks and their flush reasons);
`RenderTargetCache::{AfterDrawTail, FlushPending, Destroy(fbp)}`, `ReadbackEngine::DepositAlt16Lane`,
`TargetRefresher::{AliasPreDraw, RefreshFromVram arms, EvictRangeUnderDraw}`. OLD sources: `opengl_backend.cpp`
`:9403-9625`, `:14040-14230`, `:16170-16271`, `:16943-16963`, `:17118-17331`, `:17401-18130`, `alias_rect.h`.

Added in the 6D″ fix round / 6D‴: `target_dims.h/.cpp` (`TargetDims::Compute` — the per-draw render size: FBW in
(0, 1024] else the window width, window height, scanout content dims, scene-accumulator scissor height; OLD
`opengl_backend.cpp:8674-8716`); `RenderTargetCache::SyncDrawn` (every direct non-rescue colour sync + the OLD
`SyncRTToVRAM` tail that removes the deferred entry, `:17092-17095`; `SyncAll` routes through it), the
`AliasPageSet` walks CLAMP at page 511 (OLD `markDirtyRange :3407-3416`), `ReadbackEngine::SyncRtToVram` takes a
deposit-base override (`dest_fbp`) for the display path's stand-in deposit (OLD's `SyncRTToVRAM(fbp, target)` pair).

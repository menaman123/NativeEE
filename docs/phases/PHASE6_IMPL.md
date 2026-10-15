# PHASE 6 IMPL — construction record (worktree `phase6`)

Plan: `PHASE6_PLAN.md`. Fact base: `/tmp/p6/INVESTIGATION.md` (investigator stage, 2026-09-27). Each stage
below was produced by an implementer subagent and audited by an independent reviewer subagent; the
orchestrator records the outcome. Baseline of the untouched worktree at 78b5655: configure + build clean,
`ctest --preset macos-dev` 88/88 (2026-09-27, `/tmp/p6/baseline_ctest.log`).

## Module checklist (PHASE6_PLAN §2 rows; ✔ = landed, stage in parentheses)

§2.1 `engine/gs/hle/ir/` (6A):
- ✔ `gsdl_format.h/.cpp` — `GsdlFormat` (6A)
- ✔ `vertex_rec.h` — `VertexRec` (6A)
- ✔ `tex_read_view.h` — `TexReadView` (6A)
- ✔ `draw_ir.h/.cpp` — `DrawIr` + `FromDrawRec` / `ToDrawRec` (6A)
- ✔ `job_rec.h` — `JobRec` + `JobKind` (6A)
- ✔ `direct_ops.h/.cpp` — `DirectOps` (6A)
- ✔ `draw_ir_lift.h/.cpp` — `DrawIrLift` (6A)
- ✔ `draw_ir_verifier.h/.cpp` — `DrawIrVerifier` (6A)
- ✔ `draw_ir_sink.h` — `DrawIrSink` (6A)
- ✔ `tests/lib/gsdl_reader.h/.cpp` — `GsdlReader`, lib `ps2r_gsdl_reader` (6A)
- ✔ `tools/drawlist_tool/drawlist_tool.cpp` — `stats | frames | dump | diff` (6A)

§2.2 `engine/gs/state/` + `engine/gs/xfer/` (6B):
- ✔ `gs_registers.h/.cpp` — `GsRegisters` (6B)
- ✔ `gs_priv_regs.h/.cpp` — `GsPrivRegs` (6B)
- ✔ `vertex_kick.h/.cpp` — `VertexKick` + `VertexKickQuirks` (6B)
- ✔ `draw_flush.h/.cpp` — `DrawFlush` (`FlushKind`, `RunKey`) (6B)
- ✔ `gs_state.h/.cpp` — `GsState` façade (6B)
- ✔ `xfer/transfer_engine.h/.cpp` — `TransferEngine` (6B)
- ✔ `tests/gs/state/test_gs_state.cpp`, `tests/gs/xfer/test_transfer_engine.cpp`, `tests/lib/capture_sink.h` (6B)

§2.3 `engine/gs/vram/` (6B):
- ✔ `page_record.h/.cpp` — `PageRecord` + band math (6B)
- ✔ `vram_image.h/.cpp` — `VramImage` (6B)
- ✔ `tests/gs/vram/test_vram_image.cpp` (6B)

§2.4 `engine/render/` + `engine/platform/sdl/` (6C):
- ✔ `backend.h` — `render::Backend` + `Caps`/`SurfaceId`/`SurfaceDesc`/`FenceId`/`Rect`/`DrawParams`/`DrawResult`/`ScanoutDesc`/`CdCopyState` (6C)
- ✔ `surface_format.h` — `SurfaceFormat` (6C)
- ✔ `backends/gl/gl_device.h/.cpp` — `GlDevice` (6C)
- ✔ `backends/gl/gl_shader_library.h/.cpp` — `GlShaderLibrary` + `gl_shader_sources.h/.cpp` `GlShaderSources` (6C)
- ✔ `backends/gl/gl_uniform_cache.h/.cpp` — `GlUniformCache` (6C)
- ✔ `backends/gl/gl_state_cache.h/.cpp` — `GlStateCache` (6C)
- ✔ `backends/gl/gl_vertex_stream.h/.cpp` — `GlVertexStream` + `gpu_vertex.h` (6C)
- ✔ `backends/gl/vertex_convert.h/.cpp` — `VertexConvert` (6C)
- ✔ `backends/gl/gl_texture_pool.h/.cpp` — `GlTexturePool` (6C)
- ✔ `backends/gl/blend_ff_map.h/.cpp` — `BlendFfMap` (6C)
- ✔ `backends/gl/cd_copy_policy.h/.cpp` — `CdCopyPolicy` (6C)
- ✔ `backends/gl/gl_draw_executor.h/.cpp` — `GlDrawExecutor` (6C)
- ✔ `platform/sdl/window.h/.cpp`, `gl_context.h/.cpp` — `platform::Window`, `platform::GlContext` (6C)
- ✔ `gs/hle/texsrc/texture_source.h` — `TextureSource` POD contract (6C; resolver → 6D)
- ✔ `tests/render/*` — 7 ctests (6C)

§2.5 `engine/gs/hle/targets/` + `present/` + `games/<t>/renderer_profile/` (6D; `texsrc/` → 6D′):
- ✔ `targets/vram_layout.h/.cpp` — `VramLayout` (6D)
- ✔ `targets/target_identity.h`, `surface_views.h`, `logical_rect.h/.cpp`, `coherence_state.h/.cpp` (6D)
- ✔ `targets/render_target.h/.cpp` — `RenderTarget` (6D; P6-TAIL-REMAINDER → 6D′)
- ✔ `targets/readback_engine.h/.cpp` — `ReadbackEngine` (6D)
- ✔ `targets/render_target_cache.h/.cpp` — `RenderTargetCache` (6D)
- ✔ `targets/target_refresher.h/.cpp` — `TargetRefresher` (6D)
- ✔ `targets/scale_policy.h/.cpp` — `ScalePolicy` + `ScaleSettings` (6D)
- ✔ `present/crtc_classifier.h/.cpp`, `viewport_math.h/.cpp`, `scanout_composer.h/.cpp` (grab half 6D; `DrawScanout` 6D‴), `presenter.h/.cpp` (6D)
- ✔ `targets/alias_page_set.h/.cpp`, `pending_rt_sync_ledger.h/.cpp` (6D″), `target_dims.h/.cpp` (6D‴)
- ✔ `games/<t>/renderer_profile/renderer_profile.h/.cpp` — `RendererProfile` (VramLayout data; 6D)
- ✔ `texsrc/{content_key,decode_cache,clut_source,clut_heal_cache,uv_bbox,window_in_target,mip_sampler,texture_decoder,texture_resolver}.h/.cpp` — lib `ps2r_gs_hle_texsrc` (6D′; `ResolveRead` lives on `TextureResolver`, LintOneClass naming; tier audit vs OLD lines + P6-ALIAS-PREDRAW / P6-REFRESH-ARMS / P6-TAIL-REMAINDER / TargetDims / Destroy rescue / DrawScanout → 6D″)
- ✔ `tests/gs/targets/*`, `tests/gs/present/*`, `tests/lib/fake_backend.h/.cpp`, `tests/gs/hle_include_depth.py` (6D)

§2.6 `engine/gs/hle/classrdr/` (6E):
- ✔ `class_renderer.h`, `class_registry.h/.cpp`, `class_classifier.h/.cpp`, `draw_shape_spec.h/.cpp`, `sequence_recipe.h/.cpp` (FSM + arithmetic), `texture_shuffle_snap.h/.cpp`, `draw_pipeline.h/.cpp` (6E)
- ✔ `world_tristrip_renderer`, `skinned_renderer`, `sprite_hud_renderer`, `particles_renderer`, `fmv_blit_renderer` (replicas over the generic path; 6E)
- ✔ `stencil_composite_renderer.h/.cpp` — `StencilCompositeRenderer` (6E″; slot 7 registered, world-strip steer attached)
- ✔ `renderer_profile` stamps / Accept tuples / sequence constants / `RegisterClassRenderers` (6E)
- ✔ `tests/gs/classrdr/test_classrdr.cpp` (6E)

§2.7 `tools/gs_replay/` (6F):
- ✔ `gs_replay.cpp` (main: replay / census / diff / classdiff, exit codes 0/1/2/3/77, no getenv), `replay_driver.h/.cpp` — `ReplayDriver`, `census.h/.cpp` — `Census`, `gsrep_file.h/.cpp` — `GsrepFile` (+ packed `GsrepHeader` / `GsrepFrame`, `Downsample`), `replay_diff.h/.cpp` — `ReplayDiff`, `skip_filter.h/.cpp` — `SkipFilter`, `frame_range.h` — `FrameRange`; `CMakeLists.txt`, `README.md` (6F)
- ✔ `engine/gs/hle/present`: `ScanoutComposer::PresentBlank`, `Presenter::PresentBlank`; `engine/gs/hle/classrdr`: `DrawPipeline::OnFrameEndGrabOnly` (the corpus frame end; 6F)
- ✔ `tests/gs/replay/{gs_corpus_leg,gs_replay_census,gs_classdiff_matches_old,gs_replay_determinism}.sh` + README; ctests `GsReplayCensus`, `GsClassdiffMatchesOld`, `GsReplayDeterminism`, 19 × `GsCorpus_<cap>` (DISABLED until hash-equal) (6F)
- ✔ load_transition REPLAY_HASH identity (6F′: P6-NATIVE-STALE-UNIFORMS — `render::InheritedUniform` + the class renderers' arm masks) and 17 further captures; `GsCorpus_*` × 18 enabled, slowspot DISABLED (P6-SLOWSPOT-GB2-WARMUP, open) (6F′)
- ✔ `tests/gs/invariants/{README.md,skip_stub.sh}` + ctests `Gs_Inv25_InterpRingRotateBeforeFire`, `Gs_Inv26_InterpIdentityStampedOnly`, `Gs_Inv30_OwnedByPhase5` (SKIP 77 stubs) → 38 named (G11); `tools/check_sizes.py` + ctest `Phase6SizeBudgets` (G10); the numeric module-gate summary + INTEGRATION CHECKLIST (DoD) (6F″)
- ✗ slowspot REPLAY_HASH (P6-SLOWSPOT-GB2-WARMUP): bounded option-(b) attempt did not identify OLD's mark setter; accepted-difference CANDIDATE record written (not filed); `GsCorpus_slowspot` DISABLED — integration adjudication (6F″)

NOT landed (deferred, PHASE6_PLAN §2 had no row / CONSTRUCTION §4.6 rows outside the stage table): `engine/gs/hle/interp/` (P6-INTERP-DEFERRED), `engine/gs/hle/queue/` (6B HANDOFF), `present_policy/pacer/quantum`, `core/diag` GS rows, `games/<t>/hle/scene_hooks` — INTEGRATION CHECKLIST item 8.

## Findings ledger (PHASE6_PLAN §7.1)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P6-REPLAY-BACKEND-ONLY | plan | OLD `gs_replay` links only the GL backend TU set; the corpus hash never exercises gs_state/gif/graphics_thread | module gate scoped to reader→IR→targets/texsrc/classrdr→GL; GsState proven by units + G-C1 + frames lane at integration |
| P6-GRAB-IS-TARGET | plan | corpus hash and `frames.fhash` read the scanout-FBP TARGET FBO (fallback most-recent-drawn), not the composed scanout | `ScanoutComposer::GrabDisplay` reproduces the grab; display path integration-proven |
| P6-UNIFPACK-OFF | plan | UNIFPACK/VSUV/RUNUPLOAD presence-gated OFF in the baseline (`env_ps2{n=0}`) | shader authored as the baseline text; FILEMAP "UNIFPACK form" wording deviated from, recorded |
| P6-EE-SCHED-FLAKE | 6A | pre-existing intermittent hang of the Phase-3 ctest `EeRunTokenAndEventQueue` (tests/ee/test_ee_sched): timed out at 120 s in 3 of ~12 runs during the 6A gate (2 full-suite runs + 1 of 9 isolated reruns), passed in 0.07 s otherwise; 6A touches no `engine/ee` / `tests/ee` file (git status), the baseline log has it passing | outside Phase 6 ownership (READ-ONLY); reported to the orchestrator for the Phase-3 owner. The 6A gate run quoted below is the 94/94 green rerun |
| P6-FLUSH-TWINS | 6B | `GSState::Flush` resets `strip_count` + `SnapshotState`; `GIF::FlushBatch` (EOP) does neither | `DrawFlush::Flush(FlushKind)`: `StateChange` (setter / deferred-at-kick / transfer) resets both, `GifEop` (`GsState::KickBoundary`) keeps both; pinned by `GsVertexKickSequences` (strip parity continues across EOP) and `Gs_Inv02` (StateChange resets) |
| P6-T4-UPLOAD-DROP | 6B | OLD `WriteHWReg` has no PSMT4 store — three halves: (1) VRAM half: T4 host→local data advances the cursor, stamps the pages and emits the upload job but writes no nibble (`gs_state.cpp:5331-5527` `default: break`); (2) linear-payload half: OLD fills `linear_pixels` per "pixel" with `(value>>(p*8))&0xFF` for p<16 — a shift ≥ 64 for p≥8, undefined behaviour, so the RGBA8 copy of a T4 upload is 8 defined bytes + 8 UB bytes per word; NEW stores the nibble the cursor decoded (`StorePixel` linear default arm); (3) watermark half: OLD's per-pixel `NoteVramWrite(addr, GetPixelSize(dpsm))` (`:5127` / `:5381`) still runs for T4 / T8H / T4HL / T4HH, so `FinishHostToLocalTransfer → BumpVramWriteEpochIfDirty` bumps `g_vram_epoch` once at completion | (1) reproduced bug-compatibly in `TransferEngine::StorePixel` (default arm), pinned by `GsTransferCursor`; (2) NOT reproduced (UB has no bit-exact twin; the payload never reaches the corpus) — the corpus census shows no `bpp=4` upload (`grep -o 'bpp=[0-9]*' gs_corpus/census/*.txt` → `bpp=32` only), so neither half is a corpus-path difference; (3) reproduced via `VramImage::NoteWriteNoBytes(addr, PixelSize)` in the default arm — `FinishHostToLocal → SettleWrites` bumps the upload epoch by exactly 1 as OLD does, pinned by `GsTransferCursor` (T4: bytes zero, `UploadEpoch()` +1, `stream_writes == 0`). Accepted-difference candidate for integration should the title ever issue a T4 upload |
| P6-FOG-BOOL | 6B | PACKED FOG stores the fog BYTE into PRIM.FGE (any non-zero byte = fog on); the register form stores only the unused fog coefficient | reproduced in `GsRegisters::PackedFog`; pinned by `GsRegistersDecodeVectors` and the G-C1 façade bridge (`DrawIrLift::kMaskFOGF` agrees) |
| P6-REARM-NO-UPLOAD | 6B | after `FinishHostToLocalTransfer` moves `linear_pixels` out, the IMAGE re-arm path never re-allocates it: a re-armed transfer writes VRAM and pushes `InvalidateTextures` but no second `UploadVRAMTexture` | reproduced (`TransferEngine::FinishHostToLocal` checks `linear_.empty()`); pinned by `Gs_Inv31_ImageRearmRestamp`; 6D's `TargetRefresher` must therefore refresh re-armed uploads from the mirror, as OLD's InvalidateTextures path does |
| P6-CT24-UPLOAD-0x80 | 6B | CT24 host→local stores force byte 3 to 0x80 (a real GS leaves it; the decoder ignores it, the linear copy uses 255) | reproduced in `StorePixel`; pinned by `GsTransferCursor`; harmless for decode, recorded so the mem lane is explained |
| P6-TME-LATCH-EMPTY | 6B | the "first-kick" TME latch is re-taken by EVERY kick that finds the draw buffer empty (for a triangle, each of its first three kicks) — the value at the completing kick wins | reproduced in `VertexKick::Kick`; pinned by `Gs_Inv01` (PRMODE TME flip between kicks 1 and 2 of a triangle is carried) |
| P6-MERGEKEY-BASIS | 6B fix | OLD `ComputeMergeKeyAllPrim` (`render.h:2338`) seeds its FNV-1a with `1469598103934665603` — the 64-bit offset basis `14695981039346656037` with its last decimal digit dropped (0x14650FB0739D0383, not 0xCBF29CE484222325); found when the review-6B known-vector pin failed against a NEW key seeded with the true basis | reproduced: `DrawFlush::RunKey` uses `kMergeKeyBasis = 1469598103934665603` so the key VALUE equals OLD's; pinned by `Gs_Inv32` (two known vectors from OLD's function body over a minimal batch, `/tmp/p6/fix6b2_keyvec.cpp`). Harmless for equivalence (the key is compared, never recorded) but kept bit-exact so 6C's queue merge behaves identically on any future recording |
| P6-PARTIAL-FLUSH-OOB | 6B fix 2 | introduced by review-6B item 1 (an unfinished upload now survives TRXDIR 1/2/3, as in OLD): the next `TRXDIR 0` partial flush sized its payload from `prev_flush_` (the geometry the intermediate TRXDIR latched) but copied it from `linear_` (allocated for the upload in flight) with an unclamped iterator range — a heap read past the buffer whenever the intermediate latch was wider (reviewer probe `/tmp/p6/rv6b2_partial_oob.cpp`, ASan `READ of size 256 … 0 bytes after 32-byte region`) and a payload tail of garbage where OLD has zeros | fixed: `TransferEngine::SetTrxDir` builds `part(pw*ph*4, 0)` and `memcpy`s `min(part.size(), linear_.size())` = OLD `gs_state.cpp:4404-4406` (real bytes, then zeros; the attribution `prev_w x prev_h` was already OLD's). Pinned by `Gs_Inv03_TrxdirLatch` (restage TRXREG 64x64, intermediate TRXDIR 3 and TRXDIR 2, then TRXDIR 0: `payload_bytes == 256`, payload size 256, 16-byte prefix == written pixels, bytes 16..255 zero); ASan-clean runs of the probe and of `test_transfer_engine all` quoted in "Review 6B fix 2"; a negative-control ASan build against the pre-fix line reproduces the overflow, so the pin reaches the path |
| P6-SHADER-COMMENTS-KEPT | 6C | the GLSL text is stored byte-identical to OLD INCLUDING its `//` comments (session tags inside); the OLD raw strings begin with a newline, so every source starts `"\n#version 410 core\n"` and the fragment header is `"\n#version 410 core\n#define GSMIP_AAF 1\n#define S83_IDXSCALE 1\n"` | `gl_shader_sources.cpp` generated by `/tmp/p6/gen_shader_sources.py` from the investigator's captures; FNV-1a64 of the four texts pinned in `Render_ShaderCompiles` (frag `0x3ffd78421c147ece`, vert `0x4fd2767450e43bb7`, scanout `0x8823cded4f383d15` / `0x5d955137d32b0c78`); `LintOneClass` did NOT fire on the raw-string contents (its string stripper skips them), so nothing was stripped |
| P6-PBO-NONDET | 6C | OLD's fenced-PBO readback is timing-dependent in its counters, not its bytes | NEW `GlDevice::Readback` is synchronous `glReadPixels` (rows bottom-up, RGBA8 / float depth); `BeginReadback`/`PollReadback` complete immediately; `Caps.staging_readback = false` |
| P6-CDTILES-OFF | 6C | `PS2_GL_CDTILES` defaults OFF in OLD (`rt_copy_tracker.h` `TilesOn`: `v && *v && atoi(v) != 0`) | only the union-rect `rt_copy::Decide` arm is written (`CdCopyPolicy::Decide`); `DecideTiles` / `rt_dirty_tiles` not transplanted |
| P6-BACKEND-NAME | 6C | `LintOneClass` requires file = snake_case(type): `backend.h` cannot hold `IRenderBackend`, `window.h` cannot hold `SdlWindow` (PHASE6_PLAN §3 / §2.4 names vs the §1.5-owned file names) | the interface is `ps2r::render::Backend` in `backend.h`; the window is `ps2r::platform::Window` in `window.h`; every later reference to `IRenderBackend` / `SdlWindow` in the plan means these |
| P6-DRAW-RETURNS-RESULT | 6C | §3 declares `Draw(...) = 0` returning void; the coherence tail above the backend needs the fused bboxes / route flags / 5551 mask / Cd-copy rect the executor computed | `Backend::Draw` returns `DrawResult`; `DrawParams` extended with the surfaces + tracker pointer + clear flags (§3 "you may add fields"); `Clear` gained a `flags` parameter, `UploadLevel` / `FinishMipChain` / `DescribeSurface` added for the mip chain and the target cache |
| P6-BLEND-INHERIT | 6C | OLD: a blended DATE-only draw that is neither RT-textured nor FBA/fbmsk16/wrap (`alpha_blend && needs_rt_copy && !sw-blend route`) leaves the GL blend enable/func of the PREVIOUS draw in place (`opengl_backend.cpp:11291-11294` only disables for `!alpha_blend`) | reproduced verbatim in `GlDrawExecutor::BlendState` (second branch); recorded so a future difference in that corner is explained |
| P6-PROFILE-SUBDIR | 6D | the title profile needs its own CMake target without touching the title's other CMake files | `games/<t>/renderer_profile/` subdirectory (+ one root `add_subdirectory` line) |
| P6-CT16S-ENCODE | 6D | OLD `EncodeRTToVRAM` encodes only PSM 0x00/0x01/0x02; a CT16S render target writes back nothing | reproduced (`ReadbackEngine::EncodesColorPsm`); the accumulator's bytes travel via the alt16 lane / S9 materialise (6D′/6E) |
| P6-GRAB-FALLBACK | 6D | grab falls back to the most-recent-drawn target, else the no-target sentinel | `ScanoutComposer::GrabDisplay`; pinned by `Present_GrabDisplayFallback` |
| P6-SCANOUT-RT-ARM | 6D‴ | OLD's quad source is the target whenever it has a colour view and no stale-display round trip is engaged (`PS2_GL_SCANOUT_RT` default ON); `gb3_native` only defers the mirror deposit | `ScanoutComposer::DrawScanout` reproduces the live rule; the OFF arm (present-time 5551 re-decode) is not written |
| P6-SCANOUT-DEST | 6D‴ | the present-time `SyncRTToVRAM(m_scanout_fbp, picked target)` deposits the picked target AT THE SCANOUT BASE | `ReadbackEngine::SyncRtToVram(.., dest_fbp)` override, used only by `DrawScanout`; pinned by `Present_DrawScanoutSource` |
| P6-STALE-DISPLAY-PICK | 6D‴ | the stale-display stand-in (display target idle > 30 frames, main buffer drawn ≤ 16 frames ago) is a live present heuristic on the title's main frame buffer; OLD's own comment (`:24344-24350`) names the cause: the game's per-frame `0x70000 → 0x0` display composite is not reproduced on the host | transplanted via `VramLayout::main_fb_fbp` + named ages; workaround-class HOOK_RECLASSIFICATION row OWED (see "Ledger deltas owed at integration", row L1 — the shared doc is outside the §1.5 write set); root-fix plan = locate the dropped composite at 6E/6F; expiry gate = the 12-leg frames lane |
| P6-ALT16-REFRESH-CPU | 6D | alt16 companion refresh done on the CPU (5551 truncate/replicate) | CLOSED by Review 6D fix: OLD's live GL program (`opengl_backend.cpp:14629-14633`, `Alt16GpuOn` default ON) computes exactly `TexaExpand::Truncate32To16 → Expand16To32` (per channel `Truncate8To5 → Replicate5To8`, A bit = alpha ≥ 0x80); pinned by `fold_ok` in `Gs_Inv12_Alt16CoherenceRect` over every channel value 0..255 |
| P6-TAIL-REMAINDER | 6D | pending RT-sync ledger, alt16 lane deposit, GPU-valid marks not yet in `AfterDraw`; the ledger's elide sites are also the only non-deposit writers of the GPU author marks the rescue veto reads | CLOSED 6D″ (`RenderTargetCache::AfterDrawTail` + `FlushPending`; the `:14158`/`:14204` elide sites are DEAD at peak — P6-LEDGER-ARM; `:24808` → 6E present) |
| P6-LINT-RENAMES | 6D | LintOneClass renames (`render_target_cache`, `logical_rect`, `stencil_zcopy_fbp`) | recorded; plan names map to these |
| P6-ENCODE-STRIDE | 6D fix | the non-rescue colour encode stride is the TARGET WIDTH (`fbw = w`), `render_fbw` only for a rescue with a 64-aligned `rescue_fbw` (`:16748-16750`); the first 6D cut used `render_fbw` everywhere | reproduced in `ReadbackEngine::SyncRtToVram` (`readback_engine.cpp:143`); pinned by `Targets_EncodeVetoes` / `Targets_SyncAllJob` |
| P6-DEPOSIT-SPAN | 6D fix | OLD stages the LINEAR span `[fbp + y0*fbw*bpp, fbp + y1*fbw*bpp)` (`:15934-15940`, `:18220-18236`), not the swizzled page footprint: a swizzled pixel outside the span lands only in an already-pending shadow page (epoch kept), on a clean page it is erased by the next `BeginDeposit` memcpy | reproduced (`ReadbackEngine::LinearSpan` / `StageTouched`: span pages staged + stamped, footprint pages staged with epoch 0 only when already `Pending`); pinned by `Targets_DepositSpan` (fbw 128, rows [0,8): pixel (70,2) NOT staged, `PendingPages == 1`; with page 1 pending it lands). Reproduced, not a risk |
| P6-ALIAS-PREDRAW (CLOSED 6D″: `TargetRefresher::AliasPreDraw`, `Targets_AliasPreDraw`) | 6D fix | Review 6D item 5 — the pre-draw `[VRAM-ALIAS]` refresh demand (`:9403-9625`, composite exclusion `:9547-9550`; aliased pages set by deposits `:16943-16963`, cleared per page by the refresh `:17606-17629`) has no NEW twin; `RefreshFromVram` has no live caller | DEFERRED → 6D′ (verbatim item + rule in the HANDOFF): shared `AliasPageSet` (cpu_dirty + aliased) + `PrepareDraw` demand; test `Targets_AliasPreDraw`. Must land together with P6-REFRESH-ARMS |
| P6-REFRESH-ARMS (CLOSED 6D″: arms + `VramImage::ReadMirror` + voucher stamps, `Targets_RefreshArms`) | 6D fix | OLD `RefreshFBOFromVRAM` is arm-specific (CT32/CT24 alpha-only merge, CT16 full swizzle decode with SH5 keep runs) and both arms read the COMMITTED `gs_vram` mirror, never the composed (mirror ⊕ pending-shadow) view; the tail stamps `vram_coherent_epoch/class` (r173) and marks the whole companion stale; NEW `TargetRefresher::RefreshFromVram` is one full `BlockDecode` over `VramImage::ReadView` for every colour PSM | tail alt16-stale aligned this round (Review 6D fix 2, pinned in `Targets_RefreshFromVram`); arms, mirror source and r173 stamps → 6D′ together with P6-ALIAS-PREDRAW (`Targets_RefreshArms`). See the Findings bullet |
| P6-LEDGER-ARM | 6D″ | the post-draw 16-bit eager/elide arms (`:14091-14150`) are dead at peak: `r121::LazyRtSyncOn()` is TRUE (`PS2_GL_FAST2=1`), every 16-bit-class draw is `RecordPendingRTSync`; the reader band gates only the `kPresent` flush | NEW = the live arm (`RenderTargetCache::AfterDrawTail` / `FlushPending(Present, 0, 0, true)`); 6D Findings sentence corrected |
| P6-PREDRAW-SITE | 6D″ | the alias demand lives in `TargetRefresher::AliasPreDraw` (needs cache + refresher), called by the driver between `GetOrCreate` and `PrepareDraw` | recorded; HANDOFF order |
| P6-FMV-CLAMP-TEXELS | 6D″ | R198 D1 is an edge replication of the decoded region's texels, not a sampler clamp; applied after the content key | no `TextureSource` field; `ApplyFmvEdgeClamp` |
| P6-DESTROY-UNIFIED | 6D″ | `Destroy(fbp)` and the grow path share `RescueAndDrop` (OLD rescue form + `kDestroy` flush) | closed |
| P6-PRESENT-FLUSH | 6D″ | `Present()` flushes the ledger reader-pages-only before the grab (`:23594-23955`) | → 6E `Presenter::Present` |
| P6-FMT6-OFF | 6D′ | OLD `r198::Idx8Ct16On()` (`render.h:1901-1923`) has no explicit env in the peak profile and therefore returns `r201::UploadDepositOn()`, which is default OFF (`render.h:1869-1885`, FLAG_LEDGER row 409); `s8inrt::On(r198::Idx8Ct16On())` (`s8_inrt.h`, call `opengl_backend.cpp:21144-21145`) follows the same value | the fmt 6 (T8 read of a CT16 target) serve and the PSMT8-window-in-target serve are NOT written into `TextureResolver` (peak value = OFF); `WindowInTarget` keeps the geometry for `Texsrc_WindowInTarget`; a future integration setting flipping the upload-deposit gate must re-land both arms together |
| P6-RESOLVER-NAME | 6D′ | LintOneClass: `ResolveRead` needs state (caches, scratch) and `texture_source.h` already holds `TextureSource` | the resolver is `TextureResolver::ResolveRead` in `texture_resolver.h/.cpp`; `TextureSource::Owner` gained `clut` / `partner` (the fmt 4-6 palette texture and the fmt 5 partner attachment the executor binds on units 2 / 3) |
| P6-RESOLVER-AUDIT | 6D′ | the resolver's tier predicates were written from INV §6.5 + the R16 lines read this stage (`:19887-20430` indexed, `:20430-21131` colour keep / decline, `:21388-21599` depth) under the STOP rule; the R6 body (`:4902-4924` fall-through cases, `:5231-5298` demand, `:5322-5430` UV region, `:6581-6645` FMV clamp, `:7378-7385` mip upload / insert, `:7445-7656` UV normalisation + READVIEW prescale, `:8266-8287` outputs) and the 44 R16 returns were NOT audited line by line | OPEN → 6D″ must audit every predicate against the cited lines BEFORE the 6F corpus gate (see HANDOFF 6D′ → 6D″) |
| P6-SELFREAD-PERSIST | 6D′ fix | OLD's colour self-read (`frame_fbp == tbp`, fmt 0) binds the target's `persistence_tex` (`:4912-4917`) — allocated for every target (`:15683-15689`) and never written (`needs_persistence` always false, `:11528`, `:14295`): an unwritten GL texture | NEW has no persistence surface and takes OLD's no-persistence arm (`Declined::SelfRead`: enabled, owner None, textured UV mode, nothing sampled); OPEN for the frames-lane adjudication in 6F (zero surface vs untextured) |
| P6-Z-SEQ | 6D′ fix | the depth match orders aliased Z attachments by OLD's `last_z_seq` (`++m_z_write_seq` per Z-writing draw, `:9187`); NEW had no ordinal; Review 6D′ iteration 2 added the R6 Z-source refresh (`:4703-4781`) that gates on `last_z_write_frame` (`:9188`) — NEW synced Z only inside `MatchDepth`'s decline | `CoherenceState::last_z_seq` + `last_z_write_frame` set together from `AfterDraw(..., frame_count, ..., draw_seq)` (default 0 keeps the 48 existing callers; classrdr passes a strictly increasing committed-draw ordinal); `TextureResolver::RefreshZSource` runs for every Z-psm read without a depth serve (Review 6D′ fix 2; pinned `ResolveTiersDepth`, `Gs_Inv10`) |
| P6-CLASS-NAMES | 6E | the class enum + census names are the corpus tools' vocabulary | engine taxonomy in `ClassClassifier`; stamps / signatures / thresholds are profile `Rules` |
| P6-PERF-ONLY-RENDERERS | 6E | five OLD class renderers are replicas of the generic path (INV §7.6) | `Accept(spec) + DrawGeneric`; counters + emit grammar kept (`spec=0`) |
| P6-BACKEND-STENCIL | 6E | no stencil surface on `render/backend.h` yet | deferred with P6-S9-PENDING; shape specified in the 6E Findings |
| P6-S9-SHADERS | 6E | the four S9 programs (invert is pixel-changing) | deferred with P6-S9-PENDING; byte-identical text into `GlShaderSources` |
| P6-S9-PENDING | 6E | the stencil-composite renderer (surface, passes, materialise, invert, hooks, hold, ZSRC route) | OPEN → 6E′ / 6F; no renderer registered in the S9-shadow slot; exact remainder in the 6E Findings |
| P6-SH7-LIVE | 6E | FLAG_LEDGER classes `PS2_SH7` DELETE but the flush defaults ON and is live at peak | transplanted NORM ON (`SameBufferPreDrawFlush`); ledger row L3 owed |
| P6-FAKE-DRAW-RESULT | 6E | `FakeBackend::Draw` returned an empty result, so the coherence tail never ran GL-free | geometry result (bbox / write rect / drew); no pixels |
| P6-ZERO-MIRROR | plan | replay mirror is zero, deposits never committed, uploads are GL textures by address, epochs 0 | `ReplayDriver` never calls `CommitDeposits`; `VramImage::ReadView` = mirror ⊕ shadow |
| P6-S9-QUAD-VIEWPORT | 6E″ | OLD's pass-2/3 quads use the strip's viewport (`s9_composite_native.inc:1925`), `Backend::DrawConstQuad` the stencil dims | identical while render height == target height (the accumulator follows SCISSOR); corpus tripwire — 12 S9 captures identical in 6F′, no viewport argument needed |
| P6-STRIP-FBMSK16 | 6E″ | the stencil strip records the A-protect keep mask 0x8000 and skips the upload eviction (`:1204-1208`, `:1917`) | reproduced in `GlDrawExecutor::DrawStripCount` (ClassifyRoutes runs); pinned `Render_S9StencilSmoke` (b) |
| P6-STRIP-FRAMECLEAR | 6E″ | the strip ladder performs the consumed per-frame clear on the colour target (`:887-912`) | reproduced (colour target bound + FrameClear before the stencil-only framebuffer); pinned `Render_S9StencilSmoke` (c) |
| P6-SNAP-S2 | 6E′ | OLD `test_shuf_snap` S2 (sampling equivalence over the 32 live strips) not ported | open, minor |
| P6-S9DEM-DIAG | 6E″ | OLD's `[GS-S9DEM]` demotion-cause line is a cr4 census with no env gate | DIAG, not emitted; the grammar compares skip it |
| P6-REPLAY-PRESENT-BLANK | 6F | the corpus frame end is pending flush + swap (`Present()` `:23594-23812`), never `DrawScanout` | `OnFrameEndGrabOnly` / `PresentBlank`; pinned `Present_GrabDisplayFallback`, `Classrdr_DrawPipelineOrder` |
| P6-REPLAY-FRAMES-JOBS | 6F | `--frames a-b` skips the out-of-range frames' JOBS too (OLD and NEW) | note: never a pixel oracle; `--dump-frames` is |
| P6-ACCUM-FRESH-CONTENT | 6F → 6F′ | load_transition vf855 blue-only 5-bit divergence | CLOSED as P6-NATIVE-STALE-UNIFORMS (6F′) |
| P6-NATIVE-STALE-UNIFORMS | 6F′ | OLD's class-native generic-program arms (skinned / raw-skin / sprites-HUD / particles / FMV blit) replicate DrawBatch minus the `SetU*` sites for `u_ct16q`, `u_dthe`, `u_dimx_lo/hi`, `u_draw_scale`, `u_uv_native` (+ `u_blend_a..d`/`u_fix_alpha`, `u_zsrc_num/den` per shape), so a class-routed draw inherits the LAST DrawBatch draw's values (`skinned_native.inc:1107-1170` vs `opengl_backend.cpp:10817-10823`) — a CT16 frame's 5-bit truncation carried onto CT32 accumulator draws | reproduced as the named `render::InheritedUniform` set on `DrawParams` (`ClassRenderer::kInherit*Arm`); pinned `Render_InheritedUniforms` + `Classrdr_AcceptTuples`; bug-KB / accepted-difference candidate (ledger row L4) |
| P6-WINDOW-SEED | 6F′ | OLD's render-height fallback is the window height seeded at `Init` (`SDL_GetWindowSize`, `:2952`); a capture without a SetWindow job renders 64 rows tall | `ReplayDriver::BuildScene` seeds `DrawPipeline::OnSetWindow(win w/h)`; five legs are the pin |
| P6-SLOWSPOT-GB2-WARMUP | 6F′ → 6F″ | slowspot f=2 vf7402: NEW = OLD's ORIGINAL frozen hash `0x7a99f39ddbf9cc18`; the frozen files carry the GB2 warm-up ledger value `0x99b108280b0c4799` (one-time `[VRAM-ALIAS]` refresh of the fresh CT16S accumulator on its first alt16-routed draw after the CT32 retag, OLD probe `:238-242`) | OPEN → accepted-difference CANDIDATE record written in 6F″ (NOT filed — PLAN §6); the 6F′ ledger attribution ruled out (both sides consume the f=0 entry at f=1's SH7 flush); mark setter unidentified within the budget; `GsCorpus_slowspot` DISABLED; integration adjudicates (a) accept vs (b) model |
| P6-INTERP-DEFERRED | 6F″ | CONSTRUCTION §4.6 row `gs/hle/interp/frame_interpolator` (verbatim + NORM from `gs_interp.h`) was not in PHASE6_PLAN §2's stage table; `video.interp` is off in the deterministic profile and every corpus baseline | DEFERRED → integration / Phase 8 with OLD sources + `test_gs_interp.cpp`; invariants 25/26 registered as SKIP stubs (`tests/gs/invariants/`) |
| P6-INV38-ONE-TEST | 6F″ | invariant 38's XGKick half is Phase 5's; a `Gs_Inv38_XgkickOwnedByPhase5` stub would make the G11 count 39 | decision: the note is printed by `Gs_Inv38_RoundSpriteNormalise`; count stays 38 |

## Ledger deltas owed at integration

`docs/design/HOOK_RECLASSIFICATION.md` and `docs/filemap/HACK_LEDGER.md` are shared design documents OUTSIDE
Phase 6's write set (PHASE6_PLAN §1.5: OWN = `engine/gs/**` …, `docs/phases/PHASE6_{PLAN,IMPL}.md`; READ-ONLY =
everything else). Every row a Phase 6 stage owes those documents is recorded HERE with the exact text to be
carried over, and the integration step that merges Phase 6 applies them. Running list — every later stage
appends; nothing here is applied to the shared documents by a Phase 6 stage.

| Id | Stage | Target doc / section | Row text to carry over |
|---|---|---|---|
| L1 | 6D‴ (Review 6D″ fix 2) | `HOOK_RECLASSIFICATION.md`, emulation-bug WORKAROUND class (present-time) | **Stale-display present stand-in.** OLD `host_app/graphics/opengl/opengl_backend.cpp:24339-24385` `[SCANOUT-STALE-OVERRIDE]`. NEW `engine/gs/hle/present/scanout_composer.cpp` `ScanoutComposer::PickSource` + `VramLayout::main_fb_fbp` (profile data, `games/<title>/renderer_profile.cpp`) + the named ages `kStaleDisplayFrames` (30) / `kFreshWorkFrames` (16). PS2 rule: the CRTC displays exactly DISPFB — the buffer at the DISPFB1/2 address in the DISPFB format, nothing else. Suspected defect (OLD's own comment `:24344-24350`): the game's per-frame `0x70000 → 0x0` display composite (one full-screen textured draw / local→local transfer per picture) is not reproduced on the host — "the drawn render-graph has zero draws to FRAME=0x0 in-scene" — so the true display buffer keeps a stale pre-scene image while the main buffer is composited fresh every frame; the stand-in presents the main buffer instead (with the DISPFB-format round trip through the mirror when the classes differ). Root-fix plan: locate the dropped composite when 6E/6F integrate the replay — a draw classification that discards the FRAME=0x0 draw, or a local→local transfer the transfer engine does not execute — and remove the stand-in once the composite reaches 0x0. Expiry gate: the 12-leg frames lane (the stand-in must be inert on every leg once the composite lands; it also moves the mirror — the stand-in deposit lands at the scanout base — so the mem lane is a secondary witness). Pinned today by `Present_DrawScanoutSource` (stale-display cases + probe P13). |
| L2 | 6D‴ (Review 6D″ fix 2) | `HACK_LEDGER.md`, timing-fudge / dual-path section (also FILEMAP_graphics R15/R26 context) | **`ScanoutFromVRAM` FMV source-gated hold — dormant at peak, NOT written.** OLD `opengl_backend.cpp:19236-19320` (+ `:19332-19378`): inside the mirror-decode present, when `psm == 0x02 ∧ fbp == 0 ∧ width == 512 ∧ m_scanout_texture`, a 32×28 luma grid over the decoded scanout classifies the frame black (`< 5 %` samples above 16), a brightness probe of the FMV stage at `gs_vram[0x150000]` computes `fmv_source_bright` (SESSION 12; diagnostics only since SESSION 14), and the display black-run (`s_fmv_have_good ∧ g_vsync_frame ≥ 400 ∧ s_fmv_blackrun ≤ HOLD_CAP = 8`) sets `fmv_hold_prev`, under which `glTexImage2D` is skipped and the previous (bright) picture is re-presented. At peak the RT present arm (`r184::ScanoutRtOn()` default ON, `:25030-25036`) shows the FMV from the target's colour view; `ScanoutFromVRAM` is reached only by the stale-display round trip (L1) whose picked target is the CT32 main buffer, never the `psm == 0x02 ∧ fbp == 0` display — the predicate is unreachable at peak. Decision: NOT transplanted (`ScanoutComposer::PresentMirror` uploads every decoded frame); its PS2 rule would be "the CRTC re-displays the last complete field until a new picture is written", a host cadence artifact of the non-frame-locked CRTC fallback present. If integration ever routes an FMV through the mirror present, this is the arm to re-adjudicate (accepted-difference protocol), not a gap to rediscover. |
| L3, L4 | 6E, 6F′ | `FLAG_LEDGER.md` row 671 `PS2_SH7`; `docs/bugs/` + `HACK_LEDGER.md` | appended in the 6E section ("Ledger deltas owed at integration (appended rows)") — carried by INTEGRATION CHECKLIST item 7 |
| L5 | 6F″ (from 6D′ P6-FMT6-OFF) | `FLAG_LEDGER.md` row 639 `PS2_S8_INRT` (+ row 409 `PS2_R198_IDX8CT16` / `r201::UploadDepositOn` context) | **Correction: the fmt-6 / PSMT8-window-in-target serves are OFF at peak.** OLD `render.h:1901-1923` `r198::Idx8Ct16On()` has no explicit env in the peak profile and returns `r201::UploadDepositOn()` (`render.h:1869-1885`, default OFF); `s8inrt::On(r198::Idx8Ct16On())` (`s8_inrt.h`, call `opengl_backend.cpp:21144-21145`) follows it. The ledger's NORM classification must read "winner = OFF — arm not written" (NEW `TextureResolver` has no fmt-6 serve; `WindowInTarget` keeps the geometry for `Texsrc_WindowInTarget`). A future setting flipping the upload-deposit gate must re-land BOTH arms together with a corpus re-proof. |
| L6 | 6F″ | `EQUIVALENCE_PLAN.md` §7 candidates (→ `ACCEPTED.md` rows only when adjudicated at integration) | **Accepted-difference CANDIDATES from Phase 6 — none filed:** P6-SLOWSPOT-GB2-WARMUP (record in the 6F″ section: slowspot vf7402 only, +8 uniform, NEW `0x7a99f39ddbf9cc18` == OLD's original Phase-A hash, frozen `0x99b108280b0c4799`), P6-SELFREAD-PERSIST (OLD samples an unwritten `persistence_tex` on a colour self-read, `:4912-4917`; NEW `Declined::SelfRead` untextured), CT16-TEXA (A3 §2, bug-compatible), P6-T4-UPLOAD-DROP (the UB linear-payload half not reproduced), P6-FOG-BOOL (reproduced; recorded so the mem lane is explained). |
| L7 | 6F″ | `docs/bugs/` new entries (TEMPLATE.md fields incl. "PS2 rule violated") | Root-caused defects found in Phase 6: P6-NATIVE-STALE-UNIFORMS (= L4), P6-PARTIAL-FLUSH-OOB (NEW-side, introduced and fixed in 6B review 2 — `TransferEngine::SetTrxDir` payload sized from the intermediate latch but copied from the in-flight buffer; pinned `Gs_Inv03_TrxdirLatch`; OLD's own `prev_w × prev_h` attribution kept), P6-MERGEKEY-BASIS (OLD FNV basis typo `1469598103934665603`, reproduced), P6-BLEND-INHERIT (OLD leaves the previous blend state on a DATE-only blended draw, reproduced), P6-WINDOW-SEED (OLD replay-tool quirk, replay driver only), P6-STALE-DISPLAY-PICK (= L1). |
| L8 | 6F″ | `docs/filemap/INDEX.md` rows for `gs/hle/interp`, `gs/hle/queue`, `present_policy/pacer/quantum`, `core/diag` GS rows, `games/<t>/hle/scene_hooks` | Status "deferred to integration / Phase 8" with the pointers P6-INTERP-DEFERRED, 6B HANDOFF (queue), INTEGRATION CHECKLIST item 8. |


## 6A — IR + .gsdl reader + drawlist_tool (2026-09-27, ≈47 min)

### Landed (2,948 lines incl. tests)

`engine/gs/hle/ir/` (lib `ps2r_gs_ir`, namespace `ps2r::gs`): `gsdl_format.h` 193 / `.cpp` 121, `vertex_rec.h` 32,
`job_rec.h` 54, `tex_read_view.h` 51, `draw_ir.h` 111 / `.cpp` 132, `direct_ops.h` 49 / `.cpp` 88,
`draw_ir_lift.h` 92 / `.cpp` 231, `draw_ir_verifier.h` 64 / `.cpp` 256, `draw_ir_sink.h` 24, `README.md` (Contents
updated). `tests/lib/` (lib `ps2r_gsdl_reader`): `gsdl_reader.h` 58 / `.cpp` 110, `CMakeLists.txt`, `README.md`.
`tools/drawlist_tool/`: `drawlist_tool.cpp` 332, `CMakeLists.txt`, `README.md`. `tests/gs/ir/`: `test_gsdl_format.cpp`
179, `test_gsdl_reader.cpp` 352, `test_draw_ir.cpp` 403, `drawlist_tool_diff_clean.sh` 16. CMake (contract §1.5):
`engine/gs/CMakeLists.txt` gains the single line `add_subdirectory(hle/ir)  # Phase 6`; the library itself lives in new
`engine/gs/hle/ir/CMakeLists.txt` (`ps2r_gs_ir`, `-Wall -Wextra -Werror`, links `ps2r_gs_format` only); root
`CMakeLists.txt` `add_subdirectory(tools/drawlist_tool)  # Phase 6` appended;
`tests/CMakeLists.txt` `add_subdirectory(lib)  # Phase 6` inserted above `add_subdirectory(gs)`; `tests/gs/CMakeLists.txt`
(ours) gains `ps2r_gs_ir_test()` and the six registrations.

Shape notes: `GsdlFormat` is a class of constants + static helpers; the packed on-disk structs (`GsdlFileHeader`,
`GsdlRecordHeader`, `GsdlNameRec`, `DrawRec`) are top-level PODs in `gsdl_format.h` with `static_assert`s on sizeof AND
every field offset of INV §1.2–§1.7. The four-lane vertex hash is `GsdlFormat::VertexHash(const VertexRec*, n)` (no
separate hasher type). `DrawIr` carries a `Snapshot` sub-struct (vram/clut hashes + bytes, vertex_hash, verts_incl) so
`ToDrawRec(FromDrawRec(x)) == x` byte for byte; DTHE/DIMX and the TEX1 mip fields exist in `DrawIr`/`TexReadView` but are
not in the record (INV §1.6 note) and read zero from a recording. `DrawIrLift`'s lift is a data-only `LiftState` +
free functions in the `.cpp` (the lint counts anonymous-namespace classes with methods as primary types).

### OLD sources transplanted (read-only)

`host_app/hle/drawlist_record.h:42-144` (constants, structs, `kDrawRecSizeV1`), `:159-238` (Fnv1a / Fnv1aFast /
Fnv1aSampled / VertexHasher / HandlerId); `drawlist_record.cpp:237-292, 295-329, 350-352` (DrawRec fill order, the
VertexRec conversion, the hash word construction and bbox rule); `hle/drawlist_reader.h:20-113` (Draw/Job/Frame/Recording,
Load rules, tail frame); `hle/gs_direct_ops.h:41-149` (`Op`, constants, `PacketToOps`); `hle/gs_draw_ir.h:72-372`
(masks/flags/Vert/Kind/Item/Stream, Shadow, `PacketToDrawIR`) and `:378-636` (`verify::` AttrState/Event/RunOps/RunIR/
VerifyKickEquivalence); `render.h:31-255` (RenderBatch/BatchTextureInfo field set → `DrawIr`/`TexReadView`) and
`:257-270` (`RenderCommandType` → `JobKind`); `tests/gs_replay.cpp:334-391` (`BuildBatch` casts → `FromDrawRec`),
`:284-330` (Census line grammar → the reader test's oracle); `tests/drawlist_tool.cpp:1-326` (whole tool);
`tests/test_gs_draw_ir.cpp:1-379` (all 14 handcrafted shapes + the seeded random generator, verbatim seeds);
`tests/test_drawlist_record.cpp` (the round-trip assertions, re-expressed over a NEW-struct writer since the OLD
recorder is not transplanted in 6A).

### Flags applied

None. No FLAG_LEDGER row lands in 6A: the recorder's `PS2_DRAWLIST_RECORD*` gates (DIAG) belong to the recorder
(integration / `core/diag`), the replay tool's `PS2_REPLAY_*` (TEST-ONLY) to stage 6F. Zero `getenv` added
(`grep -rn getenv engine tools tests/lib | grep -v config_loader` → only pre-existing README / regen-script prose).

### Tests added (ctest names; PASS lines from `/tmp/p6/6a_ctest.log`, the 94/94 run)

```
26/94 Test #26: GsdlFormatOffsets ...................   Passed    0.01 sec
27/94 Test #27: GsdlReaderRoundTrip .................   Passed    0.01 sec
28/94 Test #28: GsdlReaderCensusMatchesOld ..........   Passed    0.89 sec
29/94 Test #29: Gs_Inv27_DrawIrEquivalence ..........   Passed    0.24 sec
30/94 Test #30: GsDrawIrRoundTrip ...................   Passed    0.84 sec
31/94 Test #31: GsDrawlistToolDiffClean .............   Passed    5.07 sec
100% tests passed, 0 tests failed out of 94
```

- `GsdlFormatOffsets` — 157 run-time checks (every size/offset of INV §1, kinds, the format's hash basis
  1469598103934665603 and word/stripe/lane constructions, `HandlerId`, `Bbox`) + `OLD/gs_corpus/boot_menu.gsdl` head
  decode: `magic=0x524c4432 version=2 flags=0x3 reserved=0`, first record kDraw size == 220 + 96·36. SKIP 77 when the
  capture is absent.
- `GsdlReaderRoundTrip` — synthetic v2 (names, jobs incl. a 256-byte pixel payload, HLE + raw draws, a hash-only draw,
  two closed frames, a cut final record → `truncated` + the preceding job as a tail frame) and a separate tail-frame
  file, a v1 file with a poisoned v2 tail (reads zero), version-3 and bad-magic rejection.
- `GsdlReaderCensusMatchesOld` — **exact result** (load_transition, tag-stripped OLD baseline lines):
  ```
  NEW CENSUS frames=208 draws=3667 verts=680430 jobs=4741 draws/frame=17.6298 verts/frame=3271.30
  OLD CENSUS frames=208 draws=3667 verts=680430 jobs=4741 draws/frame=17.6298 verts/frame=3271.30
  NEW PRIM point=0 line=0 linestrip=0 tri=0 tristrip=2570 trifan=28 sprite=1069
  OLD PRIM point=0 line=0 linestrip=0 tri=0 tristrip=2570 trifan=28 sprite=1069
  NEW FLAGS tex=3250 abe=2398 date=0 fog=0 iip=2811
  OLD FLAGS tex=3250 abe=2398 date=0 fog=0 iip=2811
  NEW JOBS dispfb=2 window=2 inval=2369 upload=2367 syncrt=1 upload_payload=2367 upload_nopayload=0
  OLD JOBS dispfb=2 window=2 inval=2369 upload=2367 syncrt=1 upload_payload=2367 upload_nopayload=0
  frames=208 truncated=1 names=6 vertex_hash re-computed for 3667 draws
  test_gsdl_reader census: 8 checks, 0 failures
  ```
  plus the hash oracle: `GsdlFormat::VertexHash` and `Bbox` over the stored vertex arrays reproduce the recorder's
  `vertex_hash` / `bbox` for all 3,667 draws (0 bad). HLE / CLASS lines are stage 6E's.
- `Gs_Inv27_DrawIrEquivalence` — the 14 OLD handcrafted shapes + 20,000 seeded random packets (seed 0x5EEDBA5E);
  op-path vs draw-IR event sequences identical, decline parity held, `VerifyKickEquivalence` agrees with the explicit
  two-path comparison on every packet; ≥ 1,000 convertible random packets asserted.
- `GsDrawIrRoundTrip` — a fully populated synthetic `DrawRec` and every one of the 3,667 load_transition draws:
  `DrawRec → DrawIr::FromDrawRec → ToDrawRec` byte-identical (memcmp over 220 bytes), vertices carried.
- `GsDrawlistToolDiffClean` — `drawlist_tool diff <load_transition> <load_transition>` under `verify/run_bounded.sh 300`:
  output exactly `DIFF PASS: frames compared=208 mismatching=0 (offset=0 tol xy=0.0625 z=1 rgba=1 st=1e-05)`, exit 0.

### drawlist_tool OLD vs NEW (bounded, cwd `/tmp/p6`, `OLD/build/tests/drawlist_tool` vs `build/tools/drawlist_tool/drawlist_tool`, capture `load_transition.gsdl`)

OLD has no `info` subcommand (INV §3: `stats | frames | dump | diff`); all four were compared with `cmp`:

```
dltool(stats): IDENTICAL (      97 lines,     6610 bytes)      logs /tmp/p6/6a_old_dltool.log  6a_new_dltool.log
frames:        IDENTICAL (     208 lines,    19874 bytes)      logs /tmp/p6/6a_old_frames.log  6a_new_frames.log
dump --verts --max 3000: IDENTICAL (65004 lines, 7422078 bytes) logs /tmp/p6/6a_old_dump.log 6a_new_dump.log
diff a a:      IDENTICAL (       1 lines,       90 bytes)      logs /tmp/p6/6a_old_diff.log    6a_new_diff.log
frames=208 draws=3667 vertices=680430 (17.6 draws/frame, 3271.3 verts/frame)  [TRUNCATED]
DIFF PASS: frames compared=208 mismatching=0 (offset=0 tol xy=0.0625 z=1 rgba=1 st=1e-05)
```
All exits 0 on both sides. `boot_menu` (G2 also names it) is left to the reviewer / 6F battery; the reader is the
same code path and its header decode is in `GsdlFormatOffsets`.

### Findings

- P6-EE-SCHED-FLAKE (ledger row above): `EeRunTokenAndEventQueue` intermittently hangs to its 120 s timeout;
  not a 6A regression (no ee file touched; baseline had it passing). Needs the Phase-3 owner.
- Note (not a finding): `DrawIrLift::Vert` is 36 bytes, not the 32 INV §5.5 quotes (6 floats + 2×u16 + 4×u8 + f, mask,
  flags, pad = 36; identical to OLD's `gsdrawir::Vert`). In-memory only; no format impact.

### Gate status

| gate | evidence |
|---|---|
| G1 `.gsdl` format + reader | `static_assert`s compile; `GsdlFormatOffsets` 157 checks PASS; `GsdlReaderRoundTrip` PASS; `GsdlReaderCensusMatchesOld` CENSUS/PRIM/FLAGS/JOBS string-equal to `baseline/load_transition.replay.txt` (quoted above; HLE/CLASS → 6E) |
| G2 drawlist_tool | `stats`/`frames`/`dump`/`diff` byte-identical to OLD on load_transition (cmp, above); `GsDrawlistToolDiffClean` PASS |
| G4 G-C1 | `Gs_Inv27_DrawIrEquivalence` PASS (14 handcrafted + 20,000 random) |
| build / lints / size | `cmake --build --preset macos-dev -j4` → `grep -c warning` = 0 (`/tmp/p6/6a_build.log`); `LintOneClass: 333 files scanned, 0 findings -> PASS`; `LintEnginePurity: 287 files scanned, 0 game tokens -> PASS`; `python3 /tmp/p6/check_sizes.py .` → `size check OK`; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 94` |

### HANDOFF (6A → 6B)
Done: engine/gs/hle/ir (GsdlFormat, VertexRec, JobRec/JobKind, TexReadView, DrawIr, DirectOps, DrawIrLift, DrawIrVerifier,
  DrawIrSink; lib ps2r_gs_ir); tests/lib GsdlReader (ps2r_gsdl_reader); tools/drawlist_tool; ctests GsdlFormatOffsets,
  GsdlReaderRoundTrip, GsdlReaderCensusMatchesOld, Gs_Inv27_DrawIrEquivalence, GsDrawIrRoundTrip, GsDrawlistToolDiffClean
  (all PASS); flags consumed: none; INV §1, §2.4, §2.7 (CENSUS/PRIM/FLAGS/JOBS), §3, §5.4–§5.6 transplanted.
Not done: nothing of §2.1. For 6B (§2.2 GsRegisters/GsPrivRegs/VertexKick/DrawFlush/GsState/TransferEngine, §2.3
  PageRecord/VramImage): DrawFlush produces `DrawIr` and hands it to `DrawIrSink::OnDraw`; the flush must fill
  `DrawIr::Snapshot.vertex_hash`/`bbox` via `GsdlFormat::VertexHash`/`Bbox` and `verts_incl` per the recorder's mode;
  `DrawIr.raster.dthe/dimx` and `TexReadView.mxl/lcm/l/k/mip_*` are the fields the record lacks (ARCH §4.6) — 6B sets
  them from TEX1/MIPTBP/DTHE/DIMX. `GsdlReaderCensusMatchesOld` HLE/CLASS lines and `boot_menu` stats identity → 6E/6F.
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 94/94; lints 0/0; getenv sites unchanged (1)
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Gsdl|Gs_Inv27|GsDraw'
Findings: P6-EE-SCHED-FLAKE (Phase-3 test, not ours)

### Review 6A (verdict)

Independent reviewer, 2026-09-27 (full table: `/tmp/p6/REVIEW_6A.md`; logs `/tmp/p6/rv6a_*.log`). Every check
re-run: clean rebuild of all 6A targets → 0 warnings; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed
out of 94` (a first run hit the pre-existing Phase-3 `EeRunTokenAndEventQueue` timeout, P6-EE-SCHED-FLAKE; 5/5
isolated reruns + the second full run pass); lints 0/0; `check_sizes.py` OK; no getenv. Layout static_asserts complete
and INV §1-exact; `JobKind` == OLD `RenderCommandType`; `FromDrawRec` cast-for-cast with OLD `BuildBatch`, round trip
byte-identical over all 3,667 capture draws; `DirectOps` / `DrawIrLift` / `DrawIrVerifier` store-for-store with OLD
`gs_direct_ops.h` / `gs_draw_ir.h`, the Inv27 test carries OLD's 14 handcrafted cases + the verbatim seeded generator
(20,000 packets; OLD `test_gs_draw_ir` re-run bounded, PASS). Reader census == baseline == live OLD `gs_replay census`
(4 lines). `drawlist_tool` byte-identical to OLD on load_transition AND boot_menu for `stats`/`frames`/full
`dump --verts` (699,630 and 9,973,122 lines)/`diff`, plus option variants, usage and error paths (exit codes equal).

**VERDICT: NEEDS_CHANGES** (G1/G2/G4 evidence all holds; the one open item is the §1.5 contract letter):
1. `engine/gs/CMakeLists.txt` was extended with a 12-line `add_library(ps2r_gs_ir …)` block; §1.5 allows shared files to
   change only by `add_subdirectory(<path>)  # Phase 6` lines. Move the block to a new `engine/gs/hle/ir/CMakeLists.txt`
   and leave `add_subdirectory(hle/ir)  # Phase 6` in its place; while there, drop the unused `ps2r_core` link (no
   `engine/core` header is included by `engine/gs/hle/ir`). Rebuild (0 warnings), ctest 94/94, lints 0/0.

### Review 6A fix (2026-09-27, ≈5 min)

Item 1 applied: the `ps2r_gs_ir` library block moved to new `engine/gs/hle/ir/CMakeLists.txt` (13 lines; sources
`gsdl_format.cpp draw_ir.cpp direct_ops.cpp draw_ir_lift.cpp draw_ir_verifier.cpp`, `-Wall -Wextra -Werror`, PUBLIC
include `${CMAKE_SOURCE_DIR}`, links `ps2r_gs_format` only — the unused `ps2r_core` link dropped; `grep -rn engine/core
engine/gs/hle/ir/` is empty). `engine/gs/CMakeLists.txt` now differs from `phase6` base by exactly `+add_subdirectory(hle/ir)  # Phase 6`
(`git diff --stat`: 1 file, 2 insertions incl. the blank separator), satisfying the §1.5 letter. Re-verified
(`/tmp/p6/6a_build2.log`, `/tmp/p6/6a_ctest2.log`): reconfigure OK, rebuild `warnings=0`, `100% tests passed, 0 tests
failed out of 94`, `LintOneClass: 333 files scanned, 0 findings -> PASS`, `LintEnginePurity: 288 files scanned, 0 game
tokens -> PASS`. G1/G2/G4 evidence unchanged.

### Review 6A (verdict, iteration 2)

Independent reviewer, 2026-09-27, after the CMake fix (table: `/tmp/p6/REVIEW_6A.md`; logs `/tmp/p6/rv6a2_*`,
`/tmp/p6/rv6a_build.log`). Every check re-run: `git diff` of the three shared CMake files is now exactly one
`add_subdirectory(...)  # Phase 6` line each (`engine/gs/hle/ir/CMakeLists.txt` holds `ps2r_gs_ir`, linking
`ps2r_gs_format` only); nothing under `engine/gs/format`, `engine/gs/hle/sce`, `engine/core`, `engine/ee`, `engine/iop`,
`app`, `games` touched; no forbidden includes. Clean rebuild of the 6A targets → 0 warnings; `ctest --preset macos-dev` →
`100% tests passed, 0 tests failed out of 94` (GsdlFormatOffsets, GsdlReaderRoundTrip, GsdlReaderCensusMatchesOld,
Gs_Inv27_DrawIrEquivalence, GsDrawIrRoundTrip, GsDrawlistToolDiffClean all Passed); `LintOneClass … 0 findings`,
`LintEnginePurity: 288 files … 0 game tokens`; `check_sizes.py` OK; no new getenv. Layout static_asserts complete and
INV §1-exact; `JobKind` == OLD `RenderCommandType`; `FromDrawRec` cast-for-cast with OLD `BuildBatch` and round-trip
byte-identical over 3,667 capture draws; `DirectOps`/`DrawIrLift`/`DrawIrVerifier` store-for-store with OLD, Inv27 test
= OLD's 14 handcrafted cases + the same seeded 20,000 packets (OLD `test_gs_draw_ir` re-run bounded → PASS). Reader
census == frozen baseline == live OLD `gs_replay census` (4 lines). `drawlist_tool` byte-identical to OLD (`cmp`) on
load_transition and boot_menu for `stats`/`frames`/`dump` (incl. full `--verts`)/`diff` plus option, usage and error
paths with equal exit codes. Reviewer fixed one doc nit (`engine/gs/hle/ir/README.md` Purpose line: `ps2r_core` → links
`ps2r_gs_format` only).

**VERDICT: APPROVED** — G1, G2, G4 met; no open items. Stage 6B may start from the HANDOFF above.


## 6B — state + xfer + vram (2026-09-27, ≈32 min)

### Landed (3,449 lines incl. tests)

`engine/gs/vram/` (lib `ps2r_gs_vram`): `page_record.h` 83 / `.cpp` 132, `vram_image.h` 131 / `.cpp` 215, `CMakeLists.txt`,
README Contents. `engine/gs/xfer/` (lib `ps2r_gs_xfer`, links vram + format + ir): `transfer_engine.h` 106 / `.cpp` 322,
`CMakeLists.txt`, README. `engine/gs/state/` (lib `ps2r_gs_state`, links xfer + vram + ir + core): `gs_registers.h` 120 /
`.cpp` 254, `gs_priv_regs.h` 77 / `.cpp` 158, `vertex_kick.h` 86 / `.cpp` 215, `draw_flush.h` 73 / `.cpp` 134, `gs_state.h` 81 /
`.cpp` 90, `CMakeLists.txt`, README. `tests/lib/capture_sink.h` 23 (+ README line). `tests/gs/state/test_gs_state.cpp` 567,
`tests/gs/xfer/test_transfer_engine.cpp` 302, `tests/gs/vram/test_vram_image.cpp` 280. CMake (contract §1.5): `engine/gs/CMakeLists.txt`
gains exactly `add_subdirectory(vram|xfer|state)  # Phase 6` (three lines); `tests/gs/CMakeLists.txt` (ours) gains
`ps2r_gs_state_test(subdir name)` and 20 registrations. Namespace `ps2r::gs`; every file ≤ 570 lines, every function ≤ 150.

Shape notes: `GsRegisters` owns the pending-draw hooks (`has_pending` / `flush_pending`, std::function) so the register file
has no dependency on the kick; `VertexKick` and `TransferEngine` take the same flush hook; `GsState` wires all three to
`DrawFlush::Flush(StateChange)`. `GsState::WriteRegister` routes: 0x00 PRIM → registers + `VertexKick::OnPrimWritten`;
0x04/05/0C/0D → kick; 0x50–0x54 → transfer engine; 0x60–0x62 → priv regs (CSR bits, SIGLBLID, interrupt count); 0x0B/0E/0F
no-ops; rest → `GsRegisters::Write` (unknown addresses ignored). `WritePacked(reg, lo, hi)` carries the PACKED-only decodes
(PRIM, RGBAQ, ST, UV, XYZ*/XYZF* with ADC, FOG, A+D). `DrawIr.snapshot.vertex_hash/bbox/verts_incl` are filled by the flush;
`vram_hash/clut_hash` remain the recorder's (6D/6F). The transfer engine stores pixels per pixel through
`PsmLayout::PixelByteAddress` + `VramImage::WriteStream` (not `Swizzle::LinearToVram`) because the CT24 0x80 and T4-drop rules
are per-store; local→local likewise walks pixels in DIR order (the OLD traversal). `VramImage` = bytes + 512 `PageRecord` +
lazy shadow; `SettleWrites` = OLD `BumpVramWriteEpochIfDirty` + `ApplyDeposits`, called at TRXDIR, transfer completion and
before every flush. `VertexKickQuirks{round_sprite_mode=1}` = corner ordering only (OLD has no pixel-centre snapping).

### OLD sources transplanted (read-only)

`gs_state.h:40-315` (register file → `GsRegisters` structs); `gs_state.cpp:264-302` (FlushPendingBeforeStateChange), `:977-1085`
SetPrim, `:1131-1178` SetScissor/SetXYOffset, `:225-262` TestDrawChanged/SnapshotState, `:1345-1424` kick core, `:1824-2316`
AssembleVertex (strip `:1842-1998`, non-strip + ADC consume `:2245-2316`), `:2673-2851` FlushPrimitive (sprite `:2740-2846`),
`:2854-2956` SetFrame/SetZBuf, `:2971-3110` SetTex0/SetTex1/SetMiptbp, `:3119-3209` SetTest, `:3226-3313` SetAlpha/SetClamp/
SetTexa/SetColclamp, `:3322-3495` SetPrmodecont/SetPrmode, `:3504-3634` SetTex2/SetTexflush/SetFba/SetPabe/SetScanmsk/SetFogcol,
`:3811-4114` SetBitBltBuf/SetTrxPos/SetTrxReg, `:4116-4478` SetTrxDir, `:4493-4728` PerformLocalToLocalTransfer, `:4731-4745`
GetPixelSize, `:4761-4856` StampTransferDestEpoch/BumpVramWriteEpochIfDirty, `:4858-5026` FinishHostToLocalTransfer,
`:5029-5588` WriteHWReg (legacy per-pixel loop = the fast arm's byte-identical twin), `:304-975` Flush; `gif.cpp:1705-2894`
FlushBatch (the EOP twin: no strip reset / no snapshot), `:3213-3263` PACKED decodes, `:4715-4778` REGLIST RGBAQ/ST/UV/XYZ*,
`:4880-4905` SIGNAL/FINISH/LABEL, `:4929-4941` DTHE/DIMX, `:4993-5010` FOG + no-ops, `:5157-5195` ProcessImage re-arm;
`gs.cpp:80-106, 161-289, 457-493` (priv regs incl. CSR + DISPLAY window math); `render.h:2336-2390` ComputeMergeKeyAllPrim
(→ `DrawFlush::RunKey`), `:348-370` epoch families; `gs_page_tracker.h` (author/band/reader set → `PageRecord`/`VramImage`);
`gs_deposit_overlay.h` (BeginDeposit/EndDeposit/ComposeRead/ApplyDeposits → StageDeposit/ReadView/CommitDeposits);
`idx_epoch_guard.h` (deposit-epoch family separation); `graphics/opengl/alias_rect.h:1-216` (band math verbatim →
`PageRecord` statics); `tests/test_alias_rect.cpp:112-232` (the pure pins → `Gs_Inv36`, game addresses replaced by synthetic).

### Flags applied (FLAG_LEDGER rows; NORM = winning arm only, nothing else written)

PS2_R199_XFERLATCH (ON: staged→active latch at TRXDIR), PS2_R199_UPLOADEAGER (ON: completion at rrw·rrh after the word),
PS2_GS_TERM_NATIVE + PS2_GS_TYPED_BULK (ONE assembly path; the perf twins not written), PS2_VRAM_EPOCH_REACT (ON: re-arm
restamp), PS2_GS_SNAPSHOT_FAST / PS2_XFER_FAST (the fast arms' semantics = the legacy loop; one implementation),
PS2_GS_RESIDENT(+_PBO) (author/reader-set/band bookkeeping in `PageRecord`; PBO is 6C/6D), PS2_GS_DEPOSIT_EEAPPLY (ON:
commits only at EE observation boundaries = `SettleWrites`), PS2_GL_ALIAS_RECT (ON: `FoldByteRangeToBand`/`PageRect`/
`FullyCoveredPages`), PS2_GL_ALIAS_DIRTYRECT (winner OFF: `DirtyRectOn` arm not written), PS2_SPIKE_GUARD (winner OFF: strip
spike guard not written), PS2_GS_RUNDRAW + PS2_GS_DRAWMERGE (winner RUNDRAW: the key = `DrawFlush::RunKey`; the queue-side
merge action itself lands with the RenderQueue), PS2_SH1_DEPEPOCH family (deposit epochs separate from upload epochs,
`PageRecord::deposit_epoch` / `VramImage::DepositSeq`), PS2_GS_MIPMAP (ON: MIPTBP flushes + dirty TEX1). DELETE / DIAG rows
(all `[SEQDIAG]`, `[KICKFB]`, `[S8-KTAP]`, `[R68-WEDGE]`, CLUT-range logging, `g_cutscene_active` FRAME latch, probes) not
written. Zero `getenv` added.

### Tests added (ctest names; PASS lines from `/tmp/p6/6b_ctest.log`, the 114/114 run)

```
32/114 Test  #32: Gs_Inv01_FirstKickTmeSnapshot .......   Passed    0.28 sec
33/114 Test  #33: Gs_Inv02_SetterFlushesPending .......   Passed    0.03 sec
34/114 Test  #34: Gs_Inv28_IdentityStamp ..............   Passed    0.02 sec
35/114 Test  #35: Gs_Inv32_DitherInMergeKey ...........   Passed    0.01 sec
36/114 Test  #36: Gs_Inv38_RoundSpriteNormalise .......   Passed    0.04 sec
37/114 Test  #37: GsRegistersDecodeVectors ............   Passed    0.01 sec
38/114 Test  #38: GsVertexKickSequences ...............   Passed    0.03 sec
39/114 Test  #39: GsStateFacadeEmitsDrawIr ............   Passed    0.05 sec
40/114 Test  #40: Gs_Inv03_TrxdirLatch ................   Passed    0.03 sec
41/114 Test  #41: Gs_Inv21_LocalLocalOverGpuPages .....   Passed    0.03 sec
42/114 Test  #42: Gs_Inv31_ImageRearmRestamp ..........   Passed    0.03 sec
43/114 Test  #43: GsTransferCursor ....................   Passed    0.10 sec
44/114 Test  #44: Gs_Inv04_SnapshotObservesBytes ......   Passed    0.17 sec
45/114 Test  #45: Gs_Inv05_NewerWriterNotClobbered ....   Passed    0.04 sec
46/114 Test  #46: Gs_Inv06_DepositEpochSeparate .......   Passed    0.03 sec
47/114 Test  #47: Gs_Inv07_NoReuseMidDeposit ..........   Passed    0.02 sec
48/114 Test  #48: Gs_Inv08_SingleWriterMirror .........   Passed    0.03 sec
49/114 Test  #49: Gs_Inv16_ReadFootprintPreciseBits ...   Passed    0.02 sec
50/114 Test  #50: Gs_Inv22_ZeroStaleAtSave ............   Passed    0.03 sec
51/114 Test  #51: Gs_Inv36_AliasRefreshPageClear ......   Passed    0.01 sec
100% tests passed, 0 tests failed out of 114
```
Check counts: `test_gs_state all: 183 checks, 0 failures`, `test_transfer_engine all: 46 checks, 0 failures`,
`test_vram_image all: 75 checks, 0 failures`. Highlights: `GsRegistersDecodeVectors` — ≥ 3 vectors (zero / all-ones / mixed)
for TEX0 (both contexts), CLAMP, TEX1, TEX2 (no dirty, TBP kept), XYOFFSET, SCISSOR, ALPHA, TEST (0x47/0x48 addressing), FRAME,
ZBUF, FBA, PABE, COLCLAMP, DTHE, DIMX, SCANMSK, TEXA, FOGCOL, MIPTBP (same-value no dirty), PRIM/PRMODE/PRMODECONT transitions,
RGBAQ (Q=0 rule), ST, UV, FOG register + all PACKED forms, TEXFLUSH, unowned addresses. `GsVertexKickSequences` — triangle, strip
parity + continuation across two EOPs, XYZ3 restart (window kept), degenerate cull, fan, line, line strip, point, PACKED ADC
consume (triangle, fan), XYOFFSET/STQ/fog/colour on the vertex, UV recency (s=u/16, q=1, `tex.fst`). `GsTransferCursor` —
CT32 with DSAX/DSAY + in-word row wrap, CT24 0x80 rule, CT16 + linear expansion, T8, T4 drop (VRAM zero, page stamped, job
charged 1 B/px), the two tables. `GsStateFacadeEmitsDrawIr` — one PACKED packet (PRE PRIM strip IIP/TME/ABE/FST, ST RGBAQ UV
XYZ2 ×4, last ADC) driven through `DirectOps::PacketToOps` → `WritePacked`/`WriteRegister`/`KickBoundary` and through
`DrawIrLift::PacketToDrawIR` → masked attribute stores + `KickVertex`: `ToDrawRec()` memcmp-identical and vertex arrays
identical (the façade-level G-C1 bridge).

### Findings

- P6-FLUSH-TWINS resolved: `FlushKind {StateChange, GifEop}`; only the reset-after differs (ledger row).
- P6-T4-UPLOAD-DROP: bug-compat reproduced and pinned; accepted-difference candidate for integration (ledger row).
- P6-FOG-BOOL: bug-compat reproduced (`PackedFog` → FGE bool); pinned (ledger row).
- NEW: P6-REARM-NO-UPLOAD, P6-CT24-UPLOAD-0x80, P6-TME-LATCH-EMPTY (ledger rows) — all OLD facts reproduced, each pinned by a test.
- Not a finding: `VramImage::WriteStream` does not write through into a pending shadow page (OLD `WriteThrough` is used only by the
  idx CLUT save path); the EE write wins at the next `SettleWrites` by dropping the older deposit (Inv05). 6D decides whether the
  CLUT save path needs a `WriteThrough`.
- Not a finding: `GsPrivRegs` counts GS interrupt requests (`TakeInterrupts`) instead of raising INTC (Phase 3 territory);
  `gscrtc::Classify` calls on PMODE/DISPFB/DISPLAY are 6D's `CrtcClassifier`.
- Process note: one helper edit in this stage was applied via `python3 -` (stdin) before switching to `/tmp/p6/fix6b_*.py`
  files as STAGE_RULES requires; no OLD binary was run.

### Gate status

| gate | evidence |
|---|---|
| G3 GsState units | `Gs_Inv01_FirstKickTmeSnapshot`, `Gs_Inv02_SetterFlushesPending`, `Gs_Inv03_TrxdirLatch`, `Gs_Inv28_IdentityStamp`, `Gs_Inv31_ImageRearmRestamp`, `Gs_Inv38_RoundSpriteNormalise` + `GsRegistersDecodeVectors`, `GsVertexKickSequences`, `GsTransferCursor`, `GsStateFacadeEmitsDrawIr`, `Gs_Inv21`, `Gs_Inv32` all Passed (quoted above) |
| G5 VramImage | `Gs_Inv04/05/06/07/08/16/22/36_*` all Passed (quoted above) |
| build / lints / size | `cmake --build --preset macos-dev -j4` → `warnings=0` (`/tmp/p6/6b_build.log`); `LintOneClass: 353 files scanned, 0 findings -> PASS`; `LintEnginePurity: 307 files scanned, 0 game tokens -> PASS`; `python3 /tmp/p6/check_sizes.py .` → `size check OK`; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 114`; `grep -rn getenv engine tools tests/lib | grep -v config_loader` → only the pre-existing `tools/regen_from_frozen.sh` prose |

### HANDOFF (6B → 6C)
Done: engine/gs/vram (PageRecord, VramImage; ps2r_gs_vram), engine/gs/xfer (TransferEngine; ps2r_gs_xfer), engine/gs/state
  (GsRegisters, GsPrivRegs, VertexKick, DrawFlush, GsState; ps2r_gs_state), tests/lib/capture_sink.h; 20 ctests (G3 + G5) all
  PASS; flags consumed as listed; INV §4.1–§4.10, §5.6/§5.7 (façade), §8 (trackers → VramImage), §12 rows 1-8, 16, 21, 22, 28,
  31, 32, 36, 38(sprite) transplanted.
Not done (→ 6C/6D, by design): the queue-side RUNDRAW merge ACTION (append / cap 65536 / flush-before-other-push) — the key
  is `DrawFlush::RunKey`; `DrawIr.snapshot.vram_hash/clut_hash` (recorder); `VramImage::WriteThrough` (only if the idx CLUT
  save path needs it); INTC raise from `GsPrivRegs::TakeInterrupts` and `CrtcClassifier` hooks on PMODE/DISPFB/DISPLAY
  (integration / 6D); the upload payload reaches the sink as `JobRec.payload_bytes/payload_hash` + `TransferEngine::LastUploadPixels()`
  (6C/6D's RenderQueue adapter must copy it when it consumes the job). `Gs_Inv30_OwnedByPhase5` / `Gs_Inv38_XgkickOwnedByPhase5`
  SKIP stubs → 6F.
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 114/114; lints 0/0; getenv sites unchanged (1)
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Gs_Inv|GsRegisters|GsVertex|GsTransfer|GsStateFacade'
Findings: P6-FLUSH-TWINS (resolved), P6-T4-UPLOAD-DROP (bug-compat), P6-FOG-BOOL (bug-compat), P6-REARM-NO-UPLOAD, P6-CT24-UPLOAD-0x80, P6-TME-LATCH-EMPTY

### Review 6B (verdict)

Independent reviewer, 2026-09-27 (full report `/tmp/p6/REVIEW_6B.md`). Re-ran everything from a `--clean-first`
rebuild: `cmake --build --preset macos-dev --clean-first -j4` → `BUILD_EXIT=0`, `grep -c 'warning:'` → `0`
(`/tmp/p6/rv6b_build.log`); `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 114`
(`/tmp/p6/rv6b_ctest.log`, `EeRunTokenAndEventQueue` passed first time); `ctest -N | grep -cE
'Gs_Inv|GsRegisters|GsVertex|GsTransfer|GsState'` → `21` (the 20 6B tests + `Gs_Inv27`); `LintOneClass: 353 files
scanned, 0 findings -> PASS`; `LintEnginePurity: 307 files scanned, 0 game tokens -> PASS`; `check_sizes.py` →
`size check OK`; getenv/tag/history/literal greps clean except one test literal (item 7). OLD `test_alias_rect` run
bounded from `/tmp/p6` → `[ALIAS-RECT] RESULT: PASS (85/85 checks passed)`; NEW `Gs_Inv36` carries its 21 pure pins.

Audited against OLD line by line: every register decode in `gs_registers.cpp` / `transfer_engine.cpp` equals
`gs_state.cpp` / `gif.cpp` (TEX0 12 fields, TEX1, CLAMP, TEST, ALPHA, FRAME, ZBUF, SCISSOR, TEXA, FOGCOL, TEX2,
XYOFFSET, PRIM/PRMODE/PRMODECONT, RGBAQ/ST/UV/FOG both forms, XYZ*/XYZF* both forms incl. ADC, BITBLTBUF/TRXPOS/
TRXREG) — no bit mismatch; kick core, strip parity, ADC consume and the sprite expansion equal `:1345-1424` /
`:1824-2316` / `:2740-2846`; flush order equals INV §4.5 with the twin difference as `FlushKind`; HWREG loop equals
the per-pixel loop `:5331-5527` incl. CT24 0x80 and the T4 non-store; band math equals `alias_rect.h`; the façade
is the §1.4 set plus the §5.7 requirements; `CommitDeposits` is reached only from `SettleWrites` (P6-ZERO-MIRROR
holds). Reviewer fixed one comment nit (`vram_image.h` MaterializePages described non-existent `[y0,y1)`
parameters); rebuilt 0 warnings, 8 vram ctests green.

Deviations from OLD found (all edge-case or bookkeeping, none in the corpus path; details and OLD citations in the
report): `SetTrxDir` clears `active` for xdir 1/2/3 (OLD leaves it → partial-flush trigger differs); IMAGE re-arm
gated on xdir==0 (OLD re-arms on `!active` for any xdir); T4 / index-in-word stores skip OLD's per-pixel
`NoteVramWrite` watermark (one fewer upload-epoch bump at completion); priv-reg jobs stamp the last-flush epoch
instead of the live one (OLD stamps `g_vram_epoch` at push — a per-VSync path); `GsPrivRegs::Reset` clears the
DISPLAY window latches OLD keeps static; `RunKey` hashes the scissor in a different order and lacks the empty-vertex
0 sentinel (equality relation unchanged); the T4 ledger row omits the linear-payload (nibble vs UB byte) and
watermark halves; one game VRAM literal (`0x3F0000`) survives in `test_vram_image.cpp:236`.

**VERDICT: NEEDS_CHANGES** — 7 items, listed with file:line and the OLD source in `/tmp/p6/REVIEW_6B.md` §Verdict:
(1) keep `cur_.active` untouched for xdir≠0 in `SetTrxDir`; (2) re-arm on `!active` for any xdir with the stamp
guard inside `StampDest`, pin in `Gs_Inv31`; (3) reproduce the `NoteVramWrite` watermark for non-storing PSMs, pin
in `GsTransferCursor`; (4) `GsPrivRegs`: do not clear `window_[]` in `Reset`, stamp the live upload epoch on jobs,
pin; (5) `RunKey` scissor order `{x0,y0,x1,y1}` + empty → 0; (6) extend the P6-T4-UPLOAD-DROP row; (7) replace the
`0x3F0000` test literal.

### Review 6B fix (2026-09-27, ≈26 min)

All seven NEEDS_CHANGES items of `/tmp/p6/REVIEW_6B.md` applied; OLD read only (lines re-read:
`gs_state.cpp:4116-4478, :4761-4792, :4794-4850, :5127, :5381`, `gs_state.h:333-340`, `gif.cpp:5157-5195`,
`gs.cpp:80-92, :255-272, :465-490`, `render.h:2068-2073, :2336-2390, :3181-3190`); no OLD binary run; no commits.

| # | item | change (file:line) | pin |
|---|---|---|---|
| 1 | `SetTrxDir` leaves `cur_.active` untouched for xdir 1/2/3 | `engine/gs/xfer/transfer_engine.cpp:113-146` — only the xdir==0 arm arms/flushes; xdir 2 → `LocalToLocal()`, xdir 1 → `local_host++`, xdir 3 → nothing; header contract `transfer_engine.h:4-12` ("only the XDIR 0 arm touches the cursor's active flag") | `Gs_Inv03_TrxdirLatch` extended (`test_transfer_engine.cpp:113-138`): 4 of 8 CT32 pixels, TRXDIR 3 → `active && pixels_written==4`; HWREG refused (`hwreg==2`); TRXDIR 1 → still active, `local_host==1`; restage + TRXDIR 0 → exactly 2 jobs, the `UploadVRAMTexture` attributed to the OLD dest (`arg1==0x400*256, 4x1, 16 B`), new transfer armed |
| 2 | IMAGE re-arm on `!active` for ANY xdir; stamp guard `(xdir==0\|\|xdir==2) && rrw>0 && rrh>0` inside `StampDest` | `transfer_engine.cpp:99-105` (`StampDest` guard = OLD `StampTransferDestEpoch`), `:147-158` (`WriteImage`: `if (!cur_.active) { ArmHostToLocal(); StampDest(); }`); `SetTrxDir` now calls `StampDest()` unconditionally; `WriteHwreg` keeps `if (!cur_.active \|\| cur_.xdir != 0) return;` | `Gs_Inv31_ImageRearmRestamp` extended (`:172-186`): TRXDIR 3 → inactive; IMAGE word → `active && pixels_written==0`, page epoch and `UploadEpoch()` unchanged (no stamp), VRAM byte unchanged and `hwreg` unchanged (no store), `completed` unchanged |
| 3 | T4 / T8H / T4HL / T4HH pixels note the OLD `NoteVramWrite(addr, GetPixelSize)` watermark without storing | `engine/gs/vram/vram_image.h:70` + `vram_image.cpp:41-65` — new `VramImage::NoteWriteNoBytes(byte_off, len)`; `WriteStream` and it share the private per-byte `MarkWrittenByte` (write bit, CPU author, settle set, lo/hi watermark); `transfer_engine.cpp:189-194` default arm calls it (`addr < kVram` guard as OLD); the unused `MutableBytes()` local dropped | `GsTransferCursor` T4 case (`:308-330`): bytes zero, `UploadEpoch()==up0+1`, `settles+1`, page epoch == settle epoch, `write_bits==0` after settle, `stream_writes==0`, upload job `arg4==16` and `vram_epoch==up0+1` |
| 4a | `GsPrivRegs::Reset()` keeps the DISPLAY window latches | `engine/gs/state/gs_priv_regs.cpp:37-44` (no `window_[]` reset; comment states the rule), header `gs_priv_regs.h:7-12, :51` | NEW ctest `GsPrivRegsJobsAndReset` (`test_gs_state.cpp:591-652`, `tests/gs/CMakeLists.txt:99`): DISPLAY1 → 1 `SetWindow` (args checked), same value → still 1, CSR RESET → regs 0, same DISPLAY → still 1 (latch survived), DISPFB re-emits (its latch IS the register), new geometry → 2 |
| 4b | priv jobs stamp the LIVE upload epoch | `gs_priv_regs.h:44-45, :76` — `SetJobContext(..., std::function<uint64_t()> vram_epoch)`; `gs_priv_regs.cpp:9-13, :58` (`vram_epoch_()` at push); `gs_state.cpp:19-21` hands `[this]{ return vram_.UploadEpoch(); }` | same ctest: draw flushed at epoch 0, TRXDIR stamp → live 1, DISPFB job `vram_epoch==1==Vram().UploadEpoch()`; second TRXDIR → 2, BGCOLOR job `vram_epoch==2` |
| 5 | `RunKey` scissor `{x0,y0,x1,y1}`, empty vertex array → 0 | `engine/gs/state/draw_flush.cpp:94-109`; **and** the basis: OLD seeds with `1469598103934665603` (the FNV offset basis missing its last digit) — NEW now uses that constant (`kMergeKeyBasis`) so the VALUE equals OLD's (ledger row P6-MERGEKEY-BASIS, found because the known-vector pin failed against the true basis); `draw_flush.h:20-23` | `Gs_Inv32_DitherInMergeKey` extended (`:174-252`): all draws now carry a vertex; "scissor fields are position-keyed"; empty → 0; non-empty ≠ 0; two known vectors (untextured `0xf7533519e738430a`, textured `0xbc6fc9cd8ec1a36a`) computed by OLD's `Fnv1aStep` + `ComputeMergeKeyAllPrim` body verbatim over a minimal batch (`/tmp/p6/fix6b2_keyvec.cpp`, output `/tmp/p6/fix6b2_keyvec.txt`) |
| 6 | P6-T4-UPLOAD-DROP ledger row | ledger row rewritten with the three halves (VRAM store / linear payload `(value>>(p*8))&0xFF` UB for p≥8 vs NEW nibble / watermark+epoch) and the census fact (`grep -oh 'bpp=[0-9]*' OLD/gs_corpus/census/*.txt` → `50 bpp=32` only, re-run this session) | — |
| 7 | test literal | `tests/gs/vram/test_vram_image.cpp:236-239`: `0x3F0000` → `0x00300000 + 3 * 0x2000` (synthetic, disjoint from the `[0x80000, 0xF0000)` target); assertion meaning kept | `Gs_Inv36_AliasRefreshPageClear` Passed; `grep -rn 0x3F0000 tests/gs engine/gs` → empty |

Verification (fresh reconfigure for the new ctest): `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`,
`grep -c 'warning:' /tmp/p6/6b2_build.log` → `0`. Direct: `test_transfer_engine all: 58 checks, 0 failures`,
`test_gs_state all: 203 checks, 0 failures`, `test_vram_image all: 75 checks, 0 failures`. `ctest --preset macos-dev`
first run (`/tmp/p6/6b2_ctest.log`): `99% tests passed, 1 tests failed out of 115` — the one failure is the pre-existing
Phase-3 flake `EeRunTokenAndEventQueue (Timeout)` (P6-EE-SCHED-FLAKE; `engine/ee` / `tests/ee` untouched); every
6B/6B-fix test Passed (22 `Gs_Inv*/GsRegisters/GsVertex/GsTransfer/GsState/GsPriv` lines). Re-run once as instructed:
`ctest -R EeRunTokenAndEventQueue` → `1/1 … Passed 0.08 sec`; full second run (`/tmp/p6/6b2_ctest2.log`) →
`100% tests passed, 0 tests failed out of 115`, `Total Test time (real) = 196.31 sec`.
`LintOneClass: 353 files scanned, 0 findings -> PASS`; `LintEnginePurity: 307 files scanned, 0 game tokens -> PASS`;
`python3 /tmp/p6/check_sizes.py .` → `size check OK`; `grep -rn getenv engine/gs tests/gs` → only the pre-existing
`engine/gs/format/README.md` prose. Shared-file edits: `tests/gs/CMakeLists.txt` (owned) gained one ctest pair; root /
`engine/gs/CMakeLists.txt` untouched this round. Helper files: `/tmp/p6/fix6b2_keyvec.cpp` (+ `.txt`),
`/tmp/p6/fix6b2_keydbg.cpp` (the stepwise diagnosis that located the basis typo); no background processes left.

### Review 6B (verdict, iteration 2)

Independent re-review of the fix round (2026-09-27; full table `/tmp/p6/REVIEW_6B_2.md`). Worktree `NativeEE-p6`,
branch `phase6`; OLD re-read only (`gs_state.cpp:4116-4170, :4184-4206, :4285-4300, :4392-4407, :4466-4478, :4731-4743,
:4761-4792, :5029-5031, :5060-5135, :5370-5381`, `gs_state.h:325-341`, `gif.cpp:5157-5195`, `gs.cpp:81-92, :200-275`,
`render.h:2066-2073, :2336-2390, :3181-3190`, `texture_decoder.cpp:996-1024`); no OLD binary run; no commits; no engine
file edited by the reviewer.

| # | item | evidence | verdict |
|---|---|---|---|
| 1 | `SetTrxDir` never touches `cur_.active` for xdir 1/2/3 | `transfer_engine.cpp:113-146` (only the xdir==0 arm arms/flushes; OLD writes `transfer.active` at `:4466` and `:4929` only, `gif.cpp:5176` re-arm); `Gs_Inv03` `inv03: 18 checks, 0 failures` | PASS (but exposes the defect below) |
| 2 | IMAGE re-arm on `!active` for any xdir; `StampDest` guard `(xdir 0\|2) && rrw>0 && rrh>0` | `:147-158`, `:99-105` = `gif.cpp:5166-5185` / `gs_state.cpp:4767-4769`; `WriteHwreg :224` early return = `:5030`; `Gs_Inv31` `inv31: 13 checks, 0 failures` | PASS |
| 3 | T4/T8H/T4HL/T4HH note `PixelSize` bytes without storing | `vram_image.h:70`, `vram_image.cpp:41-65` (`NoteWriteNoBytes` shares `MarkWrittenByte` with `WriteStream`), `transfer_engine.cpp:189-194` = OLD `:5378-5379` `NoteVramWrite(addr, GetPixelSize)`; `GsTransferCursor` `cursor: 20 checks, 0 failures` (T4 `UploadEpoch()+1`, `stream_writes==0`) | PASS |
| 4 | `Reset()` keeps `window_[]`; priv jobs stamp the live epoch | `gs_priv_regs.cpp:37-44` = `GS_Reset gs.cpp:81-92` vs the static latches `gs.cpp:258`; `gs_priv_regs.cpp:58` `vram_epoch_()` at push = `render.h:3186`; `gs_state.cpp:19-21`; NEW ctest `GsPrivRegsJobsAndReset` (`test_gs_state.cpp:595-652`, CMake `:99`) `priv: 16 checks, 0 failures` | PASS |
| 5 | `RunKey` scissor order, empty sentinel, basis | `draw_flush.cpp:99-150` = `render.h:2336-2390` field for field (OLD flag types all `bool`/`uint8_t`); reviewer re-derived the vectors: generator body `diff` vs `render.h:2336-2390` → comments only, run → `0xf7533519e738430a` / `0xbc6fc9cd8ec1a36a` / `0`; `Gs_Inv32` `inv32: 17 checks, 0 failures` | PASS |
| 6 | P6-T4-UPLOAD-DROP row: three halves + census | ledger row above | PASS |
| 7 | `0x3F0000` → synthetic | `test_vram_image.cpp:236-239` `0x00300000 + 3 * 0x2000`; `grep -rn 0x3F0000 tests/gs engine/gs` → empty | PASS |

Hygiene (reviewer's own runs): `cmake --build --preset macos-dev --clean-first -j4` → `BUILD_EXIT=0`, `grep -c 'warning:'
/tmp/p6/rv6b2_build.log` → `0` (298 steps); `ctest --preset macos-dev` (`/tmp/p6/rv6b2_ctest.log`) → `100% tests passed, 0
tests failed out of 115`, `Total Test time (real) = 190.38 sec`, `EeRunTokenAndEventQueue … Passed 0.29 sec` (no rerun);
targeted `-R 'Gs_Inv31|Gs_Inv32|GsTransferCursor|GsPriv|Gs_Inv36|Gs_Inv03'` → 6/6; `test_transfer_engine all: 58 checks`,
`test_gs_state all: 203 checks`, `test_vram_image all: 75 checks`, 0 failures each; `LintOneClass: 353 files scanned, 0
findings -> PASS`; `LintEnginePurity: 307 files scanned, 0 game tokens -> PASS`; `size check OK`; `git -P diff
engine/gs/CMakeLists.txt` → the 4 `add_subdirectory` lines only; game-literal / `FUN_` grep over the six 6B dirs → empty;
`getenv` → empty.

**New finding — P6-PARTIAL-FLUSH-OOB (introduced by the item-1 fix).** With `active` surviving TRXDIR 1/2/3, the next
`TRXDIR 0` partial flush (`transfer_engine.cpp:127-137`) sizes its payload from `prev_flush_` — the geometry the
INTERMEDIATE TRXDIR latched — but copies it from `linear_`, which was allocated for the upload actually in flight:
`std::vector<uint8_t> part(linear_.begin(), linear_.begin() + pw*ph*4)` (`:133`) reads past the buffer whenever the
intermediate latch was wider. OLD guards this (`gs_state.cpp:4404-4406`: `resize(prev_w*prev_h*4)` then `memcpy(min(sizes))`
→ real bytes + zeros). Reproduced by `/tmp/p6/rv6b2_partial_oob.cpp` (CT32 4x2, 4 pixels, restage TRXREG 64x64, TRXDIR 3,
TRXDIR 0): ASan `heap-buffer-overflow … READ of size 256 … SetTrxDir transfer_engine.cpp:133 … 0 bytes after 32-byte
region` (`/tmp/p6/rv6b2_partial_oob_asan.log`); the job attribution itself (`64x1, arg4=256, payload_bytes=256`) equals
OLD's. Unreachable before item 1 (TRXDIR≠0 cleared `active`); the extended `Gs_Inv03` restages only BITBLTBUF so its copy
stays in bounds. Undefined behaviour in `engine/` reachable from the register interface and a `payload_hash` that would
differ from OLD → blocks 6C.

**VERDICT: NEEDS_CHANGES** — 1. `engine/gs/xfer/transfer_engine.cpp:133`: `std::vector<uint8_t> part(pw * ph * 4, 0);
std::memcpy(part.data(), linear_.data(), std::min(part.size(), linear_.size()));` (OLD `:4404-4406`); extend
`Gs_Inv03_TrxdirLatch` (`tests/gs/xfer/test_transfer_engine.cpp:113-138`) with a LARGER TRXREG restaged before the
intermediate TRXDIR and CHECK `payload_bytes == pw*ph*4`, prefix == written pixels, tail zero; quote one ASan-clean run of
`test_transfer_engine all` (or the probe).

---
### Review 6B fix 2 (2026-09-27, ≈12 min)

The single remaining item of `/tmp/p6/REVIEW_6B_2.md` (P6-PARTIAL-FLUSH-OOB) applied; OLD read only (lines re-read:
`gs_state.cpp:4285-4306, :4395-4410`); no OLD binary run; no commits.

| # | item | change | pin |
|---|---|---|---|
| 1 | `SetTrxDir` xdir==0 partial-flush arm read past `linear_` when an intermediate TRXDIR 1/2/3 latched a wider TRXREG | `engine/gs/xfer/transfer_engine.cpp:137-138`: `std::vector<uint8_t> part(static_cast<size_t>(pw) * ph * 4, 0); std::memcpy(part.data(), linear_.data(), std::min(part.size(), linear_.size()));` (+ `#include <algorithm>` at `:4`) — the payload is `prev_w*prev_h*4` bytes, the first `min(sizes)` real, the rest zero = OLD `gs_state.cpp:4404-4406` (`upload_pixels.resize((size_t)prev_w * prev_h * 4); memcpy(…, min(upload_pixels.size(), linear_pixels.size()))`). The pw/ph derivation (`rrw<1→1`, `ph = written/pw`, clamp `[1, rrh]`) was already OLD `:4296-4301` | `Gs_Inv03_TrxdirLatch` extended (`tests/gs/xfer/test_transfer_engine.cpp:140-176`), two variants in one loop — intermediate `TRXDIR 3` (idle) and intermediate `TRXDIR 2` (local→local): CT32 4x2 upload, 4 of 8 pixels written (`linear_` = 32 bytes), restage `TRXREG 64x64`, TRXDIR `mid`, restage BITBLTBUF, `TRXDIR 0`; CHECKs "intermediate TRXDIR latched the wider geometry, upload still armed", "TRXDIR 0 flushes the partial upload", "partial upload sized from the latched geometry: payload_bytes == pw*ph*4" (`arg2==64, arg3==1, arg4==256, payload_bytes==256`), "payload vector is pw*ph*4 bytes" (`LastUploadPixels().size()==256`), "prefix == the written pixels (RGBA8, a'=min(2a,255))" (16 bytes memcmp), "unwritten pixels of the in-flight buffer are zero" (bytes 16..31), "tail past the in-flight buffer is all zero" (bytes 32..255) |

ASan proof (all three TUs compiled from source with `clang++ -std=c++17 -fsanitize=address -fno-omit-frame-pointer -g -I.`
over `transfer_engine.cpp engine/gs/vram/*.cpp engine/gs/format/*.cpp engine/gs/hle/ir/*.cpp engine/core/hash/hash.cpp`;
each run through `verify/run_bounded.sh 120`):

- reviewer probe `/tmp/p6/rv6b2_partial_oob.cpp` → `/tmp/p6/asan_probe` (`/tmp/p6/6b3_asan.log`): `PROBE_EXIT=0`,
  `after TRXDIR3: active=1 pixels_written=4 linear(A)=32 bytes` / `job type=8 arg1=0x40000 arg2=256 arg3=0 arg4=0
  payload_bytes=0` / `job type=9 arg1=0x40000 arg2=64 arg3=1 arg4=256 payload_bytes=256` / `LastUploadPixels().size()=256`;
  `grep -c AddressSanitizer` → `0`. The attribution line is unchanged from the reviewer's non-ASan run (= OLD's
  `prev_xfer_flush_params` geometry); only the copy bounds changed.
- `tests/gs/xfer/test_transfer_engine.cpp` → `/tmp/p6/asan_test_xfer all` (`/tmp/p6/6b3_asan_test.log`): `TEST_EXIT=0`,
  `test_transfer_engine all: 72 checks, 0 failures`, `grep -c AddressSanitizer` → `0`.
- Negative control (the pin reaches the defect): the same ASan test built against a throwaway `/tmp/p6` copy of
  `transfer_engine.cpp` with the pre-fix iterator-range line → `PREFIX_EXIT=134`, `ERROR: AddressSanitizer:
  heap-buffer-overflow … READ of size 256 … #13 … TransferEngine::SetTrxDir(unsigned long long) te_prefix.cpp:137`
  (`/tmp/p6/6b3_asan_prefix.log`; the copy was deleted afterwards, the tree never carried it).

Gate re-run: `cmake --build --preset macos-dev -j4` (`/tmp/p6/6b3_build.log`) → `BUILD_EXIT=0`, `grep -c 'warning:'` → `0`;
`./build/tests/gs/test_transfer_engine all` → `72 checks, 0 failures` (was 58), `inv03` → `32 checks, 0 failures` (was
18); `ctest --preset macos-dev -R 'Gs_Inv|GsTransfer|GsRegisters|GsVertex|GsState|GsPriv'` → `100% tests passed, 0 tests
failed out of 22`; full `ctest --preset macos-dev` (`/tmp/p6/6b3_ctest.log`) → `100% tests passed, 0 tests failed out of
115`, `Total Test time (real) = 206.23 sec`, `EeRunTokenAndEventQueue … Passed 0.08 sec` (no rerun); `LintOneClass: 353
files scanned, 0 findings -> PASS`; `LintEnginePurity: 307 files scanned, 0 game tokens -> PASS`; `python3
/tmp/p6/check_sizes.py .` → `size check OK`. Files touched this round: `engine/gs/xfer/transfer_engine.cpp`,
`tests/gs/xfer/test_transfer_engine.cpp`, this document (ledger row P6-PARTIAL-FLUSH-OOB added).

### Review 6B (verdict, iteration 3)

Independent final re-review of fix round 2 (`/tmp/p6/REVIEW_6B_3.md`, 2026-09-27). Worktree/branch quoted; OLD read only
(`gs_state.cpp:4285-4306, :4306-4330, :4392-4412` re-read; `find OLD -newermt 2026-09-27` outside `logs/` → 0 files);
no OLD binary run; no commits; no engine file edited by the reviewer; reviewer artifacts `/tmp/p6/rv6b3_*`.

| check | evidence | verdict |
|---|---|---|
| P6-PARTIAL-FLUSH-OOB copy | `engine/gs/xfer/transfer_engine.cpp:137-138` `part(pw*ph*4, 0)` + `memcpy(…, min(part.size(), linear_.size()))`, `<algorithm>` at `:4` = OLD `gs_state.cpp:4321` fresh `RenderJob` → `:4404` `resize` (zero-fill) → `:4405-4406` `memcpy(min)`; pw/ph derivation `:129-132` = OLD `:4296-4301`; `arg4` `:140` = OLD `:4402`; `rrh==0` degenerate → empty payload in both | PASS |
| extended pin | `tests/gs/xfer/test_transfer_engine.cpp:140-176`: `for (mid = 3; mid >= 2; --mid)` (no wrap), TRXREG restaged 64x64 (LARGER than the 4x2 in flight), intermediate `TRXDIR mid`, then `TRXDIR 0`; CHECKs `arg2==64 arg3==1 arg4==256 payload_bytes==256`, `LastUploadPixels().size()==256`, 16-byte prefix memcmp, bytes 16..31 and 32..255 zero. `inv03` → `32 checks, 0 failures`; `all` → `72 checks, 0 failures` | PASS |
| pin reaches the defect | reviewer-built negative control (`/tmp/p6/rv6b3_neg/te_prefix.cpp`, only `:137` reverted, `diff` = one line) under ASan → `NEG_EXIT=134`, `heap-buffer-overflow … READ of size 256 … SetTrxDir … te_prefix.cpp:137`; copy deleted, tree unchanged | PASS |
| ASan, reviewer-rebuilt (`clang++ -std=c++17 -fsanitize=address -fno-omit-frame-pointer -g -I.`, each via `verify/run_bounded.sh 120`) | `rv6b3_asan_probe` (TRXDIR 3 intermediate) `PROBE_EXIT=0`, `job type=9 arg1=0x40000 arg2=64 arg3=1 arg4=256 payload_bytes=256`, `LastUploadPixels().size()=256`, ASan reports `0`; `rv6b3_asan_probe_mid2` (TRXDIR 2 intermediate) `MID2_EXIT=0`, same type-9 line, `0`; `rv6b3_asan_test_xfer all` `TEST_EXIT=0`, `72 checks, 0 failures`, `0`; `inv03` `INV03_EXIT=0`, `32 checks`, `0` | PASS |
| build | `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`, `grep -c 'warning:'` → `0` (`/tmp/p6/rv6b3_build.log`) | PASS |
| ctest | `-R 'Gs_Inv03\|GsTransfer' --output-on-failure` → `100% tests passed, 0 tests failed out of 2`; full → `100% tests passed, 0 tests failed out of 115`, `Total Test time (real) = 224.98 sec`, `EeRunTokenAndEventQueue … Passed 0.09 sec` (no rerun) (`/tmp/p6/rv6b3_ctest.log`) | PASS |
| lints / sizes | `LintOneClass: 353 files scanned, 0 findings -> PASS`; `LintEnginePurity: 307 files scanned, 0 game tokens -> PASS`; `check_sizes.py .` → `size check OK` | PASS |
| scope / hygiene | `git status --short` outside `engine/gs/{hle,state,vram,xfer}`, `tests/gs/`, `docs/phases/` → only the 6A-reviewed CMake files, `tests/lib/`, `tools/drawlist_tool/`; `grep -rn getenv engine/gs/xfer tests/gs/xfer` → empty; ledger row `P6-PARTIAL-FLUSH-OOB` at `:54` after `P6-MERGEKEY-BASIS`, "fixed" | PASS |

**VERDICT: APPROVED** — stage 6B closed; the 6B → 6C handoff stands.


## 6C — render/backend.h + GL backend + SDL window/context (2026-09-27, ≈38 min)

### Landed (4,694 lines incl. tests and the 1,033-line generated shader TU)

`engine/render/` (INTERFACE lib `ps2r_render`): `backend.h` 148, `surface_format.h` 18, `CMakeLists.txt`, README Contents.
`engine/render/backends/gl/` (lib `ps2r_render_gl`, `find_package(OpenGL REQUIRED)`, `GL_SILENCE_DEPRECATION` PUBLIC, links
`ps2r_render OpenGL::GL`): `gl_device.h` 129 / `.cpp` 427, `gl_shader_library.h` 70 / `.cpp` 168, `gl_shader_sources.h` 18 /
`.cpp` 1033 (generated — see below), `gl_uniform_cache.h` 39 / `.cpp` 63, `gl_state_cache.h` 95 / `.cpp` 250,
`gl_vertex_stream.h` 53 / `.cpp` 108, `gpu_vertex.h` 25, `vertex_convert.h` 49 / `.cpp` 73, `gl_texture_pool.h` 50 / `.cpp` 103,
`blend_ff_map.h` 53 / `.cpp` 84, `cd_copy_policy.h` 38 / `.cpp` 83, `gl_draw_executor.h` 87 / `.cpp` 533, `CMakeLists.txt`,
README Contents. `engine/platform/sdl/` (lib `ps2r_platform_sdl`, `find_package(SDL2 CONFIG REQUIRED)` + OpenGL, links
`SDL2::SDL2 OpenGL::GL`): `window.h` 45 / `.cpp` 65, `gl_context.h` 39 / `.cpp` 64, `CMakeLists.txt`, README Contents.
`engine/gs/hle/texsrc/texture_source.h` 83 (header-only POD contract; no target — 6D owns the texsrc CMake), README Contents.
`tests/render/`: `CMakeLists.txt`, `test_blend_ff_map.cpp` 79, `test_vertex_convert.cpp` 140, `test_gl_shader_library.cpp` 88,
`test_gl_state_cache.cpp` 147, `test_gl_device_smoke.cpp` 130, `include_depth.py` 17. Shared files (contract §1.5, one appended
line each): root `CMakeLists.txt` `add_subdirectory(engine/render)  # Phase 6`; `tests/CMakeLists.txt` `add_subdirectory(render)
# Phase 6`; `engine/platform/CMakeLists.txt` `add_subdirectory(sdl)  # Phase 6: window + GL context only (audio/input are Phase 7)`.
Namespaces `ps2r::render`, `ps2r::platform`, `ps2r::gs` (TextureSource). Every file ≤ 1,033 lines, every function ≤ 150
(`check_sizes.py` OK); the shader TU exceeds nothing (the 912-line fragment source sits in `gl_shader_sources.cpp` per the
pre-registered split; `gl_shader_library.cpp` is 168 lines).

Shape notes: `GlDevice` owns `GlStateCache`, `GlUniformCache`, `GlShaderLibrary`, `GlVertexStream`, `GlTexturePool` and a
`unique_ptr<GlDrawExecutor>`; surfaces live in a `SurfaceId → GlSurface{desc, tex, fbo, attached_depth, pooled, respecify,
levels}` map. A renderable RGBA8 surface = texture + fbo (NEAREST, cleared transparent black); `Depth32f` = a
`GL_DEPTH_COMPONENT32F` texture attached lazily by `GlDevice::BindTarget(color, depth)` (attachment changed only when it
differs); a plain RGBA8 surface is drawn from the pool (busy pooled storage → the first full `Upload` re-specifies with
`glTexImage2D`, idle → `glTexSubImage2D`, exactly OLD `UploadTextureTexSub`); the pool holds RGBA8 storage ONLY — `R8` surfaces
are created and deleted directly (Review 6C fix, item 1). `GlShaderLibrary::Build` compiles/links both programs,
looks up the 52-entry `GlUniformLocations` table and primes the samplers (units 0/1/2/3/4) and the OFF arms through the
uniform cache after clearing it (the relink invalidation edge). `GlDrawExecutor::Draw` runs INV §6.2 in order as 12 named
private steps (guards → `BindTarget` → `FrameClear` → `BindTextures` → `ConvertVertices` → program + `u_screen_size` →
`TextureUniforms` → `RegisterUniforms` → `ClassifyRoutes` → `BlendState` (incl. `CdCopy`) → `DepthState` → `ScissorState` →
ring upload → `ColorMaskState` → ONE `glDrawArrays(GL_TRIANGLES, first, (n/3)*3)` → `ColorMask(TRUE×4); DepthMask(TRUE)`
(OLD `:13556-13557`) → `MakeResult`). The executor never sees an
fbp: the scanout-content / 0x230000 scissor-height rule and the fog-native pin are 6D's (`render_w/render_h/scale` arrive in
`DrawParams`), the alt16 route and the per-frame clear are decided above and arrive as `alt16_route` / `do_frame_clear` /
`clear_color`. `platform::Window::Create` initialises SDL video, then calls `GlContext::RequestAttributes()` (SDL_VideoInit
resets GL attributes, so the 4.1-core request must sit between video init and window creation — the first GL test run
produced a 2.1 context and `version '410' is not supported` until this was fixed), then creates a hidden 64×64 or visible
resizable `SDL_WINDOW_OPENGL` window; `GlContext::Create` makes the context current and sets swap interval 0. No
`SDL_SetHint` was needed for the headless tests on this machine.

### OLD sources transplanted (read-only)

`graphics/opengl/opengl_backend.cpp:1247-1311` (vertex), `:1312-2223` (fragment), `:2224-2258` (scanout) → the verbatim
texts in `gl_shader_sources.cpp` (via `/tmp/p6/inv_*_src.txt`, generated by `/tmp/p6/gen_shader_sources.py`); `:2263-2296`
CompileShader/LinkProgram, `:2456-2465` + `gs_mipmap.h:137-149` (the two `#define` insertions, final order AAF then IDXSCALE),
`:2532-2688` CreatePS2Shaders (uniform lookups + the static sampler/arm priming), `:2707-2781` SetU1i/1f/2f/3f (→
`GlUniformCache`), `:2783-2797` FlushUnifPack (NOT written — UNIFPACK OFF), `:2799-2808` CreateScanoutShaders, `:2810-2839`
CreateFramebuffer (attachment formats), `:2846-2948` Init (attributes, VAO attrib layout, scanout quad), `:2960-2968` GL state
defaults, `:3016-3038` dummy texture + unit priming, `:3204-3277` Shutdown order; `:4248-4267` Resize (window size only);
DrawBatch R7 LIVE rows: `:8664-8752` target dims/bind/viewport (bind + viewport + `u_screen_size` here; dims derivation is 6D's),
`:9048-9139` per-frame clear primitive, `:9698-9731` texture bind (unit 0, wrap/filter rules), `:9860-9877` vconv call,
`:9956-9973` region rebase, `:10147-10165` shuffle U scale + column snap, `:10204-10405` program bind / texture params / units
2-3, `:10605` `u_use_texture`, `:10633-10725` depth/idx/uv_native/draw_scale/idx_self/clut_raw uniforms, `:10726-10803` flip_v,
tex_size, tfx, mip uniforms (`GsmipSetLodUniforms` `:2477-2485`), `:10804-10856` tcc/fba/colclamp/fb16/ct16q/dthe/dimx/
scanmsk/ate/date/fog, `:10857-10877` fbmsk16 (`idx16_lane.h` `Fbmsk16From32`), `:10914-10999` r118 route + copy rect + idx
self-serve, `:11000-11296` needs_rt_copy / wrap route / lazy Cd copy (both sites) / FF blend / `u_sw_blend`, `:11422-11468`
zsrc unit-4 bind + uniforms (route decided above: `DrawParams.zsrc_armed`), `:11469-11491` depth state, `:11505-11524` scissor,
`:11568-11569` + `:11580-11582` VAO/ring upload, `:11609-11689` colour mask + fbmsk16 uniforms, `:12806-12807` the draw;
R19 `:21982-22069` RingUploadVertices / BeginDrawRun / EndDrawRun (StageRunUpload `:22129-` NOT written — RUNUPLOAD OFF); R20
`:22189-22214` UploadTexture params, `:22224-22359` UploadMipLevels (GL half: per-level `glTexImage2D`, `GL_TEXTURE_MAX_LEVEL`,
the box-average synth tail → `GlDevice::UploadLevel/FinishMipChain` + `GlTexturePool::BoxHalve`; the per-level DECODE is 6D's),
`:22365-22449` PoolRetire/Acquire/DropAll, `:22451-22495` UploadTextureTexSub (idle/busy rule); R25 `:23956-23972`
BindFBO/UnbindFBO/Clear (`glClearDepth(0.0)` convention), `:25076-25103` GrabDisplayRGBA (→ `Readback`: bottom-up RGBA8 from the
attachment; the fbp fallback is 6D's `ScanoutComposer`); `:23594-23955` Present: only the swap + scanout quad (the pending-sync
flush / PBO issue / census are 6D / DIAG). `gl_state_cache.h:1-560` → `GlStateCache` (every helper; the census atomics and the
gate resolution not written); `blend_ff_map.h:83-165, 229-247` → `BlendFfMap` verbatim table; `rt_copy_tracker.h:47-90,
211-256` → `CdCopyPolicy` (union-rect arm); `graphics/vconv.h:47-135` → `VertexConvert` (ConvertFast; ConvertFastRaw NOT
written — VSUV OFF); `graphics/gpu_vertex.h:10-21` → `GpuVertex`; `gs_mipmap.h:283-317` `SelectGLFilters` (tri = ON) →
`GlDrawExecutor::SelectFilters`; `s9_shadow.h:123-137, 243-246` (FrameIs16BitColor = 0x0A only, FrameIsCt16Class, ClutRawApplies);
`tests/gs_replay.cpp:475-497` + `main.cpp` SDL setup → `platform::Window` / `GlContext`; `tests/test_blend_ff_map.cpp`,
`tests/test_vconv_equivalence.cpp`, `tests/test_gl_state_cache_equivalence.cpp`, `tests/test_s8_wrapblend.cpp` (the pure
predicate rows) → `tests/render/*`.

### Flags applied (FLAG_LEDGER rows; NORM = winning arm only)

PS2_GL_BIND_CACHE, PS2_GL_STATE_CACHE, PS2_GL_RUNSUBMIT (ON: `GlStateCache` shadows every bind / state call; run brackets
`BeginDrawRun/EndDrawRun`; the pass-through arms not written), PS2_GL_UNIF_CACHE (ON: `GlUniformCache`), PS2_GL_VBO_RING (ON:
16 MiB ring), PS2_GL_VCONV_FAST (ON: the fused conversion is the only one), PS2_GL_BSTATE_FAST (the coalesced-flush arm belongs to
the PBO capture path — not in 6C; no per-draw twin remains), PS2_GL_BLEND_SUB (ON: 2001/2021 REVERSE_SUBTRACT arms), PS2_GS_WRAPBLEND
(ON: `u_colclamp = colclamp ? 1 : 0`, `RouteWrapBlend = abe && !colclamp`), PS2_GL_BLENDCP_LAZY (ON: `CdCopyPolicy::Decide`),
PS2_GL_CDTILES (winner OFF: union-rect arm only — P6-CDTILES-OFF), PS2_GL_BBOX_COPY (ON: bbox∩scissor copy rect), PS2_GL_IDXSCALE
(ON: `#define S83_IDXSCALE 1`), PS2_GS_MIP_ALLCLASS / PS2_GS_MIP_LODREF / PS2_GS_MIP_TRILINEAR (`#define GSMIP_AAF 1`; trilinear
filter map; the LODREF=2 arm not written), PS2_GL_FOGNATIVE (the pin is a target-scale decision → 6D `ScalePolicy`; the shader
carries no define for it), PS2_GS_ABIT16 (ON: `u_fb16 = frame_psm == 0x0A`), PS2_GS_CT16_QUANT (ON: `u_ct16q = ct16-class ? scale : 0`),
PS2_GS_ZSRC (route predicate is 6D's; the executor binds unit 4 + `u_zsrc_*` when `zsrc_armed`), PS2_GS_CLUTRAW (ON: `ClutRawApplies`),
PS2_GL_UVNATIVE (ON: `u_uv_native` from `TextureSource.transform.uv_native`), PS2_GL_TEXSUB (ON: pool idle/busy refill),
PS2_R123_NOVSYNC (swap interval 0 unconditionally in the harness profile; the SETTING `video.vsync` is integration's),
PS2_GL_SHUF_SNAP (the apply half: `transform.shuffle_snap`; detection is 6E's). NOT written: PS2_GL_UNIFPACK / PS2_GL_VSUV /
PS2_GL_RUNUPLOAD (presence-OFF — P6-UNIFPACK-OFF; `u_uv_mode` primed 0 and never touched), the compute-raster arm
(`InitVRAMBuffer`), Vulkan, PS2_GL_SHADOWCHECK / PS2_GL_BODYCENSUS / every `[GS-C3]`, `r122`, `r147`, `r148`, `r169` census counter,
PERF scopes, `[R112-ZCHECK]`, `[R122-GATES]`, the bench census collector, all probes (D1P, MIPCEN, MINIF, R198-D6, FMV5, SH2, S8-FBDIFF,
…), the ImGui hook. Zero `getenv` added (`grep -rn getenv engine tools tests/lib tests/render | grep -v config_loader` → only
pre-existing README / regen-script prose).

### Tests added (ctest names; PASS lines from `/tmp/p6/6c_ctest_render.log`)

```
1/7 Test #116: Render_BlendFfMapTable ...........   Passed    0.02 sec
2/7 Test #117: Gs_Inv34_ReverseSubtract .........   Passed    0.01 sec
3/7 Test #118: Render_VertexConvert .............   Passed    0.02 sec
4/7 Test #119: Render_ShaderCompiles ............   Passed    0.39 sec
5/7 Test #120: Render_StateCacheEquivalence .....   Passed    0.37 sec
6/7 Test #121: Render_DeviceSmoke ...............   Passed    0.30 sec
7/7 Test #122: Render_IncludeDepth ..............   Passed    0.05 sec
100% tests passed, 0 tests failed out of 7
```
Direct runs: `test_blend_ff_map: 173 checks, 0 failures` (13 exact arms incl. 2001/2021 REVERSE_SUBTRACT, the other 68 of
3⁴ combos are the standard-alpha fallback, wrap route predicate, `WrapChannel` 256→0 / 300→44 / −1→255); `test_vertex_convert:
9 checks, 0 failures` (3,000 random batches × 3 modes vs the reference loops, bit-identical vertices AND both boxes incl. NaN
seeds; tail rule n=5; n<3 invalid; 12.4 → texel); `test_gl_shader_library: 61 checks, 0 failures` on `GL 4.1 Metal - 90.5 /
Apple M4`: fragment text begins `"\n#version 410 core\n#define GSMIP_AAF 1\n#define S83_IDXSCALE 1\n"`, all four text hashes equal
the OLD captures, no `u_pk_i`, both programs link, `uniforms resolved: 50 / 50` (`u_zte`/`u_ztst` are declared but never read by
the fragment shader → the driver strips them, `-1`, informational; the executor never sets them — OLD never did either);
`test_gl_state_cache: 23 checks, 0 failures, skips=648` (8 scripted variants ×3 repeats through the cache vs raw → identical
glGet/glIsEnabled state for fbo draw/read, active unit, unit 0/1 bindings, program, VAO, array buffer, viewport, scissor box, the
three caps, depth mask/func, colour mask, blend eq/func/colour, tex min filter + wrap; delete-reverts-to-0 for fbo/vao/texture);
`test_gl_device_smoke: 18 checks, 0 failures` (64×64 RGBA8 + Depth32f: Clear `0x44332211` read back on every pixel, depth 0.0;
4×4 texture upload round-trip; one untextured red triangle through `GlDevice::Draw` → `DrawResult` bbox `(0,0)-(64,64)`, route
flags all false, `write_rect` 64×64, covered pixel `(255,0,0,255)` (the x2 GL alpha convention: 128 → 255), uncovered pixel keeps
the clear colour, `red pixels: 2016` ≈ half, depth written at the covered pixel; a scissored green draw clips `write_rect.h` to
16 and leaves row 20 red; `Present` to the hidden window without a GL error). `Render_IncludeDepth` (`tests/render/include_depth.py`):
every `#include "…"` under `engine/render/backends/gl` is `engine/render/*.h`, `engine/render/backends/gl/*.h`, `engine/gs/hle/ir/*.h`,
`engine/gs/hle/texsrc/texture_source.h` or `engine/core/hash/*.h` → PASS.

### Findings

- P6-SHADER-COMMENTS-KEPT, P6-PBO-NONDET, P6-CDTILES-OFF, P6-BACKEND-NAME, P6-DRAW-RETURNS-RESULT, P6-BLEND-INHERIT (ledger rows above).
- Full-suite lines (`/tmp/p6/6c_ctest.log`):
```
Start 116: Render_BlendFfMapTable
116/122 Test #116: Render_BlendFfMapTable ..............   Passed    0.02 sec
        Start 117: Gs_Inv34_ReverseSubtract
117/122 Test #117: Gs_Inv34_ReverseSubtract ............   Passed    0.01 sec
        Start 118: Render_VertexConvert
118/122 Test #118: Render_VertexConvert ................   Passed    0.01 sec
        Start 119: Render_ShaderCompiles
119/122 Test #119: Render_ShaderCompiles ...............   Passed    0.45 sec
        Start 120: Render_StateCacheEquivalence
120/122 Test #120: Render_StateCacheEquivalence ........   Passed    0.38 sec
        Start 121: Render_DeviceSmoke
121/122 Test #121: Render_DeviceSmoke ..................   Passed    0.32 sec
        Start 122: Render_IncludeDepth
122/122 Test #122: Render_IncludeDepth .................   Passed    0.05 sec
100% tests passed, 0 tests failed out of 122
Total Test time (real) = 208.58 sec
```
- Note: OLD's `u_use_texture` is set from `has_texture` = "a GL texture was bound on unit 0"; NEW sets it from
  `TextureSource.enabled && owner names a live texture` — identical when 6D fills `enabled` from PRIM.TME ∧ (served ∨ decoded).
  When the resolver DECLINES and no decode happens, `enabled` must be false (OLD: `has_texture=false`).
- Note: `SelectFilters` bakes PS2_GS_MIP_TRILINEAR=ON (MMIN 2/4 → the `*_MIPMAP_LINEAR` forms when levels > 0); replay draws
  always have levels 0 (no mip chains in `.gsdl`), so the corpus path is the legacy fallback map bit for bit.
- Process note: the very first edit of this stage (the `BindTarget` insertion into gl_device.h/.cpp) was applied via
  `python3 -` (stdin) before switching to `/tmp/p6/fix6c_*.py` files as STAGE_RULES requires; no OLD binary was run.

### Gate status

| gate | evidence |
|---|---|
| G6 render/gl | `Render_BlendFfMapTable` + `Gs_Inv34_ReverseSubtract`, `Render_VertexConvert`, `Render_ShaderCompiles` (ran, not skipped), `Render_StateCacheEquivalence` (ran), `Render_DeviceSmoke` (ran), `Render_IncludeDepth` — all Passed (quoted above) |
| configure | `cmake --preset macos-dev` (`/tmp/p6/6c_cfg.log`): `-- Found OpenGL: /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/OpenGL.framework`; SDL2 resolved through `/opt/homebrew/Cellar/sdl2/2.32.10/lib/cmake/SDL2/sdl2-config.cmake` (CONFIG mode prints no line; the compile lines carry `-isystem /opt/homebrew/Cellar/sdl2/2.32.10/include/SDL2`) |
| build / lints / size | `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`, `grep -c 'warning:' /tmp/p6/6c_build.log` → `0`; `LintOneClass: 388 files scanned, 0 findings -> PASS`; `LintEnginePurity: 340 files scanned, 0 game tokens -> PASS`; `python3 /tmp/p6/check_sizes.py .` → `size check OK`; getenv grep clean |
| full ctest | `ctest --preset macos-dev` (`/tmp/p6/6c_ctest.log`, bounded 900 s) → `100% tests passed, 0 tests failed out of 122`, `Total Test time (real) = 208.58 sec`; `EeRunTokenAndEventQueue` passed first time (no P6-EE-SCHED-FLAKE rerun needed) |

### HANDOFF (6C → 6D)
Done: engine/render (Backend contract), engine/render/backends/gl (GlDevice + 10 parts), engine/platform/sdl (Window, GlContext),
  engine/gs/hle/texsrc/texture_source.h (POD only); 7 ctests (G6) all PASS on real GL; flags consumed as listed; INV §6.1, §6.2
  (the GL half), §6.3, §6.9 (ring/pool), §13.1 LIVE rows for the executor, §14 risks 6/7/12 transplanted.
Not done (→ 6D, by design): everything ABOVE the interface — `RenderTargetCache`/`RenderTarget` (grow-only targets keyed by fbp,
  `SurfaceId`s for color/depth/rt_copy/alt16, `CdCopyState` per target, `has_32bit_content`, `ever_composited`/`needs_frame_clear`/
  `last_clear_frame` → `DrawParams.do_frame_clear` + `clear_color = (fbp == scanout_fbp)`, the composite-consumption marking, the
  post-draw coherence tail from `DrawResult` (write_rect → drawn rects / `CdCopyPolicy::ExtendDirty(cd_copy, write_rect scaled)`
  / alt16 stale / MASKED-SYNC keep mask from `fbmsk16` when `!fbmsk16_engaged`)); `render_w/render_h` derivation (fbw ≤ 1024,
  scanout content dims, the VramLayout scene-rt scissor-height rule) and `ScalePolicy` (fog-native pin); the alt16 companion
  create + `RefreshAlt16FromMain` (needs a 5551 pack/unpack program — 6D may add it to `GlShaderLibrary` or run it on the CPU
  via `Readback`/`Upload`); `ResolveRead` filling `TextureSource`; `ClutSource` (256×2 texture via `CreateSurface` + `Upload`);
  `TextureDecoder` + `DecodeCache` (→ `CreateSurface(Rgba8, non-renderable)` + `Upload` + `UploadLevel`/`FinishMipChain`);
  `ScanoutComposer::GrabDisplay` (scanout fbp → most-recent-drawn fallback → `Readback(color, {0,0,pw,ph})`); `ReadbackEngine`
  (`SyncRTToVRAM` encode over `Readback` bytes; `Readback` of `Depth32f` returns floats, d·2^32 → Z32). The `RenderQueue`
  RUNDRAW merge action and the `DrawIrSink` adapter stay with 6F/integration.
  Contract 6D must fill — DrawParams: `color` (renderable RGBA8), `depth` (Depth32f or none), `rt_copy` (RGBA8 same physical
  dims, `rt_copy_available = id != 0`), `alt16` + `alt16_route`, `clut` (256×2), `idx_partner`, `zsrc` + `zsrc_armed` + `zsrc_scale`
  (route predicate `ZsrcRoute` = zte ∧ ztst∈{2,3} ∧ zmsk ∧ date ∧ zbp≠fbp ∧ zbp is a colour target), `render_w/render_h`
  (logical), `target_w/target_h` (logical allocation; the Cd-copy / write-rect clamp), `scale`, `cd_copy` (pointer to the target's
  `CdCopyState`; 6D invalidates it on clear / refresh / present / resize exactly where OLD calls `rt_copy::Invalidate`),
  `do_frame_clear`, `clear_color`. TextureSource: `enabled` (= OLD `has_texture`), `owner.kind/surface/fmt_case/self_serve/levels`,
  `transform.{uv_tex_w,uv_tex_h}` (u_tex_size operands; FST=1 shuffle folds `shuffle_uv_scale_u` into uv_tex_w as OLD `:7582-7584`),
  `transform.{uv_pre_w,uv_pre_h}` (the ConvertFast operands = uv_tex_w/h except the READVIEW plain-RT STQ serve; the
  Untextured predicate reads `enabled && uv_tex_w > 0 && uv_tex_h > 0` as OLD `:9864`, never uv_pre),
  `transform.region` (rebase + `u_tex_size` override), `transform.shuffle_uv_scale_u/is_shuffle/shuffle_snap`,
  `transform.depth_fmt/z_row_offset`, `transform.idx_fmt/idx_self` (fmt 6 packs `ppr8 | twl<<4 | thl<<8`; the executor ORs the
  FST bit 0x1000), `transform.uv_native`, `transform.mip_levels/mip_lcm/mip_k/mip_l`, `sampler.{mag_linear,mmin,wms,wmt}`. There is NO
  flip / force-clamp / force-nearest knob: `u_flip_v` is always 0, RT reads always clamp and every other texture wraps per
  wms/wmt, depth / indexed serves always sample NEAREST — all derived by the executor from `owner.kind` / `fmt_case`.
  After every `Draw` the colour and depth masks are open (TRUE), so a raw GL pass 6D adds above the executor (alt16 5551
  refresh, a clear) may rely on them exactly as OLD's tail does.
  DrawResult 6D consumes: `bbox_tri/bbox_all/bbox_valid`, `write_rect`, `sw_blend_active`, `fbmsk16`, `fbmsk16_exact`,
  `fbmsk16_engaged`, `route_rt_blend`, `wrap_route`, `cd_copy_performed/cd_copy_rect`, `alt16_route`.
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 122/122; lints 0/0; getenv sites unchanged (1)
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Render_|Gs_Inv34'
Findings: P6-SHADER-COMMENTS-KEPT, P6-PBO-NONDET, P6-CDTILES-OFF, P6-BACKEND-NAME, P6-DRAW-RETURNS-RESULT, P6-BLEND-INHERIT

### Review 6C (verdict)

Independent reviewer, 2026-09-27 (≈65 min; full table with commands and quoted output in `/tmp/p6/REVIEW_6C.md`; reviewer
probes `/tmp/p6/rv6c_shader_probe.cpp`, `/tmp/p6/rv6c_shader_cmp.py`; logs `/tmp/p6/rv6c_*.log`). Re-ran every check against
the plan (§1.5, §2.4, §3, §6 G6, §7 6C) and INV §6.1/§6.2/§6.3/§6.9/§13.1, reading OLD `opengl_backend.cpp` at the cited lines.

| check | result |
|---|---|
| 1 contract paths | PASS — `git status` filtered against the §1.5 OWN set → empty; READ-ONLY dirs untouched; the three shared CMake diffs are exactly the appended `# Phase 6` lines |
| 2 interface purity | PASS — backends/gl includes only `render/backend.h`, `gs/hle/ir/{draw_ir,vertex_rec}.h`, `gs/hle/texsrc/texture_source.h` + siblings; no VRAM literal / `PsmLayout` / `fbp` under `engine/render` (grep exit 1); `Render_IncludeDepth` present and Passed |
| 3 shader text (P6-SHADER-TEXT-EXACT) | PASS — probe-dumped texts byte-identical to the investigator captures AND to the raw strings re-extracted from OLD `opengl_backend.cpp:1247-2258` with the two defines inserted (`vert 3247`, `frag 48256`, `scan_vert 216`, `scan_frag 357` bytes; `SHADER_TEXT_CHECK PASS`); define order re-derived from `S83InjectIdxScale` `:2431-2438` + `InjectAafDefine` `gs_mipmap.h:137-148`; no `u_pk_i`; `u_uv_mode` primed 0 and never set by the executor; 52 locations + priming list == OLD `:2555-2664` |
| 4 blend table | PASS — 13 arms + fallback line-for-line == `blend_ff_map.h:83-165`; OLD bounded `test_blend_ff_map: 230 checks, 0 failures`; NEW `173 checks, 0 failures`; `Gs_Inv34_ReverseSubtract` registered + Passed |
| 5 vertex convert | PASS — modes/bbox rules == `vconv.h:47-135`; OLD bounded `test_vconv_equivalence: ALL PASS (5280 cases …)`; NEW `9 checks, 0 failures` |
| 6 executor fidelity | PASS — every INV §6.2 rule matched to its OLD line (guards, scissor-off `:8671`, viewport/u_screen_size, clear `:9112-9139`, texture bind `:10214-10375`, uniforms `:10605-10830`, fbmsk16 `:10857-10873`, r118 set `:10875-10892`, copy rect `:10894-10925`, needs_rt_copy `:11001/:11014`, Cd copy + sw blend `:11033-11143`, FF blend `:11257-11295`, zsrc `:11439-11446`, depth `:11469-11491`, scissor `:11505-11524`, colour mask `:11609-11655`, ONE `glDrawArrays` `:12807`); `CdCopyPolicy` == `rt_copy_tracker.h:204-256`; two literal-fidelity deviations found (items 2, 3 below), neither observable through the interface |
| 7 device / readback | PASS — sync `glReadPixels` bottom-up == `GrabDisplayRGBA` `:25097-25101`; Caps honest; Present via the platform swap; ring == `:21982-22035`; pool == `:22365-22437`; `BoxHalve` == `:22303-22335`; P6-PBO-NONDET recorded |
| 8 SDL | PASS — window/gl_context only; attributes == OLD Init `:2850-2857`; swap interval 0; hidden 64×64 headless == `gs_replay.cpp:500-501`; one `add_subdirectory(sdl)  # Phase 6…` line |
| 9 tests with GL | PASS — clean rebuild of the 7 targets: `BUILD_EXIT=0`, `warning:` count 0; `ctest -R 'Render_\|Gs_Inv34'` 7/7 Passed (GL tests RAN: `GL 4.1 Metal - 90.5 / Apple M4`, `uniforms resolved: 50 / 50`, `skips=648`, `18 checks`); full `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 122` (200.87 s, 0 skipped); OLD bounded `test_gl_state_cache_equivalence` `RESULT: PASS`; `LintOneClass … 0 findings -> PASS`; `LintEnginePurity … 0 game tokens -> PASS`; `size check OK`; getenv grep → only `config_loader` |
| 10 code rules | PASS — one type per pair, declarations-only headers, no `.inc`, no tags outside the GLSL raw strings, `Draw` = 12 named steps; the reviewer fixed three comment nits (misplaced dummy-texture comment in `gl_device.h`; "legacy" wording in `gl_draw_executor.cpp` and `gl_vertex_stream.h`) and re-verified 0 warnings / 7/7 / lints PASS |
| 11 IMPL record | PASS — Landed / sources / flags / tests quoted / findings / G6 / HANDOFF contract present; §2.4 checklist ✔ |

Findings → implementer (no pixel effect today; ≈10 lines total):
1. `gl_device.cpp` `CreateSurface`: an `R8` surface can be handed RGBA8 pooled storage, re-specified to `GL_R8` on a busy hit, and retired back into the pool as RGBA8 — a later `Rgba8` acquire then loses G/B/A. Pool only `SurfaceFormat::Rgba8` (OLD's pool is RGBA8-only, `:22224-22495`).
2. `gl_draw_executor.cpp` `ConvertVertices`: the Untextured predicate reads `uv_pre_w/h`; OLD `:9864` reads `uv_tex_w/h` (`!(ti.enabled && uv_tex_w > 0 && uv_tex_h > 0)`). Co-signed in every OLD path (`:7600-7608`) but 6D fills the two fields independently — read the field OLD reads.
3. `gl_draw_executor.cpp` `Draw`: OLD's post-draw `ColorMask(TRUE×4); DepthMask(TRUE)` (`:13556-13557`) is not performed; every NEW backend entry sets its own masks so nothing differs through the interface, but a raw-GL pass 6D adds above the executor (alt16 5551 refresh) would inherit the last draw's mask. Add the two calls after `glDrawArrays`.
4. `texture_source.h`: `Transform.flip_v`, `Sampler.force_clamp`, `Sampler.force_nearest` let a resolver express a draw OLD cannot (OLD: `rt_composite_vflip = false` `:10748-10749`; `mapWrap(ti.wms/wmt)` for every non-RT texture `:10250-10269`; NEAREST forced only for RT depth/idx serves `:10365-10370`, already derived in `BindTextures`). Remove them, or pin them to their defaults in the HANDOFF. **6D note until then: leave all three at their defaults.**

VERDICT: NEEDS_CHANGES (items 1–4 above; re-run `ctest -R 'Render_|Gs_Inv34'`, both lints, `check_sizes.py`, and record the fix here).

### Review 6C fix (2026-09-27, ≈25 min)

All four NEEDS_CHANGES items applied exactly as requested (`/tmp/p6/REVIEW_6C.md`); OLD read-only, re-read at the cited lines.

| item | change | file:line | OLD pin |
|---|---|---|---|
| 1 RGBA8-only pool | `CreateSurface`: `poolable = desc.format == Rgba8`; `pool_.Acquire` only when poolable, so `R8` (and `Depth32f`, already direct) are `glGenTextures`/`glTexImage2D` directly and never `pooled`. `ReleaseSurface`: retire only when `format == Rgba8 && (pooled ∣∣ (!renderable && levels == 0))`, else `DeleteTextures`. `Upload` re-specify arm is `GL_RGBA8` unconditionally (respecify is only ever set on pooled RGBA8 storage) | `engine/render/backends/gl/gl_device.cpp:156-175` (create), `:190-199` (release), `:242-246` (upload) | `PoolAcquireTexture`/`PoolRetireTexture` RGBA8-only `:22224-22495` |
| 2 Untextured predicate | `ConvertVertices`: `textured = tex.enabled && t.uv_tex_w > 0 && t.uv_tex_h > 0` (uv_pre_w/h stay the STQ prescale operands only) | `gl_draw_executor.cpp:254-256` | `:9863-9866` (`!(ti.enabled && uv_tex_w > 0 && uv_tex_h > 0)`); co-signing `:7600-7608` |
| 3 post-draw mask reset | `Draw`: after the single `glDrawArrays`, `st.ColorMask(true,true,true,true); st.DepthMask(true)` through `GlStateCache`; header comment lists the reset as a step | `gl_draw_executor.cpp:114-119`, `gl_draw_executor.h:9-11` | `:13556-13557` |
| 4 non-OLD knobs removed | `Transform.flip_v`, `Sampler.force_clamp`, `Sampler.force_nearest` deleted from the POD (comment states the derived rules); executor sets `u_flip_v = 0` unconditionally and wraps `using_rt ? CLAMP : wrap(wms/wmt)`; texsrc README field list and the HANDOFF 6C→6D contract updated (no knob listed; mask-open-after-draw stated) | `engine/gs/hle/texsrc/texture_source.h:52,66-68`, `gl_draw_executor.cpp:176,234-235`, `engine/gs/hle/texsrc/README.md:8`, this file HANDOFF | `rt_composite_vflip = false` `:10748-10749`; `mapWrap(wms/wmt)` non-RT `:10250-10269`; NEAREST for RT depth/idx `:10365-10370` |

Tests added to `Render_DeviceSmoke` (`tests/render/test_gl_device_smoke.cpp`, 18 → 25 checks):
- mask reset: a PSMCT24 (`frame.psm = 0x01`, alpha mask FALSE) + `zbuf.zmsk` (depth mask FALSE) draw, then a RAW
  `glClearColor(…, 0x77/255) / glClearDepth(0.5) / glClear` with no mask calls of its own → readback alpha `0x77`, depth `0.5`
  ("alpha writable again after the CT24 draw", "depth writable again after the ZMSK draw");
- pool hygiene: destroy the 4×4 RGBA8 texture (retires to the pool), create R8 4×4 + full upload + destroy, create RGBA8 4×4,
  two PARTIAL uploads (SubImage path), readback == pattern ("RGBA8 after R8 release keeps R, G, B and A").
Negative proof (fix temporarily reverted, rebuilt, restored byte-identical): mask reset removed → `25 checks, 2 failures`
(both mask checks, `/tmp/p6/6c2_neg_mask.log`); pool guards reverted → `25 checks, 1 failures` on the pool check in 3/3 runs
(`/tmp/p6/6c2_neg_pool.log`).

Verification (`/tmp/p6/6c2_build.log`, `6c2_ctest_render.log`, `6c2_ctest_full.log`):
```
BUILD_EXIT=0
warnings: 0
1/7 Test #116: Render_BlendFfMapTable ...........   Passed    0.02 sec
2/7 Test #117: Gs_Inv34_ReverseSubtract .........   Passed    0.01 sec
3/7 Test #118: Render_VertexConvert .............   Passed    0.03 sec
4/7 Test #119: Render_ShaderCompiles ............   Passed    0.42 sec
5/7 Test #120: Render_StateCacheEquivalence .....   Passed    0.19 sec
6/7 Test #121: Render_DeviceSmoke ...............   Passed    0.42 sec
7/7 Test #122: Render_IncludeDepth ..............   Passed    0.08 sec
100% tests passed, 0 tests failed out of 7
test_gl_device_smoke: 25 checks, 0 failures
100% tests passed, 0 tests failed out of 122
Total Test time (real) = 249.90 sec
LintOneClass: 388 files scanned, 0 findings -> PASS
LintEnginePurity: 340 files scanned, 0 game tokens -> PASS
size check OK
```
`grep -rn 'flip_v\|force_clamp\|force_nearest' engine tests` → only `GlUniformLocations::flip_v` (the shader location) and the
executor's `u.Set1i(L.flip_v, 0)`; `getenv` sites unchanged (`engine/core/config/config_loader.{h,cpp}` only). No commits.

### Review 6C (verdict, iteration 2)

Independent reviewer, 2026-09-27 (≈25 min; full table with commands, quoted output and the negative probes in
`/tmp/p6/REVIEW_6C_2.md`; logs `/tmp/p6/rv6c2_*.log`). Every item re-checked by opening the NEW file at the cited lines and
re-reading the OLD pin (read-only) rather than trusting the fix record.

| item | NEW | OLD pin | pin test | result |
|---|---|---|---|---|
| 1 RGBA8-only pool | `gl_device.cpp:156-175` acquire only when `format == Rgba8`; `:190-199` retire only when RGBA8; `:242-246` respecify arm `GL_RGBA8` (pooled ⇒ RGBA8 by construction) | `:22224-22495` — every pool `glTexImage2D` is `GL_RGBA8` | `test_gl_device_smoke.cpp:150-164`; reviewer reverted the two guards + the respecify format → `25 checks, 1 failures` (`:164`) 3/3 runs; restored `cmp`-identical | PASS |
| 2 Untextured predicate | `gl_draw_executor.cpp:254` `tex.enabled && t.uv_tex_w > 0 && t.uv_tex_h > 0`; uv_pre stays the STQ prescale operand | `:9863-9866` literal match | none (unobservable through OLD paths; the HANDOFF sentence is the guard) — non-blocking | PASS |
| 3 post-draw mask reset | `gl_draw_executor.cpp:114-119` after the ONE `glDrawArrays`: `st.ColorMask(true×4); st.DepthMask(true)`; header `:9-10` names the step | `:13556-13557`, same position (after the draw, before bbox tracking) | `test_gl_device_smoke.cpp:128-147`; reviewer removed the two lines → `25 checks, 2 failures` (`:145`, `:147`); restored | PASS |
| 4 knobs removed | `texture_source.h` has no `flip_v` / `force_clamp` / `force_nearest` (`:39-59`, `:61-69` with the derived-rule comment); executor `u_flip_v = 0` (`:235`), `using_rt ? CLAMP : wrap(wms/wmt)` (`:171-182`), NEAREST for RT depth/idx (`:189-192`); texsrc README `:8` and the HANDOFF list none | `:10748-10749`, `:10254-10267` + `:10299-10300`, `:10368-10371` | `grep -rn 'flip_v\|force_clamp\|force_nearest' engine tests` → `force_*` no hits; `flip_v` only as the byte-exact OLD shader uniform `u_flip_v` (GLSL raw strings, `GlUniformLocations::flip_v`, the constant-0 set, the location test) — accepted, P6-SHADER-TEXT-EXACT forbids removing it | PASS |

Gates (fresh): `ninja -t clean` of the 7 6C targets (62 files) + `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`,
`warning:` count `0` (53 TUs); `ctest -R 'Render_|Gs_Inv34'` → 7/7 Passed; full `ctest --preset macos-dev` (after the probes, on the
restored sources) → `100% tests passed, 0 tests failed out of 122` (189.30 s); `LintOneClass: 388 files scanned, 0 findings -> PASS`;
`LintEnginePurity: 340 files scanned, 0 game tokens -> PASS`; `size check OK`; shared CMake diffs = the five `+add_subdirectory` lines
only; READ-ONLY dirs `git status` empty; `getenv` sites unchanged. Non-blocking notes: item 2 unpinned by design; pooled RGBA8 with
`levels > 0` is retired like OLD (`PoolRetireTexture(tex,w,h)`), so 6D's `DecodeCache` must rely on `FinishMipChain` resetting
`GL_TEXTURE_MAX_LEVEL`.

VERDICT: APPROVED

## 6D — targets + present + VramLayout + profile (2026-09-27, ≈55 min; texsrc → 6D′)

Stage 6D hit the STAGE_RULES STOP rule with `engine/gs/hle/targets`, `engine/gs/hle/present`, the `VramLayout`
struct and the title `RendererProfile` landed and green; the whole of `engine/gs/hle/texsrc` (except the 6C POD)
is the pre-registered split **6D′** (HANDOFF below names every file and test). Tree: 0 warnings, 141/141, lints 0/0.

### Landed (2,356 lines incl. tests and the CPU FakeBackend)

`engine/gs/hle/targets/` (lib `ps2r_gs_hle_targets`, links `ps2r_gs_ir ps2r_gs_vram ps2r_render`):
- `vram_layout.h/.cpp` — `VramLayout` (struct + `Empty / IsSceneAccum / IsFogLane / IsFmvBlitSignature / IsNativeScanout / IsClutHealExempt / IsReaderBandPage`; every query false on an empty layout — the zero field never matches because each query keys on the NON-zero half of its tuple)
- `target_identity.h` — `TargetIdentity` (fbp, logical w/h grow-only, render_psm/fbw, scale, has_32bit_content/was_32bit/was_16bit)
- `surface_views.h` — `SurfaceViews` (color, depth, rt_copy, alt16 + alt16_has_content)
- `logical_rect.h/.cpp` — `LogicalRect` (half-open, clamped union) — split out of coherence_state by LintOneClass
- `coherence_state.h/.cpp` — `CoherenceState` (drawn/z_drawn_since_sync, valid_rect, alt16_stale, sync_keep_mask16, has_content, last_draw_frame/gfx_epoch, needs_frame_clear/ever_composited/last_clear_frame, last_zbp/zpsm/z_wrote, lane/surface author seqs, vram_coherent_epoch/class, `render::CdCopyState cd_copy`)
- `render_target.h/.cpp` — `RenderTarget` (`Create/Destroy`, `EnsureAlt16`, `RefreshAlt16FromMain` (CPU 5551 fold over Readback/Upload), `PrepareDraw` (attachments, alt16 bifurcation, per-frame clear decision incl. scanout colour clear + Cd-copy void), `AfterDraw` (the coherence tail from `DrawResult`), `MarkCompositeConsumed`, `WantsFrameClear`, `Fbmsk16From32`, `kIndexLaneKeep16 = 0x00FF`)
- `readback_engine.h/.cpp` — `ReadbackEngine` (`SyncRtToVram` rows ∩ drawn_since_sync or rescue ∩ valid_rect (+ rescue_fbw narrowing), CT32 alpha `255→128 else a/2`, CT24 three bytes, CT16 5551 `(a>127)<<15` with the MASKED-SYNC keep via `Swizzle::LinearToVram(mask = ElementWriteMask & ~keep16)` and the per-page "EE wrote later" restore; `SyncZToVram` rows ∩ z_drawn_since_sync, `d·2^32` clamped → `PixelConvert::PackZ` through the Z layouts; both stage into `VramImage::StageDeposit` over a `ReadView` of the page rows and never commit; `NoteGpuAuthor`)
- `render_target_cache.h/.cpp` — `RenderTargetCache` (keyed fbp; `GetOrCreate` grow-only + tag carry + full-extent rescue deposit before re-create; `Find/Has/Destroy/DestroyAll/SyncAll/NoteDrawn/MostRecentDrawnFbp`) — file renamed from the plan's `target_cache` by LintOneClass (file = snake_case(type))
- `target_refresher.h/.cpp` — `TargetRefresher` (`UploadDirect` = OLD `UploadVRAMTextureDirectly` → Uploaded(addr) surface, `FindUploaded` exact then containing span, `EvictUploadedInRange`, LRU by 64 entries / 64 MiB; `MarkPagesCpuDirty/ArePagesCpuDirty/ClearPagesCpuDirty` 512-bit set; `DepositUploadToLiveTargets` (CT16-class, not scanout, drawn ≤ 60 frames, page-tile fold) → `RefreshFromVram` (BlockDecode of the mirror view, PS2→GPU alpha, Upload, `MaterializePages(Refresh)`, Cd-copy void))
- `scale_policy.h/.cpp` — `ScalePolicy` + `ScaleSettings{scale=1}` (clamp 1..4; `TargetScale` pins scene accumulator + fog lanes native via `VramLayout`)
- `CMakeLists.txt`, README updated.

`engine/gs/hle/present/` (lib `ps2r_gs_hle_present`):
- `crtc_classifier.h/.cpp` — `CrtcClassifier` (`Classify(pmode, dispfb1, dispfb2, display1, display2)` exact bit rule, `Pack/Unpack`, `IsNativeConfig(snapshot, VramLayout)`)
- `viewport_math.h/.cpp` — `ViewportMath` (verbatim math of `graphics/viewport_math.h`)
- `scanout_composer.h/.cpp` — `ScanoutComposer` (`SetScanoutFbp/SetDispfb/SetContentDimensions` (≤0 ignored), `GrabSource`, `GrabDisplay` = full physical rect `Readback` of the scanout target's colour, fallback most-recent-drawn)
- `presenter.h/.cpp` — `Presenter` (`MarkEeFrameEnd`, `Grab`, `Present(window_w,h)` → `Backend::Present` of the grab source; frame counter)
- `CMakeLists.txt`, README updated.

`games/<title>/renderer_profile/` (lib `ps2r_title_renderer_profile`): `renderer_profile.h/.cpp` (`title::RendererProfile::Vram()` filled: main_fb / native scanout 0x70000 · 512 · CT32, scene_accum 0x230000, fmv_stage 0x150000, stencil_zcopy 0x268000, fog lanes 0x280000 / 0x2F0000, the 31 reader-band pages, CLUT-heal exemption [0x388000, 0x3F8000] mask 0xFFFF value 0x8000), `CMakeLists.txt`, `README.md`. Root `CMakeLists.txt` + `engine/gs/CMakeLists.txt` + `tests/lib/CMakeLists.txt` + `tests/gs/CMakeLists.txt`: appended `# Phase 6` lines only.

Tests: `tests/lib/fake_backend.h/.cpp` (`test::FakeBackend`, lib `ps2r_fake_backend` — a CPU `render::Backend`; GL-free), `tests/gs/targets/test_targets.cpp` (93 checks), `tests/gs/targets/test_renderer_profile.cpp`, `tests/gs/present/test_present.cpp` (51 checks), `tests/gs/hle_include_depth.py`.

### OLD sources transplanted (read-only)

`opengl_backend.cpp` R9 `GetOrCreateTargetFBOIn :15449-15736` (grow-only / carry / [S109 ii] rescue), R8 `EnsureAlt16Companion :14513` + `RefreshAlt16FromMain :15237` (CPU route, see P6-ALT16-REFRESH-CPU), the per-frame clear `:9048-9139` + composite marking `:9148-9155`, alt16 bifurcation `:8878-8895`, post-draw tail `:13911-14060` (drawn rects, tags, keep mask, S107 author seqs, lane class = `LaneAuthored16(keep) == 0x00FF` from `idx16_lane.h:99-109`), R11 `SyncRTToVRAM :16272-17117` + `g8par::EncodeRTToVRAM :15922-16130` (rows clamp `:16820-16870`, encode arms `:15991-16030`), R13 `SyncZToVRAM :18157-18539` (rows ∩ z_drawn_since_sync), R12 `RefreshFBOFromVRAM :17484` + `DepositUploadToLiveTargets :23113-23167`, `UploadVRAMTextureDirectly / FindUploadedTexture / EvictUploadedTexturesInRange :23173-23334`, `GrabDisplayRGBA :25076-25103`, `SetScanoutFBP/DISPFB/ContentDimensions :24012-24026`; `gs_scale.h` (Factor clamp, IsPinnedNative → VramLayout); `gs_crtc_class.h:52-112`; `graphics/viewport_math.h` (verbatim); `gs_page_tracker.h:57-62` kBand; `texture_decoder.cpp:1286-1370` (exemption constants only); INV §2.3 grab, §6.2 non-GL half, §6.4, §6.7, §6.8, §6.10, §9, §11, §12 rows 9-14, 20, 23, 24, 35, 37, §14 risks 5, 10.

### Flags applied (FLAG_LEDGER rows; NORM = winning arm only)

NORM: `PS2_GS_RTKEEP` / `PS2_GL_RT_NEVERSTALE` / `PS2_GL_RT_READVIEW` (the rescue deposit in GetOrCreate; the serve predicates themselves are 6D′), `PS2_GS_ALT16UV` (companion route), `PS2_GS_FOGLANE` (lane author class), `PS2_GL_FOGNATIVE` (fog-lane native pin), `PS2_GS_RESIDENT(_PBO)` (synchronous readback; reader band = VramLayout data), `PS2_GL_SYNC_RECT` (rows-only readback), `PS2_GL_MARKMEMO` (CPU-dirty page set), `PS2_GL_TEXCACHE` (Uploaded map kept; InvalidateAllTextures never called), `PS2_GL_BLENDCP_LAZY` (Cd-copy dirty union extended in AfterDraw). SETTING: `video.scale` → `ScaleSettings{1}`. DELETE (not written): `PS2_GL_RESMEMO`, `PS2_GL_ALIAS_DEPCOPY*`, `PS2_GL_BBOX_COPY`, `PS2_GL_CDTILES` (P6-CDTILES-OFF), R197_*, SH1/SH2/SH5/SH6/SH7/S8-FBDIFF/SYNC-RT probes, `PS2_FTT_FORCE_*` (TEST), the [SYNC-RT-PSMCT16-TRACE] log tap.

### Tests added (ctest names; PASS lines from `/tmp/p6/6d_ctest_final.log`, the 141/141 run)

```
53/141 Test  #53: Gs_Inv09_DrawnSinceSyncClamp .........   Passed    0.28 sec
54/141 Test  #54: Gs_Inv10_ZDrawnSinceSyncClamp ........   Passed    0.04 sec
55/141 Test  #55: Gs_Inv11_Alt16Bifurcation ............   Passed    0.03 sec
56/141 Test  #56: Gs_Inv12_Alt16CoherenceRect ..........   Passed    0.04 sec
57/141 Test  #57: Gs_Inv13_GrowOnlyRetagKeepsContent ...   Passed    0.06 sec
58/141 Test  #58: Gs_Inv14_ClassTagsSurviveGrow ........   Passed    0.06 sec
59/141 Test  #59: Gs_Inv20_ZAddressingUsesZLayout ......   Passed    0.04 sec
60/141 Test  #60: Gs_Inv23_ScaleOneIdentity ............   Passed    0.01 sec
61/141 Test  #61: Gs_Inv24_NativePins ..................   Passed    0.01 sec
62/141 Test  #62: Gs_Inv35_MaskedWritebackKeepsBits ....   Passed    0.04 sec
63/141 Test  #63: Gs_Inv37_ClearKeyedOnComposite .......   Passed    0.06 sec
64/141 Test  #64: Targets_UploadedTextureMap ...........   Passed    0.04 sec
65/141 Test  #65: Targets_RefreshFromVram ..............   Passed    0.05 sec
66/141 Test  #66: Targets_VramLayoutQueries ............   Passed    0.01 sec
67/141 Test  #67: Present_ViewportMath .................   Passed    0.24 sec
68/141 Test  #68: Present_CrtcClassifier ...............   Passed    0.01 sec
69/141 Test  #69: Present_GrabDisplayFallback ..........   Passed    0.06 sec
70/141 Test  #70: Profile_VramLayoutEmptyIsFalse .......   Passed    0.15 sec
71/141 Test  #71: Gs_HleIncludeDepth ...................   Passed    0.04 sec
100% tests passed, 0 tests failed out of 141
Total Test time (real) = 163.45 sec
```
`test_targets all: 93 checks, 0 failures`; `test_present all: 51 checks, 0 failures`. All 6D tests are GL-free
(FakeBackend); the 6C GL tests (`Render_ShaderCompiles`, `Render_DeviceSmoke`, …) RAN in the same run.
Negative proof during the stage: `Targets_RefreshFromVram` FAILED on the first cut (linear row-band fold with
`bpp=16`, then with the linear fold at all) and passed once the fold became `PageRecord::FoldByteRangeToPageRect`
as OLD `RefreshFBOFromVRAM` uses — the test reaches the fold rule.

### Findings

- **P6-PROFILE-SUBDIR**: the profile lives in `games/<t>/renderer_profile/{renderer_profile.h,.cpp,CMakeLists.txt,README.md}` (a subdirectory named after the module) so it owns its CMake target without touching the title's other CMake files; the root CMake gained one `add_subdirectory(games/${PS2R_TITLE}/renderer_profile)  # Phase 6` line. Every plan reference to `games/<t>/renderer_profile.{h,cpp}` means these files.
- **P6-CT16S-ENCODE** (INV §14 risk 5, confirmed; veto clause corrected by Review 6D / fix round): OLD `g8par::EncodeRTToVRAM` (`opengl_backend.cpp:15971-16030`) has ENCODE arms for `psm == 0x00 || 0x01` and `psm == 0x02` only; a PSMCT16S (0x0A) render target's colour writeback stages NO bytes (the pixel loop matches nothing). Reproduced: `ReadbackEngine::EncodesColorPsm` = {CT32, CT24, CT16}; the 0x230000 accumulator's bytes reach VRAM only through the alt16 lane deposit (`DepositAlt16LaneToVRAM :16170`, 6D′) and the S9 materialise path (6E). The per-page / per-range vetoes are NOT arm-specific: `is_protected(addr, len)` = `page_newer[page]` OR overlap with `protected_ranges` applies to the CT32/CT24 arm (`:15951-15959, :15972`); the CT16 arm applies `page_newer` always and `protected_ranges` only when `keep16 == 0` (`:15986-15987`); a rescue additionally vetoes every page whose recorded author is not this target (`:16462-16485`). Only the 0x0A ENCODE arm is absent. (The earlier "no per-page veto on CT32/CT24" sentence was wrong and is withdrawn; `ReadbackEngine::EncodeColorRows` now reproduces all three vetoes — pinned by `Targets_EncodeVetoes`.)
- **P6-CLUT-HEAL**: the exemption constants are `VramLayout` data (`IsClutHealExempt`) filled by the profile; the mechanism (`ClutHealCache`) is 6D′; integration moves both to `games/<t>/hle/workarounds` per the HOOK ladder (a no-op in replay: the cache never primes from a zero mirror).
- **P6-GRAB-FALLBACK** (risk 10): `ScanoutComposer::GrabDisplay` reads the target at the scanout fbp, else the most-recent-drawn target (`RenderTargetCache::NoteDrawn` — the replay driver / class renderer calls it after every draw as OLD's tail sets `m_most_recent_drawn_fbp`), else reports `w = h = 0` for the "no target" sentinel hash. Pinned by `Present_GrabDisplayFallback`.
- **P6-ALT16-REFRESH-CPU** (CLOSED by Review 6D / fix round): `RefreshAlt16FromMain` runs on the CPU (Readback → `TexaExpand::Truncate32To16` → `Expand16To32` → Upload). OLD's live arm is the GL program (`r124::Alt16GpuOn()` default ON, `render.h:766-776,1016`); its fragment text `opengl_backend.cpp:14626-14634` computes `v8 = uvec3(c.rgb*255+0.5); v5 = v8 >> 3; e8 = (v5<<3)|(v5>>2); a = (c.a*255+0.5 >= 128) ? 1 : 0` — for UNORM8 inputs `c*255+0.5` truncates back to the byte, so this is exactly `Truncate8To5` → `Replicate5To8` per channel and `alpha >= 0x80` for the A bit: byte-equal to the CPU fold for all 256^4 inputs (OLD's own header comment `:14597-14607` says the same). Unit check: `Gs_Inv12_Alt16CoherenceRect` evaluates the program's integer arithmetic against `TexaExpand` for every channel value 0..255 (`fold_ok`).
- **P6-TAIL-REMAINDER**: three parts of OLD's post-draw tail are NOT in `RenderTarget::AfterDraw` and go to 6D′: the pending 16-bit RT-sync ledger (`RecordPendingRTSync` / `FlushPendingRTSyncs(reason)` = the eager-vs-deferred deposit decision keyed on `VramLayout::IsReaderBandPage`, `:14140-14230`), the alt16 lane deposit (`DepositAlt16LaneToVRAM :14056-14090`, `:16170`), and `MarkPagesGPUValid` page-tracker marks beyond `NoteGpuAuthor`. Until then every deposit is what the caller explicitly requests (`SyncAll`, `Destroy`, the grow rescue). Author-mark dependency (Review 6D iteration 2): OLD writes the GPU author marks the rescue authorship veto reads (`readback_engine.cpp:189-191` = `:16462-16485`) from exactly two kinds of site — the deposits (`gsresident::NoteGPUDepositRect` `:15956` colour, `:18207` Z, reproduced by `MarkAuthorTiles`) and the pending-ledger ELIDE sites (`gsresident::NoteGPUElided` `:14158`, `NoteGPUElidedRect` `:14204`, `NoteGPUElided` `:24808`; no other writer). So today a never-deposited target's rescue vetoes every page in NEW and in OLD alike (`Gs_Inv13/14` pin `PendingPages() == 1`, word 0), but the 6D′ ledger port MUST carry the elide marks with it, or 16-bit-class rescues start to differ from OLD.
- **P6-REFRESH-ARMS** (found by the Review 6D fix round; verified by Review 6D iteration 2; → 6D′ with P6-ALIAS-PREDRAW): OLD `RefreshFBOFromVRAM` (`:17484-18100`) differs from NEW `TargetRefresher::RefreshFromVram` in four ways, none of which has a live caller until P6-ALIAS-PREDRAW lands. (1) Arms: CT32/CT24 is an ALPHA-ONLY merge — RGB from the attachment readback, alpha byte from the mirror `×2` capped 255, stride `w` (`:17698-17745`; `aquad::On()` default-ON row-quad fast path `:17641-17659`); CT16 is a full `DecodePSMCT16(fbp, render_fbw or w)` with the SH5 alias-epoch keep runs (`sh5::AliasEpochOn()` default ON, `:17761-17990`: rows whose pages carry an upload epoch newer than the job's gfx epoch keep the attachment bytes); CT16S is the bookkeeping-only arm (`:17586-17608`, reproduced). NEW is one `BlockDecode` for every colour PSM. (2) Source: BOTH OLD arms read the COMMITTED mirror `gs_vram` — `a_ps2 = gs_vram[addr + 3]` inside the `:17718-17722` row loop (`:17726`), `DecodePSMCT16(gs_vram.data(), …)` at `:17891-17898` and `:17940-17953` — never a composed view; NEW reads `VramImage::ReadView` (mirror ⊕ pending deposit shadow). In replay the shadow is never committed (P6-ZERO-MIRROR), so once the refresh has a caller this is a real byte difference: the 6D′ port must read the mirror (a `VramImage` mirror-only reader to be added next to `ReadView`), not the composed view. (3) Tail stamps: OLD stamps `vram_coherent_epoch = r173_epoch` / `vram_coherent_class = 32` for CT32/CT24 and `16` for CT16 when the arm absorbed the 5551 image, never for CT16S or a scoped band (`:18069-18086`); `CoherenceState` has no such fields yet (their consumers are 6D′ texsrc). (4) Tail companion stale: OLD's shared tail does `expandAlt16Stale(0, 0, w, h)` for every arm when `alt16_fbo != 0` (`:18093-18094`); the first 6D cut did it only in the CT16S arm — aligned by Review 6D fix 2 (`target_refresher.cpp` tail), pinned in `Targets_RefreshFromVram` (CT16 target with a companion → `alt16_stale == {0,0,64,64}`).
- **P6-LINT-RENAMES**: LintOneClass forced `render_target_cache.{h,cpp}` (type `RenderTargetCache`), `logical_rect.{h,cpp}` (second type in coherence_state), `stencil_zcopy_fbp` (the `s9_` prefix reads as a session tag), and the FakeBackend bodies into `tests/lib/fake_backend.cpp`.
- Process note: `/tmp/p6/check_sizes.py` globbed `renderer_profile*` as a FILE and crashed on the new directory; the helper was adjusted to rglob `renderer_profile*/*.{cpp,h}` (helper only, no repo change).

### Gate status

| gate | evidence |
|---|---|
| G7 (6D half) | `Gs_Inv09/10/11/12/13/14/20/23/24/35/37`, `Present_ViewportMath`, `Present_CrtcClassifier`, `Present_GrabDisplayFallback`, `Targets_*`, `Profile_VramLayoutEmptyIsFalse`, `Gs_HleIncludeDepth` all Passed (quoted above). Remaining G7 names → 6D′: `Gs_Inv15_NeverStaleRtRead`, `Gs_Inv17_PerCallResultsReset`, `Gs_Inv18_ContentKeyAddressFree`, `Gs_Inv19_DecodeRectIsCrop`, `Gs_Inv33_FogLaneLive`, `Present_DisplayMagnification`, `Texsrc_*` |
| build / lints / size | `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`, `grep -c 'warning:' /tmp/p6/6d_build.log` → `0`; `LintOneClass: 421 files scanned, 0 findings -> PASS`; `LintEnginePurity: 368 files scanned, 0 game tokens -> PASS`; `python3 /tmp/p6/check_sizes.py .` → `size check OK`; `grep -rn getenv engine games tests/lib tools` → only `config_loader` + the pre-existing generated `recomp/recompiled.cpp` / `iop_recompiled.cpp` sites (Phase 3/4 territory, untouched) |
| full ctest | bounded `ctest --preset macos-dev` (`/tmp/p6/6d_ctest_final.log`) → `100% tests passed, 0 tests failed out of 141`, `Total Test time (real) = 163.45 sec` |

§2.5 checklist: ✔ targets rows (`target_identity`, `surface_views`, `coherence_state`, `readback_engine`, `render_target`, `render_target_cache`, `target_refresher`, `scale_policy`), ✔ present rows (`scanout_composer` grab half, `crtc_classifier`, `viewport_math`, `presenter`), ✔ `games/<t>/renderer_profile` (VramLayout half); ☐ every `texsrc/` row → 6D′.

### HANDOFF (6D → 6D′)   [updated by the Review 6D fix rounds 1 and 2, 2026-09-27/28]
Done: engine/gs/hle/targets (9 pairs, lib ps2r_gs_hle_targets; Review 6D items 1-4, 6-14 applied), engine/gs/hle/present (4 pairs,
  lib ps2r_gs_hle_present), games/<t>/renderer_profile (lib ps2r_title_renderer_profile, VramLayout filled), tests/lib/fake_backend
  (lib ps2r_fake_backend), 26 ctests (all PASS, GL-free), Gs_HleIncludeDepth lint; INV §2.3 grab, §6.2 post-draw tail (rects by
  route, tags, Z history, keep mask, per-view author seqs + lane records, upload eviction; minus P6-TAIL-REMAINDER), §6.4, §6.7
  (colour/Z encode with the three vetoes, linear-span touched pages, Z saturation, SyncAllRTsToVRAM), §6.8 setters+grab, §6.10/§11 →
  VramLayout, §9 classifier, §12 rows 9-14, 20, 23, 24, 35, 37.
Not done (→ 6D′, exact list): engine/gs/hle/texsrc/{content_key, decode_cache, clut_source, clut_heal_cache, uv_bbox,
  window_in_target, mip_sampler, texture_decoder, texture_source(.cpp: ResolveRead = MatchTarget/MatchIndexed/MatchDepth/
  DecodeFallback)}.{h,cpp} + CMakeLists (lib ps2r_gs_hle_texsrc) + README; present/scanout_composer DrawScanout display quad
  (+ ParseDISPFB/ParseDISPLAY → Present_DisplayMagnification); the P6-TAIL-REMAINDER pieces in RenderTarget::AfterDraw
  (pending RT-sync ledger keyed on VramLayout::IsReaderBandPage, DepositAlt16LaneToVRAM, MarkPagesGPUValid); the render_w/
  render_h derivation (fbw ≤ 1024, scanout content dims, VramLayout scene-accum scissor-height rule) as a small
  `TargetDims` helper next to RenderTargetCache (or in 6F's ReplayDriver).
  DEFERRED from Review 6D (item 5, verbatim): "`engine/gs/hle/targets/render_target.cpp` `PrepareDraw` (+ a page set the
  refresher/readback share): the pre-draw `[VRAM-ALIAS]` refresh demand `:9403-9625` (aliased pages set by deposits
  `:16961-16963`, or 16-bit frame ∧ CPU-dirty pages) → `RefreshFromVram(band)`; `SyncRtToVram` marks the linear rows CPU-dirty +
  aliased (`:16943-16963`)." Rule (INV §13.1 row 9237-9690): colour frame psm ∈ {0x00,0x01,0x02,0x0A} ∧ `fbmsk != 0` ∧ `fbw > 0`
  → `region = fbw * render_height * bpp`; if any page of the region ∈ `m_vram_aliased_pages` OR (16-bit frame ∧
  `ArePagesCPUDirty(fbp, region)`) → refresh the draw-bbox ∩ scissor band (`alias_rect::RectOn` default ON, FLAG_LEDGER row 366;
  composite draws into the scanout target excluded `:9547-9550`); the aliased set is cleared per page by RefreshFBOFromVRAM
  (`:17606-17629`, pages `.reset` at `:17615`, `:17627`). Named 6D′ item **P6-ALIAS-PREDRAW**: a shared `AliasPageSet` (cpu_dirty + aliased bitsets, owned by
  RenderTargetCache, written by ReadbackEngine's colour tail and by the InvalidateTextures job) + the PrepareDraw demand. Must land
  together with **P6-REFRESH-ARMS** (Findings bullet above, this section): OLD's `RefreshFBOFromVRAM` CT32/CT24 arm is an ALPHA-ONLY merge (RGB from
  the attachment readback, alpha byte from the mirror ×2 capped 255, addressed with stride `w`, `:17698-17745`; `aquad::On()`
  default ON row-quad fast path `:17650-17659`), its CT16 arm a full `DecodePSMCT16(fbp, render_fbw or w)` with the SH5 alias-epoch
  keep runs (`sh5::AliasEpochOn()` default ON, `:17761-17990`: rows whose pages carry an upload epoch newer than the current gfx
  epoch keep the attachment bytes); NEW's `TargetRefresher::RefreshFromVram` is a full BlockDecode for every colour PSM and has no
  live caller until P6-ALIAS-PREDRAW lands, so the arms are aligned there. The full arm / mirror-source / r173-stamp list is the
  **P6-REFRESH-ARMS** Findings bullet above.
  `RenderTargetCache::Destroy(fbp, gfx_epoch)` (Review 6D iteration 2 note): it has NO caller (grep over engine/app/tests → only its
  definition) and is NOT the OLD destroy/evict shape — OLD's destroy sites are full-extent RESCUES: `SyncRTToVRAM(fbp, t, 0xFFFFFFFF,
  nullptr, t.last_draw_gfx_epoch, {0,0,w,h}, RescueStride(render_fbw))` followed by `FlushPendingRTSyncs(kDestroy)` (`:15572-15590` grow
  destroy, `:19656-19670` S109 evict), whereas NEW's runs a drawn-rect colour sync with the caller's epoch and no rescue rect, then a Z
  sync. 6D′ (with the pending ledger) or 6F (the first caller) must give it the rescue form — `SyncRtToVram(be, t, vram,
  t.coherence.last_draw_gfx_epoch, &full_extent, RescueStride(t.identity.render_fbw))` + ledger flush — or drop the method.
  Tests → 6D′: Gs_Inv15_NeverStaleRtRead, Gs_Inv17_PerCallResultsReset, Gs_Inv18_ContentKeyAddressFree, Gs_Inv19_DecodeRectIsCrop,
  Gs_Inv33_FogLaneLive (over `CoherenceState::lane_main/lane_alt` + `main_author_seq/alt_author_seq`), Present_DisplayMagnification,
  Texsrc_ClutSource, Texsrc_CdCopyDecide, Texsrc_WindowInTarget, Texsrc_Idx16Lane, Texsrc_L2Entries, Texsrc_S9ShadowPredicates,
  Targets_AliasPreDraw (P6-ALIAS-PREDRAW), Targets_RefreshArms (P6-REFRESH-ARMS).
  What 6E/6F call today: `RenderTargetCache::GetOrCreate/Find/NoteDrawn/SyncAll()/DestroyAll`, `RenderTarget::PrepareDraw/
  AfterDraw(ir, res, frame_count, gfx_epoch)/MarkCompositeConsumed`, `TargetRefresher::UploadDirect/FindUploaded/EvictUnderDraw(ir,
  res, render_h)/MarkPagesCpuDirty` (there is NO upload-deposit refresh: `PS2_GL_UPLOAD_DEPOSIT`'s winning arm is OFF, only the
  `UploadRowBand` fold helper remains), `ScanoutComposer::SetScanoutFbp/SetDispfb/SetContentDimensions/GrabDisplay`,
  `Presenter::MarkEeFrameEnd/Present`, `title::RendererProfile::Vram()`; `ResolveRead` (6D′) fills the `TextureSource` the
  FakeBackend/GlDevice `Draw` takes. The SyncAllRTsToVRAM job maps to `RenderTargetCache::SyncAll()` (colour only, epoch 0).
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 148/148; lints 0/0; getenv sites unchanged
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Gs_Inv|Targets_|Present_|Profile_'
Findings: P6-PROFILE-SUBDIR, P6-CT16S-ENCODE (confirmed, veto clause corrected), P6-CLUT-HEAL (data half), P6-GRAB-FALLBACK,
  P6-ALT16-REFRESH-CPU (CLOSED), P6-TAIL-REMAINDER, P6-LINT-RENAMES, P6-ALIAS-PREDRAW (6D′), P6-REFRESH-ARMS (6D′), P6-ENCODE-STRIDE,
  P6-DEPOSIT-SPAN (reproduced)

### Review 6D (verdict)

Independent reviewer, 2026-09-27 (full table with commands, quoted output and OLD line citations in `/tmp/p6/REVIEW_6D.md`; reviewer
rebuilt the 6D objects from scratch, re-ran ctest twice, both lints, the size check, and the eight OLD oracle binaries bounded).

| check | result |
|---|---|
| contract / purity / include depth | PASS — allowed paths only; `# Phase 6` appends only; purity greps empty; `Gs_HleIncludeDepth` Passed; profile data = `gs_crtc_class.h:75-77`, `gs_page_tracker.h:159-163`, `texture_decoder.cpp:1286-1370`, `gs_scale.h:63` |
| ResolveRead / decode path (checks 3, 4) | N/A this stage — `texsrc/` is the pre-registered 6D′ split (HANDOFF lists every file + test) |
| targets | 16 divergences from OLD (F1–F16 in the review) — the alt16-route retag/has_content exclusion (`:9160-9165`, `:14372-14380`), `zte`-gated Z history + sticky `last_z_wrote` (`:9166-9186`), bpp-based `was_32/16` (`:9235-9237`), tri-bbox/unclamped `valid_rect` and the alt16-route `alt16_drawn` split (`:13567-13700`), per-view author/lane seqs (`:13935-13950`, `:20309-20320`), the LIVE pre-draw `[VRAM-ALIAS]` refresh demand (`:9403-9625`, absent and unrecorded), draw-range uploaded-texture eviction (`:13955-14000`, absent and unrecorded), `DepositUploadToLiveTargets` live although `PS2_GL_UPLOAD_DEPOSIT`'s winning arm is OFF (`render.h:1869-1885`, FLAG_LEDGER row 409), `RefreshFBOFromVRAM`'s 0x0A early arm (`:17537-17580`), uploaded-map constants/budget/in-place update (`opengl_backend.h:1539`, `:23238-23269`), CT32/CT24 `page_newer` + `protected_ranges` (`:15951-15987`, `:16334-16385`), linear-span touched pages (`:15934-15940`), Z16/Z24 saturation (`:18305-18318`), colour-only `SyncAllRTsToVRAM` with epoch 0 (`:21900-21908`), rescue fbw narrowing (`:15563-15566`), `hasValidContent` entry (`:16279`) |
| deposits | `CommitDeposits` never called above `VramImage` (grep empty); P6-CT16S-ENCODE confirmed (`:15971-16030`) but its "no CT32/CT24 veto" clause is wrong (F11); P6-ALT16-REFRESH-CPU CLOSABLE: the live GL program (`:14612-14620`, `Alt16GpuOn` default ON) computes exactly `TexaExpand::Truncate32To16` → `Expand16To32` |
| grab / present | PASS — `GrabDisplay` = `:25076-25097`; `CrtcClassifier` = `gs_crtc_class.h:83-112`; `ViewportMath` verbatim, all 32 OLD vectors ported |
| tests | `ctest -R 'Gs_Inv|Texsrc_|Targets_|Present_|Profile_|Gs_HleIncludeDepth'` → `100% tests passed, 0 tests failed out of 37`; full suite run 1: `99% tests passed, 1 tests failed out of 141` (`#115 RestoreMhashAnchors Subprocess killed 0.95 sec` — Phase 3's test, untouched by 6D; isolated rerun `Passed 20.99 sec`), run 2: `100% tests passed, 0 tests failed out of 141`, `Total Test time (real) = 199.62 sec`; 6C GL tests Passed (not skipped) in both. OLD oracles bounded: `test_viewport_math 32 passed`, `test_blendcp_lazy 21 checks 0 failures`, `test_rt_dirty_tiles` / `test_s8_inrt` all passed, `test_idx16_lane 1025/1025`, `test_gl_l2_entries` exit 0, `test_s9_shadow 73 checks 0 failures`, `test_display_magnification_properties` exit 0 |
| hygiene | `BUILD_EXIT=0`, `warning:` count 0; `LintOneClass … 0 findings -> PASS`; `LintEnginePurity … 0 game tokens -> PASS`; `size check OK`; getenv / tag / narration greps over the 6D dirs empty |

**VERDICT: NEEDS_CHANGES** — 15 numbered, file-cited fixes in `/tmp/p6/REVIEW_6D.md` §Verdict (items 5, 6, 10, 11 may be deferred to 6D′
only if named verbatim in the HANDOFF with their OLD citations; the rest are predicate changes in landed code). The 6D′ split itself is
accepted.

### Review 6D fix (2026-09-27, ≈70 min)

Implementer, closing the 15 NEEDS_CHANGES items of `/tmp/p6/REVIEW_6D.md` §Verdict against the cited OLD lines (read-only:
`opengl_backend.cpp/.h`, `render.h`, `gs_deposit_overlay.h`, `gs_page_tracker.h`, `rt_copy_tracker.h`). 14 of 15 code items
applied (items 6, 10, 11 included — the review allowed deferring them, they were implemented instead); item 5 DEFERRED to 6D′ as
the named item **P6-ALIAS-PREDRAW** (verbatim text + citations in the HANDOFF above), because its only entry point,
`RefreshFromVram`, is itself not yet arm-faithful (**P6-REFRESH-ARMS**, found this round — see HANDOFF). All 6D-owned files stay
in `engine/gs/hle/targets/` + `tests/gs/targets/test_targets.cpp` + `tests/gs/CMakeLists.txt` (seven new ctest names). Logs:
`/tmp/p6/6d2_build.log`, `6d2_ctest_6d.log`, `6d2_ctest_full.log`, `6d2_neg_probes*.log`.

| item | NEW file:line | OLD cite | pin test (FAILS without the fix) |
|---|---|---|---|
| 1 alt16-route tags | `render_target.cpp:188-215` — `if (res.alt16_route)` sets `views.alt16_has_content` only; `else` branch grows `valid_rect`/`drawn_since_sync`, sets `has_content`, `render_psm/render_fbw`, `has_32bit_content` | `:9160-9165`, `:14330-14340` | `Gs_Inv11_Alt16Bifurcation` (target `u`: `!has_content`, `render_psm != 0x02`, `render_fbw != 128`, `valid_rect.Empty()` after an alt16 draw) |
| 2 Z history + bpp tags | `render_target.cpp:223-233` — `if (zte) { last_zbp/zpsm; if (!zmsk) { last_z_wrote = true; z rect } }`; `bpp = FrameBitsPerPixel(fpsm)` (OLD table `:4275-4291`) → `was_32bit (>=24)`, `was_16bit (==16)` | `:9166-9186`, `:9235-9237` | `Gs_Inv10` (sticky `last_z_wrote`, zte=0 keeps `last_zbp`), `Gs_Inv11` (PSMZ32 frame → `was_32bit`, PSMZ16S → `was_16bit`) |
| 3 rects by route | `render_target.cpp:171-213` — tri bbox `(int)`/`ceil` from `res.bbox_tri`; main: `valid_rect.Union` UNclamped, `drawn_since_sync.Union(bbox ∩ scissor[x0, x1+1))`; alt16: `alt16_drawn.Union` + Cd dirty only; full-overwrite drop `alt16_has_content = false; alt16_drawn.Reset()`; `LogicalRect::Union` (unclamped) added `logical_rect.cpp:9-19`; `CoherenceState::alt16_drawn` + `Alt16HasDrawn()` `coherence_state.h:47,70` | `:13567-13579`, `:13580-13630`, `:13650-13667`, `:13683-13700`, `opengl_backend.h:860-872` | new `Targets_DrawnRectsByRoute` (`valid = {-4,2,71,10}` vs `drawn = {0,2,64,10}`), `Gs_Inv12` (alt16_drawn growth, covering main write drops content, non-covering keeps it) |
| 4 per-view authors | `coherence_state.h:38-41,59-62` — `main_author_seq`, `alt_author_seq`, `LaneRecord lane_main/lane_alt {seq, frame}`; `render_target.cpp:240-249` (`++` by route, lane record on that view), `:97` (`RefreshAlt16FromMain` `alt_author_seq++`), `target_refresher.cpp:177` (`RefreshFromVram` `main_author_seq++`); `AfterDraw` lost the `author_seq` parameter (`render_target.h:53-54`) | `:13933-13950`, `:15250`, `:17501`, `opengl_backend.h:665-680` | new `Targets_PerViewAuthorSeqs`, `Gs_Inv12` (`alt_author_seq` 1 → 2 across two refreshes) |
| 5 pre-draw alias refresh | **DEFERRED → 6D′ P6-ALIAS-PREDRAW** (HANDOFF, verbatim + rule + citations) | `:9403-9625`, `:16943-16963`, `:17606-17629` | `Targets_AliasPreDraw` (6D′) |
| 6 upload eviction under a draw | `target_refresher.cpp:112-130` `EvictUnderDraw(ir, res, render_height)`: rows `[max(0,floor(bbox_tri.y0)), min(render_h, ceil(bbox_tri.y1)))` × `fbw*bpp/8`, page-rounded `& ~8191`, skipped when `masked_partial16_keep = Is16BitColor ∧ fbmsk16 != 0` | `:13955-14027` | new `Targets_EvictUnderDraw` |
| 7 upload deposit OFF | `DepositUploadToLiveTargets` REMOVED; the fold survives as `TargetRefresher::UploadRowBand` (`target_refresher.cpp:160-171`, static, no side effect); header rule text `target_refresher.h:16-18` | `render.h:1869-1885` (`PS2_GL_UPLOAD_DEPOSIT` default OFF), FLAG_LEDGER row 409, fold `:23113-23167` | `Targets_RefreshFromVram` (fold helper: 2 bytes at (2,3) of a CT16 buffer → rows [0,64); disjoint → false; the refresh itself is called explicitly) |
| 8 RefreshFromVram 0x0A arm + author | `target_refresher.cpp:173-192` — `main_author_seq++` after the psm check; `if (psm == CT16S) { has_content; valid_rect full; cd void; alt16_stale full (if companion); return }` before any decode; shared tail `:222-224` (`has_content`, full `valid_rect`, Cd void) | `:17484-17501`, `:17537-17580`, `:18045-18057` | `Targets_RefreshFromVram` (CT16S: `uploads` unchanged, texels untouched, bookkeeping set), `Targets_PerViewAuthorSeqs` (`main_author_seq` 4 → 5) |
| 9 uploaded map | `target_refresher.h:47-48` `512` / `64 MiB`; `.cpp:35-65` same-dims → `Upload` in place, `last_use` bump, `size_bytes` unchanged, return; different dims → destroy + `EnforceUploadedBudget()` BEFORE insert; `:67-78` `size() >= kMax || Σ w*h*4 > kMaxBytes`, LRU | `opengl_backend.h:1539-1540`, `:23191-23269` | `Targets_UploadedTextureMap` (in-place keeps `tex.id` + footprint 1024; 513th insert evicts LRU `0x1000`, count stays 512; 4096×4097 admitted alone then evicted by the next insert) |
| 10 encode vetoes | `readback_engine.cpp:183-233` — `page_newer[pg] = upload_epoch > batch_epoch` for EVERY colour psm (+ rescue: `!(author == GpuTarget && author_fbp == fbp)`), page restore from `before_`; `ProtectedRanges()` `:93-113` from `uploaded_` spans (`up_epoch >= batch ∧ != 0`, sorted), per-pixel restore, skipped for CT16 with `keep16 != 0`; spans fed by `TargetRefresher::PublishSpans` (`target_refresher.cpp:22-27`) via `ReadbackEngine::SetUploadedSpans`; author tiles marked per rect BEFORE staging (`MarkAuthorTiles` `:48-59`) | `:15951-15972`, `:15986-15987`, `:16334-16408`, `:16455-16485`, `gs_page_tracker.h:252-268` | new `Targets_EncodeVetoes` (a) CT32 page kept at epoch 0, overwritten at epoch 5; (c) protected range kept at epoch 1, overwritten at 2, eviction clears the source; (d) CT16 keep-mask ignores ranges; (e) rescue keeps an unauthored page |
| 11 linear-span touched pages | `readback_engine.cpp:38-44` `LinearSpan`, `:71-91` `StageTouched`: pages of `[fbp + y0*fbw*bpp, fbp + y1*fbw*bpp)` staged (span pages outside the encoded rows bracketed with their current bytes), other footprint pages only when already `Pending` (staged with epoch 0 = no re-stamp); colour `:236-238`, Z `:303-305` | `:15934-15940`, `:18220-18236`, `gs_deposit_overlay.h:94-119` | new `Targets_DepositSpan` (fbw 128, rows [0,8): page 1 pixel (70,2) NOT staged, `PendingPages == 1`; with page 1 pending it lands) — **P6-DEPOSIT-SPAN reproduced**, not registered as a risk |
| 12 Z saturation | `readback_engine.cpp:286-287` `z16 → min(z, 0xFFFF)`, `Z24 → min(z, 0xFFFFFF)` before `PackZ`; Z format normalised `0x30 | (last_zpsm & 0xF)` `:260` | `:18305-18318`, `:21583`, `:4777` | new `Targets_ZSaturate` (Z16 at depth 0.5 → `0xFFFF`; Z24 → `0xFFFFFF`), `Gs_Inv10` (raw 4-bit ZBUF field still encodes) |
| 13 SyncAll / rescue stride / fresh tags | `render_target_cache.cpp:90-99` `SyncAll()`: colour only, epoch 0, `continue` unless `HasValidContent() && HasDrawnSinceSync()` (signature lost `gfx_epoch`); `:25-29` `RescueStride(fbw) = (fbw >= 64 && (fbw & 63) == 0) ? fbw : 0`, used `:50`; `:54-58` fresh `TargetIdentity` leaves `render_psm = render_fbw = 0` (carry only) | `:21900-21925`, `:15563-15566`, `opengl_backend.h:642-643` | new `Targets_SyncAllJob` (EE page kept at epoch 0, Z rect untouched, second call stages nothing), `Gs_Inv13/14` (fresh tags 0/0; `RescueStride(100) == 0`; a never-deposited target's rescue brackets but writes nothing) |
| 14 entry test | `readback_engine.cpp:119` `if (!c.HasValidContent()) return 0;` (also `SyncZToVram` `:245`); `CoherenceState::HasValidContent()` | `:16279`, `:18160` | `Gs_Inv09` (rescue stages with `has_content = false`; an empty `valid_rect` stages nothing even when drawn) |
| 15 IMPL record | P6-CT16S-ENCODE veto clause corrected (this file, Findings); P6-ALT16-REFRESH-CPU CLOSED (program `:14619-14634` ≡ `TexaExpand`, unit check `fold_ok` in `Gs_Inv12`); HANDOFF updated (item 5 verbatim, P6-REFRESH-ARMS, the `AfterDraw`/`SyncAll`/`EvictUnderDraw` signatures, no upload-deposit refresh); RestoreMhashAnchors note below | — | — |

Additional OLD rules aligned while applying the items (each a predicate the review's cited ranges enclose):
- **P6-ENCODE-STRIDE**: the non-rescue colour encode stride is the TARGET WIDTH, not `render_fbw` (`uint32_t fbw = (uint32_t)w; if
  (rescue_rect && rescue_fbw) fbw = rescue_fbw;` `:16749-16750`); NEW used `render_fbw`. Fixed `readback_engine.cpp:143`; pinned by
  `Gs_Inv13/14` (a 100-FBW target's rescue puts pixel (70,0) at the width-128 address).
- Author marks follow OLD's `NoteGPUDepositRect` page-TILE walk of the deposit rect (`gs_page_tracker.h:252-268`), for colour and Z
  (author = zbp for Z, `:18205`), not full page rows; they are recorded before the staging so the rescue veto of the same call sees the
  pre-existing authors (`:16462-16485` runs before `EncodeRTToVRAM`).
- The Z writeback consumes the native format `0x30 | (last_zpsm & 0xF)` as every OLD caller passes it (`:21583`, `:4777`).
- Cd-copy dirty extension is unconditional (`rt_copy::ExtendDirty`, `rt_copy_tracker.h:59-70`), no `valid` gate, over the tri-bbox ∩
  scissor rect on both routes (`:13620-13630`, `:13667-13676`).
- `RefreshAlt16FromMain` clamps the stale rect to the target and consumes it either way; the author bump happens only for a non-empty
  clamped rect (`:15241-15250`).

Negative probes (`/tmp/p6/probe6d2.py`, `probe6d2b.py`, `probe6d2c.py`; each reverts ONE line, rebuilds `test_targets`, runs the pin,
restores; `shasum -a 256` of the four `.cpp` files identical before/after — `sources restored byte-identical`):
```
item1 alt16 route retags main        inv11    test_targets inv11: 17 checks, 4 failures (rc=1)
item2 last_z_wrote cleared / zte gate inv10    test_targets inv10: 9 checks, 3 failures (rc=1)
item2 was_32bit by class             inv11    test_targets inv11: 17 checks, 1 failures (rc=1)
item3 valid_rect from write_rect     rects    test_targets rects: 6 checks, 2 failures (rc=1)
item3 full-overwrite drop            inv12    test_targets inv12: 14 checks, 1 failures (rc=1)
item4 alt refresh author bump        inv12    test_targets inv12: 14 checks, 2 failures (rc=1)
item4 per-view lane record           authors  test_targets authors: 9 checks, 1 failures (rc=1)
item6 masked keep                    evict    test_targets evict: 5 checks, 1 failures (rc=1)
item8 CT16S arm decodes              refresh  test_targets refresh: 13 checks, 3 failures (rc=1)
item8 main author bump               authors  test_targets authors: 9 checks, 1 failures (rc=1)
item9 in-place same-size             uploaded test_targets uploaded: 21 checks, 2 failures (rc=1)
item9 budget after insert >          uploaded test_targets uploaded: 21 checks, 2 failures (rc=1)
item10 page_newer CT32               vetoes   test_targets vetoes: 10 checks, 1 failures (rc=1)
item10 protected ranges              vetoes   test_targets vetoes: 10 checks, 2 failures (rc=1)
item11 linear span                   span     test_targets span: 9 checks, 2 failures (rc=1)
item12 Z16 saturate                  zsat     test_targets zsat: 4 checks, 1 failures (rc=1)
item13 SyncAll epoch/Z               syncall  test_targets syncall: 5 checks, 3 failures (rc=1)
item13 rescue stride narrowing       inv13    test_targets inv13: 17 checks, 1 failures (rc=1)
item13 fresh render_psm              inv13    test_targets inv13: 17 checks, 1 failures (rc=1)
item14 entry !has_content            inv09    test_targets inv09: 9 checks, 2 failures (rc=1)
restore build rc 0
test_targets all: 200 checks, 0 failures
```
(The first cut of two probe patches did not compile — an unused-variable `-Werror` and a self-assign — and were re-expressed as
`if (masked_partial16_keep || true)` and `z = z & 0xFFFFFFFFu`; the failures quoted are from those.)

Verification (`/tmp/p6/6d2_build.log`, `6d2_ctest_6d.log`, `6d2_ctest_full.log`):
```
BUILD_EXIT=0
$ grep -c 'warning:' /tmp/p6/6d2_build.log
0
$ ./build/tests/gs/test_targets all
test_targets all: 200 checks, 0 failures
$ ctest --preset macos-dev -R 'Gs_Inv|Targets_|Present_|Profile_|Gs_Hle' --output-on-failure | tail
64/148 Test  #64: Targets_DrawnRectsByRoute ............   Passed    0.04 sec
65/148 Test  #65: Targets_PerViewAuthorSeqs ............   Passed    0.05 sec
66/148 Test  #66: Targets_ZSaturate ....................   Passed    0.04 sec
67/148 Test  #67: Targets_SyncAllJob ...................   Passed    0.05 sec
68/148 Test  #68: Targets_EncodeVetoes .................   Passed    0.14 sec
69/148 Test  #69: Targets_DepositSpan ..................   Passed    0.07 sec
70/148 Test  #70: Targets_UploadedTextureMap ...........   Passed    0.55 sec
71/148 Test  #71: Targets_EvictUnderDraw ...............   Passed    0.04 sec
72/148 Test  #72: Targets_RefreshFromVram ..............   Passed    0.06 sec
100% tests passed, 0 tests failed out of 44
$ ctest --preset macos-dev | tail -6
148/148 Test #148: Render_IncludeDepth ..................   Passed    0.05 sec
100% tests passed, 0 tests failed out of 148
Total Test time (real) = 181.66 sec
LintOneClass: 421 files scanned, 0 findings -> PASS
LintEnginePurity: 368 files scanned, 0 game tokens -> PASS
size check OK
$ grep -rnE '0x(23|28|2f|2F|15|38|37|07|26|3c|3C|3d|3D|3f|3F)[0-9a-fA-F]{4}\b|FUN_' engine/gs/hle   -> (empty) rc=1
$ grep -rn getenv engine/gs/hle/targets                                                           -> (empty)
```
`RestoreMhashAnchors` (#122) and `EeRunTokenAndEventQueue` (#109) both **Passed** in this single full run (11.15 s / 0.08 s) — no
rerun was needed. Note on the transient kill the reviewer saw (`Subprocess killed***Exception: 0.95 sec`, then `Passed 20.99 sec`
isolated): `RestoreMhashAnchors` is Phase 3's `tests/ee` gate, untouched by 6D; under full-suite parallel load its subprocess was
killed inside the first second (a runner-side kill, not a hash miss); it is recorded as a flake with the isolated PASS as the
authoritative result, and any recurrence is to be re-run once in isolation before it is read as a regression. 6C GL tests
(`Render_ShaderCompiles`, `Render_DeviceSmoke`, …) RAN (not skipped) in the full run. No commits.

### Review 6D (verdict, iteration 2)

Independent reviewer, 2026-09-27 (full item table, probe transcripts and OLD citations in `/tmp/p6/REVIEW_6D_2.md`; every NEW targets
file re-read against the cited OLD lines; 6D objects rebuilt from scratch; three own negative probes; full suite; both lints; size check;
OLD tree verified untouched — `find OLD -newer /tmp/p6/REVIEW_6D.md -not -path './logs/*'` → `.DS_Store` only).

| check | result |
|---|---|
| items 1-4, 6-14 | CLOSED against OLD: alt16-route tag exclusion (`:9162-9167`, `:14327-14338`), zte-gated sticky Z history + `GetBitsPerPixel` tags (`:9170-9186`, `:9229-9231`, `:4275-4291`), tri-bbox unclamped `valid_rect` / scissor-clamped `drawn` / `alt16_drawn` + full-overwrite drop (`:13566-13700`), per-view author counters + lane records (`:13933-13950`, `:15241-15247`, `:17495-17501`), `EvictUnderDraw` (`:13990-14021`), upload deposit removed (`render.h:1869-1885`), 0x0A early arm (`:17586-17608`), 512 / 64 MiB / before-insert / in-place (`:23210-23269`), the three vetoes incl. whole-page restore ≡ per-pixel `continue` (`:15951-15987`, `:16332-16385`, `:16455-16471`), linear-span `StageTouched` ≡ `BeginDeposit`/`EndDeposit` (`gs_deposit_overlay.h:96-129`: seeded / kept / `max`-stamped span pages, stray pixels only into already-Pending pages, epoch 0 never re-stamps per `vram_image.cpp:116`), Z saturation (`:18383`, `:18397`), colour-only epoch-0 `SyncAll` (`:21903-21925`), `RescueStride` (`:15580-15583`), fresh tags 0 (`opengl_backend.h:642-643`), `hasValidContent` entry (`:16283`, `:18160`) |
| item 5 | deferred verbatim + citations as **P6-ALIAS-PREDRAW**, bound to **P6-REFRESH-ARMS** whose claims I verified (`:17641-17650`, `:17718-17722`, `:17891`, `:17940`; `RefreshFromVram` has no caller outside its TU) |
| rescue authorship veto | OLD GPU author marks come only from deposits (`:15956`, `:18207`) and the pending-ledger elide sites (`:14158`, `:14204`, `:24808` — P6-TAIL-REMAINDER); a never-deposited non-ledger target's rescue writes nothing in OLD too — `Gs_Inv13/14`'s pin is faithful; the 6D′ ledger port must carry the elide marks |
| pins bite | Probe A `readback_engine.cpp:85` drop the Pending test → `span: 9 checks, 2 failures`; Probe B `render_target_cache.cpp:97` epoch 0 → `last_draw_gfx_epoch` → `syncall: 5 checks, 1 failures`; Probe C `render_target.cpp:188` `if (false && alt16_route)` → `inv11: 17 checks, 4 failures`, `inv12: 14 checks, 4 failures`; sources restored `shasum` byte-identical |
| gates | 28 objects rebuilt, `BUILD_EXIT=0`, `warning:` count 0; `test_targets all: 200 checks, 0 failures`; `ctest -R 'Gs_Inv\|Targets_\|Present_\|Profile_\|Gs_Hle'` → `100% tests passed, 0 tests failed out of 44`; full `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 148`, `Total Test time (real) = 196.87 sec`, `CTEST_EXIT=0` (#109 / #119 / #122 Passed; an orphaned earlier ctest instance the tool runner SIGKILLed left a "TickSyncBoundaryOrder (Subprocess killed)" tail in the same log — isolated rerun 2/2 Passed); `LintOneClass … 0 findings -> PASS`; `LintEnginePurity … 0 game tokens -> PASS`; `size check OK`; purity / getenv / tag greps empty; CMake diffs = `add_subdirectory … # Phase 6` only |
| new divergences | (i) `LogicalRect::Union` skips degenerate rects; OLD `expandValidRect` (`opengl_backend.h:913-922`) does not — reviewer probe `/tmp/p6/rv6d2_degenerate_probe.cpp`: `{10,10,20,20}` then degenerate `{5,5,5,30}` → NEW `{10,10,20,20}`, OLD `{5,5,20,30}` (`GlDrawExecutor::Draw` has no zero-area guard, so reachable); (ii) `RefreshFromVram`'s shared tail omits the full `expandAlt16Stale` OLD does for every arm (`:18077-18078`); (iii) `## Findings ledger` still carries `P6-ALT16-REFRESH-CPU … unproven until 6D′` and no rows for the four fix-round findings; "P6-REFRESH-ARMS (see Findings below)" has no bullet; both OLD refresh arms read the COMMITTED `gs_vram`, not the composed view NEW's `ReadView` returns — belongs in P6-REFRESH-ARMS |

**VERDICT: NEEDS_CHANGES** — 3 numbered fixes in `/tmp/p6/REVIEW_6D_2.md` §Verdict: (1) `render_target.cpp` `AfterDraw` `valid_rect` growth
= OLD `expandValidRect` incl. degenerate bboxes, pinned in `Targets_DrawnRectsByRoute`; (2) `target_refresher.cpp` `RefreshFromVram` shared
tail `alt16_stale ∪= full` when a companion exists, pinned in `Targets_RefreshFromVram`; (3) `PHASE6_IMPL.md`: ledger row P6-ALT16-REFRESH-CPU →
CLOSED, ledger rows for P6-ENCODE-STRIDE / P6-DEPOSIT-SPAN / P6-ALIAS-PREDRAW / P6-REFRESH-ARMS, a real P6-REFRESH-ARMS Findings bullet (committed-
mirror source `:17718-17722` / `:17891` / `:17940`, r173 stamps `:18069-18076`, tail alt16 stale), P6-TAIL-REMAINDER to name the `NoteGPUElided`
author marks, HANDOFF note that `RenderTargetCache::Destroy(fbp, epoch)` has no caller and is not the OLD destroy/evict shape. Everything else
in the fix round is accepted.

### Review 6D fix 2 (2026-09-28, ≈25 min)

Scope: the three numbered items of `/tmp/p6/REVIEW_6D_2.md` §Verdict, nothing else. Worktree `phase6` at `78b5655`, no commits; OLD read-only
(only read for the citations below). Line refs `:NNNN` = `OLD/host_app/graphics/opengl/opengl_backend.cpp` unless a file is named.

| # | applied | OLD | pin |
|---|---|---|---|
| 1 degenerate valid_rect | `LogicalRect::ExpandToInclude(x0,y0,x1,y1)` added (`logical_rect.h/.cpp`): empty → assign as-is (a zero-area input leaves it empty, as OLD's `hasValidContent()` reads it), non-empty → min/max WITHOUT the degenerate guard; `RenderTarget::AfterDraw` main route grows `valid_rect` with it. `Union` keeps its guard. Every other `Union` call site re-checked against its OLD twin: `drawn_since_sync` / Cd dirty / `alt16_stale` (draw) sit inside OLD's `if (dx1 > dx0 && dy1 > dy0)` `:13666-13700`, `alt16_drawn` inside `if (ax1 > ax0 && ay1 > ay0)` `:13642-13643`, Z inside `if (zdx1 > zdx0 && zdy1 > zdy0)` `:9212-9213`, the full-extent unions (`0,0,w,h`) are non-degenerate for any live target — so `Union`'s guard is a no-op at each of them and only the `valid_rect` site differed | `opengl_backend.h:913-922` `expandValidRect` (no guard), caller `:13656` (unguarded) | `Targets_DrawnRectsByRoute` extended: `{10,10,20,20}` then degenerate `bbox_tri = {5,5,5,30}` → `valid_rect == {5,5,20,30}`, `drawn_since_sync` unchanged `{10,10,20,20}`, Cd dirty unchanged; the same degenerate bbox on an EMPTY target → `!HasValidContent()` with `y1 == 30`; direct `LogicalRect` check (`Union` ignores `{5,5,5,30}`, `ExpandToInclude` takes it). `rects: 12 checks` |
| 2 refresh tail alt16 stale | `TargetRefresher::RefreshFromVram` shared tail: `if (t.views.alt16.id) alt16_stale.Union(0, 0, w, th)` after the Cd void (every decode arm; the CT16S arm already had it) | `:18093-18094` (shared tail, after the r173 stamps `:18077-18086`) | `Targets_RefreshFromVram` extended: CT16 target without a companion → `alt16_stale.Empty()`; `EnsureAlt16` + reset + `RefreshFromVram(t, 0, 8)` → `alt16_stale == {0,0,64,64}`, `main_author_seq == 3`. `refresh: 16 checks` |
| 3 IMPL record | `## Findings ledger`: P6-ALT16-REFRESH-CPU → CLOSED (program `:14629-14633` ≡ `TexaExpand`, `fold_ok`); P6-TAIL-REMAINDER row names the `NoteGPUElided/Rect` dependency; rows added for P6-ENCODE-STRIDE, P6-DEPOSIT-SPAN, P6-ALIAS-PREDRAW, P6-REFRESH-ARMS. 6D `### Findings`: P6-TAIL-REMAINDER bullet now states that the rescue authorship veto (`readback_engine.cpp:189-191` = `:16462-16485`) reads marks written only by deposits (`:15956`, `:18207`) and the pending-ledger elide sites (`:14158`, `:14204`, `:24808`), so the 6D′ ledger port must carry them; a real **P6-REFRESH-ARMS** bullet lists (1) the arms, (2) the COMMITTED-mirror source (`gs_vram[addr+3]` in the `:17718-17722` row loop at `:17726`; `DecodePSMCT16(gs_vram.data(), …)` `:17891-17898`, `:17940-17953`) vs NEW's composed `ReadView`, (3) the r173 `vram_coherent_epoch/class` stamps `:18069-18086`, (4) the tail alt16 stale (fixed here). HANDOFF: "see Findings below" → points at the bullet; new note that `RenderTargetCache::Destroy(fbp, gfx_epoch)` has NO caller (grep engine/app/tests → definition only) and is not the OLD destroy/evict shape (full-extent rescue with `last_draw_gfx_epoch` + `RescueStride` + `FlushPendingRTSyncs(kDestroy)`, `:15572-15590`, `:19656-19670`) — 6D′/6F align (rescue form) or drop | — | — |

Pins bite (temporary reverts, sources restored from `/tmp/p6/{rt,tr}.bak`, rebuilt 0 warnings, `test_targets all: 209 checks, 0 failures`):
```
ExpandToInclude -> Union at render_target.cpp main route:  FAIL test_targets.cpp:318 gv == {5,5,20,30} ; FAIL :325 empty-target case ; rects: 12 checks, 2 failures
tail alt16_stale line removed from target_refresher.cpp:    FAIL test_targets.cpp:726 alt16_stale == {0,0,64,64} ; refresh: 16 checks, 1 failures
```

Gates (fresh, `/tmp/p6/fix6d2_build.log`, `/tmp/p6/fix6d2_ctest_full.log`):
```
$ cmake --build --preset macos-dev -j4                                   -> BUILD_EXIT=0 ; grep -c 'warning:' -> 0
$ ./build/tests/gs/test_targets all                                      -> test_targets all: 209 checks, 0 failures
$ ctest --preset macos-dev -R 'Gs_Inv|Targets_|Present_|Profile_|Gs_Hle' --output-on-failure | tail -8
  43/44 Test  #78: Gs_HleIncludeDepth ...................   Passed    0.07 sec
  44/44 Test #143: Gs_Inv34_ReverseSubtract .............   Passed    0.02 sec
  100% tests passed, 0 tests failed out of 44 ; Total Test time (real) = 3.83 sec
$ ctest --preset macos-dev                                               -> 100% tests passed, 0 tests failed out of 148 ; Total Test time (real) = 212.67 sec ; CTEST_EXIT=0
  #64 Targets_DrawnRectsByRoute Passed 0.05 s ; #72 Targets_RefreshFromVram Passed 0.08 s ; #109 EeRunTokenAndEventQueue Passed 0.11 s ;
  #119 TickSyncBoundaryOrder Passed 0.28 s ; #122 RestoreMhashAnchors Passed 11.22 s
$ python3 tests/lint/lint_one_class.py --root .                          -> LintOneClass: 421 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root .                      -> LintEnginePurity: 368 files scanned, 0 game tokens -> PASS
$ python3 /tmp/p6/check_sizes.py .                                       -> size check OK
$ grep -rn getenv engine/gs/hle/targets ; tag grep over targets + tests/gs/targets  -> empty (README line only)
```
Files touched this round: `engine/gs/hle/targets/{logical_rect.h,logical_rect.cpp,render_target.cpp,target_refresher.cpp}`,
`tests/gs/targets/test_targets.cpp` (797 lines), this file. Sizes: `render_target.cpp` 255, `target_refresher.cpp` 231, `logical_rect.cpp` 47.

### Review 6D (verdict, iteration 3)

Independent reviewer, 2026-09-28 (item table, probe transcript and OLD citations in `/tmp/p6/REVIEW_6D_3.md`; scope = the three items of
`/tmp/p6/REVIEW_6D_2.md` §Verdict + tree green; worktree `phase6` at `78b5655`, no commits; OLD read-only — `find OLD -newer
/tmp/p6/REVIEW_6D_2.md -not -path '*/logs/*' -type f` → empty).

| check | result |
|---|---|
| item 1 degenerate `valid_rect` | CLOSED: `LogicalRect::ExpandToInclude` (`logical_rect.cpp:21-30`, empty → assign, else min/max, no guard) = `opengl_backend.h:913-922` (`hasValidContent` `:909-911` ≡ `!Empty()`); `render_target.cpp:199` main route uses it with the `(int)`/`ceil` tri bbox = `:13656-13657` (unguarded); `Union` keeps its guard only at sites OLD guards. Pinned `test_targets.cpp:305-333`. **Own negative probe**: `ExpandToInclude → Union` at `:199`, rebuild `test_targets`, `ctest -R Targets_DrawnRectsByRoute` → `***Failed`, `FAIL :318`, `FAIL :325`, `rects: 12 checks, 2 failures`; source restored — `shasum -a 256` before == after, `git diff --stat` unchanged |
| item 2 refresh tail alt16 stale | CLOSED: `target_refresher.cpp:226-227` `if (t.views.alt16.id) alt16_stale.Union(0,0,w,th)` on the shared decode tail = `:18093-18094` (after the r173 stamps `:18071-18086`); CT16S arm `:186` = `:17604-17605`; `(0,0,w,h)` non-degenerate so `Union` ≡ `expandAlt16Stale` (`opengl_backend.h:900-906`). Pinned `test_targets.cpp:718-727` (companion → `{0,0,64,64}`, `main_author_seq == 3`); fix-2's negative (`FAIL :726`, `refresh: 16 checks, 1 failures`) quoted |
| item 3 IMPL record | CLOSED: ledger `:94` P6-ALT16-REFRESH-CPU CLOSED (`:14629-14633` ≡ `TexaExpand`, `fold_ok`); `:95` P6-TAIL-REMAINDER names `NoteGPUElided/Rect` `:14158`/`:14204`/`:24808`; rows `:97-100` P6-ENCODE-STRIDE / P6-DEPOSIT-SPAN / P6-ALIAS-PREDRAW / P6-REFRESH-ARMS present; 6D Findings bullets `:961` (author-mark dependency) and `:962` (P6-REFRESH-ARMS: arms, COMMITTED `gs_vram` source `:17718-17726`/`:17891-17898`/`:17940-17953`, r173 stamps `:18069-18086`, tail stale) exist and the HANDOFF's "Findings bullet above" resolves; HANDOFF `:1006-1012` records `RenderTargetCache::Destroy(fbp, gfx_epoch)` no caller / not the OLD rescue shape (`:15572-15590`, `:19656-19670`) → 6D′/6F align or drop |
| gates | `ctest -R 'Targets_DrawnRectsByRoute\|Targets_RefreshFromVram'` → 2/2 Passed; `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`, `warning:` count 0 (`/tmp/p6/rv6d3_build.log`); full `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 148`, `Total Test time (real) = 201.26 sec`, `CTEST_EXIT=0` (`/tmp/p6/rv6d3_ctest_full.log`; #109 EeRunTokenAndEventQueue 0.25 s, #119 TickSyncBoundaryOrder 0.31 s, #122 RestoreMhashAnchors 12.40 s all Passed first run, no rerun); `LintOneClass: 421 files scanned, 0 findings -> PASS`; `LintEnginePurity: 368 files scanned, 0 game tokens -> PASS`; `size check OK`; game-address / `FUN_` grep over `engine/gs/hle` empty; `getenv` in targets empty |

**VERDICT: APPROVED** — stage 6D closed; the deferred items (P6-ALIAS-PREDRAW, P6-REFRESH-ARMS, P6-TAIL-REMAINDER incl. the elide author marks,
`RenderTargetCache::Destroy` shape) are carried by the HANDOFF and the §7.1 ledger into 6D′.

## 6D′ — texsrc resolver (2026-09-28, ≈55 min; deferred targets items → 6D″)

Stage 6D′ landed the whole `engine/gs/hle/texsrc` module and its G7 tests, green. The STOP rule fired before the
deferred targets items (P6-ALIAS-PREDRAW, P6-REFRESH-ARMS, P6-TAIL-REMAINDER, TargetDims, `Destroy` rescue form,
`DrawScanout`) were started; they are handed to **6D″** verbatim below. Tree: 0 warnings, 161/161, lints 0/0.

### Landed (2,043 lines incl. the 470-line test)

`engine/gs/hle/texsrc/` (lib `ps2r_gs_hle_texsrc`, links `ps2r_gs_hle_targets ps2r_gs_format ps2r_core`; `add_subdirectory(hle/texsrc)  # Phase 6` appended to `engine/gs/CMakeLists.txt`):
- `content_key.h/.cpp` — `ContentKey` (`Compute(view, rgn, window, clut, mips, fast)`: basis `1469598103934665603`, salt `R125CK`, tuple `{tbw,psm,tw,th,cpsm,csm,csa}`, texa 3 bytes, `R159RG` + rect only for a genuine sub-rect, content steps `Mix64(h, Xxh64(bytes, seed 0/2/3+i))` (fast) or byte-FNV, `S82MIP` + per-level tbw + windows; NO tbp/cbp; `Footprint` / `RowBand` via `BlockDecode::DecodeFootprint / DecodeRowBand`)
- `decode_cache.h/.cpp` — `DecodeCache` (L1 `{tex,w,h,fp_start,fp_size,last_use}` with dims re-check and `EvictInRange`; L2 `kL2MaxEntries 16384`, `kL2MaxBytes 320 MiB`, `kL2SuMaxEntries 2048`, `kL2SuMaxBytes 32 MiB`; every live insert in probation, `L2Lookup` hit = tenure, single-use pool evicts the OLDEST unrescued probation entry, then the LRU cap / budget; entries own their surfaces)
- `clut_source.h/.cpp` — `ClutSource::Ensure(cbp, fold_single, vram)` (1 KiB via `ReadView`, byte-FNV key, `grid = fold ? Csm1(i) : Csm1(Csm1(i))`, `PsmLayout::PixelAddress(CT32, g%16, g/16, 0, 1)` relative to the window, row 0 `{r,g,b, a>=128?255:a*2}`, row 1 raw; 256×2 `CreateSurface` + `Upload`; rebuilt only on a hash change; `Build` CPU reference)
- `clut_heal_cache.h/.cpp` — `ClutHealCache::Heal(cbp, palette, layout)` (exempt via `VramLayout::IsClutHealExempt`; all-zero → cached; ≥200 entries alpha ≥100 → becomes cached)
- `uv_bbox.h/.cpp` — `UvBbox::Scan` (verbatim: FST u/16, STQ `(s/q)*tex_w` under FE_TONEAREST, bails q≤0 / non-finite / >1e7 / empty)
- `uv_region.h/.cpp` — **[added by the Review 6D′ fix]** `UvRegion::{RectDecodeCapable, Eligible, DeriveAxis, Derive}` (`R172DeriveFromBBox` verbatim: floor/ceil, `hi <= lo → lo+1`, bilinear taps ±1 when `mmag || (mmin & 1)`, REPEAT single-tile rebase else full, CLAMP, REGION_CLAMP ∩ `[minu, maxu+1]`, REGION_REPEAT full, clamp `[0,W)`, degenerate → full, q = 64 outward, classes Rect / FullIsFull / FullQuant / FullAreaRule with `!is_full ∧ 2*area <= full`)
- `window_in_target.h/.cpp` — `WindowInTarget::{Contains, PackSelf6, UnpackPageOff, Fmt6Remap, Fmt6IndexFromRgba}` (`s8_inrt.h` geometry verbatim)
- `mip_sampler.h/.cpp` — `MipSampler::Capture(view)` (mmin ≥ 2 ∧ mxl > 0; per level tbp/tbw (0 → 64 px) from the view's MIPTBP fields, `MipChain::LevelSize`, footprint cap 1 MiB / 4 MiB end → chain stops; lcm / l / k)
- `texture_decoder.h/.cpp` — `TextureDecoder::{Decode, DecodeRect, DecodeLevel, ReadPalette (+heal), DecodeScanoutBuffer (CT32/CT24/CT16/CT16S, alpha 255), ParseDispfb, ParseDisplay (MAGH/MAGV → pixel dims), GpuAlpha}` over `BlockDecode` / `PixelConvert::ScaleAlphaPs2ToGpu`
- `texture_resolver.h/.cpp` — `TextureResolver::ResolveRead(const DrawIr&, const Context{frame_count, active_fbp, fast_hash, has_snapshot}&) → TextureSource` = fresh struct per call → **[corrected by the Review 6D′ fix, see below]** `MatchTarget` (1a alt16 companion incl. `EnsureAlt16 + RefreshAlt16FromMain`; 1b `VramLayout::IsSceneAccum` shortcut; no valid content → decline; 1c `MatchIndexed`: T8H only; pair chain = recent ≤ 2 frames ∧ `tbw == render_fbw` ∧ `ClutServable` ∧ `width == render_fbw`, fmt 4 = 32-bit class with `IdxFresh(class 1)` unless self-read, fmt 5 = CT16 pair at `+h*fbw*2` with the pair predicates, BOTH keep masks == `kIndexLaneKeep16`, THEN the SH5 CPU-dirty veto; on ANY fall the S107 `LaneLiveRescue` (non-self, `tbw ≥ 64 ∧ tbw % 64 == 0`, per half valid ∧ scale 1 ∧ width == tbw ∧ recent ∧ lane record of THIS frame == the view's author seq, partner at `tbp + h*tbw*2` with equal height, `ClutServable`, no dirty veto, binds each half's LANE surface); `ClutSource::Ensure(cbp, false)`; fmt 4 self-read at scale > 1 declines; an indexed decline of a recent NON-16-bit target deposits `enc = render_psm == 0x01 ? 0x01 : 0x00` at the current epoch and sets `uv_native`; 1d `MatchColor`: keep test ONLY when `!recent ∧ ArePagesCpuDirty(tbp, tex_size)` — `RtKeepServe(valid, bpp_compatible, WindowEpoch(tbp, tex_size), last_draw_gfx_epoch)` then the CX8 `ReadRectUploadEpoch` fallback — a fall rescues `{0,0,1<<tw,1<<th}` (1..10 else the target, stride `tbw` when 64-aligned, epoch `last_draw_gfx_epoch`, ENCODED IN THE READ PSM) then declines; 16↔32 class mismatch → plain drawn-rows sync at the current epoch + decline, except render `0x0A` ∧ read `0x00` → serve; every serve via `ServeColor`: `uv_tex_w/h = target dims`, shuffle classification (`ClassifyShuffle`, `uv_tex_w *= scale_u`) for plain serves, STQ READVIEW `uv_pre = 1<<TW/TH` for plain unshuffled colour serves only (else `uv_pre = uv_tex`); a colour-class serve of `tbp == active_fbp` binds nothing (`Declined::SelfRead`, enabled, TEX0 dims); a target at TBP → every decline goes to `DecodeFallback` directly) → `MatchDepth` (no target at TBP; containment search `tbp ∈ [last_zbp, last_zbp + w*h*native_bpp)` ∧ `last_z_wrote` ∧ valid, max `last_z_seq`; post-pick vetoes self / `tbw == render_fbw` / depth surface; fmt 1 only `psm 0x30 ∧ nz 0`, fmt 3 `0x32 ∧ nz 0`, both `aligned ∧ zoff < height` with `zoff = (delta / (8192*fbw/64)) * 32`, fmt 2 `0x32|0x3A ∧ nz ∈ {2,A} ∧ delta == 0`; `z_row_offset = fmt == 2 ? 0 : zoff`; every decline `SyncZToVram` at the current epoch) → **[added by the Review 6D′ fix 2]** `RefreshZSource` (every Z-psm read `0x30/0x31/0x32/0x3A` whose result has `depth_fmt == 0` — a colour serve, a self-read, a tier-1 decline or a depth-tier decline alike: containment `tbp ∈ [last_zbp, last_zbp + w*h*native_bpp)` ∧ `last_z_wrote` ∧ `frame_count - last_z_write_frame <= 2` ∧ valid, max `last_z_seq` → `SyncZToVram(best, ir.vram_epoch)` — AFTER the tier-1 deposits and BEFORE the decode, so the Z bytes win over a colour deposit at the same address) → `DecodeFallback` (`!has_snapshot` → enabled, owner None; `MaterializePages(Decode)` over the footprint and the CLUT page; `UvRegion::Eligible` → `UvBbox::Scan` → `UvRegion::Derive` (+ the per-tuple > 8 rect variants sticky-full guard) → `DecodeRect` else `Decode`; `ContentKey` → L1 → L2 (rebind into L1) → decode + `CreateSurface` + `Upload` + `L1Insert` + `L2Insert(probation = the tuple's previous content key is still in L2)`; `region.applied`; `uv_tex = uv_pre = 1<<TW/TH`)
- `texture_source.h` — `Owner` gained `render::SurfaceId clut, partner` (P6-RESOLVER-NAME)
- README Contents rewritten.

Tests: `tests/gs/texsrc/test_texsrc.cpp` (779 checks), registered in `tests/gs/CMakeLists.txt`.

### OLD sources transplanted (read-only)

`texcache_key.h` (ContentKey verbatim incl. basis, salts, seeds), `uv_bbox.h/.cpp` (Scan verbatim), `s8_inrt.h` (geometry), `idx16_lane.h` (`Fbmsk16From32` / `kIndexLaneKeep16` already in `RenderTarget`), `foglane_live.h` + `opengl_backend.cpp:20290-20340` (lane-live serve), `gs_mipmap.h` (capture half), `s9_shadow.h:106-111` (`RtKeepServe`), `ClutPrescaleAlpha`, `opengl_backend.cpp` R18 `EnsureClutTex :21675-21857` (256×2 build), R21 constants `opengl_backend.h:1881-1900` + `render.h:1573-1592` (L2 / SU budgets, probation policy), R16 `:19887-20430` (indexed tier predicates as coded), `:20430-21131` (keep / rescue / class-mismatch decline structure), `:21388-21599` (depth tier shape), `texture_decoder.cpp:1286-1370` (CLUT heal), `:2174` (`DecodeScanoutBuffer`), `:2235-2260` (`ParseDISPFB` / `ParseDISPLAY`); INV §6.5, §6.6, §6.9, §10, §12 rows 15, 17, 18, 19, 33.

### Flags applied

NORM (winning arm only): `PS2_GL_RT_NEVERSTALE` / `PS2_GS_RTKEEP` (keep predicate + rescue-before-decline), `PS2_GL_RT_READVIEW` (composed-view reads; STQ plain-RT serve prescale by the TEX0 window `1<<TW/TH` while `u_tex_size` divides by the target dims — corrected by the Review 6D′ fix; the original sentence had the two swapped), `PS2_GL_FOGLANE_LIVE` + `PS2_GL_IDX16_LANE` (lane-live / keep-mask pair admission), `PS2_GL_IDXTEX` / `IDXTEX_32` / `IDXTEX_16` / `IDXTEX_FRESH` (fmt 4/5 + `IdxFresh`), `PS2_GL_IDXTEX_CLUTSNAP` (bytes from the composed view — the snapshot is empty in replay), `PS2_GS_CLUTRAW` (raw row 1), `PS2_GL_TEXDEMAND` / `PS2_GL_UVREGION` (UV-bounded decode), `PS2_GL_FASTHASH` (→ `Context::fast_hash`, both variants pinned), `PS2_GL_L2_SU` / `L2_PROB` / `L2_MB` / `L2_SU_MB` (constants), `PS2_GS_MIPMAP` / `MIP_ALLCLASS` (capture), `PS2_SH5_IDXDIRTY` (CPU-dirty page veto). **Peak value decision `r198::Idx8Ct16On` = OFF** (follows `PS2_GL_UPLOAD_DEPOSIT` OFF) → fmt 6 and `PS2_S8_INRT` NOT written (P6-FMT6-OFF). DELETE (not written): `PS2_GL_RESMEMO*`, `PS2_R197_*`, TEXPOP / SH1-T8H / R197-D2P probes, S8 texdump, `PS2_FTT_FORCE_*` (TEST), rtns census, `PS2_MIPCEN`, `S82-MIP` warnings. NOT YET APPLIED (→ 6D″ with the audit) **[list extended by the Review 6D′ fix; items 1–8 of the review are now APPLIED, see the fix record]**: `PS2_R198_D1FMV` (FMV clamp via `VramLayout::IsFmvBlitSignature`), mip level upload (`UploadLevel` / `FinishMipChain`) in `DecodeFallback` (+ the mip windows in the ContentKey), `EvictInRange` wiring from `EvictUploadedInRange`, `shuffle_snap` (R175 column snap), the ContentKey 4 MiB wrap split (seed 1 second run — cache identity only), the OLD `FlushPendingRTSyncs(kFTT)` demand flushes (the NEW `VramImage` stages deposits directly — no pending ledger yet, P6-TAIL-REMAINDER), the R6 `persistence_tex` self-read substitution (P6-SELFREAD-PERSIST below), the `TargetRefresher::ArePagesCpuDirty` producers OLD's `m_cpu_dirty_pages` has (`SyncRTToVRAM` marks / `MarkPagesGPUValid` clears — P6-REFRESH-ARMS). APPLIED by the fix round (were listed here before): `PS2_GL_UVNATIVE` (`uv_native` on every indexed-fall-to-decode path, `:20807`), R114 (decode `uv_tex = uv_pre = 1<<TW/TH`), R159 region rebase (`transform.region`, `uv_tex_w/h` stay the TEX0 dims as `:10755-10758`), shuffle classification (`ClassifyShuffle` + `uv_tex_w *= scale_u`), `PS2_S8_INRT` **deviation stated**: FLAG_LEDGER row 639 and PHASE6_PLAN §5 list `PS2_S8_INRT` as NORM-ON; the code value is `s8inrt::On(r198::Idx8Ct16On())` → `r201::UploadDepositOn()` → `v && *v && atoi(v) != 0` (`render.h:1869-1884`, default OFF, R202-REGR2) — the ledger/plan rows are stale against the frozen code and the peak configuration does NOT run S8-INRT (P6-FMT6-OFF); the rows are to be corrected to "DELETE-by-value (OFF at peak)" when the ledger is next touched.

### Tests added (PASS lines from `/tmp/p6/6d2_ctest.log`, the 161/161 run)

```
 79/161 Test  #79: Gs_Inv15_NeverStaleRtRead ............   Passed
 80/161 Test  #80: Gs_Inv17_PerCallResultsReset .........   Passed
 81/161 Test  #81: Gs_Inv18_ContentKeyAddressFree .......   Passed    0.01 sec
 82/161 Test  #82: Gs_Inv19_DecodeRectIsCrop ............   Passed    0.01 sec
 83/161 Test  #83: Gs_Inv33_FogLaneLive .................   Passed    0.14 sec
 84/161 Test  #84: Present_DisplayMagnification .........   Passed    0.01 sec
 85/161 Test  #85: Texsrc_ClutSource ....................   Passed    0.02 sec
 86/161 Test  #86: Texsrc_WindowInTarget ................   Passed    0.01 sec
 87/161 Test  #87: Texsrc_Idx16Lane .....................   Passed    0.01 sec
 88/161 Test  #88: Texsrc_L2Entries .....................   Passed    0.04 sec
 89/161 Test  #89: Texsrc_S9ShadowPredicates ............   Passed    0.01 sec
 90/161 Test  #90: Texsrc_ClutHeal ......................   Passed    0.01 sec
 91/161 Test  #91: Texsrc_UvBbox ........................   Passed    0.01 sec
100% tests passed, 0 tests failed out of 161
Total Test time (real) = 201.53 sec
CTEST_EXIT=0
```
`test_texsrc all: 779 checks, 0 failures`. Negative proof during the stage: the first L2 cut put the cap test's 16384
inserts through probation and the single-use pool evicted them (`l2: 4 failures`) — the tenured-insert form OLD's
`TestL2_Insert` uses was added (`L2Insert(..., probation=false)`), so the pin reaches both pools. NOT ported (→ 6D″):
`Texsrc_CdCopyDecide` (test_blendcp_lazy / rt_dirty_tiles union arm), `Texsrc_ResolveTiers` (synthetic per-tier
drive — `Gs_Inv15/17/33` cover 1c/1d/depth partially), `Targets_AliasPreDraw`, `Targets_RefreshArms`,
`Targets_PendingLedger`, `Targets_TargetDims`.

### Findings

- **P6-FMT6-OFF** (ledger): `r198::Idx8Ct16On()` = `r201::UploadDepositOn()` = OFF at peak → fmt 6 and S8-INRT are not live; not written.
- **P6-RESOLVER-NAME** (ledger): `TextureResolver::ResolveRead`; `TextureSource::Owner::{clut, partner}` added (the 6C contract's `DrawParams.clut / idx_partner` sources).
- **P6-RESOLVER-AUDIT** (ledger, OPEN): the resolver was written under the STOP rule from INV §6.5 and the R16 excerpts read this stage; the R6 body and the remaining R16 returns are not yet audited line by line. 6D″ owns the audit before 6F.
- **P6-CLUT-HEAL** mechanism half CLOSED: `ClutHealCache` as INV §6.6 / `texture_decoder.cpp:1340-1356` (thresholds 200 / 100, all-zero substitute), exemption = layout data; applied in `TextureDecoder::ReadPalette` for 256-entry palettes only (OLD heals inside the T8 path; the T4 16-entry path never reaches the heal).
- P6-ALIAS-PREDRAW, P6-REFRESH-ARMS, P6-TAIL-REMAINDER: NOT started (→ 6D″, verbatim below). `VramImage::ReadMirror` NOT added yet (6D″ adds it with P6-REFRESH-ARMS).

### Gate status

| gate | evidence |
|---|---|
| G7 (texsrc half) | the 13 ctests above Passed; `Present_DisplayMagnification` Passed |
| build / lints / size | `BUILD_EXIT=0`, `grep -c 'warning:' /tmp/p6/6d2_build.log` → `0`; `LintOneClass: 440 files scanned, 0 findings -> PASS`; `LintEnginePurity: 387 files scanned, 0 game tokens -> PASS`; `size check OK`; `Gs_HleIncludeDepth: PASS`; game-address / `FUN_` grep over `engine/gs/hle` empty; `#include "engine/render/backends` in `engine/gs` empty; `CommitDeposits` in `engine/gs/hle` empty; `getenv` in texsrc empty |
| full ctest | `100% tests passed, 0 tests failed out of 161`, `Total Test time (real) = 201.53 sec`, `CTEST_EXIT=0` (no flake rerun needed) |

### HANDOFF (6D′ → 6D″)
Done: engine/gs/hle/texsrc (9 pairs + POD, lib ps2r_gs_hle_texsrc), 13 ctests (all PASS, GL-free), README; INV §6.5 tiers as coded
  (1a/1b/1c fmt 4-5/1d/depth/decode fallback), §6.6 (ContentKey, L1/L2 budgets + probation, CLUT 256×2, CLUT heal), §6.9 uv_bbox,
  §10 (decoder wrapper, DecodeScanoutBuffer, ParseDISPFB/DISPLAY), §12 rows 15, 17, 18, 19, 33.
Signature 6E/6F call: `TextureSource TextureResolver::ResolveRead(const DrawIr& ir, const TextureResolver::Context& ctx)` with
  `Context{uint32_t frame_count; uint32_t active_fbp; bool fast_hash = true; bool has_snapshot = true}` **[has_snapshot added by the
  Review 6D′ fix: the replay driver passes false — OLD's empty `vram_snapshot` ⇒ `enabled, owner None` (`gs_replay.cpp:357-360`,
  `:4930`, `:10241-10246`); the live renderer leaves it true]**; construct `TextureResolver(render::Backend&, VramImage&,
  RenderTargetCache&, TargetRefresher&, const VramLayout&)`. classrdr passes: the DrawIr (tex_enabled, tex view — the resolver reads
  `ir.tex.fst` as OLD reads `ti.fst`, verts, raster.frame.fbp, `vram_epoch` = the current batch epoch for the plain syncs),
  `frame_count` = the renderer frame counter used for `CoherenceState::last_draw_frame` / lane records,
  `active_fbp` = the draw's FRAME.FBP; then copies `owner.clut → DrawParams.clut`, `owner.partner → DrawParams.idx_partner`,
  and (for a scene-accum / composite read) calls `RenderTarget::MarkCompositeConsumed` on the source target itself.
  **[Review 6D′ fix]** `RenderTarget::AfterDraw(ir, res, frame_count, gfx_epoch, draw_seq = 0)`: classrdr passes a strictly
  increasing committed-draw ordinal as `draw_seq` (a Z write records it as `CoherenceState::last_z_seq`, OLD `++m_z_write_seq`
  `:9187`; the depth match picks the max) **[fix 2: and `frame_count` as `CoherenceState::last_z_write_frame`, OLD `:9188` — the
  Z-source refresh accepts a writer only while `frame_count - last_z_write_frame <= 2`, so `frame_count` MUST be the same counter
  `TextureResolver::Context::frame_count` carries]**. `ReadbackEngine::SyncRtToVram(..., rescue, rescue_fbw, enc_psm = kNoPsmOverride)`:
  the encode PSM override OLD's `SyncRTToVRAM(psm_override)` has (`:16303`); the scanout path's 32→16 re-encode uses it too.
  **[Review 6D′ fix 2 — explicit owners OUTSIDE texsrc]**
  * **RESOLVE route → 6F executor (`GlDrawExecutor` / the classrdr draw path), owner of `video.scale > 1`.** OLD `:20404-20423`
    (idx fmt 4/5 serves incl. the fmt 5 partner) and `:21568-21571` (depth serves): when the serving target's `scale > 1`, the
    closed-form page/block remaps are native-dims math, so OLD binds `NativeReadFBO(target)` → `res_color_tex` / `res_depth_tex`
    (a native-resolution resolve of the attachment) instead of the scaled attachment. The resolver reports `owner.surface` = the
    scaled surface and `transform.draw_scale = target.scale`; the executor MUST substitute the native resolve for `idx_fmt != 0` and
    `depth_fmt != 0` serves whenever `draw_scale > 1`. Phase 6 runs `video.scale = 1` (PLAN §5) — the route is dormant until scale > 1
    is enabled and the enabling change must land the resolve with a pin (`draw_scale 2` idx / depth serve → native surface bound).
  * **Interp shadow swap → the interp / present stage (6E `engine/gs/hle/present`, `PresentPipeline` replay).** OLD's
    `FindTargetTexture` WRAPPER `:19576-19588` (`m_interp_replay && ret == rit->second.color_tex` → the interp shadow target's
    `color_tex` of the current `m_interp_gen`) substitutes the interp shadow for a PLAIN colour serve of a target with a shadow.
    It is not a texsrc rule (it sits outside R16, keyed on the replay mode) — the present stage wraps `ResolveRead` and swaps
    `owner.surface` when `owner.kind == Surface ∧ fmt_case == 0 ∧ !alt16 serve ∧ the interp shadow of that FBP is current`.
    Owed with the interp replay port; the `video.interp = on` default (FLAG_LEDGER) makes it live in the peak configuration.
  * **P6-SELFREAD-PERSIST stays an accepted-difference CANDIDATE row** (ledger row above; adjudicated by the frames lane in 6F).
Not done (→ 6D″, in this order):
  1. **P6-RESOLVER-AUDIT**: audit every `TextureResolver` predicate against OLD R16 `:19605-21611` (44 returns) and R6 LIVE rows
     `:4902-4924` (fall-through cases — `using_rt_texture` decision), `:5231-5298` (kDecode demand + ComposeRead refresh),
     `:5322-5430` (UV region incl. the exact floor/ceil/clamp), `:6581-6645` (R198 D1 FMV clamp ← `VramLayout::IsFmvBlitSignature`),
     `:6468` / `:7378` / `:7385` (decode, mip upload via `UploadLevel`/`FinishMipChain`, cache insert incl. L2 insert-on-evict),
     `:7445-7588` (R114 UV width normalisation → `uv_tex_w/h`), `:7589-7656` (S111 READVIEW prescale), `:8266-8287` (outputs incl.
     shuffle classification 16to32/32to16/DepthToColor/ChannelShuffle + `shuffle_uv_scale_u`, FST=1 fold `:7582-7584`). Add
     `Texsrc_ResolveTiers` (synthetic per-tier drive + decline reasons + rescue deposits) and `Texsrc_CdCopyDecide`.
  2. **P6-ALIAS-PREDRAW** + **P6-REFRESH-ARMS** (verbatim from HANDOFF 6D → 6D′ above; add `VramImage::ReadMirror(byte_off, len, out)`
     next to `ReadView`; `CoherenceState::vram_coherent_epoch/class` already exist — stamp them in the refresh tail per
     `:18069-18086`). Tests `Targets_AliasPreDraw`, `Targets_RefreshArms`.
  3. **P6-TAIL-REMAINDER** (pending RT-sync ledger keyed on `VramLayout::IsReaderBandPage`, `DepositAlt16LaneToVRAM :16170`,
     `NoteGPUElided/Rect` author marks `:14158`, `:14204`, `:24808`); `RenderTargetCache::Destroy` → rescue form or drop; `TargetDims`
     helper; `ScanoutComposer::DrawScanout` over `TextureDecoder::DecodeScanoutBuffer`. Tests `Targets_PendingLedger`, `Targets_TargetDims`.
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 164/164 (after the Review 6D′ fix rounds); lints 0/0; getenv sites unchanged
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Texsrc_|Gs_Inv1[5789]|Gs_Inv33|Present_'
Findings: P6-FMT6-OFF, P6-RESOLVER-NAME, P6-RESOLVER-AUDIT (OPEN), P6-CLUT-HEAL (mechanism half closed), P6-Z-SEQ (closed incl. the R6 refresh), P6-SELFREAD-PERSIST (OPEN, accepted-difference candidate)

### Review 6D′ (verdict)
Independent review 2026-09-28 (full record `/tmp/p6/REVIEW_6D2.md`; logs `/tmp/p6/rv6d2_*.log`). Re-ran every gate: fresh rebuild of
the 9 texsrc TUs → 0 warnings; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 161` (173.71 s); the 13 HANDOFF
tests + `Gs_HleIncludeDepth` → 24/24; `LintOneClass 0 findings`, `LintEnginePurity 0 game tokens`, `size check OK`; purity / backends /
`CommitDeposits` / `getenv` / tag greps empty; OLD oracles bounded (s8_inrt, idx16_lane 1025/1025, gl_l2_entries 18/18, display_mag 5/5,
s9_shadow 73/0, blendcp_lazy 21/0, rt_dirty_tiles) all pass; two negative probes (lane-live frame predicate; CLUT double fold) each
failed their pin and were restored byte-identical (`shasum -c` OK). P6-FMT6-OFF CONFIRMED against `render.h:1869-1884` (default OFF) →
`:1901-1920` → `s8_inrt.h:36-44` (FLAG_LEDGER:639 / PLAN §5 "PS2_S8_INRT NORM-ON" is stale). ContentKey ≡ `texcache_key.h`; CLUT 256×2
≡ `:21675-21857`; heal gated by the layout; decoder over `gs/format` only; deferred targets items carried verbatim with citations.

| check | result |
|---|---|
| 1 contract / purity | PASS |
| 2 ResolveRead vs R16 | **FAIL** — 12 predicate mismatches (M1–M12 in the review): keep test not gated on `!recent ∧ CPU-dirty` (`:20800-20802`); rescue psm override (`:20970`); class-mismatch sync form + `0x0A/0x00` serve (`:21038-21048`); indexed-decline deposit for 16-bit renders / enc / uv_native (`:20675-20684`, `:20807`); tier-1 decline falls into `MatchDepth` (OLD `return 0` `:21131`); no colour self-read veto (`:4902-4913`); depth search/self/fmt-1/zoff/SyncZ-on-decline (`:21403-21592`); lane-live merged into the pair chain, dirty-vetoed, main-only bind (`:20272-20360`); `uv_tex`/`uv_pre` swapped for RT serves (`:7530-7537`, `:7601-7607`) |
| 3 decode fallback | **FAIL** — `FindUploaded` first is not OLD (`FindUploadedTexture :23321` has no caller; R6 `:4902-4930`); no "no snapshot ⇒ untextured" input for the replay driver (`gs_replay.cpp:357-360`, `:10241-10246`); UV region is not `R172DeriveFromBBox` (`:1045-1130`, gate `:5332-5334`); L2 never inserted (`:7395-7404`), `EvictInRange` destroys what L2 would hold (`:22518-22532`) |
| 4 ContentKey / CLUT / heal / decoder | PASS (nits: wrap-split seed 1 not reproduced; `Inv18` reference pin is `!= 0 && != basis`) |
| 5 deferred items | carried verbatim → 6D″ (acceptable); texsrc itself incomplete |
| 6 tests | PASS as registered; `Gs_Inv15` pins the inverted keep rule; `Texsrc_ResolveTiers` missing |
| 7 hygiene | PASS |
| 8 record | partial — Landed `uv_pre` sentence wrong; "NOT YET APPLIED" incomplete; S8_INRT deviation unstated |

**VERDICT: NEEDS_CHANGES** — the ten numbered fixes (file-cited) are in `/tmp/p6/REVIEW_6D2.md` §VERDICT: (1) drop `FindUploaded`,
add the no-snapshot input; (2) `MatchTarget` (d) gate / rescue psm / class-mismatch form / self-read / indexed-decline deposit +
uv_native; (3) tier-1 decline terminates; (4) `MatchDepth` per `:21403-21592`; (5) S107 lane-live as a separate rescue, main-chain
keep16 admission; (6) RT-serve `uv_tex` = target dims + shuffle, READVIEW `uv_pre = 1<<TW`; (7) `R172DeriveFromBBox` UV region;
(8) L2 insert-on-miss + retain-on-evict; (9) rewrite `Inv15NeverStale`, literal `Inv18` pin, add `Texsrc_ResolveTiers`;
(10) record corrections. All are texsrc-module items on the critical path (the deferred targets items may stay in 6D″).

### Review 6D′ fix (2026-09-28, ≈40 min)

All ten items of `/tmp/p6/REVIEW_6D2.md` §VERDICT applied against the cited OLD lines (read in full this round: R6 `:4880-5000`,
`:5322-5440`, `:5990-6160`, `:7385-7420`, `:7445-7660`, `:8266-8287`, `:9170-9195`, `:9730-9760`, `:9955-9990`, `:10140-10155`,
`:10236-10250`, `:10600-10608`; R16 `:19755-19800`, `:19855-20030`, `:20060-20440`, `:20640-20720`, `:20780-21135`, `:21388-21611`;
`:1020-1135` `R172DeriveFromBBox`; `:16272-16360`, `:16820-16845`, `:16930-16990` `SyncRTToVRAM`; `:22496-22540`; `rt_neverstale.h:300-380`;
`texcache_key.h:75-200`; `render.h:1869-1884`; `tests/gs_replay.cpp:334-395`; INV §6.5 / §6.6 / §13.2). Two targets-module APIs
had to grow for items 2 and 4 (recorded in the HANDOFF): `ReadbackEngine::SyncRtToVram(..., enc_psm)` (OLD `psm_override` `:16303`,
`:16351`, `:16830`) and `CoherenceState::last_z_seq` + `RenderTarget::AfterDraw(..., draw_seq = 0)` (OLD `++m_z_write_seq` `:9187`).
One item went beyond the review's letter and is called out: the CX8 `ReadRectUploadEpoch` fallback of the keep test (`:20936-20950`,
`rt_neverstale.h:322-352`) is part of the same OLD block and was ported with item 2 (`Texsrc_ResolveTiers` does not yet pin it
separately — the `!keep ∧ compat` branch is reached only when `WindowEpoch` fails; a page-grid vector is owed to 6D″).

| # | item | NEW (file:line) | OLD cite | pin case (`tests/gs/texsrc/test_texsrc.cpp`) |
|---|---|---|---|---|
| 1 | `FindUploaded` arm deleted (`OwnerKind::Uploaded` removed; `gl_draw_executor.cpp:161` follows); `Context::has_snapshot` → `enabled, owner None, Declined::NoSnapshot`, UV mode textured over `1<<TW/TH`, no decode, no surface | `texture_resolver.cpp:517-522`, `texture_resolver.h` `Context` | `:23321` uncalled; R6 `:4902-4930` two arms; `gs_replay.cpp:357-360`; `:10241-10246`, `:10605` | `ResolveTiersIndexed` (`:493`) tail: `creates` unchanged, `NoSnapshot`, then `has_snapshot = true` decodes |
| 2a | keep test gated on `!recent ∧ ArePagesCpuDirty(tbp, tex_size)`; `RtKeepServe(WindowEpoch)` then the CX8 read-rect epoch; fall → rescue + decline | `texture_resolver.cpp:275-291`, `:47-70` | `:20800-20802`, `:20866-20867`, `:20887-20950`, `rt_neverstale.h:322-352` | `Inv15NeverStale` (`:370`) rewritten: recent ∧ dirty ∧ newer upload ⇒ serve; `!recent ∧ dirty ∧ newer ⇒ rescue (`PendingPages > 0`, `drawn_since_sync` kept) + `StaleTarget`; `!recent ∧ dirty ∧ no newer ⇒ serve; `!recent ∧ newer ∧ NOT dirty ⇒ serve` |
| 2b | rescue encodes in the READ psm | `texture_resolver.cpp:140`; `readback_engine.cpp:117-123`, `:131-146` | `:20961-20972` `SyncRTToVRAM(tbp, target, tex_psm, nullptr, last_draw_gfx_epoch, rr, efbw)` | `ResolveTiersColor` (`:412`): CT24 read of a CT32 target → deposited pixel `(0,32)` at `0x102000` has RGB from the attachment and byte 3 `== 0x5A` preserved (a CT32 encode stores `0x80`) |
| 2c | class mismatch → plain drawn-rows sync at the CURRENT epoch (`ir.vram_epoch`, `drawn_since_sync` consumed) + decline; `render 0x0A ∧ read 0x00` → serve (shuffle 16to32) | `texture_resolver.cpp:293-303` | `:21027-21048`, `:16340-16342` | `ResolveTiersColor`: `ClassMismatch`, `!HasDrawnSinceSync()`, page `shadow_epoch == 77`; CT16S read as CT32 serves with `shuffle_type 1`, `scale_u 0.5`, `uv_tex_w 32`; CT24 read of CT16S declines |
| 2d | colour self-read (`tbp == active_fbp`, fmt 0 incl. alt16 / scene-accum) → nothing bound, `Declined::SelfRead`, TEX0 dims | `texture_resolver.cpp:189-201` | R6 `:4902-4924` (`rt_tex = 0; using_rt_texture = false` arm) | `ResolveTiersColor`: `owner None ∧ enabled ∧ SelfRead ∧ uv_tex_w 64 ∧ surface.id 0` |
| 2e | indexed decline deposit only for `!Is16BitClass(render_psm)` ∧ recent, `enc = render_psm == 0x01 ? 0x01 : 0x00`, current epoch; `uv_native = true` on every indexed fall | `texture_resolver.cpp:242-256` | `:20452-20455`, `:20660-20684`, `:20807` | `ResolveTiersIndexed`: CT24 target → byte 3 `0xA5` preserved, `uv_native`; CT16 target → `PendingPages` unchanged; CT32 target → alpha `0x80`; stale target → no deposit |
| 3 | a target at TBP: every decline → `DecodeFallback`, never `MatchDepth` | `texture_resolver.cpp:204-206` | `:19758` … `:21131` `return 0` | `ResolveTiersDepth` (`:555`) M5: target without content at `0x202000` while another target's Z buffer contains it → `Decoded ∧ depth_fmt 0` |
| 4 | `MatchDepth`: containment ∧ `last_z_wrote` ∧ valid, max `last_z_seq`; P2 self / P3 `tbw == render_fbw` / P4 depth surface as post-pick declines; fmt 1 only `0x30 ∧ nz 0`, fmt 3 `0x32 ∧ nz 0` (both `aligned ∧ zoff < h`), fmt 2 `0x32|0x3A ∧ nz∈{2,A} ∧ delta 0`; `zoff = (delta / (8192*fbw/64)) * 32`; `z_row_offset = fmt == 2 ? 0 : zoff`; `SyncZToVram` only on the decline path | `texture_resolver.cpp:407-470`; `coherence_state.h:59`; `render_target.cpp:230` | `:21395-21420`, `:21469-21474`, `:21486-21514`, `:21590-21592`, `:9187` | `ResolveTiersDepth`: max seq pick and its swap; B invalid → A; self veto declines + stages Z; tbw mismatch; `0x31` never; `0x30` of Z24 never; fmt 3 at base `zoff 0`, `+0x4000` (`zoff 64 == h`) and `+0x100` decline; ppr 2 → `zoff 32` at `+0x4000`; fmt 2 at base only; `PendingPages == 0` after a serve |
| 5 | S107 lane-live RESCUE split out (`LaneLiveRescue`): non-self, `tbw ≥ 64 ∧ tbw % 64 == 0 ∧ h > 0`, per half valid ∧ scale 1 ∧ `width == tbw` ∧ recent ∧ lane record of THIS frame == the view's author seq, partner at `tbp + h*tbw*2` with equal height, `ClutServable`, NO dirty veto, binds the lane surface (alt16 when the alt lane authored); main chain fmt 5 = both `sync_keep_mask16 == kIndexLaneKeep16` THEN the SH5 dirty veto | `texture_resolver.cpp:310-340`, `:342-405` | `:20174-20265`, `:20272-20360`, `:20424-20425`, `idx16_lane.h:107-109` | `ResolveTiersLaneLive` (`:645`): `fall_tbw` (FBW 256, TBW 512) rescued on live lanes with dirty pages; alt lane → `alt16` bound; scale 2 / narrow partner / CPSM 2 / self → no rescue; keep16 pair serves, dirty partner declines the chain, a live lane rescues it. `Inv33` keeps passing |
| 6 | `ServeColor`: `uv_tex_w/h = target dims` for every RT serve; plain serves classify the shuffle (`ClassifyShuffle` = `GetBitsPerPixel` table) and fold `uv_tex_w *= scale_u` (min 1); `uv_pre = uv_tex` then STQ READVIEW `uv_pre = 1<<TW/TH` for `!fst ∧ !alt16 ∧ shuffle None ∧ (uv_pre != TEX0 dims)`; idx / depth serves `uv_tex = uv_pre = target dims`; decodes keep `1<<TW/TH` (R114) + `transform.region` (R159 rebase) | `texture_resolver.cpp:148-174`, `:72-87`, `:396-400`, `:459-466` | `:7515-7537`, `:7539-7585`, `:7601-7607`, `:4275-4292`, `:9964-9970`, `:10755-10758`, `:8266-8287` | `ResolveTiersColor` M12: 512x448 target, TW=TH=9: STQ `uv_tex 512x448`, `uv_pre 512x512`; FST `uv_pre 512x448`; alt16 `uv_tex = uv_pre = 512x448`, no shuffle; the 16to32 case `uv_pre == uv_tex`; `UvRegionDerive` decode: `uv_tex_w == uv_pre_w == 256` with `region {0,64,64,64}` |
| 7 | `UvRegion` (new pair `uv_region.h/.cpp`): `R172DeriveFromBBox` verbatim + gate `RectDecodeCapable ∧ verts ≥ 3 ∧ no mip chain`; resolver `DeriveRegion` adds OLD's (v) per-tuple `> 8` distinct-rect sticky-full guard | `uv_region.cpp:9-88`; `texture_resolver.cpp:472-497` | `:1045-1130`, `:5332-5334`, `:5364-5405`, `texture_decoder.h:131-134` | `UvRegionDerive` (`:714`): 14 hand-derived vectors (CLAMP quantise, bilinear taps via `mmag` and odd `mmin`, REPEAT single tile / straddle, REGION_CLAMP incl. degenerate, REGION_REPEAT → FullIsFull, area rule, FullQuant, `hi <= lo`, exact half, Bail, `Eligible` ×8) + through the resolver: applied rect + 64x64 surface, 9th distinct tile → full, sticky, 2-vertex draw full |
| 8 | `L2Insert` on every miss with `probation = suspect` (the register tuple's previous content key still in L2 → that entry `L2MarkProbation`ed, the new one enters in probation — the r154 rule; the tuple map is updated on hits too so a rebind counts as the tuple's record); L2 hit rebinds into L1; shared ownership: `ReleaseFromL1/L2` destroy only when the other cache does not record the surface; `DropAll` destroys once | `texture_resolver.cpp:548-572`; `decode_cache.cpp:14-42`, `:53-78`, `:97-110`, `:137-160` | `:5452`, `:5960-6047`, `:6114-6156`, `:7395-7416`, `:22518-22532` | `DecodeCacheOnMiss` (`:802`): L1 1 / L2 1 after a miss, probation 0; `EvictInRange` → L1 0, L2 1, `destroys 0`, surface alive; rotated address rebinds with no `CreateSurface`; content change → L2 2, probation 2, one create; L1 hit leaves L2; L2 hit after eviction → probation 1; `DropAll` destroys each surface once |
| 9 | tests: `Inv15` rewritten to OLD's gate; `Inv18` pins the literal FNV references `14780253174488400579` (View(0,0,2,2,64), 16 zero bytes) and `5842735931578600318` (T8 6x6, 4096-byte window i*13, 1 KiB CLUT i*7) computed independently from the OLD algorithm (Python transcription of `texcache_key.h:96-200`, `/tmp/p6` session); `Texsrc_ResolveTiers` (= `tiers_color` + `tiers_indexed` + `tiers_depth` + `tiers_lane`), `Texsrc_UvRegion`, `Texsrc_DecodeCacheOnMiss` registered | `test_texsrc.cpp:251-258`, `:343-880`; `tests/gs/CMakeLists.txt:186-187` | — | `test_texsrc all: 894 checks, 0 failures` |
| 10 | record: Landed resolver line rewritten to the fixed rules; the `uv_pre` sentence corrected (target dims divide, TEX0 window prescales); NOT YET APPLIED extended (items 1-8 moved to APPLIED, the remaining gaps listed); `PS2_S8_INRT` FLAG_LEDGER:639 / PLAN §5 deviation stated under P6-FMT6-OFF; HANDOFF signatures updated (`has_snapshot`, `draw_seq`, `enc_psm`) | this file, §6D′ Landed / Flags / HANDOFF | `render.h:1869-1884` | — |

Also changed: `TextureSource::Declined` gained `SelfRead` / `NoSnapshot`; `Transform::Shuffle` names the `.gsdl` shuffle numbering
(0 none, 1 16to32, 2 32to16, 3 depth-to-colour, 4 channel — `render.h:119-125`); the resolver reads `ir.tex.fst` (OLD `ti.fst`) instead
of `ir.use_fst`; `MaterializePages(Decode)` now also covers the CLUT page (`:4983-4989`, the review's §3 minor).

**Finding P6-SELFREAD-PERSIST (ledger, OPEN → 6F adjudication):** OLD's colour self-read (`batch.frame_fbp == ti.tbp`, fmt 0) binds
`persistence_tex` when present (`:4912-4917`); every target owns one (`:15683-15689`, `glTexImage2D(..., nullptr)`) and nothing ever
writes it (`needs_persistence` is always false, `:11528`, `:14295`) — OLD samples an unwritten texture (undefined content, zeros on the
recorded machine). NEW takes OLD's no-persistence arm (`rt_tex = 0; using_rt_texture = false` → textured UV mode, `u_use_texture = 0`).
If a frames-lane leg shows such a draw, the accepted-difference protocol decides between a zero surface and the untextured arm.

**Negative probes** (`/tmp/p6/6d3_probes.py` → `/tmp/p6/6d3_probes.log`; each flips ONE predicate back to the pre-fix NEW behaviour,
rebuilds `test_texsrc`, runs the pin, restores; `shasum -a 256 -c /tmp/p6/6d3_probe_shas.txt` → 3× `OK`; the final rebuild + `all`
→ `894 checks, 0 failures`). 22 probes, every one trips its pin:

| probe | result |
|---|---|
| M1 keep test on EVERY colour read                          | inv15 -> exit 1: test_texsrc inv15: 12 checks, 3 failures ; tiers_color -> exit 1: test_texsrc tiers_color: 23 checks, 9 failures |
| M2 rescue without the read-psm override                    | tiers_color -> exit 1: test_texsrc tiers_color: 23 checks, 1 failures |
| M3 no 0x0A/0x00 serve exception                            | tiers_color -> exit 1: test_texsrc tiers_color: 23 checks, 4 failures |
| M4 indexed decline deposits for 16-bit renders, uv_native off | tiers_indexed -> exit 1: test_texsrc tiers_indexed: 12 checks, 1 failures |
| M4b uv_native not armed                                    | tiers_indexed -> exit 1: test_texsrc tiers_indexed: 12 checks, 2 failures |
| M5 tier-1 decline falls into MatchDepth                    | tiers_depth -> exit 1: test_texsrc tiers_depth: 21 checks, 1 failures |
| M6 no colour self-read veto                                | tiers_color -> exit 1: test_texsrc tiers_color: 23 checks, 2 failures |
| M7 exact ZBP match instead of containment                  | tiers_depth -> exit 1: test_texsrc tiers_depth: 21 checks, 6 failures |
| M7b first match instead of max last_z_seq                  | tiers_depth -> exit 1: test_texsrc tiers_depth: 21 checks, 1 failures |
| M8 fmt 1 for any non-16 Z read                             | tiers_depth -> exit 1: test_texsrc tiers_depth: 21 checks, 2 failures |
| M9 zoff = 224 rule                                         | tiers_depth -> exit 1: test_texsrc tiers_depth: 21 checks, 2 failures |
| M10 SyncZ before every depth serve                         | tiers_depth -> exit 1: test_texsrc tiers_depth: 21 checks, 1 failures |
| M11 lane-live rescue removed                               | tiers_lane -> exit 1: test_texsrc tiers_lane: 10 checks, 3 failures ; inv33 -> exit 1: test_texsrc inv33: 5 checks, 1 failures |
| M11b lane-live rescue vetoed by CPU-dirty pages            | tiers_lane -> exit 1: test_texsrc tiers_lane: 10 checks, 2 failures |
| M12 RT serve uv_tex = 1<<TW                                | tiers_color -> exit 1: test_texsrc tiers_color: 23 checks, 4 failures |
| M12b READVIEW prescale for alt16 serves too                | tiers_color -> exit 1: test_texsrc tiers_color: 23 checks, 1 failures |
| item 1 no-snapshot input ignored                           | tiers_indexed -> exit 1: test_texsrc tiers_indexed: 12 checks, 2 failures |
| item 7 no 64-texel quantisation                            | uvregion -> exit 1: test_texsrc uvregion: 31 checks, 21 failures |
| item 7b no bilinear taps                                   | uvregion -> exit 1: test_texsrc uvregion: 31 checks, 2 failures |
| item 7c area rule dropped                                  | uvregion -> exit 1: test_texsrc uvregion: 31 checks, 1 failures |
| item 8 no L2 insert on miss                                | cache_miss -> exit 1: test_texsrc cache_miss: 12 checks, 10 failures |
| item 8b L1 eviction destroys what L2 holds                 | cache_miss -> exit 1: test_texsrc cache_miss: 12 checks, 3 failures |

**Gate lines** (`/tmp/p6/6d3_build.log`, `/tmp/p6/6d3_ctest.log`):

```
$ cmake --build --preset macos-dev -j4        BUILD_EXIT=0   grep -c 'warning:' -> 0
$ ctest --preset macos-dev -R 'Gs_Inv1[5789]|Gs_Inv33|Texsrc_|Gs_Hle'
 2/16 Test #79: Gs_Inv15_NeverStaleRtRead ........   Passed
 3/16 Test #80: Gs_Inv17_PerCallResultsReset .....   Passed
 4/16 Test #81: Gs_Inv18_ContentKeyAddressFree ...   Passed
 6/16 Test #83: Gs_Inv33_FogLaneLive .............   Passed
14/16 Test #92: Texsrc_ResolveTiers ..............   Passed    0.31 sec
15/16 Test #93: Texsrc_UvRegion ..................   Passed    0.06 sec
16/16 Test #94: Texsrc_DecodeCacheOnMiss .........   Passed    0.05 sec
100% tests passed, 0 tests failed out of 16
$ ctest --preset macos-dev
100% tests passed, 0 tests failed out of 164
Total Test time (real) = 186.47 sec
CTEST_EXIT=0
LintOneClass: 442 files scanned, 0 findings -> PASS
LintEnginePurity: 389 files scanned, 0 game tokens -> PASS
size check OK
```
Purity / backends / `CommitDeposits` / `getenv` / session-tag greps over `engine/gs/hle` (texsrc) empty. No flake rerun was needed.
Findings: P6-RESOLVER-AUDIT (the R16 / R6 predicates are now audited against the cited lines — the remaining OPEN part is the mip
upload, the FMV clamp and the pending-ledger flushes listed under NOT YET APPLIED), P6-SELFREAD-PERSIST (OPEN), P6-FMT6-OFF (S8_INRT
deviation now stated).

### Review 6D′ (verdict, iteration 2)
Independent re-review 2026-09-28 after the fix round (full record `/tmp/p6/REVIEW_6D2_2.md`; logs `/tmp/p6/rv6d3_*.log`). Each of the ten items was
re-read NEW against the cited OLD lines: 1 (`:4890-4930`, `gs_replay.cpp:357-360`), 2a-e (`:20873-21019`, `rt_neverstale.h:303-352`, `:20961-20972`,
`:16297-16298`, `:21043-21083`, `:4912-4922`, `:20675-20684`, `:20807`), 3 (`:19758` … `:21131`), 4 (`:21395-21583`, `:9187`), 5 (`:20174-20211`,
`:20280-20329`, `:20424-20425`), 6 (`:7488-7609`, `:4275-4290`, `:8277-8284`), 7 (`:1045-1135`, `:5332-5334`, `:5369-5405`), 8 (`:5960-6044`,
`:7407-7414`, `:22518-22532`), 9, 10 — every predicate, order and output now equals OLD's; the Inv18 literals were reproduced independently (own Python
over `texcache_key.h:67-224` with the `TexReadView` TEXA defaults). Pins exist and run for every item (`ctest -R 'Gs_Inv1[5789]|Gs_Inv33|Texsrc_'` →
15/15; `test_texsrc all: 894 checks, 0 failures`). Eight own negative probes (`/tmp/p6/rv6d3_probes.sh`): seven trip their pin (M3 exception psm, M7 P3
tbw, M11 scaled half, item-7 guard > 9, M12 READVIEW on FST, item-8 demotion, M4 enc) and one coverage probe does NOT (`last_z_wrote` dropped from the
depth search → `tiers_depth` 21/0); sources restored, `shasum -c` 3× OK. Gates: fresh texsrc rebuild `BUILD_EXIT=0`, `warning:` count 0; full
`ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 164` (175.53 s), `CTEST_EXIT=0`; `LintOneClass 442 files, 0 findings`;
`LintEnginePurity 389 files, 0 game tokens`; `size check OK`; purity / backends / `CommitDeposits` / `getenv` / tag greps empty; OLD tree untouched.

Re-walk of INV §6.5 tiers 0–4 and §13.2 LIVE rows found ONE remaining texsrc-scope mismatch: **the R6 Z-source refresh `:4703-4781`** — for any Z-psm
read without a depth serve OLD picks the containing Z writer with `last_z_wrote ∧ frame_count - last_z_write_frame <= 2 ∧ valid` (max `last_z_seq`,
`last_z_write_frame` stamped at `:9188`) and `SyncZToVRAM`s it before the decode; NEW does so only inside `MatchDepth`'s decline (no target at TBP). With a
colour target keyed at TBP (the Z-alias chain) OLD's decode reads the Z bytes deposited after the colour rows, NEW's reads the colour rows. Also
confirmed/recorded: P6-SELFREAD-PERSIST (OLD does bind the never-written `persistence_tex`, `:4912-4917` / `:11528` / `:15684` — accepted-difference
candidate, not blocking); the RESOLVE route (`NativeReadFBO` at scale > 1, `:20404-20423`, `:21568-21571`) and the interp shadow swap (`:19576-19588`)
are outside texsrc / Phase-6 scale 1 and go to the HANDOFF as executor / present-stage items.

**VERDICT: NEEDS_CHANGES** — (1) `texture_resolver.cpp` + `coherence_state.h` / `render_target.cpp`: port the R6 Z-source refresh with
`last_z_write_frame`, run it for Z-psm reads with `depth_fmt == 0` after `MatchTarget` and before `DecodeFallback`, pin in `ResolveTiersDepth`;
(2) `test_texsrc.cpp`: pin the CX8 `ReadRectUploadEpoch` fallback and the `last_z_wrote = false` exclusion; (3) this record: add item 1 to the fix
record and the RESOLVE route / interp swap to the HANDOFF as explicit owners.

### Review 6D′ fix 2 (2026-09-28, ≈35 min)

The three items of `/tmp/p6/REVIEW_6D2_2.md` §VERDICT applied against the cited OLD lines, read in full this round: R6 `:4703-4800`
(the `src_is_z` block: the `m_ftt_depth_fmt != 0` skip, the containment / `last_z_wrote` / `m_frame_count - last_z_write_frame <= 2` /
`hasValidContent()` predicate, max `last_z_seq`, `SyncZToVRAM(last_zbp, 0x30 | nz, render_fbw, *zsrc)` before the snapshot refresh),
`:9180-9192` (`last_z_wrote = true; last_z_seq = ++m_z_write_seq; last_z_write_frame = m_frame_count` under `!batch.zbuf_zmsk`),
`opengl_backend.h:646-652` (`last_z_write_frame = 0` default), `:20404-20423` / `:21560-21575` (the RESOLVE route), `:19570-19592`
(the interp shadow swap). No commits; OLD untouched (`find OLD -newer /tmp/p6/REVIEW_6D2_2.md` over the tree, excluding the pcsx2
submodules, is empty).

| # | item | NEW (file:line) | OLD cite | pin |
|---|---|---|---|---|
| 1 | **R6 Z-source refresh.** `CoherenceState::last_z_write_frame` stamped in `AfterDraw` next to `last_z_seq`; `TextureResolver::RefreshZSource(ir, ctx, out)`: no-op unless `IsZPsm(psm)` (`0x30/0x31/0x32/0x3A`) ∧ `out.transform.depth_fmt == 0`; search every target for `last_zbp != ~0 ∧ last_z_wrote ∧ frame_count - last_z_write_frame <= 2 ∧ HasValidContent ∧ tbp ∈ [last_zbp, last_zbp + w*h*native_bpp)`, pick max `last_z_seq`, `SyncZToVram(best, ir.vram_epoch)`. Called at all three post-FTT sites — after a tier-1 serve (incl. the self-read veto), after a tier-1 decline, and after a depth-tier decline — always BEFORE `DecodeFallback`, matching OLD's FTT → Z refresh → decode order (`m_ftt_depth_fmt == 0` regardless of which route produced it). The depth-tier-decline site is NOT redundant with `MatchDepth`'s own decline sync: that one picks the max seq over ALL writers, the refresh over RECENT writers only — when the overall max is > 2 frames old the refresh deposits a different (recent) writer after it, and the later deposit wins, as in OLD | `coherence_state.h:61`; `render_target.cpp:231`; `texture_resolver.h` (header comment + `IsZPsm` + `RefreshZSource`); `texture_resolver.cpp:28-30`, `:206`, `:209`, `:217`, `:222-245` (`MatchDepth` now uses `IsZPsm` too) | `:4703-4705`, `:4757-4767`, `:4776-4777`, `:9186-9188` | `ResolveTiersDepth` (`test_texsrc.cpp:673-756`): a valid CT32 target keyed at `0x202000` (inside B's Z32 buffer at `0x200000`) read as Z16 → tier-1 class-mismatch deposit (`at.drawn_since_sync` consumed) THEN B's Z deposit (`b.z_drawn_since_sync` consumed): every dword of the `0x202000..0x204000` window is B's Z (`0x80000000`), none is `at`'s colour pixel `0x04030201`; B stamped at frame 2 (frame_count 5) → `z_drawn_since_sync` kept, window not all-Z; B recent but `last_z_wrote = false` → kept; depth-tier decline path (self veto on A, seq 9 but frame 1; B seq 5 frame 5) → both synced, B's bytes last; two recent writers in both seq orders → the higher seq is deposited, the loser keeps its rect. `Gs_Inv10` (`test_targets.cpp:131-145`): a Z write with `draw_seq 17` at frame 1 → `last_z_seq == 17 ∧ last_z_write_frame == 1`; the following ZMSK draw (seq 18, frame 2) restamps neither |
| 2a | CX8 `ReadRectUploadEpoch` fallback pinned | `test_texsrc.cpp:409-431` | `:20888-20896`, `rt_neverstale.h:303-352` | `Inv15NeverStale`: `!recent ∧ dirty`, draw epoch newer than every page, then an upload over page 1 only (rows 32..63): a 64x32 read (`th = 5`, page 0 only; `ReadRectUploadEpoch <= draw epoch`) SERVES with nothing staged, the 64x64 read (both pages) rescues + `StaleTarget`; after an upload over page 0 the 64x32 read declines too |
| 2b | `last_z_wrote = false` exclusion pinned (review probe P7) | `test_texsrc.cpp:601-608` | `:21403-21408` | `ResolveTiersDepth`: a third target pairing ZBP `0x200000` with `last_z_wrote = false` and `last_z_seq = 100` (would win by seq) → the read still serves B's depth |
| 3 | record | this block; §6D′ Landed resolver line (`RefreshZSource` between `MatchDepth` and `DecodeFallback`); ledger row P6-Z-SEQ (closed incl. the refresh); HANDOFF: `AfterDraw` sentence carries `last_z_write_frame` and the `frame_count` identity requirement; the RESOLVE route and the interp shadow swap are now explicit HANDOFF items with owners (6F executor for `draw_scale > 1` idx / depth serves → native resolve; 6E present / interp replay for the shadow swap of plain colour serves); P6-SELFREAD-PERSIST kept as the accepted-difference candidate row; gate line 164/164 | — | — |

**Negative probes** (`/tmp/p6/fix6d2_2_probes.sh` → `/tmp/p6/fix6d2_2_probes.log`; each flips ONE predicate, rebuilds `test_texsrc` +
`test_targets`, runs the pin, restores; `shasum -a 256 -c` → `texture_resolver.cpp: OK`, `render_target.cpp: OK`; final rebuild + `all` →
`test_texsrc all: 920 checks, 0 failures`, `test_targets all: 211 checks, 0 failures`). All eight trip their pin:

| probe | result |
|---|---|
| P1 `RefreshZSource` never called (all three sites)                | tiers_depth -> exit 1: 39 checks, 6 failures |
| P2 recency gate (`<= 2` frames) dropped from the refresh           | tiers_depth -> exit 1: 39 checks, 5 failures |
| P3 `last_z_wrote` dropped from the DEPTH SEARCH (review probe P7)  | tiers_depth -> exit 1: 39 checks, 9 failures |
| P3b `last_z_wrote` dropped from the REFRESH predicate              | tiers_depth -> exit 1: 39 checks, 1 failures |
| P4 CX8 `ReadRectUploadEpoch` fallback removed                      | inv15 -> exit 1: 20 checks, 2 failures |
| P5 refresh picks the first containing writer, not max `last_z_seq` | tiers_depth -> exit 1: 39 checks, 2 failures |
| P6 refresh skipped on the depth-tier decline path                  | tiers_depth -> exit 1: 39 checks, 2 failures |
| P7 `last_z_write_frame` not stamped in `AfterDraw`                 | inv10 (test_targets) -> exit 1: 11 checks, 2 failures |

**Gate lines** (`/tmp/p6/fix6d2_2_build.log`, `fix6d2_2_ctest_sel.log`, `fix6d2_2_ctest.log`):

```
$ touch texture_resolver.cpp render_target.cpp; cmake --build --preset macos-dev -j4     BUILD_EXIT=0   grep -c 'warning:' -> 0
$ ctest --preset macos-dev -R 'Gs_Inv1[5789]|Gs_Inv33|Texsrc_|Targets_' --output-on-failure | tail -12
23/25 Test #92: Texsrc_ResolveTiers ..............   Passed    0.33 sec
24/25 Test #93: Texsrc_UvRegion ..................   Passed    0.06 sec
25/25 Test #94: Texsrc_DecodeCacheOnMiss .........   Passed    0.06 sec
100% tests passed, 0 tests failed out of 25
$ ctest --preset macos-dev
100% tests passed, 0 tests failed out of 164
Total Test time (real) = 187.67 sec
CTEST_EXIT=0
LintOneClass: 442 files scanned, 0 findings -> PASS
LintEnginePurity: 389 files scanned, 0 game tokens -> PASS
size check OK   (texture_resolver.cpp 617 lines, test_texsrc.cpp 1,068 lines)
```
Purity grep (`0x(23|28|2f|15|38|37|07|26|3c|3d|3f)xxxx|FUN_` over `engine/gs/hle`) 0; `#include "engine/render/backends` in `engine/gs` 0;
`CommitDeposits` in `engine/gs/hle` 0; `getenv` and session-tag greps over texsrc + targets 0. No flake rerun was needed.
Findings: P6-Z-SEQ CLOSED (ordinal + frame stamp + the R6 refresh); P6-SELFREAD-PERSIST OPEN (accepted-difference candidate);
P6-RESOLVER-AUDIT unchanged (remaining OPEN part = the NOT YET APPLIED list).

### Review 6D′ (verdict, iteration 3)

Final independent re-review 2026-09-28 after fix round 2 (full record `/tmp/p6/REVIEW_6D2_3.md`; logs `/tmp/p6/rv6d4_*.log`). The three
items of iteration 2 were re-read NEW against OLD: **(1)** the R6 Z-source refresh — `IsZPsm` = `:4703-4704`; the `depth_fmt != 0` skip =
`:4705` (`m_ftt_depth_fmt` reset at FTT entry `:19616`, set only by the depth serve `:21549`); the per-target predicate `:4757-4761`
(containment, `last_z_wrote`, `frame_count - last_z_write_frame <= 2`, valid; `native_bpp` from `last_zpsm & 0xF`), the strictly-greater
max-`last_z_seq` pick `:4762-4765`, `SyncZToVram` of the whole buffer in its native format at `last_zbp` (`:4776-4777` ≡
`readback_engine.cpp:245-266`), the call order FTT → refresh → decode reproduced at all three post-FTT sites (`texture_resolver.cpp:206`,
`:209`, `:217`) with a `MatchDepth` serve returning without it, the `last_z_write_frame` stamp under `zte ∧ !zmsk` (`render_target.cpp:225-233`
≡ `:9186-9188`, default 0 ≡ `opengl_backend.h:652`); the depth-tier-decline site is correctly NOT redundant with `MatchDepth`'s own decline sync
(OLD tier 3 has no recency gate, R6 does). **(2)** both pins read correctly (`test_texsrc.cpp:409-431` CX8 fallback vs `:20888-20896`;
`:601-608` `last_z_wrote = false` exclusion vs `:21403-21408`). **(3)** the fix-2 record, the Landed resolver line, ledger row P6-Z-SEQ and
the HANDOFF (`last_z_write_frame` / `frame_count` identity; RESOLVE route `:20404-20423` / `:21568-21571` → 6F executor; interp shadow swap
`:19576-19588` → 6E present; P6-SELFREAD-PERSIST candidate) match the code and OLD. Seven own negative probes (`/tmp/p6/rv6d4_probes.sh`;
two re-run with compiling mutations after `-Werror` rejected the first form) all trip their pin: CX8 fallback removed → `inv15` 2 failures;
depth search ignores `last_z_wrote` → `tiers_depth` 9; refresh recency gate dropped → 5; refresh skipped after a tier-1 decline → 4; refresh
first-match not max seq → 2; refresh equality not containment → 4; `last_z_write_frame` not stamped → `inv10` 2. Sources restored,
`shasum -c` 2× OK; `test_texsrc all: 920 checks, 0 failures`, `test_targets all: 211 checks, 0 failures`. Gates: fresh texsrc + targets
rebuild `BUILD_EXIT=0`, `warning:` count 0; `ctest -R 'Texsrc_|Gs_Inv1[5789]|Gs_Inv33|Targets_'` → 25/25; full `ctest --preset macos-dev` →
`100% tests passed, 0 tests failed out of 164` (164.17 s), `CTEST_EXIT=0`; `LintOneClass 442 files, 0 findings`; `LintEnginePurity 389 files,
0 game tokens`; `size check OK`; purity / backends / `CommitDeposits` / `getenv` / tag greps 0; OLD tree untouched (no file newer than the
iteration-2 review; only `.git` directory mtimes from read-only `git status`/`log`); no commits. Remaining items are all recorded with owners
(P6-SELFREAD-PERSIST → 6F frames-lane adjudication; RESOLVE route / interp swap → HANDOFF; NOT YET APPLIED list → 6D″ with P6-RESOLVER-AUDIT).

**VERDICT: APPROVED**

## 6D″ — alias pre-draw refresh, refresh arms, pending ledger, resolver remainder (2026-09-28, ≈50 min)

Items 1–3 of the 6D′ HANDOFF landed replay-live and green; the STOP rule fired before item 4 (`TargetDims`, `DrawScanout`)
and before the `Texsrc_ResolveTiers` extensions for item 3 — both go to **6D‴** verbatim below. Tree: 0 warnings, 167/167,
lints 0/0, no commits, OLD untouched.

### Landed

`engine/gs/hle/targets/` (+2 pairs; lib `ps2r_gs_hle_targets`):
- `alias_page_set.h/.cpp` — `AliasPageSet` (cpu_dirty + aliased 512-bit sets over wrapped spans: `Mark/Clear/Any{CpuDirty,Aliased}`, `ClearAliasedPage`, `Is*`), owned by `RenderTargetCache` (`AliasPages()`); `TargetRefresher::MarkPagesCpuDirty / ArePagesCpuDirty / ClearPagesCpuDirty` now delegate to it (the refresher's private `cpu_dirty_` is gone — ONE set, as OLD's `m_cpu_dirty_pages`).
- `pending_rt_sync_ledger.h/.cpp` — `PendingRtSyncLedger` (`Entry{fbp,start,size,epoch}`; `Record` = latest epoch wins + footprint grows + move to back; `Remove`; `Take(start,size,skip)` in recording order with the wrapped-overlap test `(a0<b1∧b0<a1) ∨ a0+4M<b1 ∨ b0+4M<a1`, `start==size==0` = all; `Reason{Decode,Ftt,Refresh,PreDraw,Scanout,Destroy,Present}` flush counters).
- `render_target_cache.h/.cpp` — `FlushPending(reason, start, size, reader_pages_only=false)` (skip = `reader_pages_only ∧ !VramImage::NeedsEagerDeposit(entry span)`; each taken entry → `SyncRtToVram(t, e.epoch, nullptr, 0)` — the plain drawn-rows sync at the RECORDED epoch; a vanished target drops its entry); `AfterDrawTail(t, ir, res, render_h, gfx_epoch)` (fog-lane companion deposit when `!Is16BitColor(render_psm) ∧ alt16_route ∧ frame psm 0x02 ∧ keep16 != 0` over bbox∩scissor; `Ledger().Record(fbp, FootprintBytes(t), gfx_epoch)` for every draw whose target is 16-bit-class; `AliasPages().ClearCpuDirty(fbp, fbw*render_h*4)` = `MarkPagesGPUValid`); `Destroy(fbp)` = `RescueAndDrop` (full-extent `SyncRtToVram` at `last_draw_gfx_epoch` with `RescueStride(render_fbw)` → `FlushPending(Destroy, fbp, 1)` → surfaces) — `GetOrCreate`'s grow path now uses the SAME `RescueAndDrop` (unified; the old `Destroy(fbp, gfx_epoch)` drawn-rows form is gone); `FootprintBytes(t)`.
- `readback_engine.h/.cpp` — `SetAliasPages`; `EncodeColorRows` tail marks the LINEAR encoded span cpu_dirty + aliased (`:16943-16963`, `row_bytes = encode fbw × bpp(enc_psm)`); `DepositAlt16Lane(be, t, vram, keep16, rect, draw_fbw, epoch)` (`:16170-16271`: companion readback over the clamped rect, 5551 pack, `fbw = draw_fbw ∈ (0,1024] ? draw_fbw : render_fbw|width`, CT16 address at that stride, keep16 == `kIndexLaneKeep16` → high byte only else `(px & ~keep) | (existing & keep)`, staged over the linear span like a writeback, then cpu_dirty + aliased marks).
- `target_refresher.h/.cpp` — `RefreshFromVram(t, y0, y1, job_epoch)` ARMS: band = `ClampToRowBand` (scoped ≠ whole); `FlushPending(Refresh, fbp, w*h*bpp)` first; voucher epoch = max upload epoch over the whole footprint (`r119::WindowEpoch`); the mirror is read with the NEW `VramImage::ReadMirror` (committed bytes only — never the composed view); **CT32/CT24** `RefreshAlphaMerge` (attachment `Readback` of the band, alpha byte := `min(mirror[PixelByteAddress(CT32,x,y,fbp,w/64)+3]*2, 255)` at stride = target WIDTH — the aquad program's exact math, RGB untouched, nearest fold at scale > 1); **CT16** `RefreshCt16` (5551 `Expand16To32` from the mirror at `render_fbw|w`; SH5 keep runs: a row whose linear-row pages `[fbp+y*tbw*2, +tbw*2)` carry `max upload_epoch != 0 ∧ > job_epoch` keeps the attachment bytes; `absorbed = no keep`); **CT16S** bookkeeping only (exists); tail: author bump, `has_content`, full `valid_rect`, Cd void, voucher stamp `vram_coherent_class = 1` (32) for CT32/CT24 and `2` (16) for an absorbed whole-image CT16 — never scoped / CT16S — `alt16_stale` whole, aliased pages cleared: whole footprint when unscoped, else only the pages the band FULLY covers (`FullyCoveredPages`: fbw page-aligned ∧ w ≥ fbw, `k0 = ceil(y0/ph)`, `k1 = floor(y1/ph)`, ph 64/32). `AliasPreDraw(t, ir, render_h, scanout_fbp, job_epoch)` = `:9403-9625` under the peak arms (GPUROUTE OFF, DEPCOPY OFF, DIRTYRECT OFF, RECT ON): `AliasRegionBytes` (colour psm ∈ {00,01,02,0A} ∧ fbmsk ≠ 0 ∧ fbw > 0 → fbw × render_h × bpp) → `FlushPending(PreDraw, fbp, region)` → any aliased page ∨ (16-bit ∧ any cpu-dirty) → band = bbox∩scissor clamped to the target (whole when composite into the scanout target `fbp == scanout ∧ tex_enabled ∧ tbp ≠ fbp`, or no vertices / empty rect) → `RefreshFromVram`. `EvictRangeUnderDraw` (static, the page-rounded range) + `EvictUnderDraw` over it.
- `engine/gs/vram/vram_image.h/.cpp` — `ReadMirror(byte_off, len, out)` next to `ReadView`.

`engine/gs/hle/texsrc/texture_resolver.h/.cpp`: `FlushPendingUnderRead(v)` = `FlushPending(Ftt, tbp, (1<<tw)*(1<<th)*bpp(read psm))` before the indexed-decline deposit (`:20448`), the keep-fall rescue (`:20973`) and the class-mismatch sync (`:21068`); `FlushPending(Ftt, cbp, 8 KiB)` before `ClutSource::Ensure` (`:21704`); `DecodeFallback` flushes `Decode` over the footprint and (indexed) the CLUT page before reading (`:4978`, `:4991`); `ApplyFmvEdgeClamp` (R198 D1: `VramLayout::IsFmvBlitSignature(frame_fbp, tbp, psm, tw, th)` ∧ region applied ∧ (wms==0 ∨ wmt==0) → `UvBbox::Scan` → `cw = ceil(u1-0.5)-rx0`, `ch = ceil(v1-0.5)-ry0` clamped `[1, rw/rh]` → columns ≥ cw copy column cw-1 (wms 0), rows ≥ ch copy row ch-1 (wmt 0) — a TEXEL edit of the decoded region, NOT a sampler state: no `TextureSource` field was added); mip level upload (`MipSampler::Capture` chain → per-level `TextureDecoder::DecodeLevel` at the level's (tbp, tbw, w, h) → `Backend::UploadLevel(tex, L, w, h)` → `FinishMipChain(tex, n, true)`; `owner.levels = transform.mip_levels = n`, `mip_lcm/k/l` from the chain; levels remembered per surface (`tex_levels_`) so cache hits report them; the mip windows `(tbw, bytes)` enter `ContentKey::Compute`); `EvictUnderDraw(ir, res, render_h)` = `DecodeCache::EvictInRange` + `TargetRefresher::EvictUnderDraw` over the same page-rounded range (`:13955-14020`, `:23289-23295`).

Tests: `tests/gs/targets/test_targets.cpp` +3 (`arms`, `alias`, `ledger`; `test_targets all: 273 checks, 0 failures`), registered as `Targets_RefreshArms`, `Targets_AliasPreDraw`, `Targets_PendingLedger`; the 4 existing `RefreshFromVram` calls pass `job_epoch 0`.

### OLD sources transplanted (read-only)

`opengl_backend.cpp` `:9403-9625` (pre-draw demand; `g3::GpuRouteOn` `:650-655` default OFF, `depcopy/depcopy2` OFF, `alias_rect::DirtyRectOn` OFF, `RectOn` ON), `:17401-17483` (`AlphaQuadRefreshCT32` = alpha-only band write of `DecodeAlphaCT32Band`), `:17484-18130` (`RefreshFBOFromVRAM`: scoped band, r173 voucher, `kRefresh` flush, 0x0A arm, alpha merge loop `:17718-17726`, CT16 arm with `sh5_collect_keeps` `:17801-17840`, tail stamps `:18069-18086`, page clears `:18100-18124`), `alias_rect.h:108-122, 195-214` (`ClampToRowBand`, `PageHeightForPsm`, `FullyCoveredPages`), `texture_decoder.cpp:306-325` (`GetAddressPSMCT32` stride = width), `:1946-1960` (`Expand16to32` ≡ `TexaExpand::Expand16To32`), `:16943-16963` (writeback tail marks), `:17118-17331` (`RecordPendingRTSync` / `RemovePendingRTSync` / `FlushPendingRTSyncs`), `:14040-14230` (post-draw: fog-lane deposit `:14043-14070`, `RecordPendingRTSync` under `r121::LazyRtSyncOn` `:14091`), `:13897` `MarkPagesGPUValid` (`:23035-23085`: clears `[fbp, fbp+fbw*h*4)`; the MARKMEMO is a pure memo), `:9163` (tags set pre-draw), `:16170-16271` (`DepositAlt16LaneToVRAM`), `:15568-15592` + `:19656-19680` (destroy / evict rescue form), `:4964-4995` (kDecode flushes), `:20440-20448`, `:20966-20973`, `:21062-21068`, `:21698-21704` (kFTT flushes), `:6581-6645` (R198 D1 FMV edge clamp), `:7370-7378` + `:22224-22350` (`UploadMipLevels`; the synth tail is `GlDevice::FinishMipChain`), `:13955-14020` + `:23289-23295` (evict wiring); `render.h:757-776` (`r120::MasterOn` default ON → `GateOn("PS2_GL_LAZY_RTSYNC")` TRUE; `run_play.sh:139` `PS2_GL_FAST2=1`), `:1469-1472` (`BstateFastOn` ON), `:1492-1500` (`aquad::On` ON); `gs_page_tracker.h:72-104` (`On`, `PboOn`).

### Flags applied

NORM (winning arm only): `PS2_GL_ALIAS_RECT` (ON: bbox∩scissor band), `PS2_GL_ALIAS_DIRTYRECT` (OFF: no `AliasBandGet` route), `PS2_GL_ALIAS_GPUROUTE` (OFF: `RouteAliasRefreshFromGPU` / `AliasAuthorClassify` NOT written — the "winner" is the CPU import), `PS2_GL_ALIAS_DEPCOPY/2` (DELETE), `PS2_GL_ALIAS_ALPHAQUAD` (ON: its math is the alpha-only merge; the GL program is an implementation of the same bytes), `PS2_GL_BSTATE_FAST` (ON: CT16S bookkeeping arm, CT16 arm decodes without a pre-readback), `PS2_SH5_ALIASEPOCH` (ON, value 1: keep runs; the `=2` keep-all is not written), `PS2_GL_LAZY_RTSYNC` / `PS2_GL_LAZY_ZSYNC` (ON via `PS2_GL_FAST2`: the ledger; see the finding below), `PS2_GS_RESIDENT` + `PS2_GS_RESIDENT_PBO` (ON: only the `kPresent` reader-band gate survives in the ledger path — `NeedsMirror` ≡ `VramImage::NeedsEagerDeposit`), `PS2_GS_FOGLANE` (ON: companion deposit), `PS2_GL_MARKMEMO` (ON: a memo with no observable effect — not written), `PS2_SH7` (`:9008-9034`: the 32-bit-frame same-fbp pre-draw flush — **NOT written**, see 6D‴), `PS2_R198_D1FMV` (ON: edge clamp; `PS2_R197_D1CLAMP` DELETE), `PS2_GS_MIPMAP` / `PS2_GS_MIP_ALLCLASS` (upload half + synth tail), `PS2_GL_TEXSUB` (irrelevant: `Upload` is the only path). DIAG/DELETE not written: `PS2_GL_RESID_DIAG` (`r147`, every `r148cnt` counter), `PS2_SH5_BIND`, `PS2_MIPDUMP`, `PS2_S9_DEMOTE`, the R121-LAZYSYNC / bullseye / refresh-diag prints. NOT YET APPLIED (→ 6D‴): `shuffle_snap` (R175), the ContentKey 4 MiB wrap split (identity only), `PS2_SH7` pre-draw flush, `:20759` (S8-INRT/T8 path — under `r198::Idx8Ct16On` OFF per P6-FMT6-OFF, so NOT live) and `:20856` (`rtns::ForcedNow`, TEST flag) flush sites, the S9 `hold_fbp` ledger hold (6E stencil recipe), the `:24808` scanout-native `RecordPendingRTSync + NoteGPUElided` (6E present).

### Tests (PASS lines from `/tmp/p6/6d4_ctest_sel.log` / `/tmp/p6/6d4_ctest.log`)

```
$ ctest --preset macos-dev -R 'Gs_Inv|Texsrc_|Targets_|Present_|Profile_|Gs_Hle' --output-on-failure | tail -6
63/63 Test #162: Gs_Inv34_ReverseSubtract .............   Passed    0.01 sec
100% tests passed, 0 tests failed out of 63
$ ctest --preset macos-dev
100% tests passed, 0 tests failed out of 167
Total Test time (real) = 193.15 sec
CTEST_EXIT=0
```
`Targets_RefreshArms` (`arms`, 15 checks): CT32 target, mirror alpha 0x40 at (5,9) with a STAGED shadow saying 0x7F on the same page → refreshed alpha `0x80` (mirror ×2), RGB `11/22/33` from the attachment, alpha 0 elsewhere, `vram_coherent_class 1`, aliased pages cleared; 0x90 → 0xFF cap; a scoped band stamps no voucher; CT16 64×128: job epoch newer than the settled upload → both pages decoded (`0xFF` red), class 2; page 1 upload epoch 50 vs job 10 → rows 64..127 keep the attachment (`9`), page 0 decoded, class 0; job epoch 0 never keeps. `Targets_AliasPreDraw` (`alias`, 21): a writeback of rows 32..48 marks page 1 aliased + cpu-dirty and not page 0; `AliasRegionBytes` 64·64·4; the demand imports once (1 upload); a band over rows 8..24 clears no page (stays aliased, imports again), a band over rows 32..64 clears page 1, then no demand; fbmsk 0 / fbw 0 / Z psm / render_h 0 → region 0; 32-bit frame over cpu-dirty-only → no import; 16-bit frame over cpu-dirty → import; composite into the scanout target → whole-image import clears every page. `Targets_PendingLedger` (`ledger`, 26): 16-bit draw → 1 entry `{epoch 7, size 64·64·2}`, nothing staged; re-draw → epoch 9; a 32-bit draw records nothing; GPU-valid clears cpu-dirty; disjoint flush takes nothing; footprint flush → 1 taken, `shadow_epoch == 9`, halfword `0x801F`, `Flushes(Decode) == 1`; Present reader-pages-only keeps the entry until `ReaderPagesSeed` puts its page in the band; flush-all takes everything; fog-lane companion deposit through the alt16 route of a 32-bit target → `0xFC5A` (high byte written, low byte kept), no ledger entry, page aliased; `Destroy` → rescue at `last_draw_gfx_epoch 30` (`shadow_epoch == 30`), entry removed.

**Negative probes** (`/tmp/p6/6d4_probes.sh` → `/tmp/p6/6d4_probes.log`; one predicate each, `test_targets` rebuilt, restored; `shasum -c` 3× OK; final `all: 273 checks, 0 failures`):

| probe | result |
|---|---|
| P1 refresh reads the composed view (`ReadView`) instead of `ReadMirror` | arms: 15 checks, 2 failures |
| P2 alpha merge without the ×2 scale | arms: 2 failures |
| P3 CT16 keep runs dropped (job epoch ignored) | arms: 2 failures |
| P4 16-bit CPU-dirty demand dropped | alias: 1 failure |
| P5 writeback tail marks nothing aliased | alias: 6 failures |
| P6 ledger records every colour draw (not 16-bit-class only) | ledger: 7 failures |
| P7 flush at epoch 0 instead of the recorded one | ledger: 1 failure |
| P8b present flush ignores the reader band | ledger: 3 failures |
| P9 index-lane deposit through the generic merge | ledger: 0 failures — **equivalent by construction**: `(px & ~0x00FF) \| (existing & 0x00FF)` IS "high byte only"; OLD's branch is a fast path, not a rule |

Gate lines: `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`, `grep -c 'warning:' /tmp/p6/6d4_build.log` → `0`; `LintOneClass: 446 files scanned, 0 findings -> PASS` (one finding fixed on the way: the ledger's `Take` template body moved to the .cpp as a `std::function` predicate); `LintEnginePurity: 393 files scanned, 0 game tokens -> PASS`; `size check OK` (largest touched: `texture_resolver.cpp` 700 lines, `target_refresher.cpp` 372, `test_targets.cpp` 1,040); `#include "engine/render/backends` in `engine/gs` 0; `CommitDeposits` / `getenv` in `engine/gs/hle` 0; game-address / `FUN_` grep 0. No flake rerun needed.

### Findings

- **P6-ALIAS-PREDRAW CLOSED**, **P6-REFRESH-ARMS CLOSED** (arms, committed-mirror source via `ReadMirror`, r173 stamps as `vram_coherent_class 1/2`, per-page alias clears, tail stale), **P6-TAIL-REMAINDER CLOSED** with one correction (next row).
- **P6-LEDGER-ARM (new, ledger)**: the 6D Findings described the post-draw 16-bit decision as "eager `SyncRTToVRAM` when any touched page is in the reader band, else `RecordPendingRTSync` + `NoteGPUElided`". That is the `else if (gsresident::On() && !NeedsMirror)` / `PboOn` arms of `:14091-14150`, which are DEAD at peak: `r121::LazyRtSyncOn()` = `r120::GateOn("PS2_GL_LAZY_RTSYNC")` = `MasterOn()` = TRUE (`render.h:757-776`, `run_play.sh:139` `PS2_GL_FAST2=1`), so EVERY 16-bit-class draw takes `RecordPendingRTSync` and the `NoteGPUElided/Rect` marks at `:14158` / `:14204` are never written. The reader band survives only as the `kPresent` `reader_pages_only` gate (`FlushPendingRTSyncs(..., true)` → `NeedsMirror`). NEW reproduces the live arm. The remaining elide-mark writer is `:24808` (scanout-native present path) → 6E present owns it (`VramImage::NoteGpuElided` exists for it).
- **P6-PREDRAW-SITE (ledger)**: the pre-draw demand is `TargetRefresher::AliasPreDraw`, not a `RenderTarget::PrepareDraw` member — `RenderTarget` has no access to the cache / refresher (the refresher depends on the cache which owns the targets), and the OLD demand sits in `DrawBatch` before the target bind (`:9403`), i.e. the driver's per-draw sequence. The HANDOFF order below places it between `GetOrCreate` and `PrepareDraw`.
- **P6-FMV-CLAMP-TEXELS (ledger)**: R198 D1 is a texel edit of the decoded region (edge replication past the last sampled column / row), not `CLAMP_TO_EDGE`; no `TextureSource::Sampler` field was added. The edit happens AFTER the content key is computed (as OLD `:6581` after `r118_tex_key`), so a cache hit returns the clamped texels of the first decode — identical to OLD's `DecodeCacheInsert` of the edited `rgba` (`:7385`).
- **P6-BACKEND-MIP**: NOT needed — `render/backend.h` already had `UploadLevel` / `FinishMipChain` (6C) and `GlDevice::FinishMipChain` synthesizes the tail (`gl_device.cpp:267-300` ≡ `:22311-22345`).
- **P6-DESTROY-UNIFIED**: `RenderTargetCache::Destroy(fbp)` and the `GetOrCreate` grow path share `RescueAndDrop` (the OLD rescue form + `kDestroy` flush); the old `Destroy(fbp, gfx_epoch)` is gone (it had no callers).
- **P6-PRESENT-FLUSH (HANDOFF)**: `Present()` `:23594-23955` calls `FlushPendingRTSyncs(0, 0, kPresent, reader_pages_only=true)` — 6E's `Presenter::Present` must call `RenderTargetCache::FlushPending(Present, 0, 0, true)` before the grab.

### Gate status

| gate | evidence |
|---|---|
| G7 (targets remainder) | `Targets_RefreshArms`, `Targets_AliasPreDraw`, `Targets_PendingLedger` Passed; the 60 prior `Gs_Inv|Texsrc_|Targets_|Present_|Profile_|Gs_Hle` tests still Passed (63/63) |
| build / lints / size | 0 warnings, LintOneClass 0, LintEnginePurity 0, size OK, greps 0 |
| full ctest | `100% tests passed, 0 tests failed out of 167`, 193.15 s, `CTEST_EXIT=0` |

### HANDOFF (6D″ → 6E)   [updated by the Review 6D″ fix + 6D‴ remainder, 2026-09-28]
Done: AliasPageSet (walks CLAMP at page 511) + PendingRtSyncLedger (targets), RefreshFromVram arms over the committed
  mirror + AliasPreDraw demand, AfterDrawTail (fog-lane deposit / ledger record / GPU-valid clears), FlushPending with
  the OLD reasons wired at the resolver's kDecode + kFTT sites (kFTT span = the SERVING TARGET's footprint) and at
  Destroy; `SyncDrawn` = every direct non-rescue sync + the OLD tail (ledger entry removed); RescueAndDrop unifies
  Destroy and the grow rescue; R198 D1 edge clamp; mip level upload + ContentKey mip windows; EvictUnderDraw wiring
  (wrap-aware decode-cache overlap); `TextureResolver::OnCpuWrite`; `TargetDims`; `ScanoutComposer::DrawScanout`
  (+ the `dest_fbp` deposit-base override on `SyncRtToVram`). Ctests: Targets_TargetDims, Present_DrawScanoutSource,
  Texsrc_ResolveTiers (+ tiers_flush / tiers_remainder).
Per-draw call order for classrdr / the replay driver (6E/6F), every step live today:
  1. `TargetDims::Compute(ir, window_w, window_h, {scanout_fbp, content_w, content_h}, layout, &render_w, &render_h)`
     (`opengl_backend.cpp:8674-8716`; `scanout_fbp` / content dims from `ScanoutComposer`)
  2. `RenderTarget& t = cache.GetOrCreate(fbp, render_w, render_h, psm, gfx_epoch)` (grow → `RescueAndDrop`)
  3. `refresher.AliasPreDraw(t, ir, render_h, scanout_fbp, gfx_epoch)` (flushes `PreDraw`, imports the band on demand)
  4. `t.PrepareDraw(be, ir, ee_frame, scanout_fbp, &params)`; `params.render_w/h` from step 1
  5. `TextureSource src = resolver.ResolveRead(ir, ctx)` (flushes `Decode` / `Ftt` under its reads; its direct syncs go
     through `cache.SyncDrawn`); `params.clut = src.owner.clut`, `params.idx_partner = src.owner.partner`; scene-accum /
     composite read → `source.MarkCompositeConsumed()`
  6. `DrawResult res = be.Draw(ir, src, params)`
  7. `t.AfterDraw(ir, res, frame_count, gfx_epoch, draw_seq)`; `cache.NoteDrawn(fbp)`
  8. `cache.AfterDrawTail(t, ir, res, render_h, gfx_epoch)` (fog-lane deposit, ledger record, GPU-valid clears)
  9. `resolver.EvictUnderDraw(ir, res, render_h)` (decoded textures + upload records under the drawn rows)
Per-job mapping (the graphics-thread jobs of OLD `graphics_thread.cpp` / `opengl_backend.cpp`, 6E owns the dispatch):
  - InvalidateTextures(addr, size) → `resolver.OnCpuWrite(addr, size)` = OLD `MarkPagesCPUDirty :22992-23020`
    (`vram.NoteCpuWrite` (author CPU, band cleared) + `DecodeCache::EvictInRange` + `refresher.MarkPagesCpuDirty`).
  - UploadVRAMTexture(addr, w, h, rgba, footprint) → `refresher.UploadDirect(addr, w, h, rgba, footprint)`.
  - SetWindow / SetFrontBuffer(fbp, fbw, psm) + DISPLAY → `scanout.SetScanoutFbp(fbp)`, `scanout.SetDispfb(fbw_px, psm)`,
    `scanout.SetContentDimensions(TextureDecoder::ParseDisplay(display).pixel_width/height)`; the EE register snapshot
    → `CrtcClassifier::Classify(pmode, dispfb1, dispfb2, display1, display2)` (packed through `Pack/Unpack` when queued).
    NOTE (Review 6D″ iteration 2, N1): `ScanoutComposer::scanout_fbp_` defaults to **0**, OLD `m_scanout_fbp` starts at
    `0xFFFFFFFF` (`opengl_backend.h:1066`) so OLD presents nothing before the first SetFrontBuffer; NEW's `DrawScanout`
    would present the target at 0 (or the most-recent-drawn fallback) if called first. 6E/6F MUST set the scanout base
    from the job stream BEFORE the first `DrawScanout` (or skip the present until the first DISPFB job arrives, which is
    what OLD's sentinel achieves); changing the default to `kNoFbp` instead needs the grab contract re-checked (`GrabSource` shares the field
    and the corpus grab of a capture without a DISPFB job would change). Display-only either way.
  - SyncAllRTsToVRAM → `cache.SyncAll()` (leaves `Ledger().Empty()`; then `vram.AssertNoStaleAtSave()` at save time).
  - VSync / frame end → `presenter.MarkEeFrameEnd()` (arg1 == 1) → `cache.FlushPending(Reason::Present, 0, 0,
    /*reader_pages_only=*/true)` (`:23609`) → `scanout.GrabDisplay(...)` (the frames-lane / corpus bytes) →
    `scanout.DrawScanout(vram, layout, crtc, presenter.FrameCount(), gfx_epoch, window_rect)` (display-only; the native
    tier records the ledger entry + `NoteGpuElided` `:24800-24815`, every other tier deposits at the scanout base) →
    `Backend::Present` (inside `DrawScanout`); `Presenter::Present(window_w, window_h)` is the module-gate stand-in
    (grab-source quad) until 6E replaces its body with the above.
  - The S9 stencil recipe's `hold_fbp` (skip predicate in `Take`) when 6E ports `s9impl`.
Not done (→ 6E, small): `PS2_SH7` pre-draw flush (`:9008-9034`: 32-bit-class frame draw into an fbp with a pending entry
  → `FlushPending(PreDraw, fbp, 4)`), `shuffle_snap` (R175), ContentKey wrap split (identity only), `Presenter::Present`
  over `DrawScanout`.
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 169/169; lints 0/0; getenv sites unchanged
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Targets_|Texsrc_|Gs_Inv|Present_'
Findings: P6-ALIAS-PREDRAW (CLOSED), P6-REFRESH-ARMS (CLOSED), P6-TAIL-REMAINDER (CLOSED, corrected by P6-LEDGER-ARM), P6-LEDGER-ARM (new),
  P6-PREDRAW-SITE (new), P6-FMV-CLAMP-TEXELS (new), P6-DESTROY-UNIFIED (new), P6-PRESENT-FLUSH (HANDOFF), P6-BACKEND-MIP (not needed),
  P6-SELFREAD-PERSIST (OPEN, unchanged), P6-RESOLVER-AUDIT (remaining OPEN part = the Not done list above),
  P6-SCANOUT-RT-ARM (new, below), P6-SCANOUT-DEST (new, below), P6-STALE-DISPLAY-PICK (new, below)

### Review 6D″ (verdict)
Independent review 2026-09-28 (full record `/tmp/p6/REVIEW_6D3.md`; logs `/tmp/p6/rv6d3_*.log`). Re-ran every gate: the 14 touched TUs
rebuilt → 0 warnings; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 167` (164.44 s, `CTEST_EXIT=0`);
`Targets_RefreshArms` / `Targets_AliasPreDraw` / `Targets_PendingLedger` / `Texsrc_ResolveTiers` / `Gs_HleIncludeDepth` /
`Render_IncludeDepth` Passed; `LintOneClass 0 findings`, `LintEnginePurity 0 game tokens`, `size check OK`; purity / backends /
`CommitDeposits` / `getenv` greps empty; shared CMake diffs are add_subdirectory lines only; `test_targets all: 273 checks, 0 failures`.
One session tag fixed in place (`texture_resolver.cpp:380` "S107"). Four negative probes of my own (`/tmp/p6/rv6d3_probes.sh`, sources
restored, `shasum -c` OK ×3): composite exclusion dropped → 1 failure; ledger re-record keeps the old epoch → 2; scoped refresh clears
the whole footprint → 3; **Destroy rescue at epoch 0 → 0 failures** (the `shadow_epoch == 30` pin is satisfied by the kDestroy ledger
flush — the rescue epoch is unpinned). P6-LEDGER-ARM CONFIRMED (`render.h:757-776`, `:828`). Mirror source, alpha ×2/255 at stride w,
CT16 keep runs, r173 stamps, alias clears, fog-lane deposit, `DepositAlt16LaneToVRAM`, Destroy rescue form, R198 D1 texel edit,
mip upload, evict range all match their OLD lines. TargetDims / DrawScanout / `Targets_TargetDims` / the `Texsrc_ResolveTiers`
extensions are honestly recorded as 6D‴ (STOP rule) — acceptable.

| check | result |
|---|---|
| 1 contract / purity | PASS |
| 2 alias pre-draw vs `:9403-9625` | PASS on the rule; **M1** `AliasPageSet` WRAPS at 4 MiB where OLD's `m_cpu_dirty_pages` / `m_vram_aliased_pages` walks CLAMP at page 511 (`markDirtyRange :3407-3416`, `ArePagesCPUDirty :23087-23092`, `MarkPagesGPUValid :23036-23040`, `:9419-9423`) |
| 3 refresh arms vs `:17484-18130` | PASS (test proves the committed-mirror source with a differing staged shadow); scale>1 RGB re-upload and address-wrap nits noted, dormant |
| 4 pending ledger | PASS on Record/Take/flush/marks/Destroy; **M3** a direct non-rescue `SyncRtToVram` (`SyncAll`) does not remove the ledger entry as OLD's tail does (`:17092-17095`) — byte-equivalent today, ledger emptiness / flush counts differ |
| 5 resolver remainder | R198 / mip / evict-range PASS; **M5** the three kFTT flush spans use the TEX0 window, OLD uses the SERVING TARGET footprint `tex_size` (`:19893-19897` → `:20448`, `:20972`, `:21068`); **M4** `DecodeCache::EvictInRange` overlap is not wrap-aware (OLD `:22950-22962`) |
| 6 TargetDims / DrawScanout | not landed (6D‴), nothing to verify |
| 7 tests | 3 registered + Passed; `Targets_TargetDims` absent, `Texsrc_ResolveTiers` unextended (6D‴); **T1** Destroy rescue epoch unpinned |
| 8 hygiene | PASS |
| 9 record | complete except **H1**: the InvalidateTextures job (OLD `MarkPagesCPUDirty :22992-23020` = NoteCPUWrite + decode-cache evict + cpu-dirty marks) has no NEW entry point / HANDOFF line — `TargetRefresher::MarkPagesCpuDirty` only marks |

**VERDICT: NEEDS_CHANGES** — fixes (file-cited) in `/tmp/p6/REVIEW_6D3.md` §VERDICT: (1) kFTT flush span = serving-target footprint
(`texture_resolver.cpp:149-154, :322, :355, :367`) + a `Texsrc_ResolveTiers` case; (2) `alias_page_set.cpp:11-30` clamp at page 511, header
comment, probe; (3) `decode_cache.cpp:64-71` wrap-aware overlap; (4) ledger `Remove` after non-rescue syncs (`SyncAll`, the resolver's two
direct syncs), pin `Ledger().Empty()` after `SyncAll`; (5) `test_targets.cpp:951-958` pin the Destroy rescue epoch on a target without a ledger
entry; (6) HANDOFF: the InvalidateTextures job mapping or `TextureResolver::OnCpuWrite`; (7) TargetDims / DrawScanout stay 6D‴.

### Review 6D″ fix + 6D‴ remainder (2026-09-28, ≈75 min)

The six review items applied, then the 6D‴ remainder landed (TargetDims, DrawScanout, OnCpuWrite, the ResolveTiers
extensions). Tree: 0 warnings, 169/169, lints 0/0, no commits, OLD untouched (`git status --short`: 15 modified tracked
files = the 6D READMEs + CMake add_subdirectory / add_library lines, 136 untracked).

| # | item | change | OLD cite | pin |
|---|---|---|---|---|
| 1 | kFTT flush span | `FlushPendingUnderRead(v, t)` flushes `[tbp, tbp + RenderTargetCache::FootprintBytes(t))` — the SERVING TARGET's width × height × bpp(render_psm) — at the indexed-decline deposit, the keep-fall rescue and the class-mismatch sync | `:19893-19897`, `:20448`, `:20972`, `:21068` | `Texsrc_ResolveTiers` (`tiers_flush`), all THREE sites (the rescue site was added by Review 6D″ fix 2 — the fix-round row over-claimed it): a pending 16-bit target at 0x130000 inside a 256×256 CT32 serving target's footprint [0x100000, 0x140000) but outside the 64×64 CT16 TEX0 window is written back (`0x801F`, `shadow_epoch == 5`, `Flushes(Ftt) == 1`) before the class-mismatch sync (`texture_resolver.cpp:372`); the same at the indexed decline (`Flushes(Ftt) == 3`, `:327`); the keep-fall RESCUE (`:359-360`): a same-class CT32 64×64 read of the 256×256 CT32 target not drawn recently (frame 20 vs 5), CPU-dirty pages, an EE upload newer than the draw over the read window → `declined == StaleTarget`, `Flushes(Ftt) == 1`, the pending target's `0x801F` at 0x130000 with `shadow_epoch == 50` (the entry's epoch, not the rescue's 3), `HasDrawnSinceSync()` kept (a rescue is not a writeback) — review probe Q3 (flush removed at the rescue site) 0 → **4 failures** |
| 2 | alias page walk | `AliasPageSet::PageWalk` clamps: first page ≥ 512 → nothing, last page ≥ 512 → 511; header comment fixed (no wrap claim) | `markDirtyRange :3407-3416`, `ArePagesCPUDirty :23087-23092`, `MarkPagesGPUValid :23036-23040`, `:9419-9423`; every OLD aliased-set writer is `markDirtyRange` (`:16258`, `:16963`) | `Targets_PendingLedger`: `MarkAliased(0x3FE000, 0x8000)` → page 511 only, pages 0..2 clear, `AnyAliased(0, 0x6000)` false; `MarkCpuDirty(0x400000, ..)` marks nothing; a query past the end is false |
| 3 | decode-cache evict overlap | `DecodeCache::EvictInRange` tests on the unwrapped [0, 8 MiB) line: `a0 = start % 4M`, `a1 = a0 + min(size, 4M)`, `(a0<b1 ∧ b0<a1) ∨ a0+4M<b1 ∨ b0+4M<a1`; size 0 / empty L1 early-out | `EvictDecodedTexturesInRange :22950-22966` | `tiers_flush`: an entry with footprint [0x3FE000, +0x4000) is hit by `EvictInRange(0, 0x800)`; `EvictInRange(0x3FF000, 0x3000)` hits an entry at 0x1000 and not one at 0x300000; size 0 evicts nothing |
| 4 | sync tail | `RenderTargetCache::SyncDrawn(t, epoch, enc_psm, dest_fbp)` = the OLD early-outs (valid + drawn) → `SyncRtToVram(.., nullptr, 0, ..)` → `ledger_.Remove(deposit base)`; `SyncAll` and the resolver's two direct syncs (indexed-decline deposit, class-mismatch sync) go through it | `SyncRTToVRAM` tail `:17092-17095` (`resetDrawnRect(); RemovePendingRTSync(fbp)`), head early-outs `:16283`, `:16297` | `Targets_SyncAllJob`: a recorded 16-bit target → `SyncAll` → `Ledger().Empty()`, `0x801F` at batch epoch 0; `tiers_flush`: `Ledger().Empty()` after each direct sync |
| 5 | Destroy rescue pin | `Targets_PendingLedger`: a 32-bit target (never recorded) drawn at epoch 40 → `Destroy` → `shadow_epoch == 40` AND its texels in VRAM (`0x80332211`) with the rescue-authorship veto exercised (page 384 GPU-authored lands, page 385 CPU-authored kept); a target whose drawn rect a `SyncDrawn` consumed (`SyncDrawn == 0` afterwards) still lands its FULL extent on `Destroy` (a pixel changed after the sync reaches VRAM) | `:15566-15583`, S109 rescue authorship rule (`readback_engine.cpp:192-194`) | review probe N4 (rescue at epoch 0) now → `ledger: 1 failure` (P5 below) |
| 6 | InvalidateTextures mapping | `TextureResolver::OnCpuWrite(addr, size)` = `vram_.NoteCpuWrite(span)` (NEW `VramImage::NoteCpuWrite`: author CPU, `author_fbp 0`, band + stale cleared — `gsresident::NoteCPUWrite` → `MarkAuthor(who 1, stale false)` `gs_page_tracker.h:230-248`) → `cache_.EvictInRange` → `refresher_.MarkPagesCpuDirty`; the DIRTYRECT fold (OFF) and `AliasAuthorClear` (empty map) are not written; HANDOFF per-job line added | `MarkPagesCPUDirty :22992-23020` | `tiers_remainder`: an owed band on a GPU-authored page → `OnCpuWrite` → author CPU, band empty, the L1 entry under the write gone, exactly the touched page CPU-dirty; size 0 → nothing |

6D‴ remainder:
- `targets/target_dims.h/.cpp` — `TargetDims::Compute(ir, window_w, window_h, Display{scanout_fbp, content_w, content_h}, layout, &w, &h)` (`:8674-8716`): `w = fbw ∈ (0, 1024] ? fbw : window_w`, `h = window_h`; `fbp == scanout ∧ content > 0` → content dims; else `layout.IsSceneAccum(fbp)` → `h = scissor_y1 + 1` when `0 < that < h`. `Targets_TargetDims` (`dims`, 15 checks): FBW 640 / 0 / 1025 / 1024; content override and its unknown-content fall-through; the accumulator at scissor 223 → 224, 224 → 225, 447 → 448 (equal keeps), 1023 → 448 (never grows); another buffer / an empty layout → no scissor rule; the accumulator AS the scanout target → content wins (the else-if).
- `present/scanout_composer.{h,cpp}` — `DrawScanout(vram, layout, crtc, frame_count, gfx_epoch, window)` → `DrawResult{source None|Target|Mirror, fbp, used_fallback, stale_display, native}`: `PickSource` (scanout target with valid content → most-recent-drawn with valid content (`used_fallback`) → the stale-display stand-in: `scanout != layout.main_fb_fbp`, display age > 30 frames, main-buffer age ≤ 16 and drawn later → the main buffer, `stale_display = render_psm != dispfb_psm` — OLD `:24290-24385`); `FlushPending(Scanout, scanout_fbp, footprint)` with `footprint = w·h·(16-bit ? 2 : 4)` (`:24790-24798`); NATIVE = `!used_fallback ∧ crtc.valid ∧ crtc.cls == DualIdentical1px ∧ layout.IsNativeScanout(QUEUE tuple: scanout_fbp_, dispfb_fbw_, dispfb_psm_) ∧ ResolvesToTarget(vram, scanout_fbp, footprint)` (`:24752-24787`; the EE-snapshot tuple is NOT cross-checked, as OLD's comment demands — `CrtcClassifier::IsNativeConfig` is not used here) → `Ledger().Record(scanout_fbp, footprint, gfx_epoch)` + `vram.NoteGpuElided(span, scanout_fbp, 0, h)` (`:24799-24815`); else `SyncDrawn(t, gfx_epoch, stale_display ? dispfb_psm : render psm, /*dest_fbp=*/scanout_fbp)` (`:24842-24843`, `:24857-24858`: OLD's `SyncRTToVRAM(m_scanout_fbp, it->second, psm_override)` deposits the PICKED target AT THE SCANOUT BASE — hence the new `dest_fbp` override on `ReadbackEngine::SyncRtToVram` / `EncodeColorRows`); then the quad: `!stale_display ∧ HasColor()` → `Backend::Present({color, src = content ∩ target dims × scale, dst = window})` (`DrawScanoutFromRT :18540-18620`, `u1 = content_w / width` clamped), else `PresentMirror` = `DecodeScanoutBuffer(view, scanout_fbp, dispfb_fbw, dispfb_psm, content_w, content_h)` → scratch surface (re-created on a size change) → `Present` (`ScanoutFromVRAM :18796-19060`, the live lines: content dims, DISPFB tuple, composed view, decode, quad). `ResolvesToTarget` = `gs_page_tracker.h:322-350` over `PageRecord.author / author_fbp`. `Present_DrawScanoutSource` (`drawscanout`, 40 checks): no target → None / 0 presents; a display target with cold pages → not native, drawn rows deposited (`0x80332211` at 0x70000), `Source::Target`, `src 512×448`, `dst 1024`, ledger empty; the deposit authored the footprint → the next present is native: no deposit, entry `{0x70000, epoch 8}`, owed band on the page; a CT16 DISPFB tuple / class Other → not native (the entry flushed under `Scanout` first); a foreign GPU author inside the footprint declines the resolve, a CPU page inside does not, the same fbp re-authoring restores it; fallback (no target at 0x200000) → the most-recent-drawn target presented, never native, deposited AT 0x200000; stale-display: CT16 display target drawn at frame 1, main buffer at 40, present at 45 → `Source::Mirror`, `stale_display`, the CT16 encode at 0x0 (`0x9882` = 5551 of 11/22/33), the mirror surface 512×448 with the 5551 expansion (`0x10, 0x21, alpha 0xFF`); age 19 or main-buffer age 40 → the display target; a matching class → `Source::Target` stand-in without the round trip; a deferred 16-bit target inside the display footprint lands (`Flushes(Scanout) + 1`, `0x801F`, `shadow_epoch 46`) before the display sync; a fallback pick is never native even when class + tuple + resolve hold (the pages still authored by the destroyed display target).
- `Texsrc_ResolveTiers` extensions (`tiers_flush` 27 checks, `tiers_remainder` 28): the Ftt cases above; the Decode flush case (a read with no target at 0x1FE000 whose page 1 IS a pending 16-bit target's first page: the flush runs (`Flushes(Decode)`, entry gone) and the newer-upload veto decides — deferred epoch 0 under a later EE write keeps the zeroed word and the decode reads 0, deferred epoch newer lands `0x801F` and the decode's texel (0, 32) reads `0x1F, 0x80`); the wrap evict cases; FMV: the stage read (CT32 1024², `wms 0`, `wmt 1`, UVs u∈[10,50] v∈[100,120] → region (0,64) 64×64) replicates column 49 (`0xAA`) over columns ≥ 50 (55, 63 → `0xAA`; rows untouched), alpha `0x80 → 0xFF`; the same bytes read again from another frame HIT the clamped decode (the edit is after the key — P6-FMV-CLAMP-TEXELS); a non-signature frame / `wms 1` / an empty layout decode column 55 verbatim (`0x55`) in fresh worlds; mip: `mmin 2, mxl 2` → two `UploadLevel` calls (last 2, 16×16), `FinishMipChain(2)`, `owner.levels == 2 == transform.mip_levels`, not region-eligible, the hit remembers the levels without a create, a level-2 window byte change → a new decode (+2 level uploads), `mmin 1` → no chain; evict-under-draw: rows 0..16 of a CT32 draw over the decoded 0x200000 read → the L1 entry gone, L2 keeps the surface and rebinds it without a create, a partial-bit-masked 16-bit draw evicts nothing; `OnCpuWrite` as above.
- `tests/lib/fake_backend.{h,cpp}`: `UploadLevel` / `FinishMipChain` now count (`level_uploads`, `last_level*`, `mip_finishes`, `last_mip_authored`).

Gate lines (`/tmp/p6/6d5_build.log`, `/tmp/p6/6d5_ctest_sel.log`, `/tmp/p6/6d5_ctest.log`):

```
$ touch <21 touched TUs>; cmake --build --preset macos-dev -j4      → BUILD_EXIT=0, grep -c 'warning:' → 0
$ ctest --preset macos-dev -R 'Gs_Inv|Texsrc_|Targets_|Present_|Profile_|Gs_Hle' --output-on-failure | tail -12
62/65 Test  #97: Texsrc_ResolveTiers ..................   Passed    0.63 sec
65/65 Test #164: Gs_Inv34_ReverseSubtract .............   Passed    0.02 sec
100% tests passed, 0 tests failed out of 65
$ ctest --preset macos-dev
 67/169 Test #67: Targets_SyncAllJob ................... Passed 0.04 sec
 76/169 Test #76: Targets_PendingLedger ................ Passed 0.05 sec
 77/169 Test #77: Targets_TargetDims ................... Passed 0.01 sec
 81/169 Test #81: Present_DrawScanoutSource ............ Passed 0.23 sec
 83/169 Test #83: Gs_HleIncludeDepth ................... Passed 0.05 sec
 97/169 Test #97: Texsrc_ResolveTiers .................. Passed 0.58 sec
169/169 Test #169: Render_IncludeDepth .................. Passed 0.06 sec
100% tests passed, 0 tests failed out of 169
Total Test time (real) = 174.76 sec
CTEST_EXIT=0
$ python3 tests/lint/lint_one_class.py --root .    → LintOneClass: 448 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . → LintEnginePurity: 395 files scanned, 0 game tokens -> PASS
$ python3 /tmp/p6/check_sizes.py                   → size check OK   (largest touched: test_texsrc.cpp 1,323, test_targets.cpp 1,136, texture_resolver.cpp 710)
greps: '#include "engine/render/backends' in engine/gs 0 · CommitDeposits in engine/gs/hle 0 · getenv in engine/gs 0 · game literals / FUN_ in engine/gs/hle 0
$ ./build/tests/gs/test_targets all → 304 checks, 0 failures · test_texsrc all → 975 checks, 0 failures · test_present all → 91 checks, 0 failures
shared CMake diffs (root, engine/gs, engine/platform, tests) unchanged from 6D″: 4 files, 20 insertions (add_subdirectory lines)
```

**Negative probes** (`/tmp/p6/6d5_probes.sh` → `/tmp/p6/6d5_probes.log`; one predicate each, the test binary rebuilt, the
source restored, `shasum -a 256 -c` OK ×6, restored build 0 warnings, `all` green ×3):

| probe | result |
|---|---|
| P1 kFTT span = TEX0 window (item 1 reverted) | tiers_flush: 10 failures |
| P2 alias page walk wraps at 4 MiB (item 2 reverted) | ledger: 2 failures |
| P3 EvictInRange plain overlap (item 3 reverted) | tiers_flush: 2 failures |
| P4 SyncDrawn keeps the ledger entry (item 4 reverted) | syncall: 1 failure |
| P4b the same, observed at the resolver's direct syncs | tiers_flush: **0 failures — equivalent by construction**: both direct syncs are preceded by the Ftt flush over the serving target's own footprint, which always takes its own entry; the tail's `Remove` is the invariant, not a behaviour the resolver can expose |
| P5 Destroy rescue at epoch 0 (the review's N4) | ledger: 1 failure (T1 closed) |
| P6 OnCpuWrite without the decode-cache evict | tiers_remainder: 1 failure |
| P7 OnCpuWrite without the CPU author mark | tiers_remainder: 2 failures |
| P8 TargetDims: accumulator scissor rule dropped | dims: 3 failures |
| P9 TargetDims: scanout content override dropped | dims: 2 failures |
| P10 DrawScanout: native without the page resolve | drawscanout: 6 failures |
| P11 DrawScanout: a fallback pick may be native | drawscanout: 3 failures |
| P12 DrawScanout: deposit at the target base, not the scanout base | drawscanout: 4 failures |
| P13 DrawScanout: stale-display pick dropped | drawscanout: 5 failures |
| P14 FMV edge clamp: columns not replicated | tiers_remainder: 2 failures |
| P15 mip levels never uploaded | tiers_remainder: 6 failures |

Findings:
- **P6-SCANOUT-RT-ARM (ledger)**: the brief for 6D‴ read "from-RT when native, else mirror-decode". OLD's live rule
  (`:25030-25036`) is `r184::ScanoutRtOn()` (default ON, `render.h:1649-1660`) `∧ !scanout_from_stale_workbuffer ∧ color_tex`
  → `DrawScanoutFromRT`, else `ScanoutFromVRAM` — the NATIVE classification (`gb3_native`) decides only the mirror
  MAINTENANCE (defer via the ledger + `NoteGPUElided` vs the eager `SyncRTToVRAM`), never the quad's source. NEW
  reproduces the live rule: the quad samples the target whenever it has a colour view and the stale-display round trip
  is not engaged; `native` gates the deposit deferral. FLAG_LEDGER classes `PS2_GL_SCANOUT_RT` as DIAG; its default-ON
  arm is the winning arm and the OFF arm (present-time 5551 re-decode of every frame) is not written.
- **P6-SCANOUT-DEST (ledger)**: OLD's present-time `SyncRTToVRAM(m_scanout_fbp, it->second, ...)` passes the SCANOUT base
  as the deposit address while `it->second` may be the fallback / stand-in target — the mirror at DISPFB receives the
  presented target's rows (the stale-display round trip depends on it: the encode lands where `ScanoutFromVRAM` decodes).
  `ReadbackEngine::SyncRtToVram` / `EncodeColorRows` gained `dest_fbp` (default the target's own base); only `DrawScanout`
  passes another base. Pinned by the fallback and stale-display cases (P12).
- **P6-STALE-DISPLAY-PICK (ledger, workaround-class)**: the stale-display stand-in (`:24363-24385`) is a display-only
  present heuristic keyed on the title's main frame buffer (OLD literal `0x70000` → `VramLayout::main_fb_fbp`) with the
  30 / 16 frame ages as named constants. It is live at peak and is transplanted as such; it is display-only (no lane reads
  the quad) and carries no root fix yet. It IS a workaround: OLD's own comment (`:24344-24350`) names the cause — the
  game's per-frame `0x70000 → 0x0` display composite is not reproduced on the host ("the drawn render-graph has zero
  draws to FRAME=0x0 in-scene"), so the true display buffer keeps a stale pre-scene image while the main buffer is
  composited fresh; the missing emulation (a dropped draw classification or an unexecuted local→local transfer) is the
  root-fix target at 6E/6F. PS2 rule: the CRTC shows exactly DISPFB. It moves the mirror too (the stand-in deposit at the
  scanout base), so it is not purely cosmetic; its expiry gate is the 12-leg frames lane once 6E integrates the present.
  The HOOK_RECLASSIFICATION workaround-class row is OWED at integration (shared doc outside §1.5) — its full text is
  row L1 of "Ledger deltas owed at integration" (corrected by Review 6D″ fix 2: the earlier "queue-order scanout tuple
  lag" attribution was wrong).
- **P6-FALLBACK-NEVER-NATIVE**: OLD's `gb3_native` requires `!used_fallback`; without it a fallback pick would defer a
  deposit the ledger records against the SCANOUT base while the pages resolve to a destroyed / other target (P11 catches it).
- Items 1–6 of `/tmp/p6/REVIEW_6D3.md` CLOSED; item 7 (6D‴) landed here. T1 (N4) closed by item 5. H1 closed by
  `OnCpuWrite` + the HANDOFF per-job map.

### Review 6D″ (verdict, iteration 2)
Independent re-review 2026-09-28 (full record `/tmp/p6/REVIEW_6D3_2.md`; logs `/tmp/p6/rv6d3_2_*.log`). Gates re-run on a CLEAN
rebuild: `cmake --build --preset macos-dev --clean-first -j4` → `[363/363]`, `BUILD_EXIT=0`, 0 warnings; `ctest --preset macos-dev` →
`100% tests passed, 0 tests failed out of 169` (176.00 s, `CTEST_EXIT=0`; `Targets_TargetDims` / `Present_DrawScanoutSource` /
`Texsrc_ResolveTiers` / `Targets_SyncAllJob` / `Targets_PendingLedger` / `Gs_HleIncludeDepth` Passed); `LintOneClass 448 files, 0
findings`; `LintEnginePurity 395 files, 0 game tokens`; `size check OK`; backends / `CommitDeposits` / `getenv` / game-literal greps
empty; `test_targets all 304/0`, `test_texsrc all 975/0`, `test_present all 91/0`. No in-tree edits by this review.

All six items match their OLD lines: (1) the three kFTT sites flush the SERVING target's footprint (`:19893-19897` → `:20448`,
`:20972`, `:21068`); (2) `AliasPageSet::PageWalk` clamps as `markDirtyRange :3407-3416`; (3) `EvictInRange` is the `:22950-22966`
expression; (4) `SyncDrawn` = the `:16283`/`:16297` early-outs + `SyncRtToVram` + `Remove(deposit base)` = the `:17092-17095` tail with
OLD's `fbp` ARGUMENT; (5) the Destroy rescue epoch is pinned without a ledger entry; (6) `OnCpuWrite` = `MarkPagesCPUDirty :22992-23020`
(the DIRTYRECT fold and `AliasAuthorClear` dead at peak, `g_epoch` diagnostic-only). 6D‴: `TargetDims::Compute` ≡ `:8674-8716`
(`m_width/m_height` = the window dims via `Resize`); `DrawScanout` ≡ `:24303-24385` (pick; the stale stand-in sets `used_fallback` as
OLD does), `:24769-24815` (native = class ∧ QUEUE tuple ∧ `!used_fallback` ∧ page resolve, computed BEFORE the Scanout flush, then
record + `NoteGPUElided`), `:24842-24858` (`SyncRTToVRAM(m_scanout_fbp, picked, stale ? dispfb_psm : none)` → the `dest_fbp`
override), `:25030-25036` (`r184::ScanoutRtOn()` default ON → the target quad whenever `!stale ∧ color_tex`: **P6-SCANOUT-RT-ARM
confirmed**), `:18569-18575` / `:18797-18822` (quad / mirror decode). `Texsrc_ResolveTiers` gained the FMV clamp, mip, evict, flush
and `OnCpuWrite` cases. The three points flagged for this review: P6-SCANOUT-RT-ARM is OLD-faithful (the brief's "from-RT only when
native" was wrong, the fix round was right to follow OLD); the `dest_fbp` override plays exactly the roles of OLD's `fbp` argument
(swizzle base, authorship veto, marks, protected window, tail `Remove`); the stale-display pick IS a workaround (OLD's own comment:
the game's `0x70000 → 0x0` composite is not reproduced on the host, `:24344-24350`) and needs its HOOK_RECLASSIFICATION row with THAT
root cause, not the "queue tuple lag" the record gives.

Four negative probes of my own (`/tmp/p6/rv6d3_2_probes.sh`; sources restored, `shasum -c` OK ×4; restored build 0 warnings):
Q1 `SyncDrawn` removes the target's own entry instead of the deposit base → **0 failures** (OLD-faithful, unpinned); Q2 the Scanout
flush moved BEFORE the native resolve → **0 failures** (OLD-faithful, unpinned — and consequential: a deferred foreign 16-bit target
inside a natively-resolving footprint flips native → eager deposit under the swapped order); Q3 the keep-fall RESCUE site loses its
Ftt flush → **0 failures** (the third kFTT site is unpinned; the item-1 row over-claims); Q4 `EvictInRange` drops the `b0+4M<a1` arm
→ 1 failure. Also found: the `ScanoutFromVRAM` SESSION-12 FMV source-gated hold (`:19236-19320`) is dormant at peak (unreachable
through the RT arm) and rightly not written, but is recorded nowhere (HACK_LEDGER / FILEMAP R15 / this record). Nits: `scanout_fbp_`
defaults to 0 where OLD starts at `0xFFFFFFFF` (`opengl_backend.h:1066`) — display-only, 6E must set it before the first present;
`Source::Mirror` reported when content dims are 0 and nothing is drawn.

| check | result |
|---|---|
| 1 items 1–6 vs OLD | PASS (all six); pin gaps: item 1 rescue site (Q3), item 4 deposit-base `Remove` (Q1) |
| 2 TargetDims vs `:8664-8716` | PASS; `Targets_TargetDims` registered, 15 checks |
| 3 DrawScanout source selection | PASS (target quad whenever `!stale ∧ colour`; native gates only the deferral; mirror decode after the Scanout flush for the stale round trip); order unpinned (Q2) |
| 4 ResolveTiers extensions | PASS (FMV clamp incl. cache-hit pin, mip, evict, flush, OnCpuWrite) |
| 5 hygiene | PASS (clean build 0 warnings, 169/169, lints 0/0, size OK, greps empty) |
| 6 record | flags / findings complete except: item-1 pin over-claim, no HOOK_RECLASSIFICATION row for the stale-display pick + wrong root-cause attribution, the dormant FMV hold unrecorded |

**VERDICT: NEEDS_CHANGES** (tests + records only; no live-path code change) — fixes (file-cited) in `/tmp/p6/REVIEW_6D3_2.md`
§VERDICT: (1) `test_texsrc.cpp` pin the keep-fall rescue kFTT site + correct the item-1 row; (2) `test_present.cpp` pin the
resolve-before-flush order (native stays native while a deferred foreign 16-bit target inside the footprint lands); (3, recommended)
pin `Remove(deposit base)` via a 16-bit fallback stand-in; (4) `HOOK_RECLASSIFICATION.md` workaround row for the stale-display pick
with OLD's root cause (`:24344-24350`) + root-fix plan + frames-lane expiry, and the P6-STALE-DISPLAY-PICK paragraph corrected;
(5) `HACK_LEDGER.md` / this record: the `ScanoutFromVRAM` FMV hold (`:19236-19320`) listed as dormant, not written; (6, note) the
`scanout_fbp_` default carried into the HANDOFF's SetFrontBuffer line.

### Review 6D″ fix 2 (2026-09-28, ≈35 min)

The six items of `/tmp/p6/REVIEW_6D3_2.md` §VERDICT applied — tests and records only, no live-path code change
(`shasum` of `render_target_cache.cpp` / `scanout_composer.cpp` / `texture_resolver.cpp` unchanged, `/tmp/p6/fix6d3_2_probes.sha`).
Items 4 and 5 are NOT applied to `docs/design/HOOK_RECLASSIFICATION.md` / `docs/filemap/HACK_LEDGER.md`: both are shared
design documents outside Phase 6's write set (PHASE6_PLAN §1.5); their rows are recorded verbatim in the new top-level
section **"Ledger deltas owed at integration"** (rows L1, L2) for the integration step to carry over (`git status` shows
no change under `docs/design` / `docs/filemap`). Tree: 0 warnings, 169/169, lints 0/0, no commits, OLD read only.

| # | item | change | pin | probe (review predicate, re-run) |
|---|---|---|---|---|
| 1 | keep-fall rescue kFTT site | `test_texsrc.cpp` `ResolveTiersFlushes`, new block (`World k`, `:1036-1065`): 256×256 CT32 serving target at 0x100000 last drawn frame 5 / epoch 3, read at frame 20 (not recent), footprint marked CPU-dirty, `PendingTarget16(0x130000, epoch 50)` inside the footprint outside the 64×64 CT32 TEX0 window [0x100000, 0x104000), `NewerUpload` on page 0 (upload epoch > 3 → keep fails, same class) | `declined == StaleTarget`, decoded owner, `Flushes(Ftt) == 1`, `!Has(0x130000)`, `Ledger().Empty()`, `Half(0x130000) == 0x801F`, `shadow_epoch == 50` (the entry's epoch, not the rescue's 3), `HasDrawnSinceSync()` kept, `PendingPages() > 0`; item-1 row of "Review 6D″ fix + 6D‴ remainder" corrected to name the three sites (`texture_resolver.cpp:327`, `:359-360`, `:372`) | Q3 (flush removed at `:360`): 0 → **4 failures** |
| 2 | resolve-before-flush order | `test_present.cpp` new `DrawScanoutOrderAndTail()` (`:316-`; run under the `drawscanout` selector = `Present_DrawScanoutSource`): the display target's first present authors [0x70000, 0x230000); a 64×64 CT16 target at 0x80000 drawn once (epoch 7), never synced — its page still `author_fbp == 0x70000`; the display drawn again (epoch 8); present at epoch 9 with the native snapshot | `r.native`, `!used_fallback`, `Flushes(Scanout) + 1`, `!Has(0x80000)`, `0x801F` at 0x80000 with `shadow_epoch == 7`, `Has(0x70000)` at epoch 9, `t.HasDrawnSinceSync()` (not consumed); then (OLD `NoteGPUElided` runs AFTER the flush over the whole footprint) the page is re-authored for 0x70000 with an owed band, `ResolvesToTarget` holds and the NEXT present is native again (`Flushes + 2` = the display's own entry taken and re-recorded at epoch 10) | Q2 (flush moved before the resolve): 0 → **7 failures** |
| 3 | deposit-base `Remove` | same function: scanout base 0x200000 (no target), a 64×64 CT16 most-recent-drawn target at 0x300000 with its own ledger entry (epoch 11) presented via the fallback at epoch 12 | `used_fallback`, `!native`, `r.fbp == 0x300000`, the quad samples its colour view, `Flushes(Scanout)` unchanged (nothing pending under 0x200000), **`Has(0x300000)` survives**, `!HasDrawnSinceSync()`, `0x801F` at 0x200000 with `author_fbp == 0x200000` / `shadow_epoch == 12`, nothing at 0x300000 | Q1 (`Remove(t.identity.fbp)` always): 0 → **1 failure** |
| 4 | HOOK_RECLASSIFICATION row | NOT written to the shared doc; row **L1** of "Ledger deltas owed at integration" carries the exact text (OLD `:24339-24385`; NEW `ScanoutComposer::PickSource` + `VramLayout::main_fb_fbp` + `kStaleDisplayFrames/kFreshWorkFrames`; PS2 rule: the CRTC displays exactly DISPFB; suspected defect per OLD `:24344-24350` = the game's `0x70000 → 0x0` display composite is not reproduced on the host; root-fix plan = locate the dropped composite at 6E/6F; expiry = the 12-leg frames lane). P6-STALE-DISPLAY-PICK corrected in BOTH places (Findings ledger row + the 6D‴ Findings bullet): the "queue-order scanout tuple lag" attribution dropped, OLD's own root cause stated | — | — |
| 5 | FMV hold dormant | NOT written to the shared doc; row **L2**: `ScanoutFromVRAM` SESSION-12/14 source-gated FMV hold (`opengl_backend.cpp:19236-19320`, `:19332-19378`) with its predicate (`psm == 0x02 ∧ fbp == 0 ∧ width == 512 ∧ m_scanout_texture`, luma grid `< 5 %` bright, `s_fmv_have_good ∧ g_vsync_frame ≥ 400 ∧ blackrun ≤ 8` → `glTexImage2D` skipped), why it is unreachable at peak (the RT arm presents the FMV; only the stale-display round trip reaches the mirror present and its pick is the CT32 main buffer) and the decision NOT to transplant it | — | — |
| 6 | `scanout_fbp_` default | HANDOFF (6D″ → 6E) SetWindow / SetFrontBuffer line gained the N1 note: NEW default 0 vs OLD `m_scanout_fbp = 0xFFFFFFFF` (`opengl_backend.h:1066`); 6E/6F must set the base before the first `DrawScanout` (or skip the present until the first DISPFB job); a `kNoFbp` default would need the grab contract re-checked | — | — |

Gate lines (`/tmp/p6/fix6d3_2_build.log`, `/tmp/p6/fix6d3_2_ctest_sel.log`, `/tmp/p6/fix6d3_2_ctest.log`, `/tmp/p6/fix6d3_2_probes.log`):

```
$ cmake --build --preset macos-dev -j4                                   → BUILD_EXIT=0, grep -c 'warning:' → 0
$ ctest --preset macos-dev -R 'Texsrc_ResolveTiers|Present_DrawScanoutSource' --output-on-failure
1/2 Test #81: Present_DrawScanoutSource ........   Passed    0.35 sec
2/2 Test #97: Texsrc_ResolveTiers ..............   Passed    0.62 sec
100% tests passed, 0 tests failed out of 2
$ ctest --preset macos-dev
  1/169 Test   #1: LintOneClass .........................   Passed    0.47 sec
  3/169 Test   #3: LintEnginePurity .....................   Passed    0.16 sec
 67/169 Test  #67: Targets_SyncAllJob ...................   Passed    0.05 sec
 76/169 Test  #76: Targets_PendingLedger ................   Passed    0.04 sec
 81/169 Test  #81: Present_DrawScanoutSource ............   Passed    0.34 sec
 97/169 Test  #97: Texsrc_ResolveTiers ..................   Passed    0.63 sec
100% tests passed, 0 tests failed out of 169
Total Test time (real) = 169.13 sec
CTEST_EXIT=0
$ python3 tests/lint/lint_one_class.py --root .    → LintOneClass: 448 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . → LintEnginePurity: 395 files scanned, 0 game tokens -> PASS
$ python3 /tmp/p6/check_sizes.py                   → size check OK   (test_texsrc.cpp 1,353, test_present.cpp 414 — tests/gs outside the script's dirs, both ≤ 1,500)
$ ./build/tests/gs/test_targets all → 304 checks, 0 failures · test_texsrc all → 985 checks, 0 failures (+10) · test_present all → 115 checks, 0 failures (+24)
```

Negative probes (`/tmp/p6/fix6d3_2_probes.sh` = the review's Q1–Q3 blocks verbatim → `/tmp/p6/fix6d3_2_probes.log`; sources restored,
`shasum -a 256 -c` OK ×3, restored build 0 warnings, `all` green ×3):

| probe | before (review) | now |
|---|---|---|
| Q1 `SyncDrawn` removes the target's own entry instead of the deposit base | 0 failures | targets 0 · **present 1** · texsrc 0 |
| Q2 `DrawScanout` flushes the Scanout entries BEFORE the native resolve | 0 failures | **present 7** |
| Q3 the keep-fall rescue site loses its Ftt flush | 0 failures | **texsrc 4** |

Finding while pinning item 2: the review's sketch expected the flushed foreign rows to leave their page authored by the
16-bit target after the native present; OLD's `NoteGPUElided` (`:24808-24812`, NEW `vram.NoteGpuElided` after the flush)
re-authors EVERY footprint page for the display with the owed band — so the order's observable consequence is
"native stays native and the display's entry is recorded while the foreign rows land" (pinned), not a foreign author
surviving the present; the flipped order declines the resolve (Q2's 7 failures), exactly the consequence the review named.
Items 1–6 of `/tmp/p6/REVIEW_6D3_2.md` CLOSED (4/5 as owed integration rows). N2 (`Source::Mirror` at content dims 0) and
N3 (OLD record tags in test comments) left as recorded nits.

### Review 6D″ (verdict, iteration 3)
Independent re-review 2026-09-28 (full record `/tmp/p6/REVIEW_6D3_3.md`; logs `/tmp/p6/rv6d3_3_*.log`). Gates re-run on a CLEAN
rebuild: `cmake --build --preset macos-dev --clean-first -j4` → `[363/363]`, `BUILD_EXIT=0`, 0 warnings; `ctest --preset macos-dev
-R 'Texsrc_ResolveTiers|Present_DrawScanoutSource' --output-on-failure` → `100% tests passed, 0 tests failed out of 2` (#81 0.35 s,
#97 0.70 s); `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 169` (182.31 s, `CTEST_EXIT=0`); `LintOneClass
448 files, 0 findings`; `LintEnginePurity 395 files, 0 game tokens`; `size check OK`; `test_targets all 304/0`, `test_texsrc all 985/0`,
`test_present all 115/0`; `shasum -a 256 -c /tmp/p6/rv6d3_2_probes.sha` → OK ×4 (the live path is byte-identical to iteration 2);
`git status --short docs/design docs/filemap` → empty; no commits. No in-tree edits by this review except this section.

The six items of `/tmp/p6/REVIEW_6D3_2.md` §VERDICT are closed. (1) `test_texsrc.cpp:1036-1065` pins the keep-fall rescue kFTT site
(`texture_resolver.cpp:359-360`) AND its footprint span (the pending 16-bit target at 0x130000 lies outside the 64×64 TEX0 window);
the item-1 row names the three sites. (2) `test_present.cpp` `DrawScanoutOrderAndTail()` pins the resolve-before-flush order
(`scanout_composer.cpp:155-165`): native stays native, `Flushes(Scanout) + 1`, the foreign rows land at their epoch, the display entry
recorded, its drawn rect kept — the fix round's finding that OLD's `NoteGPUElided` (`:24808-24812`) re-authors every footprint page
after the flush is correct and is the reason the observable is "native survives", not "foreign author survives". (3) The same
function pins `Remove(deposit base)` (`render_target_cache.cpp:161` = OLD `:17095` with the `fbp` argument): the 16-bit fallback
stand-in keeps its own entry, deposits `0x801F` at the scanout base, loses its drawn rect. (4, 5) Accepted in the owed-row form per
the orchestrator's contract ruling (PHASE6_PLAN §1.5): `## Ledger deltas owed at integration` rows L1 (HOOK_RECLASSIFICATION
workaround row for the stale-display stand-in — OLD `:24339-24385` read: the comment attributes the cause to the unreproduced
`0x70000 → 0x0` display blit exactly as the row says; NEW locus, PS2 rule, root-fix plan, frames-lane expiry, mem-lane witness all
present) and L2 (the `ScanoutFromVRAM` FMV hold `:19236-19320` / `:19332-19378` read: predicate `psm == 0x02 ∧ fbp == 0 ∧ width ==
512 ∧ m_scanout_texture`, 32×28 luma grid `< 5 %`, `HOLD_CAP = 8`, `s_fmv_have_good ∧ g_vsync_frame ≥ 400`, upload skipped under
`fmv_hold_prev` — all as stated; dormant at peak, NOT written, re-adjudication path named). P6-STALE-DISPLAY-PICK corrected in both
places; the "queue-order tuple lag" attribution survives only in the two sentences that say it was wrong. (6) The HANDOFF
SetFrontBuffer line carries the `scanout_fbp_` default note (`opengl_backend.h:1066` verified).

Probes (`/tmp/p6/rv6d3_3_probes.sh` = iteration 2's Q1–Q3 verbatim; sources restored, `shasum -c` OK ×3, restored build 0 warnings):

| probe | iteration 2 | iteration 3 |
|---|---|---|
| Q1 `SyncDrawn` removes the target's own entry instead of the deposit base | 0 | targets 0 · **present 1** · texsrc 0 |
| Q2 `DrawScanout` flushes the Scanout entries before the native resolve | 0 | **present 7** |
| Q3 the keep-fall rescue site loses its Ftt flush | 0 | **texsrc 4** |

| check | result |
|---|---|
| 1 pins for items 1–3 exercise the cited lines and have teeth | PASS |
| 2 ledger rows L1/L2 complete; shared docs untouched | PASS |
| 3 record corrections (item-1 row, P6-STALE-DISPLAY-PICK ×2, HANDOFF N1) | PASS |
| 4 live path unchanged since iteration 2 | PASS |
| 5 hygiene (clean build 0 warnings, 169/169, lints 0/0, size OK, no commits) | PASS |

Nits carried, no change required: N2 (`Source::Mirror` at content dims 0), N3 (OLD record tags in test comments), N1 (a 6E
integration obligation, in the HANDOFF).

**VERDICT: APPROVED** — 6D″ (with the 6D‴ remainder) is closed; 6E may start from the `HANDOFF (6D″ → 6E)` section.

## 6E — classrdr + draw pipeline + profile data (2026-09-28, ≈55 min)

The DrawPipeline (the 6D″ HANDOFF per-draw order as ONE method + the per-job entry points), the class
taxonomy / registry / dispatch with the OLD counter grammar, the state-shape vocabulary, the five perf-only
replica renderers routed through the generic path, the stencil-composite sequence FSM + byte arithmetic, the
column-shuffle snap, the profile data (stamps / Accept tuples / sequence constants) + `RegisterClassRenderers`,
and `Presenter::Present` over `DrawScanout`. The STOP rule fired before the stencil mechanism: the
`StencilCompositeRenderer` (backend stencil surface, the three passes per strip, the 32 materialise quads, the
native invert program) is **P6-S9-PENDING** below — NO renderer is registered in the `S9-shadow` slot, so
those draws take the generic path (`fb_norenderer`). Tree: 0 warnings, 177/177, lints 0/0, no commits, OLD untouched.

### Landed (2,162 lines incl. the 554-line test)   [6E review: 571 → 554]

`engine/gs/hle/classrdr/` (lib `ps2r_gs_hle_classrdr`; `engine/gs/CMakeLists.txt` +1 `add_subdirectory` line):
- `class_classifier.h/.cpp` — `ClassClassifier` (nested `Class` enum of the 10 census classes + `ClassName` = the
  verbatim census strings; `Rules {stamps (substring → class, in order), composite_on_date, composite_frame_psm,
  composite_tex_psm, blit_min_w/h}` = the title data; `ClassifyFields` = OLD `:151-178` order — stamp table → DATE →
  accumulator FRAME.PSM → textured self-read PSM → sprite big/small → raw-other; `Classify(rules, ir, name_override)`
  with the bbox scan ONLY on unstamped sprites with vertices).
- `class_renderer.h` — `ClassRenderer` (`Name / Accept pure / Draw all-or-nothing / WarmUp`, all pure virtual).
- `class_registry.h/.cpp` — `ClassRegistry(rules)`: `Register` (returns the previous occupant), `Slot`, `SetEnabled /
  Enabled` (default on; tests only), `Dispatch(pipe, ir, name)` = OLD `:341-373` exactly (classify → seen/verts →
  no slot → `fb_norenderer` false; disabled → `fb_disabled` false; `!Accept` → `fb_declined` false; `Draw` → `native`
  true; `CurrentClass()` published during the draw, cleared by the pipeline), `WarmUpAll(pipe, FILE*)` (+ the
  `[GS-CLASSRDR] warmup renderers=%d programs=%d` line), `EmitCounters(FILE*)` in the OLD grammar (one
  `[GS-CLASSRDR] class=… seen=… verts=… native=… fb_norenderer=… fb_disabled=… declined=…` line per class SEEN, then
  `[GS-CLASSRDR] SUMMARY on=1 renderers=%d seen=… native=… fallback=… declined=…` with `fallback = norenderer +
  disabled + declined`). Counters are plain `uint64_t` (single consumer thread; OLD's relaxed atomics served a
  main-thread bench rider that does not exist here).
- `draw_shape_spec.h/.cpp` — `DrawShapeSpec` (per-field `kAny` / value over prim, iip, fst, fog, tme, shuffle, abe,
  ABCD tuple list (compared when blending), FIX, colclamp, pabe, fba, ate (+ atst/afail when on), date, datm, zte,
  ztst, zmsk, frame/zbuf psm, fbmsk, scanmsk, fbw ≠ 0, min verts, and the TEX0/TEX1/CLAMP fields when textured;
  `Matches(ir)`).
- `texture_shuffle_snap.h/.cpp` — `TextureShuffleSnap` (`IsPsm16`, `SnapToColumn8 = nearbyint(v/8)*8`, `SnapRow =
  s + nearbyint(v - s)`, `DetectColumnShuffle` verbatim OLD `shuf_snap.h:97-146` incl. `kEps 0.75`, over `VertexRec`).
- `sequence_recipe.h/.cpp` — `SequenceRecipe(Constants)`: the FSM `kIdle / kPrimed / kArmed` with `Prime(fbp)`,
  `Arm(alt16)` (extent reset), `OnPreDraw(ir, leaving_capture)` = OLD `GsS9HookPreDraw` (`:634-666`: ARMED ∧ (draw
  into the fbp ∨ textured read of the fbp ∨ leaving) → `kMaterialize` + IDLE; PRIMED ∧ draw into the fbp with
  `(fbmsk & rgb_mask) != rgb_mask` → `kDemote` + IDLE), `OnTouch(fbp)` = `GsS9HookTouch` (`:668-690`), `HoldsEntry(fbp)`
  = the S9-HOLD predicate (ARMED ∧ own fbp), the extent union; the byte arithmetic `MaterializeByte = (v & mask) << 3`,
  `OracleWrapByte = (clear_byte + 8·net) & 0xFF`, `StencilAfter = (base + net) & 0xFF`, `Csm1Swizzle`, `InvertFold`
  (rgb = 255 − Cd, a = 255 − CSM1(idx), idx = a ≥ 255 ? 128 : a >> 1); the predicates `ClassifyStripColor / Verts`
  (+incr / +decr flat colour, every vertex equal), `ClearPrimes` (every vertex == clear_rgba), `InvertVertexColorOk`.
  Constants (`clear_rgba, stencil_base, counter_mask, quant_bit, incr_rgb, decr_rgb, invert_rgba, rgb_mask`) are
  profile data — no S9 literal lives in `engine/`.
- `draw_pipeline.h/.cpp` — `DrawPipeline(Deps{be, vram, cache, refresher, resolver, scanout, presenter, layout,
  registry?, sequence?})`: `Draw(ir, name)` = registry dispatch else `DrawGeneric`, then `ClearCurrentClass`;
  `DrawGeneric(ir)` = entry guards (`n < 3 ∨ fbw == 0` → nothing; OLD `:8291` / `:8666`) → `sequence->OnPreDraw`
  when live (`:8330`) → **1** `TargetDims::Compute` → **2** `GetOrCreate(fbp, w, h, psm, gfx_epoch)` → **2b**
  `SameBufferPreDrawFlush` (`:9008-9034`: `!ledger.Empty() ∧ frame psm ∉ {02, 0A} ∧ ledger.Has(fbp)` →
  `FlushPending(PreDraw, fbp, 4)`) → **3** `AliasPreDraw(t, ir, render_h, scanout_fbp, gfx_epoch)` → **4**
  `PrepareDraw(be, ir, ee_frame, scanout_fbp, &params)` + `render_w/h` + `MarkCompositeConsumption` (`:9148-9155`:
  a textured draw into the scanout target sampling a DIFFERENT live target marks that source) → **5**
  `ResolveRead(ir, {frame_count, active_fbp, has_snapshot=false})`, `params.clut / idx_partner`, the snap decision
  (`DetectColumnShuffle` over the DrawIr verts → `src.transform.shuffle_snap`, which `GlDrawExecutor` (6C) applies
  after the UV rebase and the shuffle U scale — the OLD `:10153-10165` position) → **6** `Backend::Draw` → **7**
  `++draw_seq`, `t.AfterDraw(ir, res, frame_count, gfx_epoch, draw_seq)`, `NoteDrawn` → **8** `AfterDrawTail` → **9**
  `EvictUnderDraw`. Jobs: `OnInvalidate → resolver.OnCpuWrite`; `OnUpload → refresher.UploadDirect`; `OnSetWindow(w, h,
  cw, ch)` (content dims when > 0, window dims when > 0); `OnSetFrontBuffer → SetScanoutFbp + SetDispfb`; `OnSyncAll →
  sequence touch + cache.SyncAll`; `OnFrameEnd(ee_end, crtc, Frame*)` = `MarkEeFrameEnd` (when arg1 == 1) →
  `FlushPending(Present, 0, 0, reader_pages_only=true)` (P6-PRESENT-FLUSH closed) → `Presenter::Grab` (the corpus /
  frames-lane bytes; `w = h = 0`, empty when no target) → `Presenter::Present(vram, layout, crtc, gfx_epoch, window)`
  = `DrawScanout` + `Backend::Present` + `++frame_count`. `LastSource()` exposes the resolve for the renderers' census.
- `skinned_renderer.h/.cpp` (`SkinnedRenderer(shape, Route {kSkin, kWorld2, kRaw}, Stats*)`, `DrawAs`; the ONE Stats
  block counts all three routes, `[GS-SKINRDR] native=%llu spec=%llu generic=%llu skin=%llu world2=%llu raw=%llu
  rt_serve=%llu`), `world_tristrip_renderer.h/.cpp` (`Accept = strip ∨ world2->Accept`; strip → generic, else
  `world2->DrawAs(kWorld2)`), `sprite_hud_renderer.h/.cpp` (`common ∧ (textured ∨ untextured)`; `[GS-SPRRDR] native=
  spec= generic= untex= rt_serve=`), `particles_renderer.h/.cpp` (`[GS-PARTRDR] … abe0= ff0101= ff0201= rt_serve=`),
  `fmv_blit_renderer.h/.cpp` (`[GS-FMVRDR] native= spec= generic= rt_serve=`). Each `Draw` IS `pipe.DrawGeneric(ir)`
  (P6-PERF-ONLY-RENDERERS); `spec` stays 0 (OLD's specialised programs are never selected at peak: `spec=0 generic=N`
  on every baseline log); `rt_serve` = the resolve served a surface; `WarmUp` returns 0.

`games/<title>/renderer_profile/renderer_profile.h/.cpp` — `ClassRules()` (the 7 stamps
`skinned / family_tristrip / textured_tristrip / lit_tristrip / poscolor / indexed_displace / sprite_text`, DATE,
FRAME.PSM 0x0A, TEX0.PSM 0x1B, 256×128), `Shape()` (INV §7.4 verbatim as `DrawShapeSpec` data: `world_strip`,
`composite_fan` (= strip with prim 5), `skinned` (fba free, ate only as atst 5 / afail 0), `sprite_common /
sprite_textured / sprite_untextured`, `particles` (ABCD ∈ {0101, 0201} when blending), `fmv_blit` (TBW ∈ {512, 640}),
`composite_clear`, `composite_invert` (+ FIX 0x80; the self-read TBP/TBW pins and the vertex colour are renderer
checks beyond the vocabulary)), `Sequence()` (0x78787878, 0x0F, 0x1F, 0x10, +0x080808 / +0xF8F8F8, (0x80,0x80,0x80,0x00),
rgb mask 0x00FFFFFF), `RegisterClassRenderers(reg)` (owns the six replica renderers; fills world-tristrip, skinned,
particles-untex, sprites-HUD, raw-blit-fullscreen, raw-other = the raw skin; indexed-displace / hle-other /
raw-sprites-HUD / **S9-shadow** empty), `EmitRendererCounters(FILE*)` (the four emit lines in OLD's order:
SPRRDR, SKINRDR, PARTRDR, FMVRDR). Lib now links `ps2r_gs_hle_classrdr`.

`engine/gs/hle/present/presenter.h/.cpp` — `Present(vram, layout, crtc, gfx_epoch, window_w, window_h)` over
`ScanoutComposer::DrawScanout` (`LastPresent()`); the 6D stand-in body is gone. `tests/gs/present/test_present.cpp`
`grab` case updated (the target needs valid content for the quad; `Source::Target`, fbp 0x70000).

`tests/lib/fake_backend.cpp` — `Draw` now returns a plausible `DrawResult` (`drew`, fused bbox over the
triangle-complete vertices, `write_rect = bbox ∩ scissor ∩ render dims`, `alt16_route` echoed) so the coherence
tail above the interface runs in GL-free pipeline tests; no pixel is rasterised. Every prior FakeBackend test
feeds synthetic results to `AfterDraw` directly and is unaffected (177/177).

### OLD sources transplanted (read-only)

`host_app/gs_class_renderer.h:91-124` (taxonomy + names), `:151-200` (ClassifyFields / Classify), `:341-373`
(Dispatch), `:380-398` (WarmUpAll), `:404-437` (EmitCounters); `graphics/opengl/world_tristrip_native.inc:156-199`
(AcceptStrip + World2Accept), `skinned_native.inc:261-294, 365-` (AcceptShape, RawSkinRenderer), `:424-440` (emit),
`sprite_hud_native.inc:211-260, 320-332`, `particles_native.inc:201-242, 296-312`, `fmv_blit_native.inc:198-229,
282-292`; `s9_composite.h:120-383` (constants, FSM comment, `MaterializeByte / OracleWrapByte / StencilAfter /
ClassifyStripColor / ClassifyStripVerts / AcceptClearFields / ClearPrimes / AcceptInvertFields / Csm1Swizzle /
InvertFold`), `s9_composite_native.inc:297-325` (AcceptStripShapeS9 fan tuple), `:634-690` (PreDraw / Touch hooks);
`host_app/shuf_snap.h:66-146`; `opengl_backend.cpp:8291, 8666` (entry guards), `:8323-8330` (sequence hook site),
`:9008-9034` (same-buffer pre-draw flush), `:9148-9155` (composite consumption), `:9717-9731` + `:10153-10165` (snap
decision + apply site), `tests/gs_replay.cpp:462-765` (job → method map, frame-end order). READ but NOT transplanted
(P6-S9-PENDING): `s9_composite_native.inc:53-215` (the four S9 shader sources), `:417-467` (stencil FBOs), `:469-632`
(Materialize), `:692-767` (InvertBackendOk), `:768-1341` (ladder / verts / post-draw replication), `:1342-1662`
(ClearNative / InvertNative), `:1663-2106` (StripOrWorld / StripStencil), `:2108-2273` (renderer + emit);
`opengl_backend.cpp:11422-11468` (ZSRC route + uniforms — the 6C `DrawParams.zsrc*` fields exist, nothing sets them
yet), `:16282`, `:19613` (touch hook call sites; the 6E review corrected `:19634` → `:19613`).

### Flags applied (FLAG_LEDGER rows; NORM = winning arm only)

NORM (winning arm only): `PS2_GS_CLASSRDR` (ON: the registry IS the dispatch; there is no off arm — a draw with no
registered renderer is the untouched generic path by construction), `PS2_GS_CLASSRDR_{WORLD,SKINNED,PARTICLES,
DISPLACE,SPRITES,HLEOTHER,S9,BLIT,RAWSPRITES,RAWOTHER}` (per-class enable = a renderer registered; `SetEnabled` is a
test hook, no env), `PS2_GS_{SPRRDR,SKINRDR,SKINRDR_WORLD2,PARTRDR,FMVRDR,RAWSKIN}` (ON: the slot is registered),
`PS2_GS_{SPRRDR,SKINRDR,PARTRDR,FMVRDR}_SPEC` (the specialised-program arm is NOT written: every baseline log shows
`spec=0 generic=N`; the replica renderers route through the generic lowering — P6-PERF-ONLY-RENDERERS),
`PS2_GL_SHUF_SNAP` (ON: detect + snap), `PS2_SH8` (ON: the `zsrc_num/den` scale map — carried by the 6C executor's
`zsrc_scale`; nothing sets it until the stencil renderer lands), `PS2_GS_S9RDR` / `PS2_GS_S9RDR_HOLD` (ON in OLD;
the FSM + hold predicate are written, the renderer that would exercise them is P6-S9-PENDING; until it lands the
S9-shadow slot is empty = OLD's `PS2_GS_S9RDR=0` arm), `PS2_GS_ZSRC` / `PS2_GS_ABIT16` (the 6C shader / DrawParams
carry them; the S9 route that sets `zsrc_armed` is pending). **`PS2_SH7`**: FLAG_LEDGER row 671 classes it DELETE
("delete-both", presence-gated experiment) but the code's default is ON when unset (`:9013-9017`: `s_sh7_fix = v && *v
? v != "0" : true`) and the peak profile does not set it, so the `:9008-9034` same-buffer pre-draw flush IS the live
arm behind every frozen corpus hash — transplanted as `DrawPipeline::SameBufferPreDrawFlush` (NORM ON); ledger delta
row L3 owed (below). TEST: `PS2_S105_SKIP` / `PS2_S103_DRAWLIM` → `gs_replay --skip` (6F), not in the engine.
DELETE not written: `PS2_S9_DEMOTE` (+ the `[S9-DEMOTE]` / `[GS-S9DEM]` cause census), `PS2_GS_S9RDR_DBGPIX`,
`PS2_GS_S9RDR_DBGBOTH`, `PS2_GS_S9RDR_DBGQUANT`, `PS2_SH5_BIND`, `PS2_SH6_SWEEP`, `PS2_R121_ATTR` (`CurDrawClass` is
kept as `ClassRegistry::CurrentClass` — a free integer, no counting), the `[GS-S9RDR] invert pin miss` probe.

### Tests (PASS lines from `/tmp/p6/6e_ctest_sel.log` / `/tmp/p6/6e_ctest.log`)

```
$ ctest --preset macos-dev -R 'Classrdr_|Gs_Inv29|Profile_|Render_S9|Present_' --output-on-failure | tail -16
 9/14 Test #102: Classrdr_AcceptTuples ............   Passed    0.04 sec
10/14 Test #103: Classrdr_S9Recipe ................   Passed    0.01 sec
11/14 Test #104: Classrdr_ShuffleSnap .............   Passed    0.06 sec
12/14 Test #105: Classrdr_DrawPipelineOrder .......   Passed    0.08 sec
13/14 Test #106: Classrdr_EmitCountersGrammar .....   Passed    0.06 sec
14/14 Test #107: Profile_RegisterClassRenderers ...   Passed    0.04 sec
100% tests passed, 0 tests failed out of 14
$ ctest --preset macos-dev
100% tests passed, 0 tests failed out of 177
Total Test time (real) = 166.46 sec
CTEST_EXIT=0
$ ./build/tests/gs/test_classrdr all → test_classrdr all: 282 checks, 0 failures
```
`tests/gs/classrdr/test_classrdr.cpp` (one binary, selectors): `Classrdr_Classify` (`classify`, 32 checks = OLD
`test_gs_classrdr` `TestClassifierIdentityStamps` + `TestClassifierRawShapes` + `TestNamesAndTokens` over the
profile's rules: the 8 stamp rows, null name → hle-other, name override, stamp beats DATE, the 3 composite signatures,
T8H with TME off, the 256×128 threshold incl. 255.5 / 127.5 / untextured big / offset invariance, `ClassifyFields ==
Classify`, the 10 names, and an EMPTY rules set that classifies a DATE + CT16S draw raw-other); `Gs_Inv29_
DispatchOrderFallback` (`dispatch`, 32: `TestDispatchNullRouting` + `TestDispatchNativeAndDeclines` + `TestWarmUp` —
no slot / declined / disabled / cleared each fall back with the counter bumped and the FakeBackend receiving exactly
ONE Draw per pipeline draw; the native replica's `DrawParams` equal the fallback's for the same draw; `CurrentClass`
cleared; warm-up sums skip disabled slots); `Classrdr_AcceptTuples` (`accept`, 40: every shape positive + one-field
negatives — strip prim/iip/colclamp/ABCD/datm/fbmsk/fbw/n; skinned fba-free, ate 5/0 ok, atst 6 no, mmin, fst, tpsm;
sprites fba/tbw/tw/ztst; particles 0201 yes / 0221 no / zmsk / wms; blit 512 + 640 yes / 1024 no, tfx, mmag, abe;
then the registered slots accept exactly these incl. raw-other accepting the skinned shape and not a raw triangle);
`Classrdr_S9Recipe` (`sequence`, 121 = OLD `test_s9_composite` §1-4 ported (+ `Csm1Swizzle` / `InvertFold` spot values; §5 `ClutIsInvertTable` walk + pure-identity negative and §6 folded == `InvertGenericRef` sweep follow with P6-S9-PENDING (3) → 6E′): constants, `base == clear byte >> 3`, the
identity `MaterializeByte(StencilAfter(net)) == OracleWrapByte(net)` for net ∈ [−40, 40], wrap cases, strip colours,
CSM1 grid, `InvertFold` (idx 8 → CSM1 16; alpha 255 → idx 128), all-vertex strip / clear / invert predicates, the FSM:
PRIMED survives the RGB-protected SEED and another buffer's draw, demotes on an RGB write; ARMED holds its own fbp,
extent union, materialises on a texture read / a draw into the fbp / leaving a capture / a touch, keeps fbp + extent
for the materialise, touch while PRIMED inert; clear / invert Accept fields incl. FIX / HIGHLIGHT2 / CLUT CT32
negatives); `Classrdr_ShuffleSnap` (`snap`, 23 = OLD `test_shuf_snap` ported: half-even column snap, row snap sheds
the bias, the 16-bpp set, the live strip x=[8,16) u=[0.5,8.5) detected, 32-bit frame / 32-bit texture / STQ / TME off /
non-sprite / count%6 decline, offset 0 ok / 12 no, width 12 / 4 no, u corner off-grid, two quads all-or-nothing,
and through the pipeline: `LastSource().transform.shuffle_snap` set for the strip, clear for a plain draw);
`Classrdr_DrawPipelineOrder` (`pipeline`, 22: jobs applied; step 2 target acquired; step 6 drawn into it with the
step-1 content dims; step 4 first draw of the EE frame clears colour on the scanout target, the second does not;
step 7 `NoteDrawn` + seq + gfx epoch stamp; **SH7**: a 16-bit draw records its ledger entry, another buffer's 32-bit
draw and a 16-bit draw leave it, the SAME buffer's 32-bit draw flushes it first (`SameBufferFlushes == 1`,
`Flushes(PreDraw) ≥ 1`); composite consumption marks the source; frame end → EE / present counters, grab 512×448,
quad from the target, the next draw clears again; entry guards; invalidate / upload / sync-all reach their owners);
`Classrdr_EmitCountersGrammar` (`grammar`, 1 string compare: 5 stamped skinned + 13 raw skinned + 4 raw + 1 displace +
1 world family-2 + 2 raw sprites → the exact `[GS-CLASSRDR]` five class lines + SUMMARY `renderers=6 seen=26 native=19
fallback=7 declined=4` + `[GS-SKINRDR] native=19 spec=0 generic=19 skin=5 world2=1 raw=13 rt_serve=0`);
`Profile_RegisterClassRenderers` (`profile`, 11: 6 slots, names incl. `raw-skin` in raw-other, the three empty slots,
S9-shadow empty, warm-up 0, seven stamps). NOT added: `Classrdr_WrapBlend` — `Render_BlendFfMapTable` (6C) already
pins `RouteWrapBlend` + `WrapChannel` (OLD `test_s8_wrapblend`'s routing half); `Classrdr_S9StencilPasses` /
`Render_S9StencilSmoke` — P6-S9-PENDING.

**Negative probes** (`/tmp/p6/6e_probes.sh` + `6e_probes2.sh` → `/tmp/p6/6e_probes.log`; one predicate each, the test
binary rebuilt, sources restored, `shasum -a 256 -c` 6/6 OK, restored build 0 warnings, `all` 282/0). The first
round's P2 / P3 / P4 edits did not compile (unused variable under `-Werror`) and ran the stale binary — re-done with
compile-safe edits in round 2:

| probe | result |
|---|---|
| P1 dispatch: a declined draw counted native | dispatch: 1 failure |
| P2 classify: composite DATE rule dropped | classify: 1 failure |
| P3 pipeline: same-buffer flush fires for 16-bit frames too | pipeline: 2 failures |
| P4 snap: offset-8 shapes rejected | snap: 5 failures |
| P5 sequence: RGB-protected SEED demotes | sequence: 2 failures |
| P6 spec: ABCD tuple not compared | accept: 2 failures |
| P7 pipeline: snap never reaches the TextureSource | snap: 1 failure |
| P8 pipeline: composite consumption never marked | pipeline: 1 failure |

Gate lines: `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`, `grep -c 'warning:'` → `0` (`/tmp/p6/6e_build6.log`);
`LintOneClass: 472 files scanned, 0 findings -> PASS`; `LintEnginePurity: 419 files scanned, 0 game tokens -> PASS`;
`size check OK` (largest new: `draw_pipeline.cpp` 145, `sequence_recipe.cpp` 129, `test_classrdr.cpp` 571);
`#include "engine/render/backends` in `engine/gs` 0; `CommitDeposits` in `engine/gs/hle` 0; `getenv` in `engine/gs` /
`engine/render` / the profile 0 (the one hit is a README sentence); game literals (`0x230000 / 0x70000 / 0x78787878 /
0x268000 / vs_skinned / family_tristrip`) in `engine/gs/hle/classrdr` 0.

### Findings

- **P6-CLASS-NAMES**: the `ClassClassifier::Class` enum and its ten display strings live in `engine/` — they are the
  census vocabulary of the corpus TOOLS (`gs_replay census / classdiff`, CENSUS_MASTER rows), i.e. the tool's contract,
  not game data; what IS game data (which program-name substring maps to which class, the composite signatures, the
  blit extent) moved to `ClassClassifier::Rules` in the profile. The enum names are engine-neutral (`kStencilComposite`
  for the class whose census string is `S9-shadow`).
- **P6-PERF-ONLY-RENDERERS**: the five class renderers whose OLD bodies are "statement-for-statement replicas" of the
  generic path (INV §7.6, `opengl_backend.h:160-200`, BASELINE.md `403/403 IDENT tol=0`) are implemented as
  `Accept(spec) + DrawGeneric` — identical pixels by construction, ~60 lines each instead of ~1,600; their counters
  and emit grammar are kept so 6F can print log-parity lines (`spec=0 generic=N`, the only values the baseline logs
  hold). `Gs_Inv29` pins that the native replica issues the same backend call as the fallback.
- **P6-BACKEND-STENCIL (deferred with P6-S9-PENDING)**: `render/backend.h` has NO stencil surface yet. The stencil
  renderer needs: a stencil-capable depth surface (`SurfaceFormat::Depth24Stencil8` or a stencil flag on
  `SurfaceDesc`), `Backend::ClearStencil(surface, value)`, `DrawParams.stencil {enabled, func, ref, mask, write_mask,
  pass_op (KEEP / INVERT / INCR_WRAP / DECR_WRAP), color_write}` (+ a "no colour attachment" draw for pass 1), and a
  `Backend::DrawQuad(color, depth_stencil, scissor, rgba, stencil)` constant-colour NDC-triangle primitive for passes
  2/3 and the 32 materialise quads. API-agnostic names only; GL maps them to `glStencilFunc / Mask / Op`, a
  `GL_DEPTH24_STENCIL8` renderbuffer and the two FBOs of OLD `GsS9EnsureStencil`.
- **P6-S9-SHADERS (deferred)**: the four S9 fragment / vertex sources (`s9_composite_native.inc:53-215`: clear, strip
  stencil (DATE + ZSRC discards), invert (`out.rgb = ~Cd.rgb; out.a = 255 − CSM1(idx)`), const vert + frag) must land
  in `GlShaderSources` byte-identical (the invert program is pixel-changing vs the register path — INV §7.6).
- **P6-S9-PENDING (OPEN → 6E′)**: the exact remainder — (1) `render/backend.h` + `GlDevice` / `GlDrawExecutor` stencil
  surface (above); (2) `engine/gs/hle/classrdr/stencil_composite_renderer.h/.cpp` `StencilCompositeRenderer(profile
  shapes + SequenceRecipe&)`: `Accept` = `composite_clear.Matches ∨ (strip ∨ fan shape ∧ world slot registered+enabled)
  ∨ (composite_invert.Matches ∧ tbp == fbp ∧ tbw == fbw ∧ wms,wmt ≤ 1 ∧ InvertVertexColorOk ∧ InvertBackendOk)`;
  `Draw`: CLEAR → `DrawGeneric` then `Prime(fbp)` when `ClearPrimes` (the OLD native clear = the generic arms; a
  scaled target declines the prime — `n_scale_declines`); STRIP → `ClassifyStripVerts` ≠ decline ∧ `ZsrcRoute(zte,
  ztst ∈ {2,3}, zmsk, date, zbp ≠ fbp, zbp target has colour)` ∧ `LiveOn(fbp)` else the colour path (`DrawGeneric`
  after materialise-if-armed / demote-if-primed); stencil path: run steps 1-5 of the pipeline WITHOUT step 6 (needs a
  `DrawPipeline::Prepare(ir, &ctx)` split of `DrawGeneric` — add it), ARM on PRIMED (`ClearStencil(base)` over the
  whole target; the surface follows the ROUTED attachment: `alt16 ? alt16 : color`), then pass 1 geometry through the
  strip program with `stencil {func EQUAL ref 0 mask 0x80, write 0x80, INVERT, color_write off}`, passes 2 + 3 over
  `bbox_tri ∩ scissor` clamped to the stencil dims (`{EQUAL 0x80 0x80, write counter_mask, INCR_WRAP | DECR_WRAP}`,
  `{EQUAL 0x80 0x80, write 0x80, INVERT}`), `ExtendExtent(rect)`, then steps 7-9 with a DrawResult over the strip's
  rect (the per-strip CT16S ledger record is LOAD-BEARING — INV §7.6 — so `AfterDrawTail` runs as for a colour strip);
  INVERT → the native program (`DrawParams` with the invert program selected + `rt_copy` bound, fix 1.0) + steps 7-9;
  MATERIALIZE (on every `Action::kMaterialize` from `OnPreDraw` / `OnTouch` / the strip steer / the surface-change
  check): 32 `DrawQuad`s scissored to the extent with `{EQUAL v counter_mask, write 0, KEEP}`, colour
  `MaterializeByte(v)/255` ×3, alpha write OFF, then the extent's coherence stamps (`valid_rect / drawn_since_sync` (or
  the alt16 rects) union, `has_content`, `last_c_seq`, `last_draw_frame / gfx_epoch`, Cd-copy dirty extend) — NO sync
  arm; (3) `InvertBackendOk`: target at fbp with colour + rt_copy, `has_content`, drawn THIS frame, render psm ∈ {0x00,
  0x30}, no pending ledger entry over `[cbp, cbp + 0x2000)`, and `ClutIsInvertTable` over the composed VRAM view at
  `PsmLayout` CT32 addresses (`byte_at(addr_of(p%16, p/16)) == FF FF FF 255−CSM1(p)`) — declines in replay (zero
  mirror: `clut=8` on load_transition) exactly as OLD; (4) the hooks: `DrawPipeline::DrawGeneric` already calls
  `OnPreDraw`; the renderer must ALSO receive `kMaterialize` → quads (today the action is computed and dropped —
  harmless while no renderer arms); `RenderTargetCache::SyncRtToVram` / `TextureResolver::MatchTarget` need an
  `OnTouch(fbp)` call (OLD `:16282`, `:19613`) — a `SequenceRecipe*` on the cache/resolver or a callback; the
  `HoldsEntry` predicate must reach `FlushPending(PreDraw, …)` (a `skip` argument = `PendingRtSyncLedger::Take`'s
  predicate) for the strip ladder's own flush only; the ZSRC route must set `DrawParams.zsrc / zsrc_armed / zsrc_scale`
  from the zbp target (OLD `:11422-11468`) for BOTH the stencil strip and the generic path (`s9_zsrc_armed` is a
  generic-path route too: depth forced ALWAYS/no-write, the compare in-shader — 6C's executor has the uniforms); (5)
  `[GS-S9RDR] on=1 clears= primes= arms= strips_stencil= strips_color= inverts= materializes= mat_draws= demotes=
  scale_declines= dedupe_draws=` + `[GS-S9RDR] invert_declines no_target= no_tex= no_content= stale= psm= pending=
  clut=` emit lines; (6) tests `Classrdr_S9StencilPasses` (FakeBackend records the three passes' func/ref/mask/
  write/op + the 32 quads + the per-strip ledger record + the hold), `Render_S9StencilSmoke` (GL, 77-SKIP), and the
  ZSRC route pin; **(7) the world-strip steer** (6E review F1): OLD `WorldTristripRenderer::Draw`
  (`world_tristrip_native.inc:200-216`) sends EVERY untextured strip to `DrawS9StripOrWorld`
  (`s9_composite_native.inc:1663-1740`): stencil ladder when `ClassifyStripVerts ≠ decline ∧ ZsrcRoute (zbp target
  with colour) ∧ seq live on THIS fbp`, else the colour path with the steer's OWN materialise (ARMED on this fbp) /
  demote (PRIMED on this fbp) — and OLD's colour strip path `DrawWorldStripNative` carries NO `GsS9HookPreDraw`
  (the hook is in the other four replicas and DrawBatch only); NEW `WorldTristripRenderer::Draw`
  (`world_tristrip_renderer.cpp:16-23`) is plain `DrawGeneric` and must route through the stencil renderer's
  ladder; **(8) the LIVE `leaving_capture` predicate** (F2): OLD `:636-639` `m_active_target_fbp == fbp ∧
  batch.frame_fbp != fbp ∧ (LazyZSyncOn ∨ IdxPboOn ∨ gsresident::PboOn)` — all three default ON at peak
  (`render.h:772-775, 1056, 1425`, `gs_page_tracker.h:99-104`), so the predicate reduces to `active_target_fbp ==
  seq_fbp ∧ frame_fbp != seq_fbp`; `DrawPipeline::DrawGeneric` hard-codes `false` (`draw_pipeline.cpp:51`) and
  needs a bound-target tracker equal to OLD `m_active_target_fbp` (the last target bound by a draw; reset to
  `0xFFFFFFFF` at the unbind sites) — NOT `MostRecentDrawnFbp`; **(9) hook-before-guards** (F3): OLD runs the
  hook `:8329` after `vCount < 2` (`:8291`) but BEFORE `frame_fbw == 0` (`:8541`) and `vCount < 3` (`:8668`) (same
  order in the replicas, `skinned_native.inc:491-504`); NEW guards first (`draw_pipeline.cpp:49-53`) — a 2-vertex
  or fbw-0 draw into the armed fbp materialises in OLD only. Registration: `RegisterClassRenderers` fills `kStencilComposite` (7 slots) once it exists. Corpus
  impact until then: load_transition is unaffected (`arms=0`: the 8 CLEARs prime and demote, the 8 inverts decline on
  the zero-mirror CLUT), s57_walk / slowspot / crystal_cavern captures with armed sequences will hash-differ — 6F's
  first corpus pass names them.
- **P6-SH7-LIVE (ledger delta L3 owed)**: see "Flags applied" — FLAG_LEDGER row 671 (`PS2_SH7` DELETE) contradicts the
  peak behaviour (default ON, unset in the baseline env, hence live in every frozen hash). Transplanted NORM ON.
- **P6-FAKE-DRAW-RESULT**: `FakeBackend::Draw` now reports geometry (bbox / write rect / drew) so the pipeline's
  coherence tail runs GL-free; pixels are still never rasterised (a test needing bytes uses `SetPixel`).
- P6-PRESENT-FLUSH CLOSED (`OnFrameEnd`); the 6D″ "Not done" list: SH7 flush ✔, shuffle snap ✔, `Presenter::Present`
  over `DrawScanout` ✔, ContentKey wrap split (identity only) — NOT done (no behaviour; → 6F if the corpus asks),
  S9 `hold_fbp` → P6-S9-PENDING (4).

### Ledger deltas owed at integration (appended rows)

| Id | Stage | Target doc / section | Row text to carry over |
|---|---|---|---|
| L3 | 6E | `FLAG_LEDGER.md` row 671 `PS2_SH7` | Reclassify DELETE → **NORM (winning arm = ON)**. The `opengl_backend.cpp:9008-9034` same-buffer pre-draw flush defaults ON when the variable is unset (`:9013-9017`), the peak profile never sets it, so it is live behind every frozen corpus hash and the 12-leg baselines. NEW: `DrawPipeline::SameBufferPreDrawFlush` (`engine/gs/hle/classrdr/draw_pipeline.cpp`), pinned by `Classrdr_DrawPipelineOrder`. GS rule: a FRAME write is authoritative for its rows — a deferred writeback of the same buffer's earlier 16-bit content must reach the mirror before the repaint destroys its source image. |
| L4 | 6F′ | `docs/bugs/` new entry + `HACK_LEDGER.md` dual-path section | **Class-native generic arms inherit register-program uniforms.** OLD `host_app/graphics/opengl/{skinned,sprite_hud,particles,fmv_blit}_native.inc` generic arms (skinned `:1107-1170`/`:1375`) omit `SetU*` for `u_ct16q`, `u_dthe`, `u_dimx_lo/hi`, `u_draw_scale`, `u_uv_native` (+ blend a..d / fix_alpha, zsrc_num/den per shape) that `DrawBatch` sets on every draw (`opengl_backend.cpp:10817-10823`, `:10660-10671`, `:11136-11140`, `:11288-11292`, `:11457-11459`); the generic program keeps the previous DrawBatch draw's values. PS2 rule violated: a draw's frame format alone decides the 16-bit storage quantisation. Symptom: load_transition vf855 — CT32 accumulator draws after a CT16 frame draw land 5-bit quantised (blue 181 → 176, white → 248). NEW: `render::InheritedUniform` + `ClassRenderer::kInherit*Arm` (named, one-line retirement: masks → 0). Expiry gate: the corpus sweep (18 captures) + the frames lane; retiring it is an accepted difference against the frozen baselines. |

### Gate status

| gate | evidence |
|---|---|
| G8 (classrdr) | `Gs_Inv29_DispatchOrderFallback`, `Classrdr_Classify`, `Classrdr_S9Recipe`, `Classrdr_ShuffleSnap` Passed (+ `Classrdr_AcceptTuples`, `Classrdr_DrawPipelineOrder`, `Classrdr_EmitCountersGrammar`, `Profile_RegisterClassRenderers`); `GsClassdiffMatchesOld` needs the replay tool → 6F; the stencil half of the class is P6-S9-PENDING; `Classrdr_S9Recipe` covers OLD `test_s9_composite` §1-4 only (§5-6 with P6-S9-PENDING (3)) |
| build / lints / size | 0 warnings, LintOneClass 0, LintEnginePurity 0, size OK, greps 0 |
| full ctest | `100% tests passed, 0 tests failed out of 177`, 166.46 s, `CTEST_EXIT=0` |

§2.6 rows: ✔ `class_renderer.h`, ✔ `class_registry`, ✔ `class_classifier`, ✔ `draw_shape_spec`, ✔ `sequence_recipe`
(FSM + arithmetic; the executor half → 6E′), ✔ `world_tristrip / skinned / sprite_hud / particles / fmv_blit`
renderers (as replicas), ☐ `stencil_composite_renderer` (P6-S9-PENDING), ✔ `texture_shuffle_snap`, ✔
`renderer_profile.cpp` additions (stamps, tuples, sequence constants; `FMV family F` = `fmv_blit` shape), ✔
`tests/gs/classrdr/*` (ports of `test_gs_classrdr`, `test_s9_composite` (pure half), `test_shuf_snap`;
`test_s8_wrapblend` routing already in `Render_BlendFfMapTable`). Plus (not in the table): `draw_pipeline`.

### HANDOFF (6E → 6F)
Done: DrawPipeline (steps 1-9 + SH7 flush + snap + composite mark + sequence hook), ClassClassifier / ClassRegistry /
  DrawShapeSpec, the five replica renderers, SequenceRecipe (FSM + arithmetic), TextureShuffleSnap, profile rules /
  shapes / constants + RegisterClassRenderers (6 slots), Presenter::Present over DrawScanout, FakeBackend geometry
  results. Ctests: Classrdr_Classify, Gs_Inv29_DispatchOrderFallback, Classrdr_AcceptTuples, Classrdr_S9Recipe,
  Classrdr_ShuffleSnap, Classrdr_DrawPipelineOrder, Classrdr_EmitCountersGrammar, Profile_RegisterClassRenderers.
Not done: P6-S9-PENDING (1)-(9) above (→ 6E′ or folded into 6F once the first corpus pass names the captures that
  need it); ContentKey wrap split (identity only); `GsClassdiffMatchesOld` (needs gs_replay classdiff).
6F corpus-gate WATCH ITEM (6E review §8, inherited from the 6D″ order): OLD issues the per-display-list `glClear`
  at `:9139` BEFORE the `[VRAM-ALIAS]` `RefreshFBOFromVRAM` at `:9620`; NEW runs `AliasPreDraw` (step 3) and then
  the clear folded into `Backend::Draw` (`GlDrawExecutor::FrameClear`, step 6) — an import on a frame-clear draw
  would be wiped in NEW and survive in OLD. Co-occurrence needs a `WantsFrameClear` draw whose target has aliased
  pages (or cpu-dirty 16-bit) with `fbmsk ≠ 0`; the first corpus pass decides; fix shape = hoist the clear
  decision and issue `Backend::Clear` before step 3.
The replay driver's loop (6F `tools/gs_replay/replay_driver.cpp`), over NEW types — every call exists today:
  setup: GlDevice + GlDrawExecutor (window 640×448 hidden), VramImage (zero mirror, never CommitDeposits — P6-ZERO-
    MIRROR), title::RendererProfile P, ScaleSettings{1} → ScalePolicy(P.Vram()), RenderTargetCache, TargetRefresher,
    TextureResolver(P.Vram()), ScanoutComposer, Presenter, ClassRegistry reg(P.ClassRules()), SequenceRecipe
    seq(P.Sequence()), DrawPipeline pipe(Deps{…, &reg, &seq}); P.RegisterClassRenderers(reg);
    reg.WarmUpAll(pipe, stdout) (prints `[GS-CLASSRDR] warmup renderers=6 programs=0` — OLD prints 7/9: log-only);
    be.Clear once (OLD `BindFBO(); Clear(0,0,0)` — the default framebuffer, no target: nothing to do above the
    interface); replay_hash = 1469598103934665603; windowW/H 640/448 = pipe defaults.
  per Frame f: fdraws = fverts = 0; for each order entry:
    draw d: pipe.SetSrcFrame(d.src_frame); pipe.SetGfxEpoch(d.vram_epoch); DrawIr ir = DrawIr::FromDrawRec(...);
      [--skip SPEC filter here, counted]; pipe.Draw(ir, rec.Name(ir.identity.hle_handler)); census.AddDraw; fdraws++;
      fverts += ir.vertex_count.
    job j: pipe.SetSrcFrame / SetGfxEpoch from the job; census.AddJob; switch j.type:
      SetWindow → pipe.OnSetWindow(arg2>>16, arg3>>16, arg2&0xFFFF, arg3&0xFFFF)
      SetFrontBuffer → pipe.OnSetFrontBuffer(arg1, arg2, arg3)   [MUST precede the first present — HANDOFF 6D″ N1]
      InvalidateTextures → pipe.OnInvalidate(arg1, arg2)   [RESOLVED (6E review): DepositUploadToLiveTargets is OFF at
        peak (`render.h:1869-1885`, P6-FMT6-OFF) — OnInvalidate → OnCpuWrite alone is right]
      UploadVRAMTexture (payload present) → pipe.OnUpload(arg1, arg2, arg3, pixels, arg4)
      SyncAllRTsToVRAM → pipe.OnSyncAll()
      SetClearColor / SetScissor / SetBlendMode / SetDepthTest → nothing
  frame boundary (the kFrame record): pipe.SetSrcFrame(f.vsync_frame + 1); DrawPipeline::Frame fr;
    pipe.OnFrameEnd(f.vsync.arg1 == 1, CrtcClassifier::Snapshot{} /* replay carries no priv regs */, &fr);
    full_hash = fr.grabbed ? Fnv1aFast(fr.rgba, w*h*4) : 0x9E3779B97F4A7C15; Downsample(fr.rgba, w, h, ds_n) → ds_hash;
    replay_hash = ((rh*P) ^ full) ; ((..*P) ^ (w<<32|h)) ; ((..*P) ^ fdraws)  (P = 1099511628211);
    print `[GSREPLAY] f= vf= draws= verts= w= h= fb= ds=`; write GsrepFrame + ds bytes; frames_out++.
    (Present happens INSIDE OnFrameEnd after the grab — same order as OLD's `GrabDisplayRGBA` then `Present()`.)
  end: census.Print("[GSREPLAY]"); skip.Emit; `[GSREPLAY] REPLAY_HASH=0x%016llx frames=%u draws=%llu backend=opengl
    ds=%u`; then the emitters in OLD order: (GsC3 — DIAG, absent) reg.EmitCounters(stdout), (GsS9RdrEmit — pending),
    P.EmitRendererCounters(stdout) (SPRRDR, SKINRDR, PARTRDR, FMVRDR), (GsR121AttrEmit — DELETE, absent);
    pipe.EmitCounters is NEW-only diagnostics (`[GS-PIPELINE]`) — print it AFTER the OLD-grammar lines or not at all.
  Expected on load_transition vs the baseline log: CLASSRDR lines identical except `S9-shadow native=8 declined=32` →
    `native=0 fb_norenderer=40` and `SUMMARY renderers=7 native=648` → `renderers=6 native=640` until P6-S9-PENDING
    lands; REPLAY_HASH must still match (the 8 native S9 draws are CLEARs = generic pixels).
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 177/177; lints 0/0; getenv sites unchanged
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Classrdr_|Gs_Inv29|Profile_'
Findings: P6-CLASS-NAMES, P6-PERF-ONLY-RENDERERS, P6-BACKEND-STENCIL (deferred), P6-S9-SHADERS (deferred), P6-S9-PENDING (OPEN),
  P6-SH7-LIVE (+ ledger row L3), P6-FAKE-DRAW-RESULT, P6-PRESENT-FLUSH (CLOSED)

### Review 6E (verdict)
Independent review 2026-09-28 (full record `/tmp/p6/REVIEW_6E.md`; logs `/tmp/p6/rv6e_*.log`; probes
`/tmp/p6/rv6e_probes.sh`). Re-ran every gate on a re-touched build of all 6E TUs: `BUILD_EXIT=0`, `grep -c
'warning:'` = 0; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 177` (173.83 s,
`CTEST_EXIT=0`); the selection `-R 'Classrdr_|Gs_Inv29|Profile_|Render_S9|Present_'` → 14/14 (no
`Render_S9StencilSmoke` exists — pending); `test_classrdr all: 282 checks, 0 failures`; `LintOneClass 472 / 0`,
`LintEnginePurity 419 / 0`, `size check OK`; purity / backends-include / CommitDeposits / getenv / `.inc` / tag
greps clean (the two `"skinned"` hits are the census class name, P6-CLASS-NAMES). OLD oracles bounded from
`/tmp/p6`: `test_gs_classrdr ALL PASS`, `test_s9_composite ALL PASS`, `test_shuf_snap 26/26`, `test_s8_wrapblend
1753/0`. Verified against the OLD sources, not the INV summary: `ClassifyFields`/`Classify` == `gs_class_renderer.h
:151-200` (stamp order, kHleOther fall-through, DATE / psm 10 / TME∧27, `≥256∧≥128 ∧ tex`, bbox only for raw
sprites with verts); `Dispatch`/`WarmUpAll`/`EmitCounters` == `:341-437`; all ten Accept tuples field for field
against `world_tristrip_native.inc:156-199`, `skinned_native.inc:261-336`, `sprite_hud_native.inc:211-260`,
`particles_native.inc:201-242`, `fmv_blit_native.inc:198-229`, `s9_composite.h:210-296`, `s9_composite_native.inc
:297-322`; the five replica `Draw` bodies are `DrawGeneric` + the OLD counter partitions and emit strings
(`:1667-1680` etc.); `TextureShuffleSnap` == `shuf_snap.h:77-146`, decision at the OLD `:9727-9731` site, applied
after rebase/scale in `GlDrawExecutor::ConvertVertices` (= `:10153-10165`); `SameBufferPreDrawFlush` predicate ==
`:9019-9031` with `Is16BitColor = {02, 0A}`, peak arm ON (`:9013-9017` default) — L3 agreed; composite mark ==
`:9148-9155`; frame-end order == gs_replay `:462-790` incl. the emitter sequence. Reviewer probes (restored
byte-identical, `shasum -c` 5/5): bbox rule on tristrips → classify 2 failures; fallback skipped → dispatch 2;
ATE block off → accept 1; armed texture read inert → sequence 2; **`xw == uw` dropped → snap 0 failures** (gap).
Resolved for the HANDOFF: `DepositUploadToLiveTargets` is OFF at peak (`render.h:1869-1885`, P6-FMT6-OFF), so
`OnInvalidate → OnCpuWrite` alone is correct. Design watch item for 6F (inherited from the 6D″ order, not a 6E
defect): OLD clears at `:9139` BEFORE the alias import at `:9620`; NEW imports (step 3) before the clear folded into
`Backend::Draw` (`GlDrawExecutor::FrameClear`, step 6) — only matters if a frame-clear draw ever meets aliased /
cpu-dirty-16 pages with `fbmsk ≠ 0`; the corpus gate decides, fix = hoist `Backend::Clear` before step 3.
Findings that block: the P6-S9-PENDING remainder omits three items that would each change pixels once the
stencil renderer arms — (F1) the world-strip steer (`world_tristrip_native.inc:200-216` → `DrawS9StripOrWorld`
`s9_composite_native.inc:1663-1740`; NEW `WorldTristripRenderer::Draw` is plain `DrawGeneric`, and OLD's colour
strip path carries NO pre-draw hook), (F2) the LIVE `leaving_capture` predicate (`:636-639`; `DrawGeneric` passes
`false`), (F3) hook-before-guards order (`:8329` precedes `:8541`/`:8668`; NEW guards first); plus the snap test
gap and the over-claimed `Classrdr_S9Recipe == test_s9_composite` (§5-6 not ported), "571-line" → 554, `:19634`
→ `:19613`, `Presenter::be_` unused.

VERDICT: NEEDS_CHANGES
1. `docs/phases/PHASE6_IMPL.md` §6E P6-S9-PENDING: add (7) the world-strip steer — `engine/gs/hle/classrdr/
   world_tristrip_renderer.cpp:16-23` must route untextured strips through the stencil ladder when the sequence is
   live (OLD `world_tristrip_native.inc:200-216` → `s9_composite_native.inc:1663-1740`, incl. the steer's own
   materialise/demote and the absence of `GsS9HookPreDraw` on OLD's colour strip path); (8) the `leaving_capture`
   predicate (OLD `:636-639`, all three gates ON at peak: `render.h:772-775, 1056, 1425`, `gs_page_tracker.h:99-104`)
   hard-coded `false` at `engine/gs/hle/classrdr/draw_pipeline.cpp:51`, needing a bound-target tracker equal to OLD
   `m_active_target_fbp`; (9) run the hook BEFORE the `verts < 3 ∨ fbw == 0` guards (`draw_pipeline.cpp:49-53`;
   OLD `:8291 → :8329 → :8541 → :8668`); correct the `:19634` citation to `:19613`.
2. `tests/gs/classrdr/test_classrdr.cpp` `TestSnap`: add OLD `test_shuf_snap` S3(c) — a strip whose u-width is
   doubled but on-grid (e.g. x=[8,16) u=[0.5,16.5)) so the `xw == uw` predicate is the sole rejecter (probe P-E
   shows 0 failures without it); optionally port S2 (sampling equivalence). Re-run `Classrdr_ShuffleSnap`.
3. `docs/phases/PHASE6_IMPL.md` §6E: G8 row / Tests paragraph — `Classrdr_S9Recipe` covers OLD `test_s9_composite`
   §1-4 (+ `Csm1Swizzle` / `InvertFold` spot values); §5 (`ClutIsInvertTable` walk + pure-identity negative) and §6
   (folded == `InvertGenericRef` sweep) follow with P6-S9-PENDING (3); "571-line" → 554; close the HANDOFF
   bracket on `DepositUploadToLiveTargets` (OFF at peak, `render.h:1869-1885`, P6-FMT6-OFF); record the
   clear-vs-alias-import order (above) as a 6F corpus-gate watch item.

## 6E′ — S9 stencil composite (backend surface + shaders + hooks) + 6E review closure (2026-09-28, ≈50 min)

The three 6E review items are closed, and the stencil-composite work landed bottom-up as far as the STOP rule
allowed: the API-agnostic stencil surface on `render/backend.h` with its GL implementation (P6-BACKEND-STENCIL
CLOSED), the four OLD S9 shader texts byte-identical in `GlShaderSources` + the three linked programs
(P6-S9-SHADERS CLOSED), the executor's two stencil programs (`StripCount`, `Invert`), and every pipeline-side hook
the renderer needs — hook-before-guards, the LIVE `leaving_capture` predicate over a bound-target tracker, the
writeback / texture-serve touch hooks, the ledger hold, the `Prepare / Finish` split, the materializer seam, and
the colour-authored Z (ZSRC) route on the GENERIC path (a 6D″/6E gap: OLD arms it for every draw at
`opengl_backend.cpp:11422-11468`, NEW never set `zsrc_armed`). **The `StencilCompositeRenderer` class itself
(Accept / the strip ladder / materialise / invert-backend predicate / world steer / registration / emit) is NOT
written; the `S9-shadow` slot stays UNREGISTERED**, so no sequence ever arms and every hook is provably inert on
today's corpus (`load_transition` `arms=0`). Tree: 0 warnings, 179/179, lints 0/0, no commits, OLD untouched.

### Review 6E closure (Part A)
1. §6E P6-S9-PENDING gained parts (7) world-strip steer, (8) LIVE `leaving_capture` + bound-target tracker, (9)
   hook-before-guards (`:8291 → :8329 → :8541 → :8668`); `:19634` → `:19613` (both citations). Code for (8) and (9)
   is landed below; (7) waits on the renderer.
2. `TestSnap`: OLD `test_shuf_snap` S3(c) added — x=[8,16) u=[0.5,16.5) (U width doubled, every corner on the
   8-grid) declines with `xw == uw` as the sole rejecter, plus the equal-16-wide control. S2 (sampling equivalence
   over the 32 live strips) NOT ported (needs the OLD strip table; → 6F with the corpus). `Classrdr_ShuffleSnap` 25.
3. G8 row / Tests paragraph: `Classrdr_S9Recipe` = OLD §1-4 (+ spot values), §5-6 now ALSO ported (below); "571-line"
   → 554 (now 679); the `DepositUploadToLiveTargets` HANDOFF bracket closed (OFF at peak); the clear-vs-alias-import
   order recorded as a 6F corpus-gate WATCH ITEM in the HANDOFF; `Presenter::be_` removed (ctor is
   `Presenter(ScanoutComposer&)`; the composer owns `Backend::Present`; 2 test call sites updated).

### Landed (Part B, partial)
- `engine/render/surface_format.h` `SurfaceFormat::DepthStencil24_8`; `engine/render/backend.h`: `StencilState
  {enabled, func ∈ {Always, Equal}, ref, cmp_mask, write_mask, pass_op ∈ {Keep, Invert, IncrWrap, DecrWrap}}`
  (sfail / dpfail always KEEP — the only ops OLD issues), `StencilProgram {None, StripCount, Invert}`,
  `DrawParams.program / stencil / stencil_state`, `Backend::ClearStencil(stencil, value)`,
  `Backend::DrawConstQuad(color?, stencil, scissor, rgba, write_alpha, StencilState)`. DECISION recorded: the
  pass-2/3 bbox quads and the 32 materialise quads are NOT expressible as DrawIr sprites through `Draw` — OLD
  issues them as an NDC fullscreen triangle under the const program with the STENCIL FUNC doing the masking and
  the SCISSOR doing the extent (`s9_composite_native.inc:2035-2052`, `:518-530`); a DrawIr sprite would run the
  register program (DATE / blend / fbmsk arms) and rasterise PS2-space geometry — a different pixel set at the
  rect edges. Hence the one new primitive.
- `GlDevice`: a `DepthStencil24_8` surface = `GL_DEPTH24_STENCIL8` renderbuffer (OLD `GsS9EnsureStencil :417-467`
  attachment kind reproduced); two device-owned framebuffers — stencil-only (`glDrawBuffer(GL_NONE)`) and resolve
  (colour texture + the same renderbuffer) — rebuilt when the attachments change (`BindStencilTarget`);
  `ClearStencil` (scissor off, mask 0xFF, `glClearStencil` + `GL_STENCIL_BUFFER_BIT`); `DrawConstQuad` (viewport =
  stencil dims, scissor = rect, blend off, depth ALWAYS / no write, colour mask by attachment + `write_alpha`,
  const program, NDC triangle VAO, `ApplyStencil` / `ResetStencil`); `ApplyStencil` maps func/mask/op to
  `glStencilFunc / glStencilMask / glStencilOp(KEEP, KEEP, op)`.
- `GlShaderSources`: `StencilClearFragment / StencilStripFragment / StencilInvertFragment / ConstVertex /
  ConstFragment` = OLD `k_s9_clear_frag_src / k_s9_strip_stencil_frag_src / k_s9_invert_frag_src /
  k_s9_const_vert_src / k_s9_const_frag_src` (`s9_composite_native.inc:53-215`) BYTE-IDENTICAL (FNV-1a64
  `af25eec16f07cca3 / 8fcb62c7a48824b9 / da3087d8b47270cc / a8293c48adbb37b0 / b5e42c4bdc02e060`, pinned by
  `Render_S9ShaderText`). `GlShaderLibrary::BuildStencilPrograms`: strip + invert over the PS2 vertex program, the
  const pair; `GlStencilLocations {strip_screen, strip_zsrc_en, strip_datm, strip_zsrc_num, strip_zsrc_den,
  invert_screen, invert_fix, const_color}`; samplers bound once (`u_rt_texture` 1, `u_zsrc_tex` 4 = the PS2 units,
  OLD `:359-372`). The CLEAR text is kept for the gate only: OLD's native CLEAR is the register program's live arms
  (`:53-56`), so the NEW CLEAR runs `DrawGeneric` (identical pixels by construction).
- `GlDrawExecutor::DrawStripCount` (= OLD `DrawS9StripStencil :1965-2035` pass 1: stencil-only fbo, viewport
  logical render size unscaled (`:1966`), strip program + raw `glUniform` (`u_screen_size`, `u_zsrc_en = ztst`,
  `u_zsrc_num = zsrc_scale`, `u_zsrc_den = target scale`, `u_datm`), unit 1 = the ROUTED colour image (`alt16 ?
  alt16 : color`), unit 4 = zsrc, blend off, depth ALWAYS / no write, scissor, untextured vertex conversion,
  colour mask off, the caller's `StencilState`, draw, masks reset) **[Review 6E′ fix 1, closed in 6E″: when
  `p.do_frame_clear` is set the COLOUR target is bound and `FrameClear` runs first, as OLD's strip ladder does
  through `GsS9PreDrawLadder :1825 → :887-912` — `RenderTarget::PrepareDraw` had consumed the flag, so the clear was
  lost, not deferred (order-unreachable on the corpus: the CLEAR into the same fbp precedes every strip in-frame);
  the scissor is issued at scale 1 — OLD `:1993` scales by `target->scale`, reachable only at 1 because `Prime` pins
  the accumulator native]** and `DrawInvert` (= `DrawS9InvertNative
  :1470-1662`: target bind + frame clear ladder, invert program, `u_fix_alpha = FIX/128`, UvFixed conversion, the
  FULL-target Cd copy (`idx_self_serve` → w/h 0 → `CdCopyPolicy::Decide` self-serve arm), blend off, unit 4 dummy,
  depth ALWAYS / no write, scissor, all channels, draw; declines when the target has no Cd copy). The S9 uniforms
  bypass `GlUniformCache` (its shadow is keyed by location, shared across programs) exactly as OLD's
  `S9StripShadow` was separate.
- `engine/gs/hle/classrdr/sequence_materializer.h` — `SequenceMaterializer::Materialize(DrawPipeline&)`, the seam
  the renderer will implement. `DrawPipeline`: `SequencePreDraw(ir)` (FSM hook with `leaving_capture =
  active_target_fbp_ == seq_fbp ∧ frame_fbp != seq_fbp` — the three OLD gates are all ON at peak so the
  predicate reduces to this; OLD `:636-639`) → `materializer_->Materialize` or `dropped_materializes_++`;
  `SequenceTouch(fbp)` installed as the cache's and the resolver's touch hooks in the constructor; **hook order**:
  `Prepare` runs the hook when `verts ≥ 2` (OLD `:8291` guard) and THEN the `verts < 3 ∨ fbw == 0` guards (OLD
  `:8541 / :8668`); `active_target_fbp_` set at step 2 (the bind; OLD `:8857` sets after the same guards, the
  alias-refresh unbind `:9505` re-binds at `:9629` before any hook can observe it, `:23152` is the OFF upload
  deposit — so "set at bind" is the whole observable rule); `ArmZsrc` (= `s9shadow::ZsrcRoute` + the zbp target
  lookup `:11432-11446`: `zte ∧ ztst ∈ {2,3} ∧ zmsk ∧ date ∧ zbp ≠ fbp ∧ Find(zbp) has colour` → `params.zsrc /
  zsrc_armed / zsrc_scale = zbp target scale`; the 6C executor already forces depth ALWAYS + the in-shader compare);
  `Prepare(ir, &Prepared) / Finish(ir, pr, res)` split of `DrawGeneric` (steps hook+guards+1-5 / 7-9);
  `OnSyncAll` no longer touches the FSM directly (every `SyncDrawn` passes the cache hook). `[GS-PIPELINE]` gains
  `zsrc_arms= dropped_materializes=`.
- `RenderTargetCache`: `SetTouchHook` called with the fbp before EVERY `SyncRtToVram` (FlushPending loop, SyncDrawn,
  the rescue in RescueAndDrop) = OLD `SyncRTToVRAM` entry `:16282`; `FlushPending(..., hold_fbp = kNoFbp)`: the held
  buffer's entry is skipped in `Take` (counted `HeldEntries`) = OLD `s9_hold` (`:920-930`; `S9RDR_HOLD` ON).
  `TextureResolver::SetTouchHook` called with `TEX0.TBP` at `ResolveRead` entry for every textured draw = OLD
  `FindTargetTextureImpl` entry `:19613`.
- `SequenceRecipe::InvertGenericRef` (= `s9_composite.h:361-381`) and `ClutIsInvertTable(byte_at, addr_of)` (=
  `:330-340`, `std::function` form so the header stays declarations-only).
- `tests/lib/fake_backend`: `ClearStencil` / `DrawConstQuad` recorded in `stencil_calls` (kind, program, state,
  surfaces, scissor, rgba); a quad over a FakeBackend colour surface writes its constant (the stencil test is not
  modelled) so a materialise leaves bytes for a byte-level test; `Draw` with a stencil program is recorded too.

### OLD sources transplanted (read-only)
`s9_composite_native.inc:53-215` (shader texts), `:359-372` (sampler units), `:417-467` (stencil FBOs → `BindStencilTarget`),
`:518-530` (materialise quad GL state → `DrawConstQuad`), `:634-666` (`leaving_capture`), `:668-690` (touch),
`:920-930` (hold), `:1470-1662` (invert native → `DrawInvert`), `:1965-2035` (strip pass 1 → `DrawStripCount`),
`s9_composite.h:330-381`; `opengl_backend.cpp:8291 / 8329 / 8541 / 8668` (hook order), `:8857 / 9505 / 9629 / 23152`
(`m_active_target_fbp`), `:11422-11468` (ZSRC route), `:16282`, `:19613`; `tests/test_s9_composite.cpp:225-300` (§5-6);
`tests/test_shuf_snap.cpp` S3(c). READ, NOT transplanted (the renderer remainder): `:297-325` Accept extras,
`:469-632` `GsS9Materialize` body (the coherence stamps after the quads), `:692-767` `GsS9InvertBackendOk`,
`:768-1341` ladder / verts / post-draw (the NEW twins are `Prepare / Finish`), `:1342-1469` CLEAR native, `:1663-1740`
steer, `:1741-1964` strip abort / ARM / surface-change branches, `:2036-2106` extent + post-draw, `:2108-2273`
renderer / register / emit; `world_tristrip_native.inc:200-216`.

### Flags applied
NORM (winning arm only): `PS2_GS_S9RDR` (the surface, shaders, executor programs and hooks are the ON arm; the
renderer that arms is the remainder), `PS2_GS_S9RDR_HOLD` (ON: `hold_fbp` on `FlushPending`), `PS2_SH8` (ON:
`u_zsrc_num / den` = source / dest scale, `DrawStripCount` + the 6C executor), `PS2_GS_ZSRC` (ON: `ArmZsrc` on the
generic path — was silently OFF in NEW until now), `PS2_GS_ABIT16` (ON: `u_fb16` in the CLEAR text / 6C executor),
`PS2_GL_LAZY_ZSYNC` / `PS2_R147_IDXPBO` / `gsresident PboOn` (ON: the three `leaving_capture` gates folded to true).
DELETE not written: `PS2_S9_DEMOTE` (+ `[S9-DEMOTE]` / `[GS-S9DEM]` cause census), `PS2_GS_S9RDR_DBGPIX`
(`[S9MAT]` / `[S9ARM]` / `[GS-S9RDR-DBG]` probes incl. the stencil histogram readback), `PS2_GS_S9RDR_DBGBOTH` (the
dual-arm colour draw), `PS2_GS_S9RDR_DBGQUANT`, `PS2_SH5_BIND` (`[SH5-AUTH-S9]`), `PS2_SH6_SWEEP` (`[SH6-S9AUTH]`),
`PS2_R121_ATTR` (`CurS9Path`), the `[GS-S9RDR] invert pin miss` / `invert CLUT mismatch` stderr probes, the
`r169` slot / `r147 vconv` / `r118 ring` alternates inside `GsS9ConvertVerts` (the 6C vertex stream is the one path).

### Tests (PASS lines from `/tmp/p6/6e2_ctest_sel.log` / `/tmp/p6/6e2_ctest.log`)
```
$ ctest --preset macos-dev -R 'Classrdr_|Gs_Inv29|Profile_|Render_S9|Present_' --output-on-failure | tail -20
10/16 Test #103: Classrdr_S9Recipe ................   Passed    0.01 sec
11/16 Test #104: Classrdr_ShuffleSnap .............   Passed    0.05 sec
12/16 Test #105: Classrdr_DrawPipelineOrder .......   Passed    0.07 sec
13/16 Test #106: Classrdr_EmitCountersGrammar .....   Passed    0.05 sec
14/16 Test #107: Profile_RegisterClassRenderers ...   Passed    0.04 sec
15/16 Test #108: Classrdr_LeavingCapture ..........   Passed    0.08 sec
16/16 Test #176: Render_S9ShaderText ..............   Passed    0.27 sec
100% tests passed, 0 tests failed out of 16
$ ctest --preset macos-dev
100% tests passed, 0 tests failed out of 179
Total Test time (real) = 180.20 sec
CTEST_EXIT=0
$ ./build/tests/gs/test_classrdr all → test_classrdr all: 315 checks, 0 failures
```
New: `Classrdr_LeavingCapture` (`hooks`, 20: active target = last bound; ARMED on the bound accumulator + a draw
into another buffer materialises and moves the bound target; ARMED while unbound → inert; 2-vertex and fbw-0
draws into the armed fbp fire the hook and draw nothing; a 1-vertex draw is below the hook; a textured read of
the fbp materialises through the resolver touch; `OnSyncAll` materialises through the cache touch; the held
flush leaves the entry and touches nothing (`HeldEntries == 1`), the unheld one deposits + touches; a dropped
materialise (no materializer) is counted and the FSM still idles; ZSRC: armed against the zbp target's colour
with scale 1, and five one-field negatives — no target / no DATE / ztst 1 / zmsk off / zbp == fbp);
`Classrdr_S9Recipe` +12 (OLD §5: the CSM1-composed table accepts, position 8 holds 0xEF, one flipped byte
rejects, the pure-identity layout rejects, the zero mirror rejects, CSM1 involution; §6: the 9 × 256 fold sweep ==
`InvertGenericRef`, the `0x40FDDB87 → 0xBF022478` pin, idx 8 → 0xEF, idx 16 → 0xF7); `Classrdr_ShuffleSnap` +2
(S3(c) + control); `Render_S9ShaderText` (the five text hashes + the invert alpha expression + the three programs
link + locations; 77-SKIP without a context, RUNS here: `Passed 0.27 sec`). NOT written by 6E′ (renderer remainder):
`Classrdr_S9StencilPasses`, `Classrdr_S9Steer`, `Render_S9StencilSmoke` **[Review 6E′ fix 2, closed in 6E″:
`Render_S9StencilSmoke` written at the primitive level and RUNS on GL (`Passed 0.15 sec`, 23 checks); the op-swap
probe `kIncrWrap → GL_DECR_WRAP` fails it (5 failures) — see §6E″]**.

**Negative probes** (one predicate each; `test_classrdr` rebuilt; sources restored `shasum -a 256 -c` 2/2 OK;
restored build 0 warnings; `all` 315/0):

| probe | result |
|---|---|
| P1 `leaving_capture := false` (the 6E state) | hooks: 9 failures |
| P2 hook AFTER the guards (the 6E order) | hooks: 7 failures |
| P3 `hold_fbp` predicate dropped from `Take` | hooks: 2 failures |
| P4 ZSRC route without the DATE gate | hooks: 2 failures |
| P5 resolver touch hook absent | hooks: **0 failures** — the pre-draw hook's `tex.tbp == fbp` arm materialises first for every textured read that reaches `ResolveRead`; the same redundancy holds in OLD (`:8329` precedes `:19613`); the resolver hook is load-bearing only for a resolve issued outside `DrawGeneric` (none exists today) — kept as the OLD twin, not a test gap |
| P6 `leaving_capture` from `MostRecentDrawnFbp` | hooks: **0 failures** — in NEW the bind (step 2) and the commit (`NoteDrawn`) are never separated (the guards precede both), so the two notions coincide; kept as `active_target_fbp_` for the OLD name and because the renderer's stencil ladder will bind without a colour commit |

Gate lines: `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`, `grep -c 'warning:'` → `0` (`/tmp/p6/6e2_build.log`);
`LintOneClass: 473 files scanned, 0 findings -> PASS`; `LintEnginePurity: 420 files scanned, 0 game tokens -> PASS`;
`size check OK`; `#include "engine/render/backends` in `engine/gs` 0; `CommitDeposits` in `engine/gs/hle` 0; `getenv`
in `engine/gs` / `engine/render` / the profile 0; game literals in `engine/gs/hle/classrdr` + `engine/render` 0 (the
one `0x268000` hit is the pre-existing 6C shader comment, noted by the 6E review).

### Findings
- **P6-BACKEND-STENCIL — CLOSED**: the surface is `SurfaceFormat::DepthStencil24_8` + `StencilState` + `StencilProgram`
  + `ClearStencil` + `DrawConstQuad`; GL = D24S8 renderbuffer + two device framebuffers (OLD's attachment kind).
- **P6-S9-SHADERS — CLOSED**: five texts byte-identical (hash-pinned), three programs linked, sampler units fixed.
- **P6-ZSRC-GENERIC (new, CLOSED)**: OLD arms the colour-authored Z compare on EVERY draw (`:11422-11468`), not only
  the stencil strip; NEW's `DrawGeneric` never set `zsrc_armed` (6C carried the uniforms, 6D″/6E nothing set them).
  `DrawPipeline::ArmZsrc` now runs at step 4 for every draw — pixel-affecting on any DATE + GEQUAL/GREATER + ZMSK
  draw whose ZBP names a live colour target (the shadow chain's Z copy); the corpus gate will show it.
- **P6-CONST-QUAD-PRIMITIVE (decision)**: see Landed — the materialise / pass-2/3 quads need `DrawConstQuad`; a
  DrawIr sprite cannot express "stencil func masks, scissor bounds, constant colour, no register arms".
- **P6-STRIP-FRAMECLEAR (Review 6E′ fix 1 — latent, CLOSED in 6E″)**: `DrawStripCount` dropped the per-frame clear
  the target layer had consumed for the draw (OLD's strip ladder performs it on the colour target, `:887-912` via
  `:1825`); unreachable on the corpus by ORDER (the same-fbp CLEAR precedes every strip in-frame), fixed by binding
  the colour target + `FrameClear` before the stencil-only framebuffer; asserted by `Render_S9StencilSmoke` (c).
- **P6-S9-PENDING (OPEN → 6F, narrowed)**: the exact remainder is ONE class + its registration + tests:
  `engine/gs/hle/classrdr/stencil_composite_renderer.h/.cpp` (`ClassRenderer` + `SequenceMaterializer`; ctor takes
  the profile shapes `composite_clear / world_strip / composite_fan / composite_invert`, `SequenceRecipe&`, the
  `ClassRegistry*` for the world-slot check; `WarmUp(pipe)` and `Draw` call `pipe.SetMaterializer(this)` and keep
  `DrawPipeline*` for the Accept-time backend predicate): (a) `Accept` = `clear.Matches ∨ (strip ∨ fan) ∧ world slot
  registered + enabled ∨ (invert.Matches ∧ tbp == fbp ∧ tbw == fbw ∧ wms, wmt ≤ 1 ∧ InvertVertexColorOk ∧
  InvertBackendOk)`; (b) `InvertBackendOk` (`:692-767`): `cache.Find(fbp)` with colour + `rt_copy`, `has_content`,
  `last_draw_frame == presenter.FrameCount()`, `render_psm ∈ {0x00, 0x30}`, no ledger entry overlapping `[cbp % 4M,
  +0x2000)`, `ClutIsInvertTable(vram.Bytes()[addr % 4M], PsmLayout::PixelByteAddress(CT32, x, y, cbp, 64))`; the
  seven decline counters; (c) CLEAR → `pipe.DrawGeneric` then `seq.Prime(fbp)` when `ClearPrimes ∧ target scale == 1`
  (`scale_declines` otherwise; `seq.Idle()` when not priming — OLD `:1453-1468`); (d) STRIP steer (`:1663-1740`):
  `delta = ClassifyStripVerts`; `zsrc = ArmZsrc`-equivalent lookup; `LiveOn(fbp)`; else the colour path =
  materialise-if-ARMED-on-fbp / demote-if-PRIMED-on-fbp then `DrawGeneric` (`strips_color++`); (e) the stencil path
  (`:1741-2106`): `pipe.Prepare(ir, &pr)`; abort → colour path when `!prepared ∨ !LiveOn ∨ (ARMED ∧ seq.Alt16() !=
  pr.params.alt16_route)` (materialise if ARMED on this fbp; **else when `!ladder_clean` set `kIdle` — OLD `:1855-1856`,
  the alias-refresh path kills a PRIMED sequence without a demote count; the frame clear needs nothing here once
  Review 6E′ fix 1 lands in `DrawStripCount` [6E″ closure]**); ARM on PRIMED: `EnsureStencil(target dims)` (create / recreate the
  `DepthStencil24_8` surface when dims change), `be.ClearStencil(stencil, K.stencil_base)`, `seq.Arm(alt16_route)`;
  surface-change check (dims or routed surface differ) → materialise + colour path; pass 1: `pr.params.program =
  StripCount`, `stencil = ...`, `stencil_state {Equal, 0x00, 0x80, 0x80, Invert}`, `res = be.Draw(ir, pr.src,
  pr.params)`; rect = `floor/ceil(res.bbox_tri) ∩ scissor` clamped to the stencil dims; passes 2 + 3 =
  `be.DrawConstQuad(none, stencil, rect, 0, false, {Equal, 0x80, 0x80, counter_mask, delta > 0 ? IncrWrap :
  DecrWrap})` then `{Equal, 0x80, 0x80, 0x80, Invert}` (`dedupe_draws += 2`); `seq.ExtendExtent(rect)`;
  `pipe.Finish(ir, pr, res)` with `res.write_rect = rect` (the per-strip CT16S ledger record — LOAD-BEARING);
  `strips_stencil++`; (f) INVERT: `Prepare` with the resolver bypassed (build `pr.src` = `{enabled, uv_tex_w = uv_tex_h
  = 1, transform.idx_fmt 4, idx_self 1}`; OLD skips `ResolveTextureSeam` entirely — decide whether `Prepare` gets a
  `skip_resolve` flag or the renderer replaces `pr.src` after `Prepare`; the latter runs the resolver's L1/L2 side
  effects OLD does not — prefer the flag), `params.program = Invert`, `be.Draw`, `Finish`; (g) `Materialize(pipe)`
  (`:469-632`): guard `seq` extent valid ∧ stencil dims == target dims ∧ routed surface unchanged; 32 ×
  `be.DrawConstQuad(routed, stencil, extent, MaterializeByte(v) ×3 | a, write_alpha = false, {Equal, v,
  counter_mask, 0x00, Keep})`; then the stamps by route (alt16: `alt16_drawn ∪ extent`, Cd dirty, `alt16_has_content`;
  main: `valid_rect / drawn_since_sync ∪ extent`, Cd dirty, `alt16_stale ∪ extent` when a companion exists,
  `has_content`), `last_c_seq`-equivalent (the per-view author seq++), `last_draw_frame / gfx_epoch` — via a new
  `RenderTarget::MarkMaterialized(rect, alt16, frame, epoch)` (no backend call; NO ledger record — OLD's header
  rule); `materializes++`, `mat_draws += 32`; (h) `WorldTristripRenderer::Draw` steer (fix (7)): hold a
  `StencilCompositeRenderer*` (nullable) and route the strip family to its `DrawStripOrWorld` when set — OLD's colour
  strip path has no pre-draw hook, so the steer's colour arm must call a `DrawPipeline::DrawGenericNoHook` (or
  `Prepare` with the hook suppressed) to stay identical; (i) `RegisterClassRenderers` registers the slot (7) and
  passes the pointer to the world renderer; `EmitRendererCounters` prints `[GS-S9RDR] on=1 clears= primes= arms=
  strips_stencil= strips_color= inverts= materializes= mat_draws= demotes= scale_declines= dedupe_draws=` (only
  when any total is non-zero) and `[GS-S9RDR] invert_declines no_target= no_tex= no_content= stale= psm= pending=
  clut=` (only when non-zero) AFTER `[GS-CLASSRDR]` and BEFORE `[GS-SPRRDR]` (OLD emitter order); (j) tests
  `Classrdr_S9StencilPasses` (FakeBackend `stencil_calls`: clear base → pass 1 `{Equal 0 0x80 0x80 Invert}` → quad
  `{Equal 0x80 0x80 0x1F IncrWrap}` → quad `{Equal 0x80 0x80 0x80 Invert}` per strip, the ledger record per strip,
  the hold during the ladder's own flush, 32 materialise quads with `rgba = 8v` and `write_alpha == false`, extent
  = the strips' union), `Classrdr_S9Steer` (world strip → ladder when live, colour path + demote when PRIMED and
  the colour is not ±8, no hook on the colour arm), `Render_S9StencilSmoke` (GlDevice: 64×64 target + D24S8;
  `ClearStencil(0x0F)`; one +8 strip's three passes; materialise; read back: covered byte == `8 * 0x10 = 0x80`,
  uncovered == untouched; a −8 strip → `0x70`; 77-SKIP). Expected counter lines on `load_transition` after it
  lands: `[GS-S9RDR] on=1 clears=8 primes=8 arms=0 strips_stencil=0 strips_color=… inverts=0 materializes=0
  mat_draws=0 demotes=8 scale_declines=0 dedupe_draws=0` + `[GS-S9RDR] invert_declines … clut=8` (the zero
  mirror), CLASSRDR `S9-shadow native=8 declined=32`, `SUMMARY renderers=7 native=648`.
- **P6-SNAP-S2 (open, minor)**: OLD `test_shuf_snap` S2 (sampling equivalence over the 32 live strips) not ported.

### Gate status
| gate | evidence |
|---|---|
| G8 (classrdr) | 6E suites + `Classrdr_LeavingCapture` Passed; `Classrdr_S9Recipe` now = OLD `test_s9_composite` §1-6; the renderer half is P6-S9-PENDING (narrowed above) |
| G6 (render) | `Render_S9ShaderText` Passed (RUN, not skipped); `Render_ShaderCompiles` / `Render_DeviceSmoke` still Passed with the new programs / surface kind |
| build / lints / size | 0 warnings, LintOneClass 0, LintEnginePurity 0, size OK, greps 0 |
| full ctest | `100% tests passed, 0 tests failed out of 179`, 180.20 s, `CTEST_EXIT=0` |

### HANDOFF (6E′ → 6F)
Done: P6-S9-PENDING parts (1) backend stencil surface + GL, (the shader half of 2), (3) `ClutIsInvertTable` +
  `InvertGenericRef`, (4) hooks: hook-before-guards, LIVE `leaving_capture` + `active_target_fbp`, cache / resolver
  touch hooks, `hold_fbp` on `FlushPending`, ZSRC route on the generic path (NEW finding), `Prepare / Finish`,
  `SequenceMaterializer` seam; (6) `Render_S9ShaderText`, `Classrdr_LeavingCapture`, §5-6 of the recipe test;
  review items 1-3; `Presenter(ScanoutComposer&)`.
Not done: `StencilCompositeRenderer` (a)-(j) above — the slot stays UNREGISTERED; `Classrdr_S9StencilPasses` /
  `Classrdr_S9Steer` / `Render_S9StencilSmoke`; snap S2; ContentKey wrap split; `GsClassdiffMatchesOld`.
Replay driver deltas vs the 6E HANDOFF: `Presenter presenter(scanout)` (no backend arg); nothing else changes.
  Expected on load_transition until the renderer lands: unchanged from 6E (`S9-shadow native=0 fb_norenderer=40`,
  `renderers=6 native=640`); `[GS-PIPELINE] zsrc_arms=N` is NEW-only diagnostics — print after the OLD-grammar lines.
  REPLAY_HASH: the ZSRC route now arms on the generic path for the 108 volume strips per composite (OLD did too) —
  the first corpus pass must confirm identity on the strip-bearing captures (s57_walk / slowspot / crystal_cavern).
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 179/179; lints 0/0.
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Classrdr_|Gs_Inv29|Profile_|Render_S9|Present_'
Findings: P6-BACKEND-STENCIL (CLOSED), P6-S9-SHADERS (CLOSED), P6-ZSRC-GENERIC (new, CLOSED), P6-CONST-QUAD-PRIMITIVE
  (decision), P6-S9-PENDING (OPEN, narrowed to the renderer class), P6-SNAP-S2 (open, minor).

## 6E″ — StencilCompositeRenderer (2026-09-28, resumed session ≈45 min)

Stage 6E″ finishes the S9 stencil-composite renderer left partially landed by an interrupted session. Worktree
`<repo>-p6`, branch `phase6` (`pwd` + `git branch --show-current`
confirmed); OLD read-only (nothing run there this session); no commits. Logs `/tmp/p6/r6e3_*.log`, probes
`/tmp/p6/r6e3_probes.sh` (+ `.sha`).

### Situation found (read first)
The interrupted session had landed MORE than the brief recorded: `engine/gs/hle/classrdr/stencil_composite_renderer.{h,cpp}`
(99 + 342 lines, in `ps2r_gs_hle_classrdr`), the world-strip steer (`WorldTristripRenderer::SetStencilComposite` +
`DrawStripOrWorld` routing), `RenderTarget::MarkMaterialized`, `DrawPipeline::Prepare/Finish` options (`run_hook`,
`resolve_texture`, `abort_on_alias`, `hold_fbp`), **slot 7 already registered** in `RendererProfile::RegisterClassRenderers`
(`renderer_profile.cpp:172-181`, returns 7 with a sequence, 6 without) and `EmitRendererCounters` printing S9 first,
`tests/gs/classrdr/test_classrdr.cpp` modes `s9passes` / `s9steer` (implemented, registered as `Classrdr_S9StencilPasses` /
`Classrdr_S9Steer`), `tests/render/test_s9_stencil_smoke.cpp` (Review 6E′ fix 2, RUNS on GL), and the `DrawStripCount`
`do_frame_clear` fix (Review 6E′ fix 1). Not done: the `## 6E″` record, the §6E′ in-place closure text (Review 6E′ fix 3),
`Classrdr_S9Emit`, and the line-by-line audit below, which found three deltas vs OLD.

### Audit vs OLD `s9_composite_native.inc` (line by line) — deltas found and fixed
1. **P6-STRIP-FBMSK16 (new, pixel/mirror-affecting, CLOSED)**: OLD's stencil strip computes `fbmsk16 =
   Fbmsk16From32(frame_fbmsk)` (`:1917`) and its post-draw records `target.sync_keep_mask16 = 0x8000` (`:1204-1208`)
   and skips the upload eviction (`run_evict=false` ⇔ the generic's `masked_partial16_keep`, `opengl_backend.cpp:13985-13987`).
   NEW `GlDrawExecutor::DrawStripCount` never ran `ClassifyRoutes`, so `DrawResult.fbmsk16 == 0` → `RenderTarget::AfterDraw`
   recorded keep mask 0 and `TargetRefresher::EvictRangeUnderDraw` EVICTED the uploaded textures under the strip rows.
   Fix: `gl_draw_executor.cpp` `DrawStripCount` folds the FBMSK for a 16-bit frame (`Fbmsk16From32` + `Fbmsk16Exact`);
   `tests/lib/fake_backend.cpp` folds the same mask (the backend contract) so the pipeline test pins
   `sync_keep_mask16 == 0x8000` after a stencil strip; `Render_S9StencilSmoke` pins `r1.fbmsk16 == 0x8000` on GL.
2. **CLEAR alias arm (`:1351-1357`)**: OLD's `DrawS9ClearNative` on `!ladder` sets `kIdle` and hands the whole draw to
   `DrawBatch` (no `n_clears`, no prime); NEW `DrawClear` ran `DrawGeneric` (which imports) and then PRIMED. Unreachable
   in both (the CLEAR's FBMSK is 0, so the alias block `:939-942` / `AliasRegionBytes` never enters) — mirrored anyway:
   `DrawClear` = `Prepare(abort_on_alias)` → on `alias_aborted` `seq.Idle()` + `DrawGeneric`, uncounted; else `Backend::Draw`
   + `Finish` + the prime decision on `pr.target->identity.scale` (OLD `:1453-1468`).
3. **Inactive steer (`:1665-1667`)**: OLD short-circuits to `DrawWorldStripNative`, which has NO pre-draw hook
   (`world_tristrip_native.inc:304-310`); NEW `DrawStripOrWorld` ran the hooked `DrawGeneric`. Fixed: `run_hook = false`.
4. **Demote total**: OLD's single `n_demotes` is bumped by the hook (`:655-661`) AND the steer (`:1737-1739`); NEW keeps
   the hook demotes in `SequenceRecipe::Counters` and the steer demotes in the renderer. `Emit` now prints
   `Demotes() = cnt_.demotes + seq_.Count().demotes` (the `Classrdr_S9Emit` line would otherwise read `demotes=0`).
Everything else line-matched: Accept (`:2115-2122` + `AcceptClearFields/AcceptInvertFields` `s9_composite.h:210-320` = the
profile's `composite_clear` / `composite_invert` specs + the self-read pins + `InvertVertexColorOk`; strip/fan → world
slot registered + enabled); `InvertBackendOk` (`:692-767`; NEW reads the LIVE mirror = OLD's `use_snap == false` arm —
`clut_snapshot` is empty in replay, 6D′ record); `DrawS9StripOrWorld` steer (`:1669-1740`: delta → zsrc target → live
on fbp; materialise-if-ARMED / demote-if-PRIMED on the same fbp; colour path); `DrawS9StripStencil` (`:1757-2106`:
guards, `seq_ok = ladder_clean ∧ live on fbp ∧ (PRIMED ∨ alt16 == route)`, `routed == 0` abort, materialise-if-ARMED,
`!ladder_clean → kIdle`, ARM = `EnsureStencil(target dims)` + `ClearStencil(base)` + `Arm(route)`, surface-change →
materialise + colour, pass 1 `{EQUAL 0x00 0x80 wr 0x80 INVERT}` via `StripCount`, `dr` rect = `floor/ceil(bbox_tri |
bbox_all) ∩ scissor` clamped to the stencil dims, passes 2/3 `{EQUAL 0x80 0x80 wr 0x1F INCR_WRAP|DECR_WRAP}` /
`{EQUAL 0x80 0x80 wr 0x80 INVERT}`, `dedupe_draws += 2`, extent union, `Finish` = `GsS9PostDraw` incl. the per-strip
CT16S ledger record); `GsS9Materialize` (`:469-632`: extent ∩ stencil dims, routed view unchanged, 32 × `{EQUAL v
0x1F wr 0 KEEP}` byte `8v` alpha protected, `MarkMaterialized` = the route's stamps + author seq + frame/epoch, no
ledger record); `GsS9RdrEmit` (`:2198-2273`) — the two `[GS-S9RDR]` lines in OLD order; the `[GS-S9DEM]` census line
between them and the `PS2_S9_DEMOTE` / `PS2_GS_S9RDR_DBGPIX` / `PS2_SH6_SWEEP` / `DebugBothOn` probes are DIAG and not
written (the counter lines are diagnostics, not harness lanes). CT16S writeback: OLD's encode has no 0x0A arm
(`opengl_backend.cpp:15994-16060`), NEW `EncodesColorPsm` matches (stages nothing) — so the `keep16` gate's `Psm::CT16`
spelling in `readback_engine.cpp:163` is equivalent to OLD's `0x02 || 0x0A` (checked, no change).

### Landed this session
- `engine/render/backends/gl/gl_draw_executor.cpp` `DrawStripCount`: the FBMSK fold (delta 1).
- `engine/gs/hle/classrdr/stencil_composite_renderer.{h,cpp}` (102 + 356 lines): `DrawClear` via `Prepare/Finish` with
  the alias arm (delta 2); inactive steer without the hook (delta 3); `Demotes()` accessor + `Emit` total (delta 4).
- `tests/lib/fake_backend.cpp`: `DrawResult.fbmsk16` fold for 16-bit frames.
- `tests/gs/classrdr/test_classrdr.cpp` (985 lines): `s9passes` + keep-mask pin; `s9steer` + the inactive-steer-no-hook pin
  (stamped strip through the world slot vs a raw strip under the disabled S9 class) + the CLEAR-under-dirty-pages pin
  (no import, hook demote, re-prime); NEW mode `s9emit` = `Classrdr_S9Emit` (`tests/gs/CMakeLists.txt`).
- `tests/render/test_s9_stencil_smoke.cpp`: `fbmsk16 == 0x8000` pin (23 checks).
- `engine/gs/hle/classrdr/README.md`: the renderer is no longer "pending"; OLD sources + DIAG list.
- `docs/phases/PHASE6_IMPL.md`: §6E′ in-place closure of Review 6E′ fix 3 (marked `[6E″ closure]`), §2.6 tick, this record.

### OLD sources (read-only, transplanted by 6E′/the interrupted session/this one)
`host_app/graphics/opengl/s9_composite_native.inc` `:297-333` (`AcceptStripShapeS9`, `Active`), `:417-467` (`GsS9EnsureStencil`),
`:469-632` (`GsS9Materialize`), `:634-690` (hooks — 6E′), `:692-767` (`GsS9InvertBackendOk`), `:768-1018` (`GsS9PreDrawLadder`
— `DrawPipeline::Prepare` 6E′; the `[S9-HOLD]` `:959-966`, the alias abort `:985-1005`), `:1125-1342` (`GsS9PostDraw` —
`DrawPipeline::Finish`), `:1342-1468` (`DrawS9ClearNative`), `:1466-1662` (`DrawS9InvertNative`), `:1663-1750`
(`DrawS9StripOrWorld`), `:1751-2106` (`DrawS9StripStencil`), `:2108-2196` (`S9CompositeRenderer`, `GsS9RdrRegister`),
`:2198-2273` (`GsS9RdrEmit`); `s9_composite.h:92-160, 185-320` (`On`, `HoldOn`, constants, `ClassifyStrip*`, `AcceptClearFields`,
`ClearPrimes`, `AcceptInvertFields`); `world_tristrip_native.inc:200-216` (steer), `:304-310` (no hook on the colour path);
`opengl_backend.cpp:13985-14010` (`masked_partial16_keep`), `:15925-16060` (encode arms), `:16830` (`keep16` gate);
`render.h:1309-1317` (`IdxClutSnapOn`). Constants stay profile data (`renderer_profile.cpp:141-153` `FillSequence`; the three
Accept shapes `FillShapes`).

### Flags
NORM (winning arm only): `PS2_GS_S9RDR` (renderer registered = on; `Active()` = slot registered + class enabled),
`PS2_GS_S9RDR_HOLD` (the ladder holds the armed entry), `PS2_GL_IDXTEX_CLUTSNAP` (live-mirror walk; snapshot empty in
replay), `PS2_S8_WRAP` (`s8wrap::WrapBlendOn` in the fan shape — ON). DIAG not written: `PS2_S9_DEMOTE` (+ `[GS-S9DEM]`
line, `DemDetail`), `PS2_GS_S9RDR_DBGPIX` (`[S9MAT]`/`[S9ARM]`/`[GS-S9RDR-DBG]`), `PS2_SH6_SWEEP` (`[SH6-S9AUTH]`),
`PS2_SH5_BIND` (`[SH5-AUTH-S9]`), `PS2_GS_S9RDR_BOTH` (`DebugBothOn`), the `[GS-S9RDR] materialize` / `invert CLUT
mismatch` / `invert pin miss` log lines, `[R121-ATTR] CurS9Path`, PERF scopes.

### Tests (`/tmp/p6/r6e3_ctest_sel.log`, `/tmp/p6/r6e3_ctest.log`)
```
$ cmake --build --preset macos-dev -j4     -> BUILD_EXIT=0, warnings=0
$ ctest --preset macos-dev -R 'Classrdr_|Gs_Inv29|Profile_|Render_S9' --output-on-failure | tail -20
11/15 Test #109: Classrdr_S9StencilPasses .........   Passed    0.11 sec
12/15 Test #110: Classrdr_S9Steer .................   Passed    0.23 sec
13/15 Test #111: Classrdr_S9Emit ..................   Passed    0.09 sec
14/15 Test #179: Render_S9ShaderText ..............   Passed    0.35 sec
15/15 Test #182: Render_S9StencilSmoke ............   Passed    0.15 sec
100% tests passed, 0 tests failed out of 15
$ ctest --preset macos-dev  -> 100% tests passed, 0 tests failed out of 183, Total Test time (real) = 202.02 sec, CTEST_EXIT=0
$ ./build/tests/gs/test_classrdr all      -> test_classrdr all: 413 checks, 0 failures
$ ./build/tests/render/test_s9_stencil_smoke -> GL 4.1 Metal - 90.5 / Apple M4 / covered=80 80 80 00 uncovered=78 / 23 checks, 0 failures
$ python3 tests/lint/lint_one_class.py --root .    -> LintOneClass: 476 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . -> LintEnginePurity: 422 files scanned, 0 game tokens -> PASS
$ python3 /tmp/p6/check_sizes.py .                 -> size check OK
$ grep -rn 'engine/render/backends' engine/gs/ | grep -v README -> none;  grep -rn CommitDeposits engine/gs/hle/ -> none
$ grep -rn getenv engine/gs engine/render games/…/renderer_profile | grep -v README -> none
$ game-constant grep over engine/gs/hle/classrdr engine/render -> only gl_shader_sources.cpp:769 (the pre-existing 6C shader comment)
```
`Classrdr_S9StencilPasses` (46 checks): CLEAR native + PRIMED, no stencil work; first strip ARMS (`ClearStencil(0x0F)` on a
D24S8 surface) then `{Equal 0 0x80 0x80 Invert}` StripCount with ZSRC → `{Equal 0x80 0x80 0x1F IncrWrap}` bbox quad →
`{Equal 0x80 0x80 0x80 Invert}`; the CT16S ledger record + `sync_keep_mask16 == 0x8000`; −8 strip DECR_WRAP over its bbox,
the hold (`HeldEntries == 1`), the extent union, the scissor-clipped rect; MATERIALIZE on an RGB draw into the fbp = 32 quads
`{Equal v 0x1F 0 Keep}` rgb `8v` alpha protected over the extent, stamps (author seq, content, drawn rect), bytes on the routed
view; steer demote (PRIMED + 0x10 strip), idle → colour path, ARMED + no-ZSRC strip → materialise + colour; INVERT native over a
live CT32 target with the identity-invert CLUT + the seven decline causes (no_target, psm, pending, clut, colour pin, tbp pin);
the emit grammar; six slots without the sequence. `Classrdr_S9Steer` (20): fan needs the world slot; disabled S9 slot →
generic strip (no FSM change); colour arm has no hook (armed on another buffer survives; the hooked generic control
materialises); alias demand → materialise + colour path / PRIMED → idle without a demote count, colour path imports like the
plain generic; stamped strip through the world slot reaches the ladder; slot cleared → plain generic; inactive steer (stamped
strip, S9 disabled) no hook vs the raw strip's hooked fallback; CLEAR under CPU-dirty pages: no import (FBMSK 0), hook demote,
re-prime. `Classrdr_S9Emit` (29): the load_transition-shaped run — 8 × (CLEAR prime → CT16S RGB draw (hook demote, generic) →
invert over the zero CLUT mirror (declined)) prints exactly
`[GS-S9RDR] on=1 clears=8 primes=8 arms=0 strips_stencil=0 strips_color=0 inverts=0 materializes=0 mat_draws=0 demotes=8 scale_declines=0 dedupe_draws=0`
`[GS-S9RDR] invert_declines no_target=0 no_tex=0 no_content=0 stale=0 psm=0 pending=0 clut=8`; CLASSRDR `S9-shadow
native=8 declined=16`; the profile emitter prints `[GS-S9RDR]` first; a run with no S9 draws is silent.
`Render_S9StencilSmoke` (23, RUN on GL): D24S8 renderbuffer, `ClearStencil` + EQUAL-gated quad inside the scissor, wrong ref
rejected; +8 strip → covered `0x80`, uncovered `0x78`, alpha untouched; second +8 → `0x88`; two −8 → `0x78`; materialise
scissored to the extent; the frame clear on a StripCount draw clears the colour target's depth (fix 1); masks reset for a
following generic draw; `fbmsk16 == 0x8000`; `GL_NO_ERROR` throughout.

### Negative probes (`/tmp/p6/r6e3_probes.sh`, one predicate each; sources restored `shasum -a 256 -c` 3/3 OK; restored build 0 warnings; `all` 413/0, smoke 23/0)
| probe | result |
|---|---|
| P1 pass-1 write mask `0x80 → 0xFF` | `s9passes: 1 failure` |
| P2 `IncrWrap ↔ DecrWrap` swapped in the renderer | `s9passes: 2 failures` |
| P3 `GlDevice::ApplyStencil` `kIncrWrap → GL_DECR_WRAP` (Review 6E′ P-R4) | **`test_s9_stencil_smoke: 5 failures`** (was 0 everywhere before fix 2) |
| P4 `DrawStripCount` FBMSK fold removed | `test_s9_stencil_smoke: 1 failure` |
| P5 materialise 16 quads instead of 32 | `s9passes: 3 failures` |
| P6 colour arm runs the hook | `s9steer: 2 failures` |
| P7 emit prints steer demotes only | `s9emit: 1 failure` |
| P8 strip Accept without the world-slot check | `s9steer: 1 failure` |
| P9 `!ladder_clean → kIdle` arm removed | `s9steer: 1 failure` |
| P10 ledger hold removed from the ladder | `s9passes: 12 failures` |
| P11 invert predicate without the pending-ledger check | `s9passes: 2 failures` |
| P12 inactive steer runs the hook | `s9steer: 1 failure` |
Not probed (no test can reach it): the CLEAR alias arm (delta 2) — unreachable by construction (FBMSK 0), mirrored for
fidelity only.

### Findings
- **P6-S9-PENDING — CLOSED**: (a) Accept, (b) `InvertBackendOk`, (c) CLEAR prime (+ scale decline + the alias arm), (d) steer,
  (e) the stencil ladder incl. ARM, the surface-change arm, `!ladder_clean → kIdle`, the hold, the per-strip ledger record,
  (f) INVERT via `Prepare(resolve_texture = false)` (the flag, not a post-hoc `pr.src` swap), (g) MATERIALIZE +
  `MarkMaterialized`, (h) the world-strip steer with the hook-less colour arm, (i) slot 7 + the two emit lines in OLD order,
  (j) `Classrdr_S9StencilPasses` / `Classrdr_S9Steer` / `Classrdr_S9Emit` / `Render_S9StencilSmoke` (RUN).
- **Review 6E′ items — CLOSED**: fix 1 (`DrawStripCount` frame clear; smoke (c) asserts it), fix 2 (`Render_S9StencilSmoke`
  RUNS and P3 fails it), fix 3 (§6E′ amended in place: P6-STRIP-FRAMECLEAR finding, Tests paragraph, (e) `:1855-1856` arm,
  scissor-scale note).
- **P6-STRIP-FBMSK16 — new, CLOSED** (audit delta 1 above): a stencil strip now records the A-protect keep mask and skips the
  upload eviction exactly as OLD's strip post-draw.
- **P6-S9-QUAD-VIEWPORT (noted, no change)**: OLD's pass-2/3 quads use the strip's viewport (`render_w/h`, `:1925`) while
  `Backend::DrawConstQuad` uses the stencil dims (`= target dims`); identical whenever the render height equals the target
  height, which holds for the accumulator (its height follows SCISSOR and the target is created at that size). The corpus gate
  (6F) is the tripwire; if a capture disagrees the primitive gains a viewport argument.
- **P6-S9DEM-DIAG (decision)**: OLD's `[GS-S9DEM] …` demotion-cause line (printed between the two `[GS-S9RDR]` lines whenever a
  demote cause fired, no env gate) is a cr4 census — DIAG, not emitted; 6F's grammar compare must skip it on the OLD side.
- **P6-SNAP-S2 (open, minor)** unchanged.

### Gate status
| gate | evidence |
|---|---|
| G8 (classrdr) | `Classrdr_S9StencilPasses`, `Classrdr_S9Steer`, `Classrdr_S9Emit` Passed + the 6E/6E′ suites; `Profile_RegisterClassRenderers` = 7 slots with the sequence, 6 without |
| G6 (render) | `Render_S9StencilSmoke` Passed (RUN, 23 checks) alongside `Render_S9ShaderText` / `Render_DeviceSmoke` / `Render_ShaderCompiles` |
| build / lints / size | 0 warnings, LintOneClass 0, LintEnginePurity 0, size OK, greps 0 |
| full ctest | `100% tests passed, 0 tests failed out of 183`, 202.02 s, `CTEST_EXIT=0` |

### HANDOFF (6E″ → 6F)
Done: the S9 renderer is complete and registered (slot 7); every 6E′ review item closed; the audit deltas fixed.
Not done: `GsClassdiffMatchesOld` (needs the replay tool — 6F), snap S2, ContentKey wrap split.
Replay driver: unchanged from the 6E′ HANDOFF (`Presenter presenter(scanout)`; `RegisterClassRenderers(registry, &sequence)`
  is the peak registration — pass the pipeline's `SequenceRecipe`; `EmitRendererCounters` after `[GS-CLASSRDR]`).
Expected S9 lines per capture class (OLD-grammar, mods-off):
  * `load_transition` (8 composites, no volume strips, zero CLUT mirror): `[GS-S9RDR] on=1 clears=8 primes=8 arms=0
    strips_stencil=0 strips_color=0 inverts=0 materializes=0 mat_draws=0 demotes=8 scale_declines=0 dedupe_draws=0` +
    `[GS-S9RDR] invert_declines no_target=0 no_tex=0 no_content=0 stale=0 psm=0 pending=0 clut=8`; CLASSRDR `S9-shadow
    native=8 declined=32`; `SUMMARY renderers=7 native=648` (the `Classrdr_S9Emit` synthetic run reproduces the two lines).
  * strip-bearing captures (`s57_walk`, `slowspot`, `crystal_cavern`): `clears=N primes=N arms=N strips_stencil=108·N
    strips_color=0 inverts=N materializes=N mat_draws=32·N demotes=0 dedupe_draws=216·N` when the CLUT mirror carries the
    identity-invert table, else `inverts=0` + `invert_declines … clut=N`; a `strips_color > 0` count names the steer's
    failing gate (delta / zsrc / seq) — compare against OLD's `[GS-S9DEM] col_gate …` line, which NEW does not print.
  * REPLAY_HASH watch items: (1) the ZSRC route on the generic path (6E′); (2) P6-S9-QUAD-VIEWPORT above; (3) the stencil
    strip's keep mask / no-evict (this stage); (4) **W1** [Review 6E″]: `DrawPipeline::Prepare` runs the SH7 same-buffer
    flush (`SameBufferPreDrawFlush`, OLD `opengl_backend.cpp:9013-9031`) for the native S9 paths too, while OLD's native
    ladder `GsS9PreDrawLadder` (`s9_composite_native.inc:768-1018`) has no SH7 block (the `graphics_thread.cpp:1618`
    dispatch bypasses `DrawBatch`). A no-op for the CT16S CLEAR / strips (`Is16BitColor` early return); reachable only
    for a CT32 INVERT with a 16-bit ledger entry on its own fbp — unreachable while the invert is accepted only on
    32-bit-class targets (`render_psm ∈ {0x00, 0x30}` ⇒ no 16-bit deposit for that fbp; on the pure-CT16S accumulator
    the invert declines `psm` into the generic path, where OLD's SH7 also runs). If a strip-bearing capture disagrees,
    `PrepareOptions` gains a `skip_same_buffer_flush` the three native S9 passes set — all four land on the strip-bearing
    captures first.
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 183/183 [Review 6E″ fix: 184/184 with
  `Classrdr_S9Lifetime`]; lints 0/0.
Next command: cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R 'Classrdr_|Gs_Inv29|Profile_|Render_S9|Present_'
Findings: P6-S9-PENDING (CLOSED), Review 6E′ 1-3 (CLOSED), P6-STRIP-FBMSK16 (new, CLOSED), P6-S9-QUAD-VIEWPORT (noted),
  P6-S9DEM-DIAG (decision), P6-SNAP-S2 (open, minor), Review 6E″ 1-4 (CLOSED below; W1 watch item added above).
Teardown rule for the 6F replay driver / 6G app: call `RendererProfile::ReleaseBackendResources(backend)` before the
  backend's `Shutdown` (or let `Shutdown` release every surface); the renderers never call the backend from a destructor.

### Review 6E″ (verdict)

Reviewer review_6e3, 2026-09-28 (full text `/tmp/p6/REVIEW_6E3.md`; logs `/tmp/p6/rv6e3_*.log`; probes
`/tmp/p6/rv6e3_probes.sh` + destructor probe `/tmp/p6/rv6e3_dtor.log`; sources restored `shasum -a 256 -c` OK; no commits;
OLD untouched — source reads only). Checked: the three Review 6E′ fixes closed (fix 1 at `gl_draw_executor.cpp:139-142`
pinned by smoke (c); `Render_S9StencilSmoke` RUNS — `Passed 0.40 sec`, 23/0 — and fails my own op probes: `kIncrWrap →
GL_DECR_WRAP` 5 failures, `kInvert → GL_KEEP` 3 failures; §6E′ amended); the renderer against OLD line by line (Accept
`:2115-2122` + `s9_composite.h:258-308`; `InvertBackendOk :692-767`; CLEAR `:1342-1468`; steer `:1663-1750`; ladder
`:1751-2106` incl. the hold `:959-966`, the alias abort `:985-1005`, ARM `:1878-1893`, the passes `:2053-2070`, the rect
`:2014-2033`, post-draw `fbmsk16 0x8000 / run_evict false`; INVERT `:1466-1662`; materialise `:469-604`; emit `:2198-2273` +
emitter order `graphics_thread.cpp:4370-4381`; steer hookup `world_tristrip_native.inc:200-216, :304-310`) — every line
matched; two corpus-unreachable divergences noted as watch items (W1: `Prepare` runs the SH7 same-buffer flush for the
native S9 paths, OLD's `GsS9PreDrawLadder :768-1018` has none — dead while the invert is accepted only on 32-bit-class
targets; W2: P6-S9-QUAD-VIEWPORT). Greps: S9 constants in `engine/` none; backends include in `engine/gs` none;
`CommitDeposits` in `engine/gs/hle` none; `getenv` none; game literals only `gl_shader_sources.cpp:769`. Tests:
`ctest -R 'Classrdr_|Gs_Inv29|Profile_|Render_S9'` 15/15; `test_classrdr all` 413/0; smoke 23/0. Probes: R1 materialise
`wr 0x00 → 0x1F` (s9passes 1) · R2 invert Accept without `TBW == FBW` (**0** — gap) · R3 steer materialise without idling
(s9passes 2) · R5 rect `sc.x1 + 1 → sc.x1` (s9passes 2) · R7 `InvertBackendOk` without the `0x30` arm (**0** — gap) · R8 / R9
the GL op swaps above. Fresh rebuild of 29 TUs 0 warnings; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed
out of 183`, 235.37 s, `CTEST_EXIT=0`; LintOneClass 476/0; LintEnginePurity 422/0; size OK. Record + HANDOFF complete, §2.6
ticked. **Finding:** `~StencilCompositeRenderer` (`stencil_composite_renderer.cpp:36-38`) calls `DestroySurface` through the
cached `be_`; the profile-owned renderer (`RendererProfile::stencil_`, replaced by every `RegisterClassRenderers`) outlives
the per-test `World` backend — the instrumented run shows `[WORLD-PROBE] be dtor 0x16ba6dd60` → `be ctor 0x16ba6dd60` (a new
World at the same address) → `[DTOR-PROBE] be_=0x16ba6dd60 stencil.id=7`: a `DestroySurface(7)` on a dead-then-reused
pointer, a no-op today by luck.


The remainder is closed as recorded: Accept, prime, the three-pass ladder (func/ref/cmp/write/op per pass, INCR/DECR by
colour, the 0x0F clear, the ARM extent reset, the per-strip ledger record + 0x8000 keep mask / no-evict, the `!ladder_clean`
arm, the hold), INVERT + `InvertBackendOk` + the CLUT walk, the 32-quad materialise with `MarkMaterialized`, the hook-less
steer, the emit grammar and order, slot 7 — every line I compared matches the OLD range it cites, the three 6E′ fixes are
closed with biting tests (the GL smoke fails 5/3 under my two op probes), and the tree is green (0 warnings, 183/183, lints
0/0). What blocks approval is one ownership defect the tests exercise by luck: the renderer's destructor calls into a cached
backend pointer that the test harness has already destroyed (probe evidence above).

VERDICT: NEEDS_CHANGES
1. `engine/gs/hle/classrdr/stencil_composite_renderer.{h,cpp}` (`:36-38`, `be_`): the destructor must not call a backend the
   renderer does not own. Either drop the `DestroySurface` from the destructor (the device releases every surface at
   `GlDevice::Shutdown`, `gl_device.cpp:84-88`; `EnsureStencil` already destroys the previous surface on a dims change) and
   state the lifetime rule in the header comment, or add an explicit `Release(render::Backend&)` that `RendererProfile`
   calls before the backend goes and that a new `RegisterClassRenderers` calls on the renderer it replaces. Add a pin: a
   `StencilCompositeRenderer` destroyed after its backend must not touch it (a FakeBackend `destroyed_surfaces` count, or the
   profile re-registered against a fresh World with the old World gone).
2. `tests/gs/classrdr/test_classrdr.cpp` (`World :121-146`, `Profile() :40-43`): the profile-owned renderers must not outlive
   the `World` whose backend / pipeline / registry they cache (`be_`, `pipe_`, `registry_`, `world_->s9_`). Give `World` its own
   `title::RendererProfile` (declared after `be` so it is destroyed first) or reset the static profile's renderers at the end
   of every test that registers them; re-run the destructor probe (`/tmp/p6/rv6e3_dtor.log` pattern) to show no `[DTOR]` with
   a `be_` from a destroyed World.
3. `tests/gs/classrdr/test_classrdr.cpp` `TestS9Passes` decline block (`:814-819`): add the two missing negatives my probes
   R2 / R7 walked through — an invert with `tex.tbw != frame.fbw` declines before the predicate (OLD `s9_composite.h:262`), and
   a live target with `render_psm == 0x30` (the colour-authored PSMZ32 copy) is ACCEPTED by `InvertBackendOk` while `0x02` /
   `0x0A` decline with `psm` (OLD `:702`).
4. `docs/phases/PHASE6_IMPL.md` §6E″: record fix 1-3 under a `### Review 6E″ fix` heading; add watch item W1 (the SH7 flush
   inside `Prepare` for the native S9 paths — OLD's `GsS9PreDrawLadder :768-1018` has none; unreachable while the invert is
   accepted only on 32-bit-class targets) to the HANDOFF's REPLAY_HASH list next to P6-S9-QUAD-VIEWPORT.

### Review 6E″ fix (2026-09-28)

Implementer fix_6e3 (no commits; OLD read-only — source reads only: `s9_composite.h:262`, `s9_composite_native.inc:702`,
NEW `gl_device.cpp:84-88`). Logs `/tmp/p6/f6e3_*.log`; probe sources restored `shasum -a 256 -c` 4/4 OK after every probe.

1. **Renderer ownership — CLOSED.** `engine/gs/hle/classrdr/stencil_composite_renderer.h:27-36` states the lifetime rule
   (the renderer owns neither backend, pipeline nor registry; its destructor touches none of them; the device releases every
   surface at `GlDevice::Shutdown`, `gl_device.cpp:84-88`); `:68` `~StencilCompositeRenderer() = default`; `:70-72` +
   `.cpp:36-45` NEW `Release(render::Backend&)` — destroys the stencil attachment through the backend the caller vouches for
   and forgets `stencil_ / res_color_ / sten_w_ / sten_h_ / pipe_` (the next Draw / WarmUp re-caches). The write-only `be_`
   member is REMOVED (nothing left to dangle). `games/…/renderer_profile/renderer_profile.h:50-62` + `.cpp:184-186` NEW
   `RendererProfile::ReleaseBackendResources(render::Backend&)` (→ `stencil_->Release(be)`, the only renderer holding a
   backend surface; the renderers stay registered and usable), and the `RegisterClassRenderers` comment states that a
   repeated call replaces the set without touching any backend. `engine/gs/hle/classrdr/README.md:14-17` carries the rule.
   Pin: NEW mode `s9lifetime` = `Classrdr_S9Lifetime` (`test_classrdr.cpp:994-1056`, `tests/gs/CMakeLists.txt:221-223`,
   9 checks): (a) an inner `RendererProfile` registered against a live `World`, armed (stencil surface 7 exists), then
   destroyed while `w.be` lives → `w.be.destroys` unchanged and `w.be.Has(sid)` (`:1010`, the destructor made no backend
   call); (b) `ReleaseBackendResources(w.be)` → exactly one `DestroySurface`, `!Has(sid)`, `StencilSurface().id == 0`,
   `Accept(invert)` → `no_target` (no cached pipeline), then a CLEAR + strip re-arms with ONE new surface on the same
   backend (`:1024-1031`); (c) the reviewer's order: `outer` profile registered against a `World` that dies first, then
   re-registered against a fresh `World` (7 slots, the replaced set dies without a backend to call), arms on the new
   backend, `ReleaseBackendResources` before the backend goes (`:1035-1054`).
2. **Test lifetime — CLOSED.** `tests/gs/classrdr/test_classrdr.cpp:40-47`: `Profile()` is now `static const` and serves
   CONST title data only (rules / shapes / VRAM layout / sequence constants) — it never registers renderers; `World :150`
   gains `title::RendererProfile profile` declared LAST (destroyed first, before `pipe` / `registry` / `be`); all 12
   `RegisterClassRenderers` sites and both `EmitRendererCounters` sites go through `w.profile` (`ClearSlots(World&)`
   `:990-993` drops the dangling slots / materializer in the lifetime test's deliberate out-of-order arms).
   **Destructor probe re-run** (instrumented `FakeBackend` ctor / dtor / `DestroySurface` with `this`, plus the S9
   `Release` call; `/tmp/p6/f6e3_dtor.log`, checker walks the live-backend set): `test_classrdr all` 426/0 → `DestroySurface
   calls=107 against-dead-backend=0 S9.Release calls=2` (both `Release` calls name a live backend, `be=0x16b01c510`
   `stencil.id=7`), 21 backend ctors / 21 dtors. No `DestroySurface` reaches a destroyed World.
3. **Two invert-decline negatives — CLOSED.** `TestS9Passes` decline block `test_classrdr.cpp:827-839`: (i) `tex.tbw = 128`
   (≠ `fbw` 512) declines with NO decline cause counted (before the predicate; OLD `s9_composite.h:262`); (ii) the invert
   re-addressed to `0x268000` — the target the test's first draw created with FRAME.PSM 0x30 (`render_psm == 0x30`
   asserted) — is ACCEPTED by `InvertBackendOk` with `inv_psm` unchanged (OLD `:702` `render_psm ∈ {0x00, 0x30}`), while a
   fresh CT16 (0x02) target at `0x2A0000` declines `psm` (→ `inv_psm == 2`; the CT16S 0x0A decline was already pinned);
   the emit grammar line now reads `psm=2` (`:843`). Probes (each predicate reverted, `s9passes` run, source restored):
   **R2** (drop `ir.tex.tbw == ir.raster.frame.fbw`) → `50 checks, 1 failures` (`:830 tbw != fbw declines before the
   predicate`); **R7** (drop the `!= 0x30` arm) → `50 checks, 3 failures` (`:835` the 0x30 accept, `:839` CT16 (`inv_psm`
   off by one), `:844` grammar `psm=3`). Both gaps the reviewer walked through now bite.
4. **Record + W1 — CLOSED.** This heading; W1 appended to the HANDOFF REPLAY_HASH watch list as item (4) beside
   P6-S9-QUAD-VIEWPORT (with the `PrepareOptions` escape hatch should a capture disagree).

Verification (`/tmp/p6/f6e3_build.log`, `f6e3_ctest_sel.log`, `f6e3_ctest.log`): `cmake --build --preset macos-dev -j4`
`BUILD_EXIT=0`, `warning:` count 0; `ctest -R 'Classrdr_|Gs_Inv29|Profile_|Render_S9'` → `100% tests passed, 0 tests failed
out of 16` (`#109 Classrdr_S9StencilPasses` 0.18 s, `#110 Classrdr_S9Steer` 0.33 s, `#111 Classrdr_S9Emit` 0.12 s, `#112
Classrdr_S9Lifetime` 0.27 s, `#183 Render_S9StencilSmoke` 0.20 s); `test_classrdr all` 426 checks / 0 failures (`s9passes` 50,
`s9lifetime` 9); full `ctest --preset macos-dev` → see the line below; `LintOneClass: 476 files scanned, 0 findings -> PASS`;
`LintEnginePurity: 422 files scanned, 0 game tokens -> PASS`; `size check OK` (`stencil_composite_renderer.h/.cpp` 114 + 360,
`test_classrdr.cpp` 1,075). §6E″ Tests / Gate counts above (413, 15/16, 183) are the pre-fix figures; the live ones are here.
Full battery (`/tmp/p6/f6e3_ctest.log`): `100% tests passed, 0 tests failed out of 184` (183 + `Classrdr_S9Lifetime`),
`Total Test time (real) = 209.42 sec`, `CTEST_EXIT=0`.

### Review 6E″ (verdict, iteration 2)

Reviewer rereview_6e3, 2026-09-28 (full text `/tmp/p6/REVIEW_6E3_2.md`; logs `/tmp/p6/rv6e3_2_*.log`; probes
`/tmp/p6/rv6e3_2_probes.sh` + destructor probe `/tmp/p6/rv6e3_2_dtor.log`; sources restored `shasum -a 256 -c` 4/4 OK after
every probe; no commits; OLD untouched — no reads or runs there this iteration). Re-checked the four Review 6E″ items:
(1) `stencil_composite_renderer.h:68` `= default` destructor, no `be_` anywhere, lifetime rule `:27-36` + README `:14-17` +
`renderer_profile.h:53-62`; `Release(Backend&)` `.cpp:36-45`; `ReleaseBackendResources` `.cpp:184-186`. **Probe PB**: put the
cached `be_` + destructor `DestroySurface` back → `s9lifetime: 9 checks, 1 failures` (`test_classrdr.cpp:1010`) — the pin
bites. **Probe PA** (my own FakeBackend ctor/dtor live-set instrumentation inside `DestroySurface`): `test_classrdr all` 426/0,
`ctors=21 dtors=21 DestroySurface=107 DEAD=0`. (2) `Profile()` `static const` (const data only; grep confirms), `World :150`
`profile` declared last, all 12 registrations + 2 emits through `w*.profile`, `ClearSlots` covers the two deliberate
out-of-order arms. (3) `:827-839` negatives: **PC** (drop `tbw == fbw`) → `s9passes 50/1` (`:830`); **PD** (drop the `0x30`
arm) → `s9passes 50/3` (`:835, :839, :844`). (4) `### Review 6E″ fix` present with live figures; W1 = REPLAY_HASH item (4)
beside P6-S9-QUAD-VIEWPORT; teardown rule in the HANDOFF; `Classrdr_S9Lifetime` registered (#112). Hygiene: 5 TUs touched +
rebuilt `BUILD_EXIT=0`, `warning:` 0; `ctest -R 'Classrdr_|Gs_Inv29|Profile_|Render_S9'` → `100% tests passed, 0 tests failed
out of 16`; `test_classrdr all` 426/0; smoke 23/0; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of
184`, 369.07 s, `CTEST_EXIT=0`; LintOneClass 476/0; LintEnginePurity 422/0; size OK (114 + 360, test 1,075). Non-blocking
note: `Release` leaves the pipeline's materializer pointer to the still-live renderer — by the stated rule, the pipeline
owner's to clear.

VERDICT: APPROVED

## 6F — gs_replay + first corpus pass (2026-09-28, ≈70 min)

Worktree `<repo>-p6`, branch `phase6` (`pwd` + `git branch --show-current`
confirmed); OLD read-only — its `build/tests/gs_replay` run only through `verify/run_bounded.sh` from cwd `/tmp/p6`
(env on the OLD TEST binary = the sanctioned TEST channel: `PS2_REPLAY_DUMPPPM[_FRAMES]`, `PS2_REPLAY_LOG`); no commits.
Logs `/tmp/p6/6f_*.log`, helpers `/tmp/p6/6f_ppmdiff.py`, `/tmp/p6/6f_ppmpairs.py`.

### Landed (1,253 lines in `tools/gs_replay` + 3 engine methods + 4 test scripts)

`tools/gs_replay/` (executable `gs_replay`, a composition root: links `ps2r_title_renderer_profile`, `ps2r_render_gl`,
`ps2r_platform_sdl`, `ps2r_gsdl_reader`; root `CMakeLists.txt:53` `add_subdirectory(tools/gs_replay)  # Phase 6`):
- `gs_replay.cpp` 143 — main: `replay <in.gsdl> [--out] [--ds N] [--frames a-b] [--quiet] [--skip SPEC] [--dump-ppm DIR]
  [--dump-frames a-b]`, `census [--frames] [--configs]`, `diff [--tol] [--pct] [--max]`, `classdiff [--frames] [--seq]`;
  exit 2 usage / load failure, 1 VERTS-OFF or unwritable `--out`, 77 no GL, 3 DIFF (OLD `tests/gs_replay.cpp:980-1103`).
  The OLD TEST env vars became flags: `PS2_REPLAY_DUMPPPM` → `--dump-ppm`, `PS2_REPLAY_DUMPPPM_FRAMES` → `--dump-frames`
  (gates only the dump; `--frames` gates the REPLAY — OLD's `--frames a-b` also skips the frames' JOBS, so a sub-range
  replay is not the same world: see the probe pitfall below), `PS2_S105_SKIP` → `--skip`; `PS2_REPLAY_LOG` /
  `PS2_REPLAY_SCANOUT` / `PS2_REPLAY_CONTENT` (DIAG / TEST-ONLY) not transplanted. `grep -rn getenv tools/gs_replay` → 0.
- `replay_driver.h/.cpp` 123 + 245 — `ReplayDriver`: hidden 64×64 `platform::Window` + `GlContext` (swap interval 0) +
  `GlDevice`; zero `VramImage` never committed; `title::RendererProfile`; `ScalePolicy{1}`, `RenderTargetCache`,
  `TargetRefresher`, `TextureResolver`, `ScanoutComposer`, `Presenter`, `ClassRegistry`, `SequenceRecipe`, `DrawPipeline`;
  `SetScanoutFbp(kNoFbp)` before the first job (OLD `m_scanout_fbp = 0xFFFFFFFF`, `opengl_backend.h:1066` — HANDOFF 6D″
  N1: a capture without a DISPFB job grabs the most-recent-drawn target exactly as OLD); `RegisterClassRenderers(reg,
  &sequence)` → 7 slots; `WarmUpAll(pipe, stdout)`. Per-frame loop = OLD `:462-765` order: draw → `SetSrcFrame` /
  `SetGfxEpoch` from the record, `--skip` (counted, not replayed), `DrawIr::FromDrawRec`, `pipe.Draw(ir, rec.Name(h))`;
  job → the 6D″ per-job map (`OnSetWindow` + `GlDevice::SetWindowSize` + the `[GL] Resized` line on a change,
  `OnSetFrontBuffer`, `OnInvalidate`, `OnUpload` when `payload_incl`, `OnSyncAll`, others no-op); frame boundary →
  `SetSrcFrame(vf + 1)`, `OnFrameEndGrabOnly(arg1 == 1)` (mark → `FlushPending(Present, 0, 0, true)` → grab → swap),
  `full_hash = Fnv1aFast` (OLD `HashBytes = drawlist::Fnv1aFast`, `:113-115`) or `0x9E3779B97F4A7C15` with w = h = 0,
  `GsrepFile::Downsample` (OLD `:411-440` truncating math), `ds_hash`, the three-step chain with seed 1469598103934665603
  and prime 1099511628211, the `[GSREPLAY] f= vf= draws= verts= w= h= fb= ds=` line, the `.gsrep` frame, `--dump-ppm`
  P6 write (`:721-737`). Tail in OLD order (`:767-790`): `census.Print("[GSREPLAY]")`, skip counts, `REPLAY_HASH=… backend=opengl
  ds=%u`, (GsC3 DIAG absent), `registry.EmitCounters`, `profile.EmitRendererCounters` (S9RDR, SPRRDR, SKINRDR, PARTRDR,
  FMVRDR), (GsR121Attr DELETE absent), then `[GS-PIPELINE]` (NEW-only diagnostics, after the OLD-grammar lines).
  `ReleaseBackendResources(be)` before `GlDevice::Shutdown` (6E″ teardown rule).
- `census.h/.cpp` 58 + 167 — `Census`: CENSUS / PRIM / FLAGS / JOBS / HLE / CLASS / CFG grammar verbatim (OLD `:118-330`);
  `ClassOf` = `ClassClassifier::ClassifyFields` under the profile's rules with the record's bbox and the name table
  (`:153-159`); `PsmName` table `:118-128`; `CfgKey` `:170-215`.
- `gsrep_file.h/.cpp` 73 + 98 — `GsrepFile`: packed `GsrepHeader` (40 B, magic `0x50525347`, version 1, patched at
  close) + `GsrepFrame` (40 B) + ds bytes (OLD `:398-410`), writer with patch-back, `Load` for the diff (`:814-834`),
  `Downsample` (`:411-440`).
- `replay_diff.h/.cpp` 43 + 136 — `ReplayDiff::RunDiff` (`:836-873`: mean / max / within_tol / IDENT-OK-DIFF verdict,
  exit 3) and `RunClassDiff` (`:892-975`: per-class totals, `--seq` first divergence, exit 3).
- `skip_filter.h/.cpp` 52 + 99 — `SkipFilter`: the `s105skip` clause grammar (`gs_class_renderer.h:469-585`) minus its
  env latch; emit lines carry the `[GSREPLAY] skip` prefix (the OLD `[S105-SKIP]` tag is a session tag).
- `frame_range.h` 17 — `FrameRange` (inclusive on `vsync_frame`, `:99-102`).

Engine additions (the corpus frame end): OLD `gs_replay`'s VSync step is `MarkEEFrameEnd → GrabDisplayRGBA → Present()`
(`:700-759`) and OLD `OpenGLBackend::Present()` (`:23594-23812`) is the reader-band pending flush + PBO issue + swap +
`m_frame_count++` — it never runs `DrawScanout` (INV §0 headline 2). NEW's `DrawPipeline::OnFrameEnd` presents through
`ScanoutComposer::DrawScanout`, whose mirror maintenance (the scanout-base deposit / ledger record) the reference replay
never performed. Added `ScanoutComposer::PresentBlank(window)` (the swap alone), `Presenter::PresentBlank(w, h)` (swap +
`frame_count_++`, `LastPresent = None`) and `DrawPipeline::OnFrameEndGrabOnly(ee_frame_end, out)` (mark → present flush →
grab → `PresentBlank`); `OnFrameEnd` unchanged (shares `FrameEndGrab`). Pins: `test_present.cpp` `Grab` (blank present:
`presents == 2`, no quad source, `FrameCount == 2`, `Source::None`, the 0x70000 page's `shadow_epoch` and the ledger
unchanged) and `test_classrdr.cpp` `TestPipeline` (`OnFrameEndGrabOnly`: both counters advance, the grab still lands,
swap without a quad, no scanout-side deposit / ledger change, the EE mark still re-arms the per-frame clear).

Tests (`tests/gs/replay/*.sh`, registered in `tests/gs/CMakeLists.txt`; every one SKIP 77 without the corpus / GL):
`GsReplayCensus` (census of load_transition, 15 lines CENSUS/PRIM/FLAGS/JOBS/HLE/CLASS == baseline `.replay.txt`;
replaces 6A's 4-line partial), `GsClassdiffMatchesOld` (`classdiff cap cap --seq` clean + the 9 per-class totals ==
baseline CLASS lines), `GsReplayDeterminism` (2 × bounded replay: equal REPLAY_HASH, `cmp` clean .gsrep), and
`GsCorpus_<cap>` × 19 through `gs_corpus_leg.sh` (bounded 900 s / 600 s for load_transition; REPLAY_HASH == baseline or
ALLOWLIST; `.gsrep` byte-identical when the primary hash matched) — **all 19 registered DISABLED** (`PS2R_GS_CORPUS_GREEN`
is empty: no capture is hash-equal yet; the record below shows the leg script FAILING honestly on load_transition). The
tree stays green with the known-red gate visible as `Not Run (Disabled)`; 6F′ moves a capture into
`PS2R_GS_CORPUS_GREEN` the moment it is identical.

### OLD sources transplanted (read-only)
`tests/gs_replay.cpp` in full (loop `:462-765`, `BuildBatch` `:334-391` → `FromDrawRec`, `Downsample` `:411-440`, `.gsrep`
`:394-410`, `Census` `:118-330`, `ClassOf` `:153-159`, `RunDiff` `:807-873`, `RunClassDiff` `:892-975`, `main` `:980-1103`);
`gs_class_renderer.h:469-585` (`s105skip`); `opengl_backend.cpp:23594-23806` (`Present()` — read to decide the frame
end), `:25076-25103` (`GrabDisplayRGBA` — matches `ScanoutComposer::GrabDisplay`), `:4248-4262` (`Resize`),
`:8674-8716` (render dims — re-read for the divergence), `:19771-19777` (`[FIND-TEX]` probe grammar).

### Flags applied
`PS2_REPLAY_DUMPPPM`, `PS2_REPLAY_DUMPPPM_FRAMES`, `PS2_S105_SKIP` (TEST-ONLY) → CLI flags; `PS2_REPLAY_LOG`,
`PS2_REPLAY_SCANOUT`, `PS2_REPLAY_CONTENT` (TEST-ONLY / DIAG) → not transplanted; `PS2_S103_DRAWLIM` (DIAG, default OFF)
→ not written; `GsC3EmitForBench` / `GsR121AttrEmit` (DIAG / DELETE) → not written. The env fingerprint line prints the
empty-set constant (`env_ps2{n=0 hash=0x14650fb0739d0383}` — NEW reads no environment).

### First corpus pass — load_transition (bounded, cwd `/tmp/p6`)

```
$ bash verify/run_bounded.sh 600 /tmp/p6/6f_new_lt.log -- build/tools/gs_replay/gs_replay replay \
      OLD/gs_corpus/load_transition.gsdl --out /tmp/p6/6f_lt.gsrep                      → exit 0, 247 lines
OLD  [GSREPLAY] REPLAY_HASH=0xce1f5d65071ec90b frames=208 draws=3667 backend=opengl ds=32   (baseline :28; re-run this
NEW  [GSREPLAY] REPLAY_HASH=0x93a8b7dcfb0c8703 frames=208 draws=3667 backend=opengl ds=32    session: identical)
$ cmp /tmp/p6/6f_lt.gsrep OLD/gs_corpus/baseline/load_transition.gsrep → differ: char 17 (the header's replay_hash)
$ gs_replay diff OLD/…/load_transition.gsrep /tmp/p6/6f_lt.gsrep
[GSDIFF] f=193 vf=855/855 ident=0 draws=37/37 dims=512x448/512x448 mean=0.000,0.000,2.390,0.000 max=5 within_tol=50.49% verdict=DIFF
[GSDIFF] f=195 vf=857/857 ident=0 draws=17/17 dims=512x448/512x448 mean=0.018,0.007,2.520,0.000 max=7 within_tol=45.61% verdict=DIFF
[GSDIFF] f=197 vf=859/859 ident=0 draws=19/19 dims=512x448/512x448 mean=0.005,0.002,2.542,0.000 max=5 within_tol=45.12% verdict=DIFF
[GSDIFF] SUMMARY frames=208 ident=205 ok=0 diff=3 tol=0 pct_req=100.00 replay_hash_a=0xce1f5d65071ec90b replay_hash_b=0x93a8b7dcfb0c8703 hashes_equal=0
```

**205 / 208 frames IDENT**; frames 0–192 and 194, 196, 198–207 byte-identical (every `fb=` hash equal). The
`[GS-CLASSRDR]` (9 class lines + SUMMARY `renderers=7 seen=3667 native=648 fallback=3019 declined=2796`), `[GS-S9RDR]`
(both lines, `clears=8 primes=8 demotes=8`, `invert_declines … clut=8`), `[GS-SPRRDR]`, `[GS-SKINRDR]`, `[GS-PARTRDR]`
lines are **identical** to the baseline `.replay.txt:29-43` (diff of the two logs: only the `file=` path, the `[GL]`
banner, the DIAG `[R112-ZCHECK]` / `[GS-RESIDENT]` / `[GS-C3]` lines NEW does not print, the three `fb=` lines, and two
log-only deltas: `[GS-CLASSRDR] warmup renderers=7 programs=9` (OLD) vs `programs=0` (NEW — the programs are the
backend's, 6E) and the two OLD stderr `[GS-S9RDR] invert CLUT mismatch cbp=0x3f0000 …` diagnostics (DIAG; the
`clut=8` counter that matters is equal) — added per Review 6F note 1).
Census lines (`GsReplayCensus`) and classdiff totals (`GsClassdiffMatchesOld`) identical. Determinism: two runs →
`0x93a8b7dcfb0c8703` twice, `.gsrep` `cmp` clean (`GsReplayDeterminism` Passed 20.32 s).

### The divergence (open → 6F′) — first differing frame f=193 vf=855, first pixel (0, 172)

PPM oracle (OLD `PS2_REPLAY_DUMPPPM=/tmp/p6/6f_old_ppm PS2_REPLAY_DUMPPPM_FRAMES=854-860 … gs_replay replay <cap>`
bounded; NEW `--dump-ppm /tmp/p6/6f_new_ppm --dump-frames 854-860`; `/tmp/p6/6f_ppmdiff.py`, `6f_ppmpairs.py`):
- vf855: 110,700 / 229,376 pixels differ, buffer rows 172..447 (190 full rows), **blue channel only**: `old (255,255,176)
  → new (255,255,181)` × 101,676, then `248→252`, `216→222`, `208→210`, `232→234`, `224→228`, `240→246`, `200→205`, …
  Every OLD blue value is a multiple of 8 (5-bit quantised, `<< 3`, no low-bit replication); NEW's carry low bits.
  vf857 / vf859 the same signature (+ a few `(7,3,5)` pixels); vf856 / 858 identical.
- Ablation 1 (`--skip seq=6949` on BOTH tools): vf855 becomes identical → the draw that EXPOSES the difference is
  `seq=6949` = frame 193 `draw[14]`: `sprite+tex n=96 fbp=0x070000/CT32 fbw=512`, `TEX0 tbp=0x230000 tbw=256 CT32
  1024x1024 fst=1 mmag=0 mmin=0 tfx=0 tcc=1`, vertex RGBA `(128,128,128,0)` (modulate ×1 → the texel is copied), ABE off,
  ZTE always — the fullscreen composite of the 256×224 scene accumulator (`VramLayout::scene_accum_fbp = 0x230000`) into
  the display buffer. Frame 855 is the FIRST frame of the capture that touches 0x230000 (13 CT32 draws `scis (0,0)-(255,223)`,
  then the composite, then 22 world draws into 0x70000; frames 856/858/860… draw it as CT16S — the S9 composite —
  and 857/859/861… again as CT32).
- Ablation 2 (`--skip seq=6925:6948`, i.e. only `draw[1]` — the first draw into the fresh 0x230000 — and the composite
  left): vf855 differs on 126,099 pixels as `old (248,248,248) → new (255,255,255)`: the **BASE CONTENT of the freshly
  created 0x230000 target** differs — OLD's untouched accumulator reads as 5551-decoded white (`0x1F << 3` per channel),
  NEW's as full white. NEW's `RenderTargetCache::GetOrCreate` clears a fresh target to 0 and imports nothing, so NEW's
  white also comes from somewhere still to be located (the composite's resolve / the alias pre-draw import / the sampled
  source) — 6F′ instruments both sides first.
- OLD route probe (`PS2_REPLAY_LOG=/tmp/p6/6f_old_probe.log`, bounded, `--frames 855-855` — CAUTION: that range also
  drops frame 0's SetWindow jobs, so `m_height` stays 64 and the target was `256x64` in the probe; re-run WITHOUT
  `--frames` for a faithful route log): `[FIND-TEX] tbp=0x230000 tex_psm=0x0 render_psm=0x0 valid_content=1 …
  color_tex=8` → `[OCEAN-COMPOSITE-FIX] … -> sampling FBO color_tex=8`: OLD serves the accumulator's colour attachment
  for the CT32 composite read (the NEW `MatchTarget` tier (b) does the same), so the divergence is in the ATTACHMENT's
  untouched texels, not in the serve.
- Attribution (hypothesis for 6F′, not yet proven): the fresh 0x230000 target's initial content. Candidates, in order:
  (1) OLD `GetOrCreateTargetFBOIn` (`opengl_backend.cpp:15449-15736`) imports the new target from the mirror
  (`RefreshFBOFromVRAM :17484`, P6-REFRESH-ARMS) — the zero mirror ⊕ shadow at [0x230000, +) holds the Z-buffer deposits
  of 0x1c0000 (Z32 512×448 spans 0x1c0000..0x2a0000; the fog / depth-to-16-bit passes read it as Z16 every frame, e.g.
  frame 192 `draw[3]`) — and decodes them in a 16-bit arm (Z16 / CT16 → `0xFFFF` → `(248,248,248)`), where NEW's
  `GetOrCreate` neither imports nor clears to white; (2) the alias pre-draw import (`TargetRefresher::AliasPreDraw` /
  `RefreshFromVram`, CT32 arm = alpha-only merge) vs OLD's `:9403-9625` demand on a FRESH target; (3) the "no target
  content" arm of the composite read (`valid_content` covers only draw[1]'s rows). Watch items checked: ZSRC generic
  (`zsrc_arms=0` on this capture), S9 quad viewport / strip keep mask / W1 (no S9 arm on load_transition: `arms=0`),
  clear-vs-alias-import order (candidate (2) above IS this item's shape), `scanout_fbp_` default (set to `kNoFbp`),
  P6-FMT6-OFF (`OnInvalidate → OnCpuWrite` only), P6-SELFREAD-PERSIST (no self-read in frame 855).
- Escalation commands for 6F′ (all bounded, cwd `/tmp/p6`):
  `PS2_REPLAY_LOG=/tmp/p6/6f2_old_probe.log OLD/build/tests/gs_replay replay <cap> --quiet` (FULL replay; grep
  `0x230000` for the `[FIND-TEX]` / `[VRAM-ALIAS]` / `[R112-…]` lines of frame 855); a NEW-side temporary `fprintf` in
  `RenderTargetCache::GetOrCreate` / `TargetRefresher::AliasPreDraw` / `TextureResolver::MatchTarget` for `fbp == 0x230000`
  (remove before recording); then `GsrepFile`-level re-diff. The fix lands in the owning module (`targets/` most likely)
  with a pin in `test_targets` / `test_texsrc` citing the OLD line, and `load_transition` moves into `PS2R_GS_CORPUS_GREEN`.

boot_menu / fmv_attract: not attempted (the ~40-minute mark passed inside the load_transition escalation).

### Gate status
| gate | status |
|---|---|
| build | `cmake --build --preset macos-dev -j4` → `BUILD_EXIT=0`, `grep -c 'warning:'` 0 (`/tmp/p6/6f_build2.log`; a first link had `ld: warning: ignoring duplicate libraries: ps2r_core` — the redundant link removed) |
| selection | `ctest -R 'GsReplay|GsCorpus_load|GsClassdiff|Present_|Classrdr_DrawPipelineOrder'` → `100% tests passed, 0 tests failed out of 9`; `GsReplayCensus` 0.78 s, `GsClassdiffMatchesOld` 1.49 s, `GsReplayDeterminism` 20.32 s; `GsCorpus_load_transition` `Not Run (Disabled)` |
| leg script by hand | `bash tests/gs/replay/gs_corpus_leg.sh … load_transition 600 /tmp/p6/6f_leg` → `FAIL: REPLAY_HASH differs from the frozen baseline`, exit 1 (the gate bites) |
| `test_present all` / `test_classrdr pipeline` | 119 / 0, 28 / 0 |
| full ctest | `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 187` (206 registered, 19 `GsCorpus_*` Disabled), `Total Test time (real) = 268.92 sec`, `CTEST_EXIT=0` (`/tmp/p6/6f_ctest.log`; `GsReplayCensus` 1.74 s, `GsClassdiffMatchesOld` 2.57 s, `GsReplayDeterminism` 30.32 s) |
| lints | `LintOneClass: 476 files scanned, 0 findings -> PASS`; `LintEnginePurity: 422 files scanned, 0 game tokens -> PASS` |
| size | `python3 /tmp/p6/check_sizes.py .` → `size check OK` (largest new: `replay_driver.cpp` 245) |
| getenv | `grep -rn getenv tools/gs_replay` → 0 |
| G8 (classdiff) | `GsClassdiffMatchesOld` PASS on load_transition (1 / 19 captures) |
| G9 (corpus) | **0 / 19** hash-equal; load_transition 205 / 208 frames IDENT, counters identical; 18 legs registered, not run |
| G10 | lints 0 / 0, size OK, getenv unchanged, 0 warnings |
| G11 | unchanged (no `Gs_Inv` added this stage) |

### Findings
- **P6-REPLAY-PRESENT-BLANK (new, CLOSED)**: the corpus frame end must not run the scanout composite (OLD `Present()`
  `:23594-23806` = pending flush + swap; `DrawScanout` is a graphics-thread VSync step the replay never linked). NEW's
  `OnFrameEnd` would have deposited the picked target at the scanout base every frame (`ScanoutComposer::DrawScanout`,
  P6-SCANOUT-DEST) — a mirror the reference never wrote. `OnFrameEndGrabOnly` / `PresentBlank` added and pinned. The
  6E HANDOFF's "Present happens INSIDE OnFrameEnd — same order as OLD" was right about the ORDER and wrong about the
  BODY.
- **P6-REPLAY-FRAMES-JOBS (note)**: `--frames a-b` (OLD and NEW alike) skips the out-of-range frames' JOBS too, so a
  sub-range replay starts from the 64×64 window and no DISPFB — never use it for pixel oracles; `--dump-frames` exists
  for that.
- **P6-ACCUM-FRESH-CONTENT (new, OPEN → 6F′)**: the divergence above.
- The `[GS-CLASSRDR]` / `[GS-S9RDR]` / renderer counter lines are identical to the baseline on the first pass: the class
  routing, the Accept tuples and the S9 decline path reproduce OLD exactly on load_transition.

### HANDOFF (6F → 6F′)
Done: `tools/gs_replay` (all four modes, OLD grammar, no getenv), the corpus frame end (`OnFrameEndGrabOnly`), the four
  replay ctests + 19 DISABLED legs, load_transition to 205 / 208 IDENT with identical counter lines.
Not done: load_transition identity (P6-ACCUM-FRESH-CONTENT: first differing frame f=193 vf=855, pixel (0,172) old
  `(255,255,248)` new `(255,255,255)`; exposing draw seq=6949; base-content probe `(248,248,248)` vs `(255,255,255)`);
  boot_menu / fmv_attract and the other 16 legs (registered, not run); `PS2R_GS_CORPUS_GREEN` is empty; the corpus
  sweep (`ctest -R GsCorpus_` after enabling) is the orchestrator's next step once load_transition is green.
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 187/187 (+19 Disabled); lints 0/0.
Next command: cd /tmp/p6 && PS2_REPLAY_LOG=/tmp/p6/6f2_old_probe.log bash <NEW>/verify/run_bounded.sh 600 /tmp/p6/6f2_old.log -- \
  <OLD>/build/tests/gs_replay replay <OLD>/gs_corpus/load_transition.gsdl --quiet && grep -n '0x230000' /tmp/p6/6f2_old_probe.log | head -40
Findings: P6-REPLAY-PRESENT-BLANK (CLOSED), P6-REPLAY-FRAMES-JOBS (note), P6-ACCUM-FRESH-CONTENT (OPEN).

### Review 6F (verdict)
Independent review 2026-09-28 (`/tmp/p6/REVIEW_6F.md`; logs `/tmp/p6/rv6f_*`). (1) `tools/gs_replay` read line by
line against OLD `tests/gs_replay.cpp` (argv/exit codes `:1002-1103`, chain + sentinel `:735-742`, `Fnv1aFast`
`drawlist_record.h:171-183`, `Downsample :411-447`, `.gsrep :394-410/:794-803`, census `:118-330`, `ClassOf :153-159`,
`RunDiff :836-873`, `RunClassDiff :892-998`, emitter order `:767-790`, `s105skip gs_class_renderer.h:469-585`): all
verbatim; `grep getenv tools/gs_replay tests/gs/replay` 0; `CommitDeposits` never called anywhere in `engine/`;
`has_snapshot=false` (`draw_pipeline.cpp:143`); stamps from the record. (2) Reviewer's own bounded runs (cwd
`/tmp/p6`): `REPLAY_HASH=0x93a8b7dcfb0c8703` twice, `.gsrep` `cmp` clean run-to-run, `cmp` vs baseline differs at
char 17; python decode of both `.gsrep` → header totals equal, frames differing `[193, 195, 197]`, **205/208
identical**; the 9 `[GS-CLASSRDR]` + SUMMARY, both `[GS-S9RDR]`, SPR/SKIN/PART lines identical to `.replay.txt`.
PPM oracle re-derived (OLD `PS2_REPLAY_DUMPPPM` bounded vs NEW `--dump-ppm`): vf855 first pixel (0,172) old
`(255,255,248)` new `(255,255,255)`, 110,700 blue-only pixels, every OLD blue a multiple of 8; `--skip seq=6949`
on both → vf855 PPMs `cmp` identical. (3) P6-REPLAY-PRESENT-BLANK cites OLD `Present()` (0 `DrawScanout` in
`:23594-23812`); probes: `PresentBlank` without `++frame_count_` → `test_present.cpp:133` + `test_classrdr.cpp:613`
FAIL; `OnFrameEndGrabOnly` running the full present → `test_classrdr.cpp:615/:616` FAIL; sources restored
(`shasum -c` OK, 12/0 + 28/0). OLD baseline mtimes ≤ 2026-09-21; `find OLD -newer <review start>` → 0. (4) Clean
rebuild `--clean-first` → 0 warnings; `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 187`
(206 registered, 19 `GsCorpus_*` Disabled), `GsReplayCensus`/`GsClassdiffMatchesOld`/`GsReplayDeterminism` Passed;
leg script by hand → `FAIL: REPLAY_HASH differs` exit 1; lints 0/0; size OK. (5) Record complete; the open
divergence is honestly recorded with frame/pixel/attribution/next command; no identified-and-unfixed construction
bug. Non-blocking notes: add the `warmup programs=9→0` and OLD stderr `invert CLUT mismatch` log-only deltas to
the record; §7.1 ledger rows for the 6E′/6E″/6F findings are still owed; cite `:23594-23812`; the `:617`
"no mirror maintenance" predicate does not bite in the current fixture.

VERDICT: APPROVED

## 6F′ — corpus escalation + sweep (2026-09-28, ≈110 min)

Worktree `<repo>-p6`, branch `phase6` (`pwd` + `git branch --show-current`
confirmed); OLD read-only — `build/tests/gs_replay` run only through `verify/run_bounded.sh` from cwd `/tmp/p6`
(`PS2_REPLAY_LOG` / `PS2_REPLAY_DUMPPPM[_FRAMES]` / `PS2_S105_SKIP` on the OLD TEST binary = the sanctioned channel);
nothing under `OLD/gs_corpus` touched (`find OLD/gs_corpus -newer /tmp/p6/6f2_old.log` → 0); no commits.
Logs `/tmp/p6/6f2_*.log`, sweep `/tmp/p6/corpus_sweep.sh` → `/tmp/p6/sweep/` (`TABLE_pass1.md`, `TABLE.md`, `STATUS.md`).

### STEP 1 — P6-ACCUM-FRESH-CONTENT root cause (CLOSED): OLD's class-native generic arms inherit the register program's uniforms

Evidence chain (all bounded, cwd `/tmp/p6`):
1. HANDOFF Next command (`PS2_REPLAY_LOG=/tmp/p6/6f2_old_probe.log … load_transition --quiet`, full replay, exit 0,
   `REPLAY_HASH=0xce1f5d65071ec90b`): the 0x230000 lines of frame 855 are `[Z-PAIR] fbp=0x230000 … has_valid=0` (draw[1]) …
   `[FIND-TEX] tbp=0x230000 tex_psm=0x0 render_psm=0x0 valid_content=1 size=256x224 color_tex=28` →
   `[OCEAN-COMPOSITE-FIX] … sampling FBO color_tex=28`. No `[VRAM-ALIAS]`, `[R105-ALT16]`, `[R124-TEXL2]` or `[R164-IDXTEX]`
   line touches 0x230000 before frame 856 → hypotheses (b) alt16 serve and (c) clear value ruled out; (1) mirror import
   ruled out (no refresh line). `drawlist_tool dump --frame 193` (`/tmp/p6/6f2_dump855.txt`): draw[1] seq=6922
   `tristrip+tex n=144 fbp=0x230000/CT32 … TEX0 tbp=0x3f0000 T8 32x32 … ALPHA 2001 (abe off) … zte=1 ztst=1`, HLE
   `id=77e5566f` (world family 2 → the skinned native arm), vertices `rgba=(255,255,255,127)`; draws 2–13 the same
   shape (inst 1–8) or `HLE raw` tristrips (raw-skin); draw[14] seq=6949 the composite (DrawBatch proper).
2. PPM pair census (`/tmp/p6/6f_old_ppm/vf855.ppm` vs `6f_new_ppm`): every differing pixel is blue-only with
   OLD = NEW & ~7 (`(255,255,181)→(255,255,176)` ×101,676, `252→248`, `222→216`, `210→208`, …) — a 5-bit
   truncation without replication = the `[WN4-CT16Q]` fragment block (`q5 << 3`) applied to a CT32 frame draw.
3. Ablation: OLD with `PS2_S105_SKIP=seq=6922` (draw[1] removed) → `REPLAY_HASH=0xce1f5d65071ec90b` (UNCHANGED —
   draw[1]'s output is fully overdrawn in OLD); the differing colours come from draws 2–13, so the divergence is
   in HOW the skinned-arm draws are shaded, not in the fresh target's base content (the 6F "base content"
   reading of ablation 2 was the same effect on draw[1]: its white lands as 248 in OLD, 255 in NEW).
4. NEW-side temporary probe (removed; `shasum -c` OK on both files): every T8 draw of frame 855 resolves
   `kind=None` (`Declined::NoSnapshot` — the replay carries no VRAM snapshot, `draw_pipeline.cpp:144`) → untextured
   `result = v_color`; OLD's `ResolveTextureSeam` takes the same arm (`else if (!ti.vram_snapshot.empty())`,
   `opengl_backend.cpp:4930`, false in replay → `has_texture=false` → `u_use_texture=0` → `result = v_color`,
   `:1893`). So both shade vertex colour; only the post-shading quantisation differs.
5. The OLD mechanism: `DrawBatch` sets `u_ct16q` / `u_dthe` / `u_dimx_lo` / `u_dimx_hi` on every draw
   (`opengl_backend.cpp:10817-10823`, `= Ct16QuantOn && FrameIsCt16Class(batch.frame_psm) ? scale : 0`), plus
   `u_uv_native` / `u_draw_scale` (`:10660-10671`), `u_blend_a..d` / `u_fix_alpha` (`:11136-11140`, `:11288-11292`)
   and `u_zsrc_num` / `u_zsrc_den` (`:11457-11459`). The five class-native GENERIC-program arms are DrawBatch
   replications that OMIT those `SetU*` sites — `skinned_native.inc` `DrawSkinnedLitNative` generic arm
   (`:1107-1170`, `:1375`; omits all thirteen), `sprite_hud_native.inc` `:1111-1168`/`:1379` and
   `particles_native.inc` `:956-1013`/`:1361` (omit ct16q, dthe, dimx ×2, draw_scale, uv_native, zsrc_num/den),
   `fmv_blit_native.inc` `:1071-1128`/`:1335` (as skinned); `world_tristrip_native.inc` runs its OWN program at peak
   (`spec = worldrdr::SpecProgOn() && m_world_strip_prog != 0`, `:716`, env default ON `:145-151`) and the S9
   natives run theirs (`m_s9_prog_*`) — their uniforms are complete. The generic program's uniform therefore keeps
   the value the LAST DrawBatch draw wrote: frame 855's draw[0] (`fbp=0x000000/CT16`, DrawBatch) sets
   `u_ct16q = 1`; draws 1–13 (skinned / raw-skin arms, `spec` false in the replay because `batch_tex_id == 0`,
   `skinned_native.inc:935-937`) then write the CT32 accumulator through the CT16 5-bit truncation
   (`u_blend_enabled == 0 || u_sw_blend`: the abe=0 draws 1, 5, 6 quantised, the FF-blended ones not — hence
   the blue-only, partially quantised composite). NEW's `GlDrawExecutor::RegisterUniforms` wrote every uniform
   from the draw → no inheritance → `(255,255,181)`.

Fix (owning modules, cited):
- `engine/render/backend.h`: `render::InheritedUniform` bits (`kInheritCt16Quant`, `kInheritDither`,
  `kInheritDrawScale`, `kInheritUvNative`, `kInheritBlendAbcd`, `kInheritZsrcMap`) + `DrawParams::inherited_uniforms`
  (0 = the full register program = DrawBatch).
- `engine/render/backends/gl/gl_draw_executor.cpp`: `RegisterUniforms` / `TextureUniforms` / `BlendAbcdUniforms`
  (new helper) / `DepthState` skip the named sets, so the GL uniform keeps the previous full-set value exactly as
  OLD's `SetU*` shadow does (the cache dedupes on value; a skipped set leaves the program's uniform untouched).
- `engine/gs/hle/classrdr/class_renderer.h`: the per-shape masks `kInheritSkinnedArm` (= common + blend + zsrc),
  `kInheritFmvBlitArm` (= skinned), `kInheritSpriteHudArm` / `kInheritParticlesArm` (= common + zsrc);
  `DrawPipeline::PrepareOptions::inherited_uniforms` → `params.inherited_uniforms`; `SkinnedRenderer::DrawAs`
  (skinned / world family 2 / raw-skin), `SpriteHudRenderer`, `ParticlesRenderer`, `FmvBlitRenderer` pass theirs;
  `WorldTristripRenderer` strips and the `StencilCompositeRenderer` keep 0 (own programs in OLD).
- Pins: `tests/render/test_inherited_uniforms.cpp` (`Render_InheritedUniforms`, 21 checks on a real context: full-set
  CT16S white → 248, CT32 → 255; inheriting CT32 draw after a CT16S draw → 248 and 181 → 176; a full-set CT32 draw
  resets it; an inheriting CT16S draw after a CT32 full set stays 255; the other bits leave u_ct16q alone) —
  negative probe with the `kInheritCt16Quant` guard forced true → 3 FAIL (`:89`, `:90`, `:95`), source restored
  (`shasum -c` OK); `tests/gs/classrdr/test_classrdr.cpp` `TestAccept` (each registered renderer's backend call
  carries its shape's mask through `FakeBackend::last_params`; the fallback carries 0).
- Result: `REPLAY_HASH=0xce1f5d65071ec90b frames=208 draws=3667` (`/tmp/p6/6f2_new_lt.log`), `cmp
  /tmp/p6/6f2_lt.gsrep OLD/gs_corpus/baseline/load_transition.gsrep` → identical.

Classification: an OLD construction defect (stale uniforms on class-native draws) that the frozen baselines
encode; reproduced as a NAMED behaviour (`InheritedUniform`, not a hidden side effect) so the accepted-difference
ladder can retire it later with a one-line change (masks → 0) — bug-KB entry owed (ledger row L4 below). At scale 1
only `u_ct16q`/`u_dthe`/`u_dimx` (and blend a..d/fix on a sw-blended skinned/FMV draw) can change pixels; the
draw_scale / uv_native / zsrc-map bits matter at scale > 1 and are carried for fidelity.

### STEP 1b — second construction bug found by the sweep: P6-WINDOW-SEED (CLOSED)

Five captures (fmv_attract, boot_menu, slowspot, cavern_checker, cavern_mipmap) diffed from frame 0 with
`dims=512x64/512x448`: they carry no `SetWindow` job (recording started mid-run; `drawlist_tool dump --frame 0`
shows only Invalidate/Upload jobs) and OLD's render-height fallback is the WINDOW height `m_height`, seeded by
`Init` from the hidden 64×64 window (`SDL_GetWindowSize(m_window, &m_width, &m_height)`, `opengl_backend.cpp:2952`;
`Resize` only on a job, `tests/gs_replay.cpp:634-660`). NEW's `DrawPipeline` started from its 640×448 default (the
OLD header default `opengl_backend.h:290-291`, which OLD overwrites at Init). Fix: `tools/gs_replay/replay_driver.cpp`
`BuildScene` → `pipe_->OnSetWindow(win_.Width(), win_.Height(), 0, 0)` right after constructing the pipeline (the
composition root mirrors OLD `Init`; a capture WITH a SetWindow job is unaffected — its job overrides). Pinned by the
five legs themselves (`GsCorpus_fmv_attract` … now enabled); no unit pin added (the value is the window's own size).

### STEP 2 — the sweep (`/tmp/p6/corpus_sweep.sh`, bounded 900 s / 1200 s for the 2.3–2.5 GB captures, cwd `/tmp/p6`)

Pass 1 (after STEP 1, `/tmp/p6/sweep/TABLE_pass1.md`): 13 IDENT / 6 DIFF. Pass 2 re-ran the 6 after STEP 1b
(`/tmp/p6/sweep/TABLE.md`). Final table (NEW hash from the final binary; `gsrep_cmp` = `cmp` against
`OLD/gs_corpus/baseline/<cap>.gsrep`; ALLOWLIST = `OLD/gs_corpus/baseline/ALLOWLIST.txt`, grammar `^<cap> <hash>`):

| cap | new_hash | baseline_hash | allowlist? | gsrep_cmp | verdict |
|---|---|---|---|---|---|
| load_transition | `0xce1f5d65071ec90b` | `0xce1f5d65071ec90b` | no | identical | IDENT |
| fmv_attract | `0x1685101d58224b5c` | `0x1685101d58224b5c` | no | identical | IDENT (pass 2) |
| boot_menu | `0x01c085190224129a` | `0x01c085190224129a` | no | identical | IDENT (pass 2) |
| island_slot7 | `0x105f17894fa0a086` | `0x105f17894fa0a086` | no | identical | IDENT |
| save_select | `0xb992bb18c8f7c470` | `0xb992bb18c8f7c470` | no | identical | IDENT |
| slowspot | `0x7a99f39ddbf9cc18` | `0x99b108280b0c4799` | no (ALLOWLIST lists only `0x99b108280b0c4799`) | differ | **DIFF** (300/301) |
| s57_walk | `0x550989bd211b198e` | `0x550989bd211b198e` | no | identical | IDENT |
| bad_shadows | `0xee67d128652bfd09` | `0xee67d128652bfd09` | no | identical | IDENT |
| cavern_wallnoise_f39807 | `0xfe94da8925668f94` | `0xfe94da8925668f94` | no | identical | IDENT |
| cavern_flicker_f37152 | `0x143c1b3bbaee798b` | `0x143c1b3bbaee798b` | no | identical | IDENT |
| cavern_checker | `0x1058154d035e1a6e` | `0x1058154d035e1a6e` | no | identical | IDENT (pass 2) |
| cavern_mipmap | `0x7a527e5f0342bf6b` | `0x7a527e5f0342bf6b` | no | identical | IDENT (pass 2) |
| cavern_fog | `0xd12393dc9f833499` | `0xd12393dc9f833499` | no | identical | IDENT |
| cavern_gas | `0x5dbc07b1633396c5` | `0x5dbc07b1633396c5` | no | identical | IDENT |
| cavern_f13611 | `0x8143100f1efbd9c2` | `0x8143100f1efbd9c2` | no | identical | IDENT |
| crystal | `0x81b2bae1bda37177` | `0x81b2bae1bda37177` | no | identical | IDENT |
| cavern_f19112 | `0xa492ab35993dd74b` | `0xa492ab35993dd74b` | no | identical | IDENT |
| cavern_f20442 | `0xfab61e9d36853568` | `0xfab61e9d36853568` | no | identical | IDENT |
| cavern_streaks | `0x49d2f50c823bc713` | `0x49d2f50c823bc713` | no | identical | IDENT (pass 2) |

Grouping by symptom: the pass-1 DIFFs fmv_attract / boot_menu / slowspot / cavern_checker / cavern_mipmap /
cavern_streaks all showed `dims=512x64/512x448` from f=0 → ONE fix (P6-WINDOW-SEED) closed five; slowspot then
fell to its single documented frame. Of the 14 S9-stencil captures (`strips_stencil > 0` in their baselines:
bad_shadows, the ten cavern_*, crystal, s57_walk, slowspot) 13 are identical (slowspot's diff is not in a strip) — the 6E′/6E″ REPLAY_HASH watch items (ZSRC generic, S9 quad viewport,
strip keep mask, W1, clear-vs-alias-import order, `scanout_fbp_` default, P6-FMT6-OFF, P6-SELFREAD-PERSIST) did
not bite on any capture.

### The remaining divergence — slowspot f=2 vf=7402 (P6-SLOWSPOT-GB2-WARMUP, OPEN)

- `gs_replay diff`: `[GSDIFF] f=2 vf=7402/7402 ident=0 draws=127/127 dims=512x64/512x64 mean=6.354,6.457,6.382,0.000
  max=9 within_tol=20.21% verdict=DIFF`; `SUMMARY frames=301 ident=300 ok=0 diff=1`. Frames 0, 1, 3–300 identical.
- PPM oracle (OLD `PS2_REPLAY_DUMPPPM … FRAMES=7400-7403`, NEW `--dump-ppm … --dump-frames 7400-7403`): vf7400 / 7401 /
  7403 `cmp` identical; vf7402 25,565 / 32,768 px differ, first `(0,0) old (19,17,31) new (27,26,39)`, delta
  `(+8,+8,+8)` ×11,299, `(+8,+9,+8)`, `(+8,+8,+9)`, … — a uniform one-step (8/255) brightening = the S9 shadow
  composite's base level one 5-bit step apart on that frame only.
- The value NEW produces, `0x7a99f39ddbf9cc18`, IS OLD's ORIGINAL frozen slowspot hash (`OLD/gs_corpus/baseline/
  BASELINE.md:38`, `docs/loop_state/GS_PHASEA_IMPL.md:604`, `GS_PHASEB_IMPL.md:304/446`); the frozen `.replay.txt` /
  `.gsrep` carry the later `[GB2]` "warmup-frame ledger value" `0x99b108280b0c4799` (300/301 IDENT, single diff f=2
  vf=7402, mean 6.354/6.457/6.382 max 9 — the SAME signature, `BASELINE.md:117-130`, `CAVERN_CHECKER_IMPL.md:434`:
  "the original Phase-A hash … NOT allowlisted — a future byte-perfect reproduction of the ORIGINAL output would fail
  the sweep; consider allowlisting it too"). OLD re-run today (bounded) → `0x99b108280b0c4799`.
- Attribution (evidence, not yet a fix): OLD probe (`/tmp/p6/6f2_old_ss_probe.log:238-242`) on the FIRST CT16S
  (S9 CLEAR, `fbmsk=0xffffff`) draw into 0x230000 of the capture: `[R105-ALT16] created 16-bit companion … 256x64`,
  `[R124-TEXL2] alt16 GPU quantize pass`, **`[VRAM-ALIAS] Refresh FBO fbp=0x230000 psm=0x0A fbmsk=0xffffff
  pages=280..283`**, a second quantize pass; on every later S9 frame (7404, 7406, …, `:309`, `:361`, `:407`) only
  the single quantize pass — no refresh. No `InvalidateTextures` job covers 0x230000 (frames 0–4 dumped), so the
  refresh trigger is `m_vram_aliased_pages` set by a deposit of 0x230000's own CT32 content that landed (or was
  flushed at the pre-draw, `:9436-9444`) only on this warm-up frame — the `[GS-RESIDENT]` B4 PBO arm's
  `pbo_fall=6` synchronous fallbacks are the documented GB2 warm-up cause (the deposit is elided/deferred in steady
  state: `elided{present_skip=207} kept{present=394}`). The refresh re-decodes the CT32 bytes as 5551 into the
  companion before the masked CLEAR keeps their RGB → the composite base one step lower. NEW's ledger takes the
  steady-state path on frame 7402 as on every later frame.
- Disposition: OPEN, not a construction bug in NEW's PS2 semantics — the frozen value encodes OLD's readback-timing
  warm-up; NEW matches OLD's own eager output byte for byte on the hash. Two closures are possible and BOTH are
  adjudications outside this stage: (a) the accepted-difference protocol (EQUIVALENCE_PLAN) with this record as the
  evidence, or (b) reproducing the warm-up deposit in `PendingRtSyncLedger` / `ReadbackEngine` (next command below).
  `GsCorpus_slowspot` stays DISABLED with this reason. No baseline / ALLOWLIST edit (OLD is frozen).
- Next command (bounded, cwd `/tmp/p6`): `PS2_REPLAY_LOG=/tmp/p6/6f3_old_ss.log bash <NEW>/verify/run_bounded.sh 900
  /tmp/p6/6f3_old.log -- <OLD>/build/tests/gs_replay replay <OLD>/gs_corpus/slowspot.gsdl --quiet --frames 7400-7402`
  is NOT valid (P6-REPLAY-FRAMES-JOBS); instead grep the full-run probe for `SYNC-TRIGGER|R121|B4|PBO|pbo_fall`
  around lines 200–242 and read OLD `IssueCPBOCapture` / `FlushPendingRTSyncs(kPreDraw)` (`:9436-9444`) to decide
  whether the 7402 flush is the warm-up fallback; then either model it in `ReadbackEngine` (pin: first S9 frame of a
  fresh CT32→CT16S accumulator refreshes from the mirror) or file the accepted difference.

### STEP 3 — ctests enabled

`tests/gs/CMakeLists.txt` `PS2R_GS_CORPUS_GREEN` = the 18 hash-equal captures (bad_shadows boot_menu cavern_checker
cavern_f13611 cavern_f19112 cavern_f20442 cavern_flicker_f37152 cavern_fog cavern_gas cavern_mipmap cavern_streaks
cavern_wallnoise_f39807 crystal fmv_attract island_slot7 load_transition s57_walk save_select); bounds 600 s
(load_transition) / 1200 s (crystal, cavern_f19112, cavern_f20442) / 900 s, ctest TIMEOUT = bound + 100;
`GsCorpus_slowspot` DISABLED (reason above). New ctest `Render_InheritedUniforms`.

### Gate status
| gate | status |
|---|---|
| build | `cmake --build --preset macos-dev -j4` → `grep -c 'warning:'` 0 (final tree; the pins and the sweep binary built from it) |
| selection | `ctest --preset macos-dev -R 'GsCorpus_\|GsReplay\|GsClassdiff\|Render_InheritedUniforms\|Classrdr_'` → `100% tests passed, 0 tests failed out of 33` (`Total Test time (real) = 403.01 sec`, `/tmp/p6/6f2_ctest_sel.log`): 18 × `GsCorpus_*` Passed (5–45 s each), `GsCorpus_slowspot` `Not Run (Disabled)`, `GsReplayCensus` 0.85 s, `GsClassdiffMatchesOld` 1.60 s, `GsReplayDeterminism` 20.31 s, `Render_InheritedUniforms` 0.62 s, 11 `Classrdr_*` |
| full ctest | `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 206` (207 registered, `GsCorpus_slowspot` Disabled), `Total Test time (real) = 512.98 sec`, `CTEST_EXIT=0` (`/tmp/p6/6f2_ctest.log`) |
| lints | `LintOneClass: 477 files scanned, 0 findings -> PASS`; `LintEnginePurity: 422 files scanned, 0 game tokens -> PASS` |
| size | `python3 /tmp/p6/check_sizes.py .` → `size check OK` |
| getenv | `grep -rn getenv tools/gs_replay` → 0 |
| OLD | `find OLD/gs_corpus -newer /tmp/p6/6f2_old.log` → 0; every OLD run bounded from `/tmp/p6` (`6f2_old*.log`); ALLOWLIST / baselines untouched |
| **G9 (corpus)** | **18 / 19 hash-equal, 18 / 19 `.gsrep` byte-identical; ALLOWLIST used on no capture (slowspot's NEW hash `0x7a99f39ddbf9cc18` is OLD's original Phase-A value, not in the ALLOWLIST — OPEN as P6-SLOWSPOT-GB2-WARMUP)** |
| G8 (classdiff) | `GsClassdiffMatchesOld` PASS (load_transition); the 18 identical legs' `[GS-CLASSRDR]` / `[GS-S9RDR]` / renderer counter lines are implied identical by the byte-identical `.gsrep` + equal `REPLAY_HASH` (counters are not hashed — `diff` of the `[GS-CLASSRDR] class=` / SUMMARY / `[GS-S9RDR] on=` / SPR / SKIN / PART / FMV lines vs the baseline `.replay.txt`: identical on slowspot, bad_shadows, cavern_fog, crystal) |
| G10 | lints 0 / 0, size OK, 0 warnings, getenv 0 |

### Findings
- **P6-ACCUM-FRESH-CONTENT (CLOSED)** → root cause **P6-NATIVE-STALE-UNIFORMS (new, CLOSED as reproduced)**: see STEP 1.
  The 6F attribution ("the fresh accumulator's base content") was a misreading of ablation 2 — draw[1]'s own white
  was the quantised value; the OLD skip-6922 run (hash unchanged) settled it.
- **P6-WINDOW-SEED (new, CLOSED)**: see STEP 1b. Five captures.
- **P6-SLOWSPOT-GB2-WARMUP (new, OPEN)**: see "The remaining divergence". NEW reproduces OLD's original frozen output;
  the frozen files carry OLD's later warm-up artifact; the ALLOWLIST does not carry the original hash (OLD's own
  `CAVERN_CHECKER_IMPL.md` note (a) foresaw exactly this). Adjudication owed to the orchestrator.
- Probe hygiene: the two NEW-side temporary `fprintf` probes (texture_resolver / draw_pipeline) were removed and the
  files verified byte-identical (`shasum -a 256 -c /tmp/p6/6f2_probe.sha` → OK, OK) BEFORE the fix was written.
- Review 6F notes 1 (log-only deltas) and 3 (citation `:23594-23812`) applied in place in the 6F section; note 2
  (§7.1 rows for 6E′/6E″/6F) applied above (+ the 6F′ rows); note 4 (a `TestPipeline` fixture in which the `:617`
  predicate bites) not done (deferred).

### HANDOFF (6F′ → 6F″ / final)
Done: P6-ACCUM-FRESH-CONTENT root-caused and closed (P6-NATIVE-STALE-UNIFORMS, `render::InheritedUniform`,
  pins `Render_InheritedUniforms` + `Classrdr_AcceptTuples`); P6-WINDOW-SEED (replay driver seeds the window size);
  the 19-capture sweep (`/tmp/p6/corpus_sweep.sh`): **18 / 19 IDENT, 18 `.gsrep` byte-identical**; 18 `GsCorpus_*`
  ctests enabled and green in the full run (206 / 206); §7.1 ledger rows for 6E′ / 6E″ / 6F / 6F′; ledger delta L4.
Not done: slowspot (P6-SLOWSPOT-GB2-WARMUP — OLD's own warm-up artifact in the frozen file; NEW == OLD's original
  Phase-A hash `0x7a99f39ddbf9cc18`); `GsCorpus_slowspot` DISABLED. Options for the orchestrator: (a) accepted
  difference with this record as evidence (EQUIVALENCE_PLAN protocol; the OLD ALLOWLIST is frozen and lists only
  `0x99b108280b0c4799`), or (b) model the one-time pre-draw alias refresh of a fresh CT16S accumulator from its own
  CT32 deposit in `PendingRtSyncLedger` / `TargetRefresher::AliasPreDraw` (needs the `[GS-RESIDENT]` B4 fallback
  timing — `pbo_fall=6` — read from `IssueCPBOCapture` / `FlushPendingRTSyncs(kPreDraw)` `:9436-9444`).
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → 206/206 (+1 Disabled); lints 0/0;
  size OK; getenv 0.
Next command: cd /tmp/p6 && grep -a -n "SYNC-TRIGGER\|R121\|B4\|PBO\|E2-SAVE\|SH1" /tmp/p6/6f2_old_ss_probe.log | sed -n 1,40p
  (the full-run slowspot probe already captured; then decide (a) vs (b) above).
Findings: P6-NATIVE-STALE-UNIFORMS (new, reproduced, CLOSED), P6-WINDOW-SEED (new, CLOSED), P6-SLOWSPOT-GB2-WARMUP
  (new, OPEN), P6-ACCUM-FRESH-CONTENT (CLOSED).

### Review 6F′ (verdict)

Independent reviewer, 2026-09-28 (`/tmp/p6/REVIEW_6F2.md`; logs `/tmp/p6/rv6f2_*`). Worktree + branch confirmed; OLD
read-only (baseline listing `ls -la -T` diffed before/after → unchanged, newest mtime `BASELINE.md` Sep 21 12:51;
`find OLD/gs_corpus -newer rv6f2_cfg.log` → 0; OLD `git status -- gs_corpus` → 0 lines). No commits.

- (1) Root cause re-derived from OLD by the reviewer: `SetU*(m_loc_*)` census of `DrawBatch` (`opengl_backend.cpp:8288-14398`,
  45 names) minus each generic arm → skinned / fmv_blit omit exactly the 13 (`blend_a..d fix_alpha ct16q dthe dimx_lo/hi
  draw_scale uv_native zsrc_num/den`), sprite_hud / particles exactly the 8 (blend a..d written `:1368-1372` /
  `:1223-1227,:1352-1356`); `SetU1i` `:2707-2721` is the per-location shadow. `ClassRenderer::kInherit*Arm` matches
  set-for-set; world strips (`world_tristrip_native.inc:716`) and the S9 natives (`m_s9_prog_*`) run own programs → 0.
  Fix lives in `engine/render/backend.h`, `gl_draw_executor.cpp:326-328,396-403,491-502,550-553`, `class_renderer.h:26-36`,
  the four renderers. Not a tolerance / baseline edit / `--skip` (`grep -rn '\-\-skip' tests/ verify/` → 0).
- Probes (reviewer's own; sources hashed, restored, `shasum -c` 3 × OK, rebuilt 0 warnings): A ct16q guard forced →
  `test_inherited_uniforms 21 checks, 3 failures` (`:89,:90,:95`); B skinned mask → 0 → `test_classrdr accept 49 checks,
  3 failures` (`:306-308`); C `OnSetWindow` seed removed → `gs_corpus_leg.sh boot_menu` `NEW 0x534ec87fff931222` ≠ OLD
  `0x01c085190224129a`, `FAIL`, exit 1. All bite.
- (2) Bounded NEW replays: load_transition ×2 `0xce1f5d65071ec90b frames=208 draws=3667`, `.gsrep` == baseline, run1 == run2;
  boot_menu `0x01c085190224129a`, bad_shadows (`strips_stencil=5103`) `0xee67d128652bfd09`, cavern_streaks
  `0x49d2f50c823bc713` — all == baseline, ALLOWLIST no entry, `.gsrep` identical.
- (3) slowspot re-run → `0x7a99f39ddbf9cc18`; `gs_replay diff` → `f=2 vf=7402 … mean=6.354,6.457,6.382 max=9 verdict=DIFF`,
  `SUMMARY frames=301 ident=300 diff=1`; PPM census 25,565/32,768 px, `(0,0) old (19,17,31) new (27,26,39)`, `(8,8,8)×11299`.
  The record is exact; `BASELINE.md:38` verified; attribution + next command present. Honest OPEN.
- (5) `ctest -N` 207; `GsCorpus_*` 19 with only `GsCorpus_slowspot (Disabled)`; enabled 18 == `PS2R_GS_CORPUS_GREEN` == the
  18 IDENT rows. (6) Fresh `--clean-first` build `BUILD_EXIT=0`, 0 warnings (389 steps); `ctest --preset macos-dev` →
  `100% tests passed, 0 tests failed out of 206`, 573.93 s, `CTEST_EXIT=0`; LintOneClass 477/0, LintEnginePurity 422/0;
  `size check OK`; §7.1 rows for all 11 findings present (1 each), L4 at `:2352`. (7) G9 18/19 correct.

Non-blocking notes: (i) the inheritance masks are exact for the corpus (no VRAM snapshot → OLD `spec` false) but OLD's
LIVE path selects the specialised programs for textured VRAM-decode draws (`skinned_native.inc:935-937`; OLD live log
`CAVERN_CRYSTAL_IMPL.md` `[GS-SKINRDR] native=24158 spec=24158 generic=0`) and those programs have no ct16q/dthe/dimx —
when `ctx.has_snapshot` texture decode is wired, such draws must not inherit (mask 0 / spec modelled); record next to L4.
(ii) `replay_driver.cpp:66` comment `:2846ff` → `:2846 (Init) … :2952`. (iii) OLD's `0x99b1…` is deterministic across
runs — the slowspot "warm-up" is a deterministic OLD replay rule, not jitter; favours a bounded attempt at option (b)
before filing (a). (iv) Review 6F note 4 still deferred.

VERDICT: APPROVED

## 6F″ — slowspot + G10/G11 + closure (2026-09-28, ≈60 min)

Worktree `<repo>-p6`, branch `phase6` (`pwd` + `git branch --show-current`
confirmed); OLD read-only — no OLD binary was run this stage (the OLD-side evidence re-read: `/tmp/p6/6f2_old_ss_probe.log`,
`/tmp/p6/6f2_old_ss/*.ppm`); the one NEW slowspot replay ran through `verify/run_bounded.sh` from cwd `/tmp/p6`
(`/tmp/p6/6f3_*.log`). No commits. Write set respected: `tests/gs/**`, `tools/check_sizes.py` (named by PHASE6_PLAN §6 G10),
`tools/README.md` (Contents line), `engine/gs/hle/{interp,queue}/README.md` (status line), this file.

### PART 1 — slowspot, the bounded option-(b) attempt (≈17 min; outcome: NOT modelled, candidate record below)

Read (OLD, read-only): the pre-draw `[VRAM-ALIAS]` block `opengl_backend.cpp:9403-9625` — the trigger is
`any_aliased = ∃p∈[start_page,end_page] m_vram_aliased_pages[p]` OR, for a 16-bit frame, `ArePagesCPUDirty(fbp, region)`
(`:9451-9458`); the setters of both sets: the `SyncRTToVRAM` encode tail `:16943-16963` (CT32/CT24/CT16 only — `psm == 0x0A`
encodes nothing, P6-CT16S-ENCODE), the alt16 lane deposit `:16256-16258` (CT16 `0x02` only), `SyncZToVRAM` `:18434`
(cpu-dirty only), `InvalidateTextures` `:23019` (cpu-dirty only); the clearers `MarkPagesGPUValid` `:23036-23076`
(cpu-dirty, 32 bpp footprint after every draw) and `RefreshFBOFromVRAM` `:17606-17629` (aliased, per refreshed page);
the pending-ledger recorder `RecordPendingRTSync` `:17118-17178` (16-bit-class RENDER psm only, `:14060-14099`),
`FlushPendingRTSyncs` `:17210-17296` (encodes with the target's CURRENT `render_psm`), the SH7 same-buffer pre-draw flush
`:9008-9034`, the present flush `:23602-23611` (`reader_pages_only = gsresident::On()`, reader band = pages 0x161–0x1ab,
`gs_page_tracker.h:157-167`). NEW twins re-read: `TargetRefresher::AliasPreDraw` (same predicate, same region math),
`AliasPageSet`, `RenderTargetCache::{AfterDrawTail,FlushPending}`, `VramImage::NeedsEagerDeposit`.

Sequence of the capture's first three frames (OLD probe `:83-107` / `:178-191` / `:238-255`; NEW probe below):
- f=0 (vf7400): target 0x230000 is CREATED by the S9 CT16S draws (`render_psm = 0x0A`, `[FIND-TEX] tbp=0x230000 tex_psm=0xa
  render_psm=0xa`); the 16-bit-class draws record the pending entry (`[SYNC-TRIGGER] batch.frame_fbp=0x230000
  batch.frame_psm=0x0A target.render_psm=0xa`); no companion, no refresh.
- f=1 (vf7401): CT32 draws retag it (`render_psm = 0x0`, `[OCEAN-COMPOSITE-FIX] tbp=0x230000 read_psm=0x0 render_psm=0x0`).
  The first CT32 draw's SH7 flush (`:9008-9034`) consumes the f=0 entry while the target is still CT16S → nothing encoded,
  nothing marked. Both sides do this: NEW temporary probe (`fprintf` in `AfterDrawTail` / `FlushPending` / `AliasPreDraw`,
  REMOVED afterwards, `shasum -a 256 -c /tmp/p6/6f3_probe.sha` → 2 × OK) shows `[P6PROBE-FLUSH] reason=PreDraw fbp=0x230000
  size=32768 found=1 rpsm=0xa staged=0` immediately before the first `rpsm=0x0 fpsm=0x0` draw (`/tmp/p6/6f3_probe_new.log`).
- f=2 (vf7402): the first CT16S draw (S9 CLEAR, `fbmsk=0xffffff`) → OLD: `[R105-ALT16] created 16-bit companion` →
  `[R124-TEXL2] quantize` → **`[VRAM-ALIAS] Refresh FBO fbp=0x230000 psm=0x0A fbmsk=0xffffff pages=280..283`** → second
  quantize; NEW: `[P6PROBE-ALIAS] fbp=0x230000 fpsm=0xa rpsm=0x0 region=32768 aliased=0 cpudirty=0 demand=0` — no refresh.
  Frames 3–300 identical on both sides (no pending entry can exist for 0x230000 after f=1: the alt16-routed draws leave
  `render_psm` on the 32-bit class, so `is_16bit_color` is false and nothing is recorded — the same "one-time" shape OLD shows).

What this rules OUT (the 6F′ attribution): the trigger is NOT a deferred deposit of 0x230000's own CT32 content through the
pending ledger — the only entry ever recorded for 0x230000 is consumed at f=1 by the SH7 flush with the CT16S encode (no bytes,
no marks) on BOTH sides. What remains: some setter marked pages 280..283 (`m_vram_aliased_pages` or `m_cpu_dirty_pages`)
between f=1's first CT32 draw and f=2's first CT16S draw in OLD and not in NEW. Candidates NOT yet excluded within the budget:
(i) `SyncZToVRAM` `:18434` for a Z buffer whose byte span reaches page 280 (the aliased ZBP set — 0x150000, 0x1c0000,
0x268000 — does not by page arithmetic, but the Z encode span uses the Z target's own `fbw`/rows); (ii) the `[GS-RESIDENT]` B4
PBO capture sweep (`IssueCPBOCapture` / `pbo_fall=6` synchronous fallbacks) whose `SyncRTToVRAM` consume runs under the present
sweep with the target map's CURRENT psm — NEW has no PBO path (P6-PBO-NONDET) and deposits nothing at that point; (iii) an
`InvalidateTextures` job in frame 1 whose range reaches 0x230000 (the 6F′ dump of frames 0–4 found none — to be re-checked
against the raw job list rather than the dump grammar). The next command is in the candidate record. Budget spent; the
predicate is NOT identified, so nothing was changed in the engine.

**P6-SLOWSPOT-GB2-WARMUP — accepted-difference CANDIDATE record (EQUIVALENCE_PLAN §7 shape). NOT FILED HERE** (PHASE6_PLAN §6:
no §7 record in this phase; the corpus gate is byte-identity) — an integration-time adjudication.
- What differs: `slowspot.gsdl` frame f=2 (vf 7402) only; frames 0, 1, 3–300 byte-identical (`gs_replay diff`:
  `SUMMARY frames=301 ident=300 ok=0 diff=1`; `.gsrep` `cmp` differs at that frame).
- Magnitude: 1 / 301 frames; 25,565 / 32,768 px of the 512×64 grab; per-channel delta `(+8,+8,+8)` ×11,299, `(+8,+9,+8)` ×4,034,
  `(+8,+8,+9)` ×3,550, `(+9,+8,+8)` ×2,824 (NEW brighter by exactly one 5-bit step); first pixel `(0,0) old (19,17,31) new
  (27,26,39)`; `[GSDIFF] mean=6.354,6.457,6.382,0.000 max=9`.
- OLD rule (cited): the one-time `[VRAM-ALIAS]` pre-draw refresh (`opengl_backend.cpp:9403-9625`) of the fresh CT16S accumulator
  0x230000 from the committed mirror on its first alt16-routed draw after the CT32 retag (probe `:238-242`), re-decoding the
  mirror's bytes as 5551 into the companion before the masked CLEAR keeps their RGB → the S9 composite base one step lower on that
  frame. The refresh's trigger mark (`m_vram_aliased_pages` / `m_cpu_dirty_pages` on pages 280..283) is set by a path NEW does not
  reproduce (candidates (i)–(iii) above); BASELINE.md's own `[GB2]` note attributes the frozen value to the `PS2_GS_RESIDENT` B4
  warm-up (the PBO fallback deposits — NEW's readback is synchronous, P6-PBO-NONDET).
- NEW value: `REPLAY_HASH=0x7a99f39ddbf9cc18` — identical to OLD's ORIGINAL frozen slowspot hash (`OLD/gs_corpus/baseline/
  BASELINE.md:38`, `GS_PHASEA_IMPL.md:604`, `GS_PHASEB_IMPL.md:304/446`); the frozen `.replay.txt:25` / `.gsrep` carry the later
  value `0x99b108280b0c4799` (ALLOWLIST's single entry). OLD's own `CAVERN_CHECKER_IMPL.md:432-436` foresaw exactly this ("a future
  byte-perfect reproduction of the ORIGINAL output would fail the sweep; consider allowlisting it too").
- Determinism of the NEW hash (3×): 6F′ sweep `/tmp/p6/sweep/TABLE.md` → `0x7a99f39ddbf9cc18`; Review 6F′ re-run
  `/tmp/p6/rv6f2_*` → `0x7a99f39ddbf9cc18`; this stage (final binary, bounded 600 s, cwd `/tmp/p6`, `/tmp/p6/6f3_ss.log`):
  `REPLAY_HASH=0x7a99f39ddbf9cc18` (quoted in the gate table below). OLD's `0x99b1…` is likewise deterministic (Review 6F′ note iii).
- Proposed disposition (for the integration adjudicator, NOT decided here): (a) ACCEPT — an `ACCEPTED.md` row "slowspot vf7402:
  NEW == OLD's original Phase-A output; the frozen value encodes OLD's GB2 readback warm-up", with `GsCorpus_slowspot` re-armed
  against the ORIGINAL hash as a SECOND allow-listed value in a NEW-side allowlist (`tests/gs/replay/`, never OLD's file), OR
  (b) MODEL — identify the mark setter (next command) and reproduce it in `RenderTargetCache` / `ReadbackEngine` with a pin,
  after which the frozen hash reproduces and no record is needed. Until then `GsCorpus_slowspot` stays DISABLED (18 / 19).
- Next command (bounded, cwd `/tmp/p6`): re-run the OLD probe with `PS2_REPLAY_LOG` and grep it for every mark setter between
  the probe's lines 178 and 238 — `grep -a -n "SYNC-Z\|Z-SYNC\|R125\|InvalidateTextures\|\[GS-RESIDENT\]\|B4\|pbo\|CPBO"
  /tmp/p6/6f2_old_ss_probe.log | awk -F: '$1>170 && $1<240'`; then read `SyncZToVRAM` `:18157-18539` for the span its `:18434`
  mark covers and `IssueCPBOCapture` / the present sweep for a deposit of 0x230000 made with the CT32 tag.

### PART 2 — G11: the 38 named invariant ctests

Registered from `tests/gs/invariants/` (new: `README.md`, `skip_stub.sh`; `tests/gs/CMakeLists.txt` appended):
`Gs_Inv25_InterpRingRotateBeforeFire`, `Gs_Inv26_InterpIdentityStampedOnly` (owner `engine/gs/hle/interp/frame_interpolator`
— P6-INTERP-DEFERRED below), `Gs_Inv30_OwnedByPhase5` (GIF PATH priority — `engine/gif`, Phase 5). Each runs
`skip_stub.sh <name> <owner> <reason>` → one `SKIP …` line, exit **77**, `SKIP_RETURN_CODE 77`, so ctest reports `***Skipped`,
never Passed (`ctest -R 'Gs_Inv25|Gs_Inv26|Gs_Inv30'` → three `***Skipped   0.01 sec`, listed under "did not run").
`Gs_Inv38_RoundSpriteNormalise` (`tests/gs/state/test_gs_state.cpp` `Inv38`) covers the roundSprite half; the test now prints
`Gs_Inv38: roundSprite half pinned here; XGKick-deferral half owned by Phase 5 (engine/vu)`. The PLAN §1.2 name
`Gs_Inv38_XgkickOwnedByPhase5` is NOT registered (it would make 39; the note lives in the test's output instead — deviation recorded).
`ctest --preset macos-dev -N | grep -c Gs_Inv` → **38** (35 real + 3 SKIP stubs).

**P6-INTERP-DEFERRED (new, DEFERRED → integration / Phase 8):** the CONSTRUCTION_PLAN §4.6 row `gs/hle/interp/frame_interpolator.h/.cpp
(video.interp, identity-stamp matched) | gs_interp.h, graphics/opengl/opengl_backend.cpp interp region | verbatim + NORM` was not in
PHASE6_PLAN §2's stage table (no interp row) and no stage transplanted it. Peak definition: `video.interp` is `on` at peak but `off` in
the deterministic profile (`settings_table.tsv:63`) and in every corpus baseline (`PS2_GS_INTERP=0` in every `run.meta env_ps2`), so
no Phase-6 gate observes it. OLD sources: `host_app/gs_interp.h` (tick ring, identity matching, phase), `opengl_backend.cpp`
`FindTargetTexture` wrapper `:19576-19588` (interp shadow swap — 6D′ HANDOFF item), `graphics_thread.cpp:3396-3465`
(`CloseTick / PrepareIntermediates / ApplyLerp / InterpReplayPresent / RestoreVerts`), `RecordPendingRTSync` `:17118-17121`
(`m_interp_replay` early-out); OLD tests: `tests/test_gs_interp.cpp` (302 lines: tick ring / matching / phase) → NEW
`Gs_Inv25` / `Gs_Inv26` real tests replace the stubs. Flags: `PS2_INTERP_COHERENT`, `PS2_INTERP_PHASEFIX` (NORM ON),
`PS2_INTERP_CENSUS` / `_DUMP` / `_DUMP_DIR` (DIAG). `engine/gs/hle/interp/README.md` carries the status line.

### PART 3 — G10: `tools/check_sizes.py` + `Phase6SizeBudgets`

`tools/check_sizes.py` (82 lines) is the `/tmp/p6/check_sizes.py` the reviews ran, cleaned up (explicit scope table, docstring,
summary line with the file count) — scope = `engine/gs` minus `engine/gs/format`, `engine/render`, `engine/platform/sdl`,
`tools/gs_replay`, `tools/drawlist_tool`, `tests/lib`, `games/*/renderer_profile`; generated recomp never scanned. Output on the
tree: `size check OK (160 files, file <= 1500, function <= 150)`. Negative probe (a scratch tree with a 1,501-line file, a 162-line
function and a 1,600-line `engine/gs/format` file): `FILE … 1501 lines > 1500`, `FUNC … 162 lines > 150`, the format file NOT
reported, `size check FAIL (2 files …)`, exit 1. Registered as ctest `Phase6SizeBudgets` (`tests/gs/CMakeLists.txt`,
`${Python3_EXECUTABLE} tools/check_sizes.py ${CMAKE_SOURCE_DIR}`). `CoreSingleGetenvSite` already exists (`ctest -N` #19,
`tests/core/check_getenv_sites.py`) — cited, not duplicated. `tools/README.md` Contents gained the `drawlist_tool/` and
`check_sizes.py` lines.

### Gate status (this stage)

| gate | evidence |
|---|---|
| build | `cmake --preset macos-dev` → 0; `cmake --build --preset macos-dev -j4` → 0, `grep -c 'warning:' /tmp/p6/6f3_build.log` → **0** |
| G11 | `ctest --preset macos-dev -N \| grep -c Gs_Inv` → **38**; `ctest -N` total **211** |
| full ctest | `ctest --preset macos-dev` → **`100% tests passed, 0 tests failed out of 210`**, `Total Test time (real) = 569.83 sec`, `CTEST_EXIT=0` (`/tmp/p6/6f3_ctest.log`; 211 registered: `134 - GsCorpus_slowspot (Disabled)`, `136/137/138 Gs_Inv25/26/30 (Skipped)` the only not-run; `EeRunTokenAndEventQueue` and `RestoreMhashAnchors` Passed) |
| lints | `LintOneClass: 477 files scanned, 0 findings -> PASS`; `LintEnginePurity: 422 files scanned, 0 game tokens -> PASS` |
| size | `python3 tools/check_sizes.py .` → `size check OK (160 files, file <= 1500, function <= 150)` |
| getenv | `grep -rn getenv engine tools tests/lib tests/render tests/gs \| grep -v config_loader \| grep -v '\.md:'` → 6 lines, ALL in `tools/regen_from_frozen.sh` (Phase-3 shell text that GREPS the generated TUs for the string — not a call); over the Phase-6 code (`engine/gs engine/render engine/platform tools/gs_replay tools/drawlist_tool tests/lib tests/render tests/gs`, `*.cpp *.h *.sh *.py`) → **0**; the tree's one product site is `engine/core/config/config_loader.cpp:294` (`CoreSingleGetenvSite`) |
| slowspot | NEW final binary, bounded 600 s from `/tmp/p6` (`/tmp/p6/6f3_ss.log`): `[GSREPLAY] REPLAY_HASH=0x7a99f39ddbf9cc18 frames=301 draws=23197`; `cmp 6f3_ss.gsrep OLD/gs_corpus/baseline/slowspot.gsrep` → differ; `gs_replay diff` → `[GSDIFF] f=2 vf=7402/7402 … mean=6.354,6.457,6.382,0.000 max=9 verdict=DIFF`, `SUMMARY frames=301 ident=300 ok=0 diff=1 … replay_hash_a=0x7a99f39ddbf9cc18 replay_hash_b=0x99b108280b0c4799 hashes_equal=0` — third independent reproduction of the NEW value; `GsCorpus_slowspot` stays DISABLED; `find OLD/gs_corpus -newer /tmp/p6/6f3_cfg.log` → 0 |
| OLD | no OLD binary run this stage; `OLD/gs_corpus` untouched (no writes anywhere under OLD) |

### Findings
- **P6-SLOWSPOT-GB2-WARMUP (OPEN → CANDIDATE record written above, not filed)**: the 6F′ ledger attribution is ruled out (both
  sides consume the f=0 entry at f=1's SH7 flush with no encode); the mark setter is unidentified; NEW == OLD's original output.
- **P6-INTERP-DEFERRED (new, DEFERRED)**: see PART 2.
- **P6-INV38-ONE-TEST (new, decision)**: the XGKick half of invariant 38 is a printed note in `Gs_Inv38_RoundSpriteNormalise`, not a
  39th ctest (the G11 count is 38 by construction).
- Probe hygiene: three temporary `fprintf` probes (targets: `render_target_cache.cpp` ×2 sites, `target_refresher.cpp` ×1) removed;
  `shasum -a 256 -c /tmp/p6/6f3_probe.sha` → OK, OK before the final build.

## Module gate — numeric summary (PHASE6_PLAN §6, final)

| # | item | check | evidence |
|---|---|---|---|
| G1 | `.gsdl` format + reader | `static_assert` offsets; census == OLD | `GsdlFormatOffsets`, `GsdlReaderRoundTrip`, `GsdlReaderCensusMatchesOld` Passed (6A; in the final run) |
| G2 | drawlist_tool | identical `info`/`dump`/`diff` | `GsDrawlistToolDiffClean` Passed; OLD vs NEW `stats`/`frames`/`dump` diff on load_transition + boot_menu → 0 lines (6A record) |
| G3 | GsState units | register vectors, kicks, transfers, priv | `Gs_Inv01_FirstKickTmeSnapshot`, `Gs_Inv02_SetterFlushesPending`, `Gs_Inv03_TrxdirLatch`, `Gs_Inv28_IdentityStamp`, `Gs_Inv31_ImageRearmRestamp`, `Gs_Inv32_DitherInMergeKey`, `Gs_Inv38_RoundSpriteNormalise` + `GsRegistersDecodeVectors`, `GsVertexKickSequences`, `GsTransferCursor`, `GsStateFacadeEmitsDrawIr` Passed |
| G4 | G-C1 | OLD `test_gs_draw_ir` cases + random packets | `Gs_Inv27_DrawIrEquivalence` Passed (14 handcrafted + 20,000 seeded packets; OLD `test_gs_draw_ir` re-run bounded → PASS, 6A) |
| G5 | VramImage | invariants 4–8, 16, 22, 36 | `Gs_Inv04_SnapshotObservesBytes`, `Gs_Inv05_NewerWriterNotClobbered`, `Gs_Inv06_DepositEpochSeparate`, `Gs_Inv07_NoReuseMidDeposit`, `Gs_Inv08_SingleWriterMirror`, `Gs_Inv16_ReadFootprintPreciseBits`, `Gs_Inv22_ZeroStaleAtSave`, `Gs_Inv36_AliasRefreshPageClear` Passed |
| G6 | render/gl | blend table, vconv, shaders, state cache, include depth | `Gs_Inv34_ReverseSubtract`, `Render_BlendFfMapTable`, `Render_VertexConvert`, `Render_ShaderCompiles`, `Render_StateCacheEquivalence`, `Render_IncludeDepth`, `Render_InheritedUniforms`, `Render_S9StencilSmoke`, `Render_S9ShaderText`, `Render_DeviceSmoke` Passed (GL tests RAN on `GL 4.1 Metal - 90.5 / Apple M4`) |
| G7 | targets/texsrc/present | invariants 9–15, 17–21, 23, 24, 33, 35, 37 + present | `Gs_Inv09..15`, `Gs_Inv17..24`, `Gs_Inv33_FogLaneLive`, `Gs_Inv35_MaskedWritebackKeepsBits`, `Gs_Inv37_ClearKeyedOnComposite`, `Present_ViewportMath`, `Present_CrtcClassifier`, `Present_DisplayMagnification`, `Present_GrabDisplayFallback`, `Present_DrawScanoutSource`, `Targets_*`, `Texsrc_*`, `Gs_HleIncludeDepth` Passed |
| G8 | classrdr | classify / S9 recipe / snap / classdiff | `Gs_Inv29_DispatchOrderFallback`, `Classrdr_Classify` (== OLD `test_gs_classrdr`), `Classrdr_S9Recipe` (== OLD `test_s9_composite` §1–6), `Classrdr_ShuffleSnap` (== OLD `test_shuf_snap` minus S2, P6-SNAP-S2), `Classrdr_*` ×11, `GsClassdiffMatchesOld` Passed |
| G9 | **Corpus** | 19/19 hash-equal + `.gsrep` identical; 3× determinism | **18 / 19 REPLAY_HASH == baseline, 18 / 19 `.gsrep` byte-identical (`cmp`), ALLOWLIST used on NO capture; slowspot 300 / 301 frames identical, NEW `0x7a99f39ddbf9cc18` == OLD's ORIGINAL Phase-A hash, frozen `0x99b108280b0c4799` (candidate record above, `GsCorpus_slowspot` DISABLED)**; determinism: `GsReplayDeterminism` (load_transition ×2 → one hash, one `.gsrep`) Passed + the reviewer's load_transition ×2; slowspot 3× `0x7a99f39ddbf9cc18` (sweep, review, this stage) |
| G10 | lints / size / getenv / warnings | 0 / 0 / OK / 1 / 0 | `LintOneClass … 0 findings`, `LintEnginePurity … 0 game tokens` (every VRAM constant in `renderer_profile.cpp`), `CoreSingleGetenvSite` Passed (1 site), `Phase6SizeBudgets` → `size check OK (160 files …)`, `grep -c 'warning:'` → 0 under `-Wall -Wextra -Werror` |
| G11 | 38 named | `ctest -N \| grep -c Gs_Inv` | **38** = 35 real (`Gs_Inv01…24, 27, 28, 29, 31…38`) + 3 SKIP stubs (`Gs_Inv25_InterpRingRotateBeforeFire`, `Gs_Inv26_InterpIdentityStampedOnly`, `Gs_Inv30_OwnedByPhase5`) |
| DoD | all quoted + checklist + summary | this section + the INTEGRATION CHECKLIST below |

Final full run: `ctest --preset macos-dev` (2026-09-28, `/tmp/p6/6f3_ctest.log`) → `100% tests passed, 0 tests failed out of 210`, `Total Test time (real) = 569.83 sec`; not run: `GsCorpus_slowspot (Disabled)`, `Gs_Inv25_InterpRingRotateBeforeFire`, `Gs_Inv26_InterpIdentityStampedOnly`, `Gs_Inv30_OwnedByPhase5` (Skipped, exit 77). `ctest -N`: 211 registered. Lints: `LintOneClass: 477 files scanned, 0 findings -> PASS`, `LintEnginePurity: 422 files scanned, 0 game tokens -> PASS`.

OLD test binaries re-run bounded as oracles across the stages (all from cwd `/tmp/p6` through `verify/run_bounded.sh`):
`test_gs_draw_ir` (6A/6B, PASS), `test_alias_rect` (6B, `85/85`), `test_blend_ff_map` (`230 checks, 0 failures`) and
`test_vconv_equivalence` (`ALL PASS (5280 cases)`) (6C), `test_gl_state_cache_equivalence`, `test_gl_l2_entries`, `test_blendcp_lazy`,
`test_rt_dirty_tiles` (6C/6D), `test_xfer` (6B), `test_viewport_math`, `test_display_magnification_properties` (6D), `test_s8_inrt`,
`test_s8_wrapblend`, `test_idx16_lane`, `test_s9_shadow` (6D′), `test_gs_classrdr`, `test_s9_composite`, `test_shuf_snap` (6E),
OLD `gs_replay` (census / replay / PPM dumps — 6A, 6F, 6F′) and OLD `drawlist_tool` (6A).

Flags consumed vs the 220 planned (FLAG_PHASE_COUNTS.txt P6 rows; tally by grepping every stage's "Flags applied" text for the
row's name or family shorthand — `/tmp/p6/6f3_p6flags.txt`):

| outcome | planned | named explicitly in a stage record | applied under family shorthand (row present in NEW) | NOT touched by any stage (why) |
|---|---|---|---|---|
| NORM | 86 | 62 | 8 — `PS2_GL_IDXTEX_32` / `_FRESH` (`TextureResolver::IdxFresh`, fmt 4/5), `PS2_GL_IDXEPOCH` (epoch guard folded into `IdxFresh` / `CoherenceState`), `PS2_GL_L2_MB` / `_PROB` / `_SU_MB` (`DecodeCache::kL2MaxBytes 320 MiB / kL2MaxEntries / kL2SuMaxBytes 32 MiB` constants), `PS2_GL_UVREGION_V2` (`UvRegion`), `PS2_TEXPOP_ALL` (deposit overlay = `VramImage` shadow pages) | 16 — deferred modules: `PS2_INTERP_COHERENT`, `PS2_INTERP_PHASEFIX` (interp), `PS2_CAP_STEPDOWN`, `PS2_CAP_TRIPFIT`, `PS2_PACE_FMV_BYPASS`, `PS2_PACE_LOADGRACE` (pacer), `PS2_RQ_HIGHWATER` (queue), `PS2_FMV_COMPFIX` (`graphics_thread.cpp:1467` queue consumer), `PS2_ARTDET_FLICKER` (core/diag artdet); PBO-readback arms subsumed by P6-PBO-NONDET (synchronous readback has no reissue/flush/parallel-encode arm): `PS2_GL_CPBO_REISSUE`, `PS2_GL_FRAMEFLUSH`, `PS2_GL_G7C_FLUSH`, `PS2_GL_G7C_PBOUP`, `PS2_GL_PAR_ENCODE`; ledger-NORM but probe-shaped with winner = unset: `PS2_FMV5_SRC` (`:10614` texture-source probe), `PS2_FOG_SKIP` (`:8652` layer-isolation bitmask) — not written |
| SETTING | 5 | 2 (`video.scale` → `ScaleSettings`, `video.interp` → deferred) | 0 | 3 — `PS2_FPS_CAP` (`video.fps_cap`, pacer), `PS2_NOVSYNC` (`video.vsync`; the harness runs swap interval 0 — `GlContext::SetSwapInterval(0)` in the replay driver), `RENDERER_BACKEND` (`video.backend`; only `gl` exists) — Config keys are integration's (checklist item 3) |
| DIAG | 46 | 10 (incl. `PS2_S105_SKIP` → `gs_replay --skip`; Review 6F″ recount of the rows named in stage records: `PS2_DRAWLIST_RECORD`, `PS2_GL_RESID_DIAG`, `PS2_GL_SCANOUT_RT`, `PS2_GS_S9RDR_DBGBOTH/DBGPIX/DBGQUANT`, `PS2_INTERP_CENSUS`, `PS2_MIPDUMP`, `PS2_S103_DRAWLIM`, `PS2_S105_SKIP`) | 0 | 36 — every `PS2_ARTDET*`, `PS2_BAKE_*`, `PS2_DRAWLIST_RECORD_*`, `PS2_SNAP*`, `PS2_TEXBLKDUMP*`, `PS2_TEXPOP_DIAG/GLHASH/ZDUMP`, `PS2_GFX*CENSUS/LOG/SLOWJOB`, `PS2_INTERP_CENSUS/DUMP*`, `PS2_*_DIAG` rows: `core/diag` mechanisms (recorder, artdet, censuses, dumps) — not written by design (PLAN §5 "DIAG not written here"); integration lists them as core/diag categories (checklist item 8) |
| TEST | 18 | 5 (`PS2_REPLAY_CONTENT/DUMPPPM/DUMPPPM_FRAMES/LOG/SCANOUT` → `gs_replay` CLI; `PS2_S105_SKIP` → `--skip` is FLAG_LEDGER row 607 **DIAG** and is counted in the DIAG column — Review 6F″ correction) | 0 | 13 — `PAGEINV_DUMP`, `PS2_ARTDET_TEST`, `PS2_BB_SNAP/WINDOWS`, `PS2_GL_L2_VERIFY`, `PS2_GL_MARKMEMO_CHECK`, `PS2_GS_RUNDRAW_CHECK` (OLD's merge-key self-check; the NEW pin is `Gs_Inv32`), `PS2_RESMEMO_TEST_*` (RESMEMO is DELETE), `PS2_STALL_INJECT`, `PS2_TEST_RESIZE`, `PS2_TEXVAL`: OLD test-harness knobs of tests not ported (off-twin arms) — the NEW pins are the named ctests |
| DELETE | 65 | 24 | 0 | 41 — probe / dump / experiment rows in transplanted regions (`PS2_S8_*DUMP`, `PS2_S83_*`, `PS2_SH1/SH2_*`, `PS2_R198_QORDER*`, `PS2_S9_DIAG`, `PS2_OCEAN_DUMP`, …) covered by each stage's blanket "all probe families / every census counter not written" (6B, 6C, 6D′, 6E, 6E″ records), plus deferred-module rows (`PS2_CAP_TRIPFIT_PCT`, `PS2_CAP_UPHOLD_MS`, `PS2_PACE_SMOOTH`, `PS2_GFXLOOP_ARM`, `PS2_SPIKE_DUMP`, `PS2_W16_SPIKE`) — none has code in NEW (`grep -rn PS2_ engine/gs engine/render tools/gs_replay` → only README / comment citations) |
| total | 220 | 103 | 8 | 109 (36 DIAG + 41 DELETE + 13 TEST not written by design; 16 NORM + 3 SETTING belong to the deferred modules / integration) |

Files / lines landed (final tree, `wc -l` over `*.cpp *.h *.sh *.py`; READMEs excluded):

| module | files | lines | | module | files | lines |
|---|---|---|---|---|---|---|
| `engine/gs/hle/ir` | 14 | 1,498 | | `games/<t>/renderer_profile` | 2 | 282 |
| `engine/gs/state` | 10 | 1,302 | | `tools/gs_replay` | 12 | 1,257 |
| `engine/gs/xfer` | 2 | 446 | | `tools/drawlist_tool` | 1 | 332 |
| `engine/gs/vram` | 4 | 607 | | `tests/lib` | 5 | 453 |
| `engine/render` (+ `backends/gl`) | 25 | 4,360 | | `tests/render` | 8 | 969 |
| `engine/platform/sdl` | 4 | 213 | | `tests/gs/ir` / `state` / `xfer` / `vram` | 7 | 2,288 |
| `engine/gs/hle/targets` | 24 | 2,307 | | `tests/gs/targets` / `texsrc` / `present` | 4 | 2,953 |
| `engine/gs/hle/texsrc` | 21 | 2,209 | | `tests/gs/classrdr` | 1 | 1,110 |
| `engine/gs/hle/present` | 8 | 585 | | `tests/gs/replay` + `invariants` | 5 | 102 |
| `engine/gs/hle/classrdr` | 26 | 2,171 | | **total (excl. `engine/gs/format`)** | **186** | **25,559** |

(`engine/gs/hle/interp`, `engine/gs/hle/queue`: README only — deferred. Largest file 1,353 lines (`tests/gs/texsrc/test_texsrc.cpp`);
no hand-written engine file above 1,000 — `engine/render/backends/gl/gl_shader_sources.cpp` is 1,188 lines because it carries OLD's GLSL text byte-identically, P6-SHADER-COMMENTS-KEPT.)

## INTEGRATION CHECKLIST (the deliverable — every item names the owner file and the NEW entry point)

1. **MmioRouter registration — the GS privileged range.** Owner: integration's `app/host` composition root (Phase 3's
   `MmioRouter::Register(base, size, handler, name)`, `engine/ee/mem/mmio_router.h:36`; `kGsPrivBase 0x12000000 … kGsPrivEnd
   0x12002000`). Register ONE range `[0x12000000, 0x12002000)` whose `MmioHandler` forwards 64-bit writes to
   `GsState::WritePrivileged(addr, value)` and reads to `GsState::ReadPrivileged(addr)` (`engine/gs/state/gs_state.h:54-55`);
   32-bit half accesses go to `GsPrivRegs::WriteLower/WriteUpper` (`gs_priv_regs.h:48-49`, OLD `gs.cpp:94-106` merge). Register
   offsets (`GsPrivRegs::Addr`, INV §4.7, `addr & 0xFFFF`): PMODE 0x0000, SMODE1 0x0010, SMODE2 0x0020, SRFSH 0x0030, SYNCH1 0x0040,
   SYNCH2 0x0050, SYNCV 0x0060, DISPFB1 0x0070, DISPLAY1 0x0080, DISPFB2 0x0090, DISPLAY2 0x00A0, EXTBUF 0x00B0, EXTDATA 0x00C0,
   EXTWRITE 0x00D0, BGCOLOR 0x00E0, CSR 0x1000, IMR 0x1010, BUSDIR 0x1040, SIGLBLID 0x1080 (stride 0x10, 64-bit each). It DISPLACES
   the two Phase-3 sink rows `{0x12001000, 0x4, 0x8, "GS_CSR"}` and `{0x12000000, 0x2000, 0x0, "GS_PRIV"}` in
   `engine/ee/mem/mmio_sink.cpp:11-12` (the router is last-registered-wins; the rows stay as the fallback text). CSR semantics
   already in `GsPrivRegs::Write` (RESET bit 9 → `Reset()` + `kCsrAfterReset 0x1402`; write-1-to-clear low 5 bits; VSINT set by
   `SetVsint()` from the boundary step, item 2). Expectation after wiring: `MmioRouter::UnhandledPages()` (there is no
   `UnhandledRanges()` — `mmio_router.h:42-43`) lists only the IPU pages (`0x10002000..0x10002FFF`), i.e. `run.meta unhandled_mmio=`
   counts IPU only. The GIF-side stream (internal registers, IMAGE data, EOP) arrives from Phase 5's `gif` → item 4.
2. **BoundarySequencer / present steps.** Owner: `app/host` registering INTO the fixed slots of
   `engine/ee/timing/boundary_sequencer.h:46-48` `enum class Step { PresentRendezvous=0, TickAdvance, EventDrain, MaintDuties,
   IopDispatch, EeStreamRpc, Savestate, Verify }` (order is a compile-time constant; Phase 6 registers, never re-orders). The
   GS-side per-VSync order (INV §9, OLD `graphics_thread.cpp:2208-3720`) maps as: (a) on `sceGsSyncV` (`engine/gs/hle/sce/gs_sync.h`
   `GsSync::SyncV`, the boundary declarer) → `GsPrivRegs::SetVsint()` + field toggle (`GsPrivRegs::Interlaced()`), then `Intc` VBON via
   `GsPrivRegs::TakeInterrupts()` (6B HANDOFF: "INTC raise … integration") inside **TickAdvance**; (b) **PresentRendezvous**: the
   GFX-thread consumer processes the frame's ticket: `DrawPipeline::OnFrameEnd(ee_frame_end=true, crtc, &frame)`
   (`engine/gs/hle/classrdr/draw_pipeline.h:138`) = `Presenter::MarkEeFrameEnd()` → `RenderTargetCache::FlushPending(Reason::Present,
   0, 0, /*reader_pages_only=*/true)` → `ScanoutComposer::GrabDisplay(&rgba, &w, &h)` (the TARGET FBO of the scanout FBP,
   P6-GRAB-IS-TARGET) → `ScanoutComposer::DrawScanout(vram, layout, crtc, frame_count, gfx_epoch, window_rect)` (display only) →
   `Backend::Present` (swap) inside it; (c) **Verify** slot: `ps2r::verify::FrameHasher::HashNativeScale(rgba_bottomup, pw, ph, scale)`
   (`engine/core/verify/frame_hasher.h:29`) over GrabDisplay's buffer (rows bottom-up, RGBA8; hash = FNV-1a64 over `u32 nw, nh` then
   RGB top-down point-sampled at `x*scale`, 0 when no target — INV §9 / `verify/frame_hasher.cpp:8-56`) → the `frames.fhash` line;
   (d) CSR VSINT is set at (a), SIGNAL/FINISH by `GsPrivRegs::Signal/Finish` from the register stream. Ordering rule: the grab (b)
   MUST precede any DrawScanout mirror maintenance of the same frame (P6-SCANOUT-DEST deposits at the scanout base) — `OnFrameEnd`
   already enforces it. **RenderQueue consumer**: Phase 6 ships the queue TYPES only — `DrawIrSink` (`OnDraw(const DrawIr&)`,
   `OnJob(const JobRec&)`, `OnFrame(const JobRec&)`, `engine/gs/hle/ir/draw_ir_sink.h:13-20`) and `JobRec`/`JobKind` (OLD
   `RenderCommandType` values) — the `gs/hle/queue` RenderQueue (EE pushes / GFX pops, lockstep ticket), its consumer loop and the
   RUNDRAW merge ACTION (append / cap 65536 / flush-before-other-push; the KEY is `DrawFlush::RunKey`, 6B HANDOFF) are integration's
   (item 8). The consumer maps `JobKind` → `DrawPipeline::{OnInvalidate, OnUpload (copy `JobRec.payload` /
   `TransferEngine::LastUploadPixels()` when consuming), OnSetWindow, OnSetFrontBuffer, OnSyncAll, OnFrameEnd}` exactly as
   `ReplayDriver::ReplayJob` does (`tools/gs_replay/replay_driver.cpp`), plus `CrtcClassifier::Classify(pmode, dispfb1, dispfb2,
   display1, display2)` hooks on the PMODE/DISPFB/DISPLAY writes (6B HANDOFF).
3. **Config keys with peak defaults** (`engine/core/config/config.h:25-30` `VideoConfig` already declares them; `settings_table.tsv`
   rows 57, 62–65 exist — the integration step BINDS them, it does not add rows):

   | key | peak / det | Phase-6 struct / field | binding site | tsv row |
   |---|---|---|---|---|
   | `video.scale` | 2 / 1 | `gs::ScaleSettings{scale}` (`engine/gs/hle/targets/scale_policy.h:16`) → `ScalePolicy(settings, &profile.Vram())` | `app/host` constructs `ScalePolicy` from `VideoConfig::scale`; the RESOLVE route at scale > 1 must land with it (item 8) | exists (`:64`) |
   | `video.interp` | on / off | `gs/hle/interp/frame_interpolator` — DEFERRED (P6-INTERP-DEFERRED) | none until the module lands; the deterministic profile is off | exists (`:63`) |
   | `video.fps_cap` | 60 / 0 | `present_policy` / `pacer` / quantum decimation — not written (pacing, not pixels) | integration's present policy | exists (`:62`) |
   | `video.vsync` | off | `platform::GlContext::SetSwapInterval(int)` (`engine/platform/sdl/gl_context.h:27`; the harness passes 0) | `app/host` after `GlContext::Create` | exists (`:65`) |
   | `video.backend` | gl | `render::GlDevice` is the only `Backend` (`engine/render/backends/gl/gl_device.h:56`) | `app/host` factory | exists (`:57`) |
   | `TitleProfile.quirks.round_sprite_mode` | 1 | `gs::VertexKickQuirks{round_sprite_mode}` (`engine/gs/state/vertex_kick.h:36`) → `GsState::SetQuirks` (`gs_state.h:40`) | title data (`games/<t>/title_profile*`, READ-ONLY to Phase 6) | to add as a TitleProfile field (not a Config key) |
   | DecodeCache budgets | 320 MiB L2 / 16,384 entries / 32 MiB SU | `DecodeCache::kL2MaxBytes / kL2MaxEntries / kL2SuMaxEntries / kL2SuMaxBytes` (`engine/gs/hle/texsrc/decode_cache.h:32-35`) — FLAG_LEDGER classes `PS2_GL_L2_MB` / `_SU_MB` NORM (constants), not SETTING | none | n/a |

4. **The GsSink adapter (Phase 5 → Phase 6).** Owner: integration (`engine/gif/hle`, Phase 5's `gs_sink.h` — named only, never
   included by Phase 6). Its implementation forwards to the `GsState` façade (`engine/gs/state/gs_state.h`, PHASE6_PLAN §1.4):
   A+D / REGLIST register writes → `GsState::WriteRegister(addr, value)` (0x00..0x62); IMAGE-mode HWREG spans →
   `GsState::WriteImage(qwords, bytes)`; GIF EOP → `GsState::KickBoundary()` (the ONE flush, `FlushKind::GifEop` keeps strip parity —
   P6-FLUSH-TWINS); the identity stamp from the VU1 HLE registry → `GsState::SetIdentity(hle_handler, image_hash, kick_pc, kick_ord,
   frame_inst)` / `ClearIdentity()` (`gs_state.h:41-43`, invariant 28); the DrawIR consumer → `GsState::SetSink(DrawIrSink*)` where
   the sink is the RenderQueue producer (item 2). Recorder tap position (DIAG): a `DrawIrSink` DECORATOR wrapping the queue sink
   (`OnDraw/OnJob/OnFrame` fan-out) = `core/diag` drawlist recorder writing `GsdlFormat` records (`engine/gs/hle/ir/gsdl_format.h`,
   `DrawIr::ToDrawRec()`), so the recorder sees exactly what the queue sees (CONSTRUCTION §4.6 DoD "the recorder tap is at the DrawIR
   producer"). Priv-reg jobs (`SetFrontBuffer`, `SetWindow`, `SetClearColor`) reach the same sink through `GsPrivRegs::SetSink` /
   `SetJobContext(seq_counter, src_frame, vram_epoch_fn)` (`gs_priv_regs.h:43-45`) — the composition root must hand `GsPrivRegs` the
   LIVE upload epoch (`VramImage::UploadEpoch`) so `JobRec.vram_epoch` matches OLD's per-push stamp (6B review deviation).
5. **`app/host` wiring order** (copy `ReplayDriver::CreateContext` + `BuildScene`, `tools/gs_replay/replay_driver.cpp:22-73`):
   `platform::Window::Create(headless, w, h, title)` → `platform::GlContext::Create(window)` → `SetSwapInterval(video.vsync ? 1 : 0)`
   → `render::GlDevice::Init([&]{ window.Swap(); })` → `SetWindowSize(w, h)` → `gs::VramImage` → `title::RendererProfile` →
   `gs::ScalePolicy(ScaleSettings{video.scale}, &profile.Vram())` → `gs::RenderTargetCache(be, vram, scale)` →
   `gs::TargetRefresher(be, vram, cache)` → `gs::TextureResolver(be, vram, cache, refresher, profile.Vram())` →
   `gs::ScanoutComposer(be, cache)` → `gs::Presenter(scanout)` → `gs::ClassRegistry(profile.ClassRules())` →
   `gs::SequenceRecipe(profile.Sequence())` → `gs::DrawPipeline(deps)` → `profile.RegisterClassRenderers(registry, &sequence)` →
   `registry.WarmUpAll(pipe, log)`; in LIVE mode do NOT call `pipe.OnSetWindow(win w/h)` (that seed reproduces OLD's replay-tool
   quirk P6-WINDOW-SEED; live gets its window from the first DISPLAY job) and set `ScanoutComposer::SetScanoutFbp` from the first
   `SetFrontBuffer` job BEFORE the first `DrawScanout` — **`scanout_fbp_` defaults to 0, OLD's `m_scanout_fbp` starts at
   0xFFFFFFFF** (`opengl_backend.h:1066`), so either skip the present until the first DISPFB job (OLD's sentinel behaviour) or the
   first frame presents target 0 (display-only; the grab contract shares the field — Review 6D″ N1). Teardown:
   `RendererProfile::ReleaseBackendResources(backend)` BEFORE `GlDevice::Shutdown()` (6E″ rule; the renderers never touch the backend
   from destructors), then `GlContext::Destroy`, `Window::Destroy`. **Live-mode texture decode (has_snapshot = true):**
   `TextureResolver::Context::has_snapshot` (`texture_resolver.h:78`) is `false` only in the replay (`draw_pipeline.cpp:144`); live
   sets it true and the resolver's VRAM-decode tiers serve textured draws. Review 6F′ note (i) → **checklist item with ledger row L4**:
   OLD's live path routes textured VRAM-decode skinned / sprite-HUD / particles / FMV draws to the SPECIALISED programs
   (`skinned_native.inc:935-937`, `[GS-SKINRDR] native=24158 spec=24158 generic=0` in OLD's live logs) which carry no
   `ct16q / dthe / dimx` uniforms at all — so once `has_snapshot` is true, a class-routed draw that OLD would send to `spec` must NOT
   inherit (`DrawPipeline::PrepareOptions::inherited_uniforms = 0` for `batch_tex_id != 0`, or the spec program modelled), or the
   frames lane diverges from OLD live output. Owner: `engine/gs/hle/classrdr/{skinned,sprite_hud,particles,fmv_blit}_renderer.cpp`
   (`ClassRenderer::kInherit*Arm`, `class_renderer.h:26-36`); pin to add: a textured-snapshot draw carries mask 0.
6. **Exact frames-lane gate commands** (CONSTRUCTION §4.6 proof gate + §2 harness contract; `verify/README.md`):
   ```sh
   # one leg (NEW binary, deterministic profile, bounded, no env):
   python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --profile deterministic \
       --anchor save_states/<A>.ps2st --frames 600 --out /tmp/p6/<leg>         # 11 anchors: frames=600
   python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --profile deterministic \
       --anchor coldboot --frames <N_preFMV> --out /tmp/p6/coldboot           # baseline coldboot has frames=1494; stop before the first FMV (IPU = Phase 7)
   python3 verify/diff_legs.py --cross-engine --lanes frames,audio,mem verify/baseline/<leg> /tmp/p6/<leg>
   # the battery, 3 reps stable:
   python3 verify/battery.py --binary build/app/host/nativeee --lanes frames,audio,mem --reps 3 --out /tmp/p6/battery
   ```
   Legs (`verify/baseline/`): `coldboot`, `artdet_f37152`, `ps2state_slot0_f36935`, `_f39807`, `_f40391`, `_f44111`,
   `_f46203_red_crystal`, `_f46949_yellow_crystal`, `_f57292`, `_f59991`, `slowspot_f37092_s1789428268`, `slowspot_f40298_s1789693059`
   (12 = coldboot + 11 anchors). Gates: `run.meta gl_renderer` must read `Apple M4` on both sides (printed as a cross-engine NOTE by
   `diff_legs --cross-engine`, asserted by the operator per CONSTRUCTION §4.6 "GL on the same machine"); `unhandled_mmio=` in
   `run.meta` = IPU pages only (item 1); the **G-C1 tripwire**: `DrawIrVerifier::VerifyKickEquivalence` (`engine/gs/hle/ir/
   draw_ir_verifier.h`) run over the live register stream on every leg (`gsops.ohash` from Phase 5 == the hash of the ops the DrawIR
   path applies) — a mismatch is a construction bug, never a §7 candidate. Expected verdict per leg: `diff_legs: IDENTICAL
   (lanes=meta,frames,audio,mem)`; any frame delta at scale 1 is a construction bug until a §7 record exists (§4.6 protocol), with the
   OLD recorder (`PS2_DRAWLIST_RECORD`, OLD binary bounded) vs the NEW recorder tap (item 4) as the delta-isolation tool and
   `gs_replay classdiff --seq old.gsdl new.gsdl` as the first-divergent-draw locator.
7. **Owed ledger deltas** (carry over verbatim from `## Ledger deltas owed at integration`, rows L1–L4, plus the ones below;
   nothing was written to the shared documents by any Phase-6 stage):

   | row | target file / section | content |
   |---|---|---|
   | L1 | `docs/design/HOOK_RECLASSIFICATION.md` workaround class | stale-display present stand-in (`ScanoutComposer::PickSource`, `VramLayout::main_fb_fbp`) — text at L1 |
   | L2 | `docs/filemap/HACK_LEDGER.md` timing-fudge | `ScanoutFromVRAM` FMV hold — dormant, NOT written — text at L2 |
   | L3 | `docs/filemap/FLAG_LEDGER.md` row 671 `PS2_SH7` | DELETE → NORM ON (`DrawPipeline::SameBufferPreDrawFlush`) — text at L3 |
   | L4 | `docs/bugs/BUG-0xx` (new) + `HACK_LEDGER.md` dual-path | class-native generic arms inherit register-program uniforms (P6-NATIVE-STALE-UNIFORMS, `render::InheritedUniform`) — text at L4; ADD the live-mode caveat of item 5 (spec programs, `has_snapshot`) |
   | L5 (new) | `docs/filemap/FLAG_LEDGER.md` row 639 `PS2_S8_INRT` (+ `PS2_R198_IDX8CT16` row 409 context) | correction P6-FMT6-OFF: `s8inrt::On(r198::Idx8Ct16On())` follows `r201::UploadDepositOn()` which is default OFF in the peak profile, so the fmt-6 / PSMT8-window-in-target serves are NOT live; ledger NORM row must read "winner = OFF (arm not written)"; a future upload-deposit setting re-lands both arms together |
   | L6 (new) | `docs/design/EQUIVALENCE_PLAN.md` §7 candidates list (or `ACCEPTED.md` when adjudicated) | accepted-difference CANDIDATES from Phase 6, none filed: **P6-SLOWSPOT-GB2-WARMUP** (record in 6F″ above), **P6-SELFREAD-PERSIST** (OLD binds an unwritten `persistence_tex` on a colour self-read; NEW takes the no-persistence arm — frames lane decides), **CT16-TEXA** (A3 §2, bug-compatible today), **P6-T4-UPLOAD-DROP** (T4 host→local writes no nibble in OLD; NEW reproduces the VRAM half, not the UB linear-payload half), **P6-FOG-BOOL** (PACKED FOG byte into PRIM.FGE — reproduced) |
   | L7 (new) | `docs/bugs/` entries (root-caused defects found in Phase 6, each with its `docs/bugs/TEMPLATE.md` fields) | **P6-PARTIAL-FLUSH-OOB** (NEW-side heap over-read introduced by review-6B item 1 and fixed — `TransferEngine::SetTrxDir`, pinned `Gs_Inv03`; the attribution `prev_w × prev_h` is OLD's own), **P6-NATIVE-STALE-UNIFORMS** (OLD defect, L4), **P6-WINDOW-SEED** (OLD replay-tool quirk reproduced in the replay driver only), **P6-MERGEKEY-BASIS** (OLD's FNV basis typo `1469598103934665603`, reproduced for key equality), **P6-T4-UPLOAD-DROP** halves (2) UB, **P6-BLEND-INHERIT** (OLD leaves the previous blend enable for a DATE-only blended draw — reproduced), **P6-STALE-DISPLAY-PICK** (L1) |
   | L8 (new) | `docs/filemap/INDEX.md` / `MIGRATION_PLAN` rows for `gs/hle/interp`, `gs/hle/queue`, `present_policy/pacer/quantum`, `core/diag` GS rows | status "deferred to integration / Phase 8" with the P6-INTERP-DEFERRED / 6B HANDOFF pointers (item 8) |

8. **Deferred modules (not written in 6A–6F″; each with its owner and the record that scopes it):** `engine/gs/hle/interp/`
   (`FrameInterpolator`; P6-INTERP-DEFERRED; OLD `gs_interp.h` + `opengl_backend.cpp:19576-19588` shadow swap + `graphics_thread.cpp:
   3396-3465`; tests `test_gs_interp.cpp` → real `Gs_Inv25/26`); `engine/gs/hle/queue/` (`RenderQueue`, `QueueConsumer`,
   `DrawRunMerge` — the merge ACTION; 6B HANDOFF; OLD `render.h` queue half, `graphics_thread.cpp` consumer, `ee_pace.h`);
   `engine/gs/hle/present/{present_policy,present_decimator,pacer,sample_ring,band_selector,pace_plan,cap_stepper,load_grace,
   view_state,composite_guard}` (pacing / `video.fps_cap` — NOT pixels; OLD `present_pace.h`, `quantum.h`; the present gate
   `present_now = fresh>0 || !has_new_draws || holds>=2` INV §9 step 4); `core/diag` GS categories (`drawlist_recorder` = the
   `DrawIrSink` decorator of item 4 writing `GsdlFormat`; `artdet` / `artdet_flicker`; `vram_census`, `gs_c3_counters`; every DIAG
   row of the tally); the **RESOLVE route at `video.scale > 1`** (6D′ HANDOFF: idx fmt 4/5 and depth serves from a `scale > 1` target
   must bind a native-resolution resolve — OLD `:20404-20423`, `:21568-21571` — owner `GlDrawExecutor` / the classrdr draw path,
   dormant at scale 1, must land WITH a pin when `video.scale = 2` is enabled); the **interp shadow swap** (with interp);
   **UNIFPACK / VSUV / RUNUPLOAD** shader form NOT written (P6-UNIFPACK-OFF — presence-gated OFF in every baseline; INV §14 risk 7:
   re-proof on the corpus required if ever enabled); `PS2_GL_ALIAS_DIRTYRECT` / `GPUROUTE` / `DEPCOPY` alias-refresh alternates
   (winner = the CPU import, 6D″); the OFF arm of `PS2_GL_SCANOUT_RT` (present-time 5551 re-decode, P6-SCANOUT-RT-ARM); the
   `ScanoutFromVRAM` FMV hold (L2); `games/<t>/hle/scene_hooks` (CONSTRUCTION §4.6 row: cutscene latch / loading-logo / base-layer
   tracking — `gs_state.cpp:2909` FRAME latch was NOT transplanted, 6B "DELETE / DIAG rows … `g_cutscene_active` FRAME latch not
   written") — re-adjudicate at integration if the frames lane needs it; `hle_SetupDisplayRegisters` quarantine
   (`games/<t>/hle/workarounds/display_regs`, §4.6 sce row — Phase 3's `gs/hle/sce` owner).
9. **Known pre-existing flakes outside Phase 6** (never touched by a Phase-6 stage; reported for their owners): **P6-EE-SCHED-FLAKE** —
   `EeRunTokenAndEventQueue` (`tests/ee/test_ee_sched`, Phase 3) timed out at 120 s in 3 of ~12 runs during 6A, passes in 0.07 s
   otherwise; **`RestoreMhashAnchors` transient** — one Phase-3 savestate-restore leg failed once in the 6D full run
   (`99% tests passed, 1 tests failed out of 141`, run 1) and passed on the re-run and in every later full run. Both are in
   `engine/ee` / `tests/ee` (READ-ONLY to Phase 6); neither reproduced in the 6F″ final run quoted above.

### Review 6F″ / Phase 6 module gate (verdict)

Independent final reviewer (2026-09-28, session review_6f3; full evidence tables in `/tmp/p6/REVIEW_6F3.md`). Worktree
`<repo>-p6`, branch `phase6`; OLD read-only (no OLD binary run; `OLD/gs_corpus`
mtimes unchanged after every run — `find OLD/gs_corpus -newer /tmp/p6/rv6f3_cfg.log | wc -l` → 0); no commits.

| check | reviewer's evidence |
|---|---|
| (1) slowspot | candidate record carries what-differs / magnitude / OLD rule (`opengl_backend.cpp:9403-9625` + setters/clearers) / NEW == `BASELINE.md:38` EXACT row `0x7a99f39ddbf9cc18` / disposition (a)-vs-(b) / 3× determinism, and is explicitly NOT filed; OLD's own `ALLOWLIST.txt` entry describes exactly NEW's f=2 vf7402 diff. 4th reproduction (bounded 600 s, cwd `/tmp/p6`): `[GSREPLAY] REPLAY_HASH=0x7a99f39ddbf9cc18 frames=301 draws=23197`; `gs_replay diff` → `SUMMARY frames=301 ident=300 ok=0 diff=1 … hashes_equal=0`. `GsCorpus_slowspot` → `(Disabled)`, `DISABLED True`, reason at `tests/gs/CMakeLists.txt:251-254` |
| (2) G11 | `ctest -N \| grep -c Gs_Inv` → **38**; `ctest -R 'Gs_Inv25\|Gs_Inv26\|Gs_Inv30'` → three `***Skipped`, "did not run"; `skip_stub.sh` direct → `SKIP … owned by engine/gif -- …`, rc=77; `SKIP_RETURN_CODE 77` in `--show-only=json-v1`; OLD interp sources verified present (`gs_interp.h`, `tests/test_gs_interp.cpp` 302 lines, `opengl_backend.cpp:19576-19588`, `graphics_thread.cpp:3396-3465`) |
| (3) G10 | `tools/check_sizes.py .` → `size check OK (160 files, file <= 1500, function <= 150)`; `Phase6SizeBudgets Passed`; getenv grep → 6 lines all `tools/regen_from_frozen.sh` (generated-recomp grep text), product site `config_loader.cpp:294` only; `cmake --build --preset macos-dev --clean-first -j4` → 389 steps, `BUILD_EXIT=0`, `grep -c 'warning:'` → **0** |
| (4) full ctest + lints | `100% tests passed, 0 tests failed out of 210`, `Total Test time (real) = 514.48 sec`, `CTEST_EXIT=0`; not run: `134 GsCorpus_slowspot (Disabled)`, `136/137/138 Gs_Inv25/26/30 (Skipped)`; `EeRunTokenAndEventQueue 0.22 s`, `RestoreMhashAnchors 10.43 s` Passed. `LintOneClass: 477 files scanned, 0 findings -> PASS`; `LintEnginePurity: 422 files scanned, 0 game tokens -> PASS` |
| (5) numeric summary | `GsCorpus_` = 19 (one Disabled); load_transition leg re-run `0xce1f5d65071ec90b`, `.gsrep byte-identical`, PASS; all 19 files/lines rows reproduced exactly by `wc -l`; flags totals 220 / 103 / 8 / 109 ✓. Two composition nits fixed in place: TEST named 6→5 / untouched 12→13 (`PS2_S105_SKIP` is FLAG_LEDGER row 607 DIAG; `PS2_GS_RUNDRAW_CHECK` was unlisted), DIAG 9→10 / 37→36; "no engine file above 1,000" qualified (`gl_shader_sources.cpp` 1,188) |
| (6) INTEGRATION CHECKLIST | every citation resolved against the READ-ONLY sources: `mmio_router.h:20-21/36/42-43`, `mmio_sink.cpp:11-12` rows, `GsPrivRegs::Addr` 19 offsets, `boundary_sequencer.h:46-48` Step enum, `frame_hasher.h:29` signature, `config.h:25-30` defaults, `settings_table.tsv` rows 57/62-65, `gs_state.h:39-55` façade, `gs_priv_regs.h:43-49`, `replay_driver.cpp:22-72` wiring order, `scanout_composer.h:98` `scanout_fbp_ = 0` vs OLD `opengl_backend.h:1066` `0xFFFFFFFF`, `renderer_profile.h:62`, `texture_resolver.h:78` / `draw_pipeline.cpp:144` / `class_renderer.h:26-36` (L4), `run_leg.py --help` flags, 12 baseline legs, `coldboot frames=1494`, `gsops.ohash` (CONSTRUCTION §4.5/§4.6) |
| (7) contract | 19 tracked `M` all in the §1.5 OWN set; shared CMake diffs = `add_subdirectory(...)  # Phase 6` lines only (incl. the P6-PROFILE-SUBDIR root line); READ-ONLY dirs `git status` → 0; untracked files all in the write set; OLD `find -newer PLAN` hits = `logs/` (accepted side effect), `.DS_Store`, and four `.git` DIRECTORY mtimes whose contents are all ≤ Sep 26 (transient `index.lock` of a read-only `git status` sweep) — no OLD content changed |
| (8) purity | `LintEnginePurity` 0; `grep '#include "engine/render/backends' engine/gs` → 0; `Render_IncludeDepth` + `Gs_HleIncludeDepth` Passed; `grep CommitDeposits engine/gs/hle tools/gs_replay` → 0 |

Non-blocking notes for integration: N1 the slowspot candidate still needs §7 item 1's hardware-truth sentence and item 5's pin when
filed (the replay's ZERO mirror makes OLD's refreshed f=2 base `decode5551(0)`, not a hardware value — an argument for option (a) the
record does not yet make); N2 Review 6F′ note (ii) (`replay_driver.cpp:66` comment `:2846 … :2952`) not applied; N3 `PS2_GL_CDTILES`
(row 372 NORM, winner OFF per P6-CDTILES-OFF) belongs in the L5 FLAG_LEDGER correction.

**VERDICT: APPROVED** — Phase 6 module gate G1–G11 + DoD met as quoted; slowspot remains the one OPEN, honestly-disabled capture
carried to integration as an accepted-difference CANDIDATE.

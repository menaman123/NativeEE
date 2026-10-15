# PHASE 6 PLAN — `engine/gs/{state,vram,xfer,hle}` + `engine/render/{backend.h,backends/gl}` → rendered frames (MODULE GATE)

Status: planning complete (2026-09-27). Written in the `phase6` worktree
(`<repo>-p6`) in parallel with Phases 4, 5 and 7 in their own
trees; the PARALLEL-WORK CONTRACT (§1.5) is binding for every stage. `STAGE_RULES.md` binds every stage
(bounded runs, OLD read-only and never built or `git`-touched, no commits, tree green at every stop).

Sources: `CONSTRUCTION_PLAN.md` §1, §2, §4.6, §5, §6; `ARCHITECTURE.md` §3 (gs/, render/), §4.1–§4.6, §5.3, §6,
§7, §11, §13, §14.3; `PHASE2_IMPL.md` (the `gs/format` API this phase USES and never edits); `PHASE4_PLAN.md`
(format); `FILEMAP_graphics.md` (the opengl_backend REGION MAP R0–R29) and the rows for
`gs_state / gs_hle / graphics_thread / texture_decoder / gs_class_renderer / gs_page_tracker / shuf_snap /
render.h / texture_cache.h / hle/{drawlist_*, gs_drawir*, gs_direct*}`; `FLAG_LEDGER.md` (220 Phase-6 rows);
`HACK_LEDGER.md`; `docs/bugs/BUG-006…018`; `A3_GS_RENDER.md` §1–§9; `OLD/gs_corpus/{MANIFEST,CENSUS_MASTER}.md`,
`baseline/{BASELINE.md,ALLOWLIST.txt}`; and the investigator's fact base `/tmp/p6/INVESTIGATION.md`
(2,715 lines, OLD `file:line` cited throughout — every implementer stage reads its named sections).

## 1. Goal and scope

### 1.1 What the investigation changed about the plan (read first)

1. **The corpus REPLAY_HASH is a function of the GL backend path only.** OLD `tests/gs_replay` links
   `opengl_backend.cpp + drawlist_record.cpp + texture_decoder.cpp + uv_bbox.cpp + display_settings.cpp` and
   three stubs (`OLD/tests/CMakeLists.txt:1170-1178`); it does NOT link `gs_state.cpp`, `gif.cpp`,
   `graphics_thread.cpp`. Replay feeds recorded `DrawRec` (= a `RenderBatch` snapshot) straight to
   `DrawBatch`. Therefore the MODULE GATE (19 captures hash-equal) proves: the `.gsdl` reader, DrawIR record →
   backend lowering, targets, texture-source resolution, VRAM shadow/deposit rules as READ by the backend,
   class renderers, the GL device. It does NOT prove `GsRegisters / VertexKick / DrawFlush / TransferEngine`
   — those are proven here by unit tests (register-decode vectors, kick sequences → draw-buffer bytes,
   transfer cursor sequences → VRAM bytes) and the G-C1 verifier, and by the `frames` lane at integration.
2. **Both the corpus hash and the harness `frames.fhash` read the TARGET FBO of the scanout FBP** (OLD
   `GrabDisplayRGBA` `opengl_backend.cpp:25076-25103`; `verify/frame_hasher.cpp`), not the composed scanout.
   `DrawScanout`/`ScanoutFromVRAM`/`DrawScanoutFromRT` are display-only. The module gate needs a
   `ScanoutComposer` only as far as the grab (`ScanoutFbp`, `most_recent_drawn_fbp` fallback, physical dims);
   the display path is written but proven only visually / at integration.
3. **The baseline shader arm is the UNTRANSFORMED fragment source** with `#define GSMIP_AAF 1` and
   `#define S83_IDXSCALE 1` injected after `#version 410 core`, scalar uniforms, `u_uv_mode == 0`
   (CPU-prescaled UVs). `PS2_GL_UNIFPACK / VSUV / RUNUPLOAD` are presence-gated OFF and absent from the
   baseline (`env_ps2{n=0}`). NEW authors that exact text as its single source (UNIFPACK form is deferred:
   INVESTIGATION §14 risk 7 — re-proof on the corpus would be required).
4. In replay the 4 MiB mirror is the process-lifetime ZERO vector, GPU deposits land in the shadow and are
   never committed, uploads become GL textures keyed by VRAM address (`UploadVRAMTextureDirectly`), and every
   EE epoch is 0 (INVESTIGATION §0, §2.2, §2.10). The NEW `VramImage` must reproduce these outcomes under the
   replay driver (no scheduled `CommitDeposits`; `ReadView` = mirror ⊕ pending shadow).
5. `PS2_GL_ALIAS_*`, `RESMEMO`, compute-raster, Vulkan, the ~60 probe families are not written (FLAG_LEDGER
   DELETE/DIAG); the FILEMAP REGION MAP's "live at peak" columns (INVESTIGATION §13) are the transplant list.

### 1.2 End state (module gate)

- `tools/gs_replay` (NEW) replays the 19 `OLD/gs_corpus/*.gsdl` through NEW `gs/hle/ir → gs/hle/{targets,
  texsrc,classrdr} → render/backends/gl` and reproduces every `OLD/gs_corpus/baseline/<cap>.replay.txt`
  `REPLAY_HASH` (ALLOWLIST = the single `slowspot 0x99b108280b0c4799` entry), byte-identical `.gsrep`.
- `tools/drawlist_tool` output identical to OLD's on the corpus (`info`, `dump`, `diff` grammar).
- G-C1 verifier (`DrawIrVerifier`) green on OLD `test_gs_draw_ir`'s cases + random packets.
- Invariants 1–38 (A3 §9) each a NAMED ctest in the owning module (INVESTIGATION §12 map); 30 (GIF PATH
  priority) and 38's XGKick half are Phase 5's and are registered here as `Gs_Inv30_OwnedByPhase5` /
  `Gs_Inv38_XgkickOwnedByPhase5` SKIP-with-message stubs so the checklist column is complete.
- `LintOneClass` 0, `LintEnginePurity` 0, getenv sites 1, 0 warnings, tree green.
- `PHASE6_IMPL.md` ends with the INTEGRATION CHECKLIST + the numeric module-gate summary.

### 1.3 NOT this phase (integration, serial after Phases 4/5)

`frames` lane on the 11 anchors + pre-FMV coldboot; `MmioRouter` registration of the GS priv range;
`BoundarySequencer` GS steps (arbiter drain, VSync push, present rendezvous, CSR writes); `Config` keys /
`settings_table.tsv` / `FLAG_LEDGER` / README edits; `app/host` wiring; the adapter from Phase 5's `GsSink`;
`RenderQueue` consumer threading (the queue types are written, the GFX thread loop is integration).

### 1.4 Input boundary (what this phase's `GsState` exposes)

```cpp
class GsState {                       // engine/gs/state/gs_state.h — the façade Phase 5 / integration feed
  void WriteRegister(uint8_t addr, uint64_t value);   // internal regs 0x00..0x62 (A+D address space)
  void WriteImage(const uint8_t* qwords, size_t bytes); // IMAGE-mode HWREG spans (TRXDIR host->local)
  void KickBoundary();                                  // GIF EOP: the ONE flush → DrawIR to the sink
  void WritePrivileged(uint32_t addr, uint64_t value);  // 0x12000000.. priv regs (PMODE..BUSDIR)
  uint64_t ReadPrivileged(uint32_t addr) const;         // CSR/SIGLBLID reads
  void SetSink(DrawIrSink*);                            // DrawIR + JobRec consumer (RenderQueue / recorder / replay)
};
```

### 1.5 PARALLEL-WORK CONTRACT (binding)

OWN: `engine/gs/**` except `engine/gs/format/**` (Phase 2) and `engine/gs/hle/sce/**` (Phase 3's boundary
owner); `engine/render/**`; `engine/platform/sdl/{window,gl_context}.{h,cpp}` only;
`games/<title>/renderer_profile.{h,cpp}`; `tests/gs/**` except `tests/gs/format/**`;
`tests/render/**`; `tests/lib/**` (new); `tools/gs_replay/**`, `tools/drawlist_tool/**`;
`docs/phases/PHASE6_{PLAN,IMPL}.md`. Shared files touched ONLY by appending `add_subdirectory(<path>)` lines
with a `# Phase 6` comment: root `CMakeLists.txt`, `tests/CMakeLists.txt`, `engine/gs/CMakeLists.txt`,
`engine/platform/CMakeLists.txt`. READ-ONLY: everything else (`engine/core`, `engine/ee`, `engine/iop`,
`engine/{dmac,gif,vif,vu}` — may not exist, never depended on, `app/**`, `games/**/{recomp,hle,iop,vu1_library,
title_profile*}`). Settings live in module-local `<Module>Settings` structs with the peak default; the
intended `Config` keys are listed in the INTEGRATION CHECKLIST.

## 2. Module table

Conventions: one primary type per `.h/.cpp` pair, `snake_case(type)`; namespace `ps2r::gs` (state/vram/xfer/
hle), `ps2r::render` (backend), `ps2r::platform` (sdl). Every file ≤ ~1,500 lines, every function ≤ ~150
lines. Comments state the GS rule (GS User's Manual / ps2tek section), never history. "INV §n" = the
INVESTIGATION section the implementer reads.

### 2.1 `engine/gs/hle/ir/` — the DrawIR record family (stage 6A; INV §1, §5)

| file | type | OLD source | notes |
|---|---|---|---|
| `gsdl_format.h/.cpp` | `GsdlFormat` (constants + the packed on-disk structs `FileHeader 24 B`, `RecordHeader 8 B`, `JobRec 52 B`, `VertexRec 36 B`, `DrawRec 220 B / kDrawRecSizeV1 188`, `kMagic 0x524C4432`, `kVersion 2`, `Kind {kJob=1,kDraw=2,kFrame=3,kName=4}`; `static_assert`s on every offset) | `hle/drawlist_record.h` | byte layout pinned by 16 GB of captures |
| `vertex_rec.h` | `VertexRec` (the in-flight vertex: x,y post-XYOFFSET floats, z as float(zu), s,t,q, uv 12.4 u16, rgba8, fog8, draw_kick) — identical to the on-disk record | `drawlist_record.h:73-79`, `render.h GSVertex` | POD ≤ 30 lines beside DrawIr |
| `tex_read_view.h` | `TexReadView` (tbp BYTES, tbw px, psm, tw/th log2, tfx, tcc, cpsm, csm, csa, cld, cbp BYTES, texa {ta0,aem,ta1}, clamp {wms,wmt,minu,maxu,minv,maxv}, mmag/mmin/mxl/lcm/l/k, miptbp1/2) | `render.h BatchTextureInfo`, `gs_mipmap.h` | ARCH §4.5 input |
| `draw_ir.h/.cpp` | `DrawIr` (seq, src_frame, vram_epoch, identity {hle_handler, image_hash, frame_inst, kick_pc, kick_ord}, prim_type, gouraud, fog, use_fst, alpha_blend, scanmsk, shuffle_type, tex_view?, raster {frame,zbuf,test,alpha,scissor,colclamp,pabe,fba,fogcol,dthe,dimx}, ctx, xyoffset, verts span, bbox, `FromDrawRec()` / `ToDrawRec()` field-for-field) | `render.h RenderBatch` (31-255), `drawlist_record.cpp:237-292`, `tests/gs_replay.cpp:334-391 BuildBatch` | THE record every consumer sees; no GL types |
| `job_rec.h` | `JobRec` (type, seq, src_frame, arg1..4, vram_epoch, payload) + `JobKind` enum = OLD `RenderCommandType` values | `render.h RenderCommandType`, INV §1.9 | |
| `direct_ops.h/.cpp` | `DirectOps` (`Op` layout, `kModeReglist/kOpFlush/kOpSrcId/kOpSrcName`, `PacketToOps`) | `hle/gs_direct_ops.h` | body out of header |
| `draw_ir_lift.h/.cpp` | `DrawIrLift` (`gsdrawir` Item/Vert/Stream + `PacketToDrawIR`) | `hle/gs_draw_ir.h` (minus verify) | |
| `draw_ir_verifier.h/.cpp` | `DrawIrVerifier` (G-C1: `VerifyKickEquivalence` dual-converter canonicalizer) | `hle/gs_draw_ir.h verify::` | invariant 27 |
| `draw_ir_sink.h` | `DrawIrSink` interface (`OnDraw(const DrawIr&)`, `OnJob(const JobRec&)`, `OnFrame(...)`) | — | the boundary GsState / recorder / replay share |
| `tests/lib/gsdl_reader.h/.cpp` | `GsdlReader` (Frame/Draw/Job/Recording, name table, order-preserving interleave, pixel payload load, truncated-tail rule) | `hle/drawlist_reader.h` | test/tool support lib `ps2r_gsdl_reader` |
| `tools/drawlist_tool/drawlist_tool.cpp` | `main` (`info`, `dump`, `diff` — OLD grammar) | `tests/drawlist_tool.cpp` | INV §3 |

### 2.2 `engine/gs/state/` + `engine/gs/xfer/` — register model, kick, flush, transfers (stage 6B; INV §4, §5.6)

| file | type | OLD source | notes |
|---|---|---|---|
| `gs_registers.h/.cpp` | `GsRegisters` (ARCH §4.1: prim, env, ctx[2], xfer latch, clut_state; every setter's bit decode; `FlushPendingBeforeStateChange` = invariant 2) | `gs_state.h/.cpp` setters | pure register file |
| `gs_priv_regs.h/.cpp` | `GsPrivRegs` (PMODE, SMODE1/2, DISPFB1/2, DISPLAY1/2, EXTBUF/EXTDATA/EXTWRITE, BGCOLOR, CSR, IMR, BUSDIR, SIGLBLID; CSR read value) | `gs.h/.cpp` | INV §4.7 |
| `vertex_kick.h/.cpp` | `VertexKick` (3-slot window, strip parity, fan, sprite corner expansion + roundSprite mode-1 normalisation as a `quirks` parameter, ADC, first-kick TME snapshot = invariant 1) | `gs_state.cpp KickVertexImpl/AssembleVertex/TryKickRunNative` | ONE assembly (TERM_NATIVE/TYPED_BULK NORM) |
| `draw_flush.h/.cpp` | `DrawFlush` (THE flush → `DrawIr`; snapshot order per INV §4.5; the EOP-vs-setter twin difference as an explicit parameter — INV §14 risk 2; RUNDRAW merge key incl. dthe/dimx = invariant 32; identity stamp = invariant 28) | `gs_state.cpp Flush/SnapshotState`, `gif.cpp FlushBatch`, `render.h r160` | |
| `gs_state.h/.cpp` | `GsState` façade (§1.4) | — | |
| `xfer/transfer_engine.h/.cpp` | `TransferEngine` (BITBLTBUF/TRXPOS/TRXREG latch at TRXDIR = invariant 3; HWREG cursor advance per PSM incl. CT24 3-byte / T4 nibble per INV §4.9; completes when rrw·rrh pixels land; local→local; local→host stub; dest-page epoch stamp = invariant 31; write via `gs::Swizzle::LinearToVram`) | `gs_state.cpp WriteHWReg/TRXDIR`, `gs_hle.cpp hle_sceGsSendImage` deposit | T4 upload-drop bug-compat decision recorded (INV §14 risk 3) |
| `tests/gs/state/*`, `tests/gs/xfer/*` | register decode vectors, kick sequences, transfer cursor sequences, G-C1 | OLD `test_gs_draw_ir.cpp` + new | |

### 2.3 `engine/gs/vram/` — the single-writer mirror (stage 6B; INV §8)

| file | type | notes |
|---|---|---|
| `page_record.h/.cpp` | `PageRecord` (author Cold/Cpu/GpuTarget(fbp), upload_epoch, deposit_epoch, state Clean/Encoding/Pending/Applying, write_bits u64, dirty_band, reader_flag) + `alias_rect` band math (invariant 36) | replaces gs_page_tracker/gs_deposit_overlay/idx_epoch_guard/alias_rect/render.h epochs |
| `vram_image.h/.cpp` | `VramImage` (bytes[4 MiB], pages[512], shadow, `WriteStream`, `StageDeposit`, `CommitDeposits` (upload_epoch ≤ deposit_epoch ⇒ commit, else drop = invariant 5/8), `ReadView` = mirror ⊕ pending shadow, `MaterializePages(reason)`, `AssertNoStaleAtSave` = invariant 22, `ReaderPagesSeed(span)` from profile) | |

### 2.4 `engine/render/` — the backend contract + GL (stage 6C; INV §6.1–§6.3, §6.8–§6.10)

| file | type | OLD source |
|---|---|---|
| `backend.h` | `IRenderBackend` + `Caps`, `SurfaceId`, `SurfaceDesc`, `FenceId`, `DrawParams`, `ScanoutDesc` (§3) | `graphics/graphics_backend.h` rewritten |
| `surface_format.h` | `SurfaceFormat {Rgba8, Depth32f, R8}` | — |
| `backends/gl/gl_device.h/.cpp` | `GlDevice : IRenderBackend` (init/caps/teardown, surfaces, readback, fences, present; owns the members below) | `opengl_backend.cpp` R2, R5, R23 |
| `backends/gl/gl_shader_library.h/.cpp` | `GlShaderLibrary` (the ONE vertex + fragment + scanout sources = INVESTIGATION §6.1.a/b/c text with the two `#define`s in the baseline order; compile/link; uniform-location table) | R1 (`:1247-2258`, `:2532-2560`) |
| `backends/gl/gl_uniform_cache.h/.cpp` | `GlUniformCache` (per-location shadow compare for 1i/1f/2f/3f) | R1 `:2707-2797` |
| `backends/gl/gl_state_cache.h/.cpp` | `GlStateCache` (bind/state shadows as members) | `gl_state_cache.h` |
| `backends/gl/gl_vertex_stream.h/.cpp` | `GlVertexStream` (VBO ring + run submit; the 32-byte `GpuVertex` layout) | R19, `gpu_vertex.h` |
| `backends/gl/vertex_convert.h/.cpp` | `VertexConvert` (Untextured/UvFixed/Stq modes + fused bboxes) | `graphics/vconv.h` |
| `backends/gl/gl_texture_pool.h/.cpp` | `GlTexturePool` (acquire/retire/drop, mip upload) | R20 |
| `backends/gl/blend_ff_map.h/.cpp` | `MapAbcdToGlFF` table verbatim incl. REVERSE_SUBTRACT (invariant 34), wrap route | `blend_ff_map.h` |
| `backends/gl/gl_draw_executor.h/.cpp` | `GlDrawExecutor` (`Draw(DrawIr, TextureSource, color, depth)`: program bind, uniforms per INV §13.1 LIVE rows, blend/test/mask/scissor/depth state, `glDrawArrays`) | R7 live rows |
| `platform/sdl/window.h/.cpp`, `gl_context.h/.cpp` | `SdlWindow` (hidden 64×64 in headless/replay), `GlContext` (4.1 core, swap interval 0) | `main.cpp` SDL setup, `tests/gs_replay.cpp:475-497` |
| `tests/render/*` | blend map table (port `test_blend_ff_map`), vconv (`test_vconv_equivalence` fast-only), shader compiles + uniform table complete (GL, SKIP 77 without context), state-cache equivalence | |

### 2.5 `engine/gs/hle/targets/` + `texsrc/` + `present/` (stage 6D; INV §6.3–§6.7, §9, §10, §11)

| file | type | OLD source |
|---|---|---|
| `targets/target_identity.h`, `surface_views.h`, `coherence_state.h/.cpp`, `readback_engine.h/.cpp`, `render_target.h/.cpp` | ARCH §4.4 four-way split (alt16 = invariants 11/12; drawn_since_sync clamps = 9/10; masked keep = 35; composite-keyed clear = 37) | `TargetFBO`, R8, R11, R13, R23, `rt_copy_tracker.h` |
| `targets/target_cache.h/.cpp` | `RenderTargetCache` (keyed fbp; grow-only, retag keeps content = 13; class tags carry = 14) | R9 |
| `targets/target_refresher.h/.cpp` | `TargetRefresher` (mirror → surface upload, band scoped; `DepositUploadToLiveTargets`) | R12, R22 |
| `targets/scale_policy.h/.cpp` | `ScalePolicy` (factor from settings, default 1 here; native pins from profile = 23/24) | `gs_scale.h` |
| `texsrc/content_key.h/.cpp` | `ContentKey` (address-free = 18) | `texcache_key.h` |
| `texsrc/decode_cache.h/.cpp` | `DecodeCache` (L1 epoch/key + L2 content hash; budgets as constants) | R21, `texture_decoder.cpp` caches |
| `texsrc/clut_source.h/.cpp` | `ClutSource` (256×1 prefolded CLUT texture, content-hash keyed, `ClutLayout::ReadClut`) | R18 |
| `texsrc/uv_bbox.h/.cpp`, `window_in_target.h/.cpp`, `mip_sampler.h/.cpp` | UV-bounded region, PSMT8-window-in-RT geometry (`s8_inrt.h`), mip chain sampling params (`gs_mipmap.h`) | |
| `texsrc/texture_source.h/.cpp` | `TextureSource` + `ResolveRead` (owner match tiers COLORTEX/RESOLVE/ALT16/DEPTH 1-3/IDX 4-6/decline in INV §6.5 order, never-stale ladder = 15/16, per-call fields = 17, fmt=5 lane-live = 33, READVIEW prescale, R198 FMV clamp signature from profile) | R6, R16, `rt_neverstale.h`, `foglane_live.h`, `idx16_lane.h` |
| `texsrc/texture_decoder.h/.cpp` | `TextureDecoder` (decode a `TexReadView` over a `VramImage::ReadView` into RGBA8 via `gs/format` — the leaves INV §10 maps) | `texture_decoder.cpp` non-format parts |
| `present/scanout_composer.h/.cpp` | `ScanoutComposer` (scanout fbp/DISPFB/content dims; `GrabDisplay` = the target-FBO grab with the `most_recent_drawn_fbp` fallback; display quad path) | R14, R15, R25, R26, `GrabDisplayRGBA` |
| `present/crtc_classifier.h/.cpp` | `CrtcClassifier` (`kDualIdentical1px` rule; native pin from profile) | `gs_crtc_class.h` |
| `present/viewport_math.h/.cpp` | `ViewportMath` (verbatim) | `graphics/viewport_math.h` |
| `present/presenter.h/.cpp` | `Presenter` (per-VSync order INV §9 minus pacing/interp; `MarkEEFrameEnd`) | `graphics_thread.cpp` VSync case |
| `games/<t>/renderer_profile.h/.cpp` | `RendererProfile` with `VramLayout` (INV §11 table: display 0x70000, scene_rt 0x230000, fog lanes 0x280000/0x2F0000, s9_zcopy 0x268000, fmv_stage 0x150000, tile atlas 0x380000 stride 0x10000 CLUT +0xC00, 0x370000+0x5400, fonts 0x3F0000, ocean 0x3C0000/0x3D0000, z 0x150000; reader band pages 0x161–0x1AB; CLUT pairs), `IsNative` scanout tuple | every `fbp==0xNNNNNN` site |
| `tests/gs/{targets,texsrc,present}/*` | ports of `test_alias_rect`, `test_blendcp_lazy`, `test_rt_dirty_tiles`, `test_page_invariance`, `test_s8_inrt`, `test_idx16_lane`, `test_idx_epoch_guard`, `test_gl_l2_entries`, `test_viewport_math`, `test_display_magnification_properties`, `test_s9_shadow` predicates | |

### 2.6 `engine/gs/hle/classrdr/` (stage 6E; INV §7)

| file | type |
|---|---|
| `class_renderer.h` | `ClassRenderer` interface (`Accept` pure / `Draw` all-or-nothing / `WarmUp`) |
| `class_registry.h/.cpp` | `ClassRegistry` (register, `Dispatch` caller-runs-fallback = invariant 29, counters, `WarmUpAll`, `EmitCounters` in OLD grammar) |
| `class_classifier.h/.cpp` | `ClassClassifier` (reads `RendererProfile.stamps/raw`; `ClassName` table = the 10 census names) |
| `draw_shape_spec.h` | `DrawShapeSpec` (per-field require/forbid/any) |
| `sequence_recipe.h/.cpp` | `SequenceRecipe` FSM engine (kIdle/kPrimed/kArmed; hold rule) |
| `world_tristrip_renderer`, `skinned_renderer`, `sprite_hud_renderer`, `particles_renderer`, `fmv_blit_renderer`, `stencil_composite_renderer` `.h/.cpp` | the six lowerings, parameterized by spec + `VramLayout`; each keeps its DECLINE path |
| `texture_shuffle_snap.h/.cpp` | `TextureShuffleSnap` (`DetectColumnShuffle`, `SnapToColumn8`, kEps 0.75) |
| `renderer_profile.cpp` additions | stamps → class, Accept tuples verbatim, S9 seven-pass recipe (0x78787878, ZCOPY 0x268000, stencil base 0x0F mask 0x1F, byte == 8·stencil mod 256), FMV family F |
| `tests/gs/classrdr/*` | ports of `test_gs_classrdr`, `test_s9_composite`, `test_shuf_snap`, `test_s8_wrapblend` |

### 2.7 Tools + gate (stage 6F; INV §2, §3, §12)

`tools/gs_replay/gs_replay.cpp` (`replay|census|diff|classdiff`, exact hash/downsample/.gsrep/census grammar
of INV §2.3–§2.8; exit 77 without GL), `tools/gs_replay/replay_driver.h/.cpp` (`ReplayDriver`: the per-job
dispatch of INV §2.3 over NEW types), `tests/gs/replay/test_gs_replay.cpp` (census fixture + 3× determinism),
`tests/gs/corpus_gate.py` (ctest `GsCorpusReplayHashes`: 19 captures, bounded, ALLOWLIST-aware; each capture
also as `GsCorpus_<cap>`), the 38 invariant ctests, `GsDrawlistToolDiffClean`.

## 3. `render/backend.h` — the interface (binding for 6C–6F)

```cpp
namespace ps2r::render {
struct Caps { int max_texture_size; bool staging_readback; bool reverse_subtract; bool depth_sample;
              bool texture_barrier; bool compute; };
enum class SurfaceFormat : uint8_t { Rgba8, Depth32f, R8 };
struct SurfaceDesc { uint32_t width, height; SurfaceFormat format; bool renderable; uint8_t scale; };
struct SurfaceId { uint32_t id; };           // opaque handle; 0 = none
struct FenceId   { uint64_t id; };
struct Rect { int32_t x, y, w, h; };
struct DrawParams {                          // everything GlDrawExecutor needs beyond DrawIr + TextureSource
  SurfaceId color, depth, rt_copy /*sw-blend Cd source*/, alt16 /*companion*/, clut /*256x1*/, idx_partner;
  Rect scissor; Rect viewport; bool needs_rt_copy; uint32_t fbmsk_strategy; uint8_t scale; };
struct ScanoutDesc { SurfaceId source; Rect src; Rect dst; bool flip_y; };
class IRenderBackend {
 public:
  virtual ~IRenderBackend() = default;
  virtual Caps QueryCaps() const = 0;
  virtual SurfaceId CreateSurface(const SurfaceDesc&) = 0;
  virtual void DestroySurface(SurfaceId) = 0;
  virtual void Clear(SurfaceId color, SurfaceId depth, uint32_t rgba, float z) = 0;
  virtual void Upload(SurfaceId, const Rect&, const void* bytes, size_t stride) = 0;
  virtual void Draw(const gs::DrawIr&, const gs::TextureSource&, const DrawParams&) = 0;
  virtual void CopyRect(SurfaceId src, const Rect&, SurfaceId dst, const Rect&, uint32_t transform) = 0;
  virtual void Readback(SurfaceId, const Rect&, void* out, size_t stride) = 0;   // sync
  virtual FenceId BeginReadback(SurfaceId, const Rect&) = 0;                      // staging when caps
  virtual bool   PollReadback(FenceId, void* out, size_t stride) = 0;
  virtual void Present(const ScanoutDesc&) = 0;
  virtual void BeginFrame() = 0; virtual void EndFrame() = 0;
};
}
```

Rules: no PS2 VRAM address, no `PsmLayout` call, no page/epoch logic below this line — `gs/hle/targets`
owns surfaces by `SurfaceId`, `texsrc` resolves reads to a `TextureSource` naming a surface or decoded bytes,
`classrdr` lowers to `Draw` calls. `render/backends/gl` includes only `gs/hle/ir/*`, `gs/hle/texsrc/
texture_source.h` (the struct) and `render/backend.h` (ARCH §11 include-depth budget). A soft/GXM backend
replaces `GlDevice` without touching `engine/gs`.

### 3.1 Render-graph target table (the NAMED targets above the interface)

| name (role) | owner | source of the fbp | surfaces | scale | notes |
|---|---|---|---|---|---|
| `Scene` | `RenderTargetCache` | `VramLayout.scene_rt` (0x230000) | color RGBA8 (CT32/CT16S content tags), depth D32F, alt16 companion | native-pinned (24) | S9 accumulator; per-game-frame clear keyed on composite consumption (37) |
| `Display` | `RenderTargetCache` | `VramLayout.display` (0x70000 / 0x0) | color, depth | native | the grab target of the corpus hash and `frames.fhash` |
| `FogLaneA/B` | `RenderTargetCache` | `VramLayout.fog_lanes` (0x280000 / 0x2F0000) | color CT16 (fbmsk 0x3FFF strips), Z16 lane views | native (fognative) | fmt=5 lane-live serve (33) |
| `FmvStage` | `RenderTargetCache` | `VramLayout.fmv_stage` (0x150000) | color | native | family-F blit source; R198 clamp signature |
| `ZCopy` | `SequenceRecipe` | `VramLayout.s9_zcopy` (0x268000) | color | native | S9 pass-C |
| `Generic(fbp)` | `RenderTargetCache` | any other FRAME.FBP | color, depth, alt16 on demand | policy | grow-only, retag keeps content (13/14) |
| `ClutTex` | `ClutSource` | content hash | 256×1 RGBA8 ×2 rows (prefolded / raw) | — | never a target |
| `Decoded(key)` | `DecodeCache` | `ContentKey` (address-free) | RGBA8 texture | — | L1/L2 |
| `Uploaded(addr)` | `TargetRefresher` | upload VRAM byte address | RGBA8 texture | — | replay's `UploadVRAMTextureDirectly` |

## 4. Oracle and test strategy

- **Corpus (module gate)**: `tools/gs_replay replay <cap> --out <gsrep>` per capture through
  `verify/run_bounded.sh` (600 s each; crystal/cavern_f20442 2.3–2.5 GB); REPLAY_HASH == baseline or
  ALLOWLIST; `.gsrep` `cmp` identical; on mismatch `gs_replay diff` numbers quoted, then the first divergent
  frame escalated with OLD `PS2_REPLAY_DUMPPPM` (OLD binary, bounded, cwd `/tmp/p6`) vs NEW `--dump-ppm`.
- **OLD replay tool as oracle for intermediate stages**: `OLD/build/tests/gs_replay census <cap>` (CPU-only)
  is the oracle for the NEW `.gsdl` reader + `ClassClassifier` (6A/6E can pass before any GL exists).
- **G-C1**: NEW `DrawIrVerifier` over OLD `test_gs_draw_ir.cpp`'s handcrafted + random packets.
- **Unit ports**: §2 rows list the OLD tests each stage ports (peak arm only; gate-off arms not ported).
- **GL-needing tests** exit 77 → ctest `SKIP_RETURN_CODE 77` when no context.
- **OLD binaries** run only via `verify/run_bounded.sh` from `/tmp/p6`; OLD `logs/` never written by us.

## 5. Flags consumed (220 rows: 86 NORM · 5 SETTING · 46 DIAG · 18 TEST · 65 DELETE)

Applied at transplant time per `FLAG_LEDGER.md`; each stage's IMPL record names the rows it consumed.
NORM (winning arm only): the FAST/FAST2 family, `PS2_GL_ALIAS_RECT`, `PS2_GL_BLENDCP_LAZY`, `PS2_GL_RUNSUBMIT`,
`PS2_GS_ALT16UV`, `PS2_GS_RTKEEP`, `PS2_GS_TERM_NATIVE`, `PS2_GS_TYPED_BULK`, `PS2_GS_WRAPBLEND`, `PS2_S9_*`,
`PS2_GS_DEPOSIT_EEAPPLY`, `PS2_S8_INRT`, `PS2_GL_BIND_CACHE`, `PS2_GL_STATE_CACHE`, `PS2_R199_*`,
`PS2_GS_SNAPSHOT_FAST`, `PS2_XFER_FAST`, `PS2_R198_D1FMV`, `PS2_SH5_ALIASEPOCH`, `PS2_GS_RESIDENT(_PBO)`,
`PS2_GL_RT_NEVERSTALE`, `PS2_GL_RT_READVIEW`, `PS2_GL_UVNATIVE`, `PS2_GL_IDXSCALE`, `PS2_GL_FOGNATIVE`,
`PS2_GS_CLASSRDR(_*)`, `PS2_GS_{S9RDR,SPRRDR,SKINRDR,PARTRDR,FMVRDR}(_SPEC)`, `PS2_GS_RAWSKIN`, `PS2_SH8`,
`PS2_GS_{ABIT16,ZSRC,CLUTRAW,CT16_QUANT}`, `PS2_GL_BLEND_SUB`, `PS2_GL_SHUF_SNAP`, `PS2_GL_FOGLANE_LIVE`,
`PS2_GS_FOGLANE`, `PS2_GS_MIP_*`, `PS2_GS_RUNDRAW`, `PS2_FMV_COMPFIX`, `PS2_GL_L2_*` (constants), …
SETTING (module-local `<Module>Settings`, peak default; Config keys in the checklist): `video.scale` (1 for
gate; 2 play), `video.interp` (off gate; on play), `video.fps_cap` (60), `video.vsync`, `video.backend` (gl).
DIAG (not written here; `core/diag` categories listed for integration): drawlist recorder, artdet, censuses,
dumps. TEST: `PS2_REPLAY_DUMPPPM`-class → `gs_replay --dump-ppm DIR [--frames a-b]` CLI, `--skip SPEC`.
DELETE: `PS2_GL_RESMEMO`, `PS2_GL_IDXTEX*`, `PS2_GL_BBOX_COPY`, `PS2_GL_ALIAS_DEPCOPY*`, `PS2_FMV3_*`,
`PS2_CRYSENV_*`, `PS2_DRAW_SKIP`, `PS2_GFXLOOP_ARM`, `PS2_U3_*`, compute arm, Vulkan, all probe families.
`PS2_GL_UNIFPACK/VSUV/RUNUPLOAD`: presence-gated OFF in the baseline → NOT written (recorded as a deviation
from the FILEMAP "UNIFPACK form authored" wording; INV §0/§14 risk 7).

## 6. Module gate and Definition of Done

| # | item | check |
|---|---|---|
| G1 | `.gsdl` format + reader | `static_assert` offsets = INV §1; `GsdlReader` over all 19 captures: frame/draw/job/name counts == OLD `gs_replay census` CENSUS/PRIM/FLAGS/JOBS/HLE lines (string-equal after tag strip) — ctest `GsdlReaderCensusMatchesOld` (bounded, per capture) |
| G2 | drawlist_tool | `tools/drawlist_tool info|dump <cap>` output identical to OLD's on load_transition + boot_menu; `diff a a` clean — ctest `GsDrawlistToolDiffClean` |
| G3 | GsState units | register-decode vectors, kick sequences, transfer cursor sequences, priv regs; ctests named `Gs_Inv01_FirstKickTmeSnapshot`, `Gs_Inv02_SetterFlushesPending`, `Gs_Inv03_TrxdirLatch`, `Gs_Inv28_IdentityStamp`, `Gs_Inv31_ImageRearmRestamp`, `Gs_Inv38_RoundSpriteNormalise` |
| G4 | G-C1 | `Gs_Inv27_DrawIrEquivalence` (OLD `test_gs_draw_ir` cases + 1000 random packets) |
| G5 | VramImage | `Gs_Inv04/05/06/07/08/16/22/36_*` |
| G6 | render/gl | `Render_BlendFfMapTable` (invariant 34 → `Gs_Inv34_ReverseSubtract`), `Render_VertexConvert`, `Render_ShaderCompiles` (77-SKIP), `Render_StateCacheEquivalence` (77-SKIP); `engine/render/backends/gl` includes only the §3 allowance (lint grep in ctest `Render_IncludeDepth`) |
| G7 | targets/texsrc/present units | `Gs_Inv09..15,17,18,19(P2 ref),20(P2 ref),21,23,24,33,35,37_*`, `Present_ViewportMath`, `Present_CrtcClassifier`, `Present_DisplayMagnification` |
| G8 | classrdr | `Gs_Inv29_DispatchOrderFallback`, `Classrdr_Classify` (== OLD `test_gs_classrdr`), `Classrdr_S9Recipe` (== OLD `test_s9_composite`), `Classrdr_ShuffleSnap`; NEW `gs_replay classdiff` == OLD's per-class totals on all 19 captures (`GsClassdiffMatchesOld`) |
| G9 | **Corpus** | `GsCorpusReplayHashes`: 19/19 REPLAY_HASH == baseline or ALLOWLIST, `.gsrep` byte-identical; 3× determinism on load_transition + slowspot |
| G10 | lints / size / getenv | `LintOneClass` 0, `LintEnginePurity` 0 (every VRAM constant in `renderer_profile.cpp`), `CoreSingleGetenvSite` 1, `tools/check_sizes.py` (≤1,500 / ≤150) green, 0 warnings under `-Wall -Wextra -Werror` |
| G11 | 38 named | `ctest -N | grep -c Gs_Inv` == 38 (incl. the two Phase-5 SKIP stubs) |
| DoD | all G1–G11 quoted in `PHASE6_IMPL.md` + INTEGRATION CHECKLIST + numeric summary | |

Accepted-difference protocol: NO §7 record is expected in this phase — the corpus gate is byte-identity
against OLD's own GL output on the same machine. Candidates that MAY become records at integration (never
here): CT16-TEXA (A3 §2), T4 host→local drop (INV §14 risk 3), PACKED FOG bool (INV §14 risk 4).

## 7. Stages (≤ 45 min each; investigator → implementer → reviewer; tree green at every stop)

Each stage runs as: **implementer** subagent (writes code + tests + IMPL record section, builds, ctests) →
**reviewer** subagent (independent: re-builds, re-runs ctest + lints + size check, audits the stage's files
against this plan's rows and INV citations, verdict APPROVED / NEEDS_CHANGES with a numbered list) → loop
back to the implementer on NEEDS_CHANGES (max 2 iterations) → orchestrator records the outcome. Pre-registered
splits (`6X′`) follow the STAGE_RULES STOP rule at ~35 min.

| Stage | Scope | Green criterion |
|---|---|---|
| **6A** IR + reader + drawlist_tool | §2.1 rows; `engine/gs/hle/ir` lib `ps2r_gs_ir`; `tests/lib/gsdl_reader` lib; `tools/drawlist_tool`; tests `Gs_Inv27_DrawIrEquivalence`, `GsdlFormatOffsets`, `GsdlReaderRoundTrip`, `GsdlReaderCensusMatchesOld` (load_transition; bounded), `GsDrawlistToolDiffClean`; CMake appends (`# Phase 6`) | G1, G2, G4 |
| **6B** state + xfer + vram | §2.2, §2.3; libs `ps2r_gs_state`, `ps2r_gs_vram`; tests G3, G5 | G3, G5 |
| **6C** backend.h + GL + SDL | §2.4; libs `ps2r_render`, `ps2r_render_gl`, `ps2r_platform_sdl` (one self-contained block in `engine/platform/CMakeLists.txt`); `find_package(SDL2 CONFIG)` + `OpenGL`; tests G6 | G6 |
| **6D** targets + texsrc + present + VramLayout | §2.5; libs `ps2r_gs_hle`; `games/<t>/renderer_profile` lib `ps2r_title_renderer_profile`; tests G7 | G7 |
| **6E** classrdr + profile data | §2.6; tests G8 | G8 |
| **6F** gs_replay + corpus gate + 38 named + IMPL final | §2.7; `GsCorpusReplayHashes`; INTEGRATION CHECKLIST | G9, G10, G11, DoD |

### 7.1 Findings ledger (`P6-<NAME>` rows, filled during the stages in `PHASE6_IMPL.md`)

Pre-registered from the investigation: P6-REPLAY-BACKEND-ONLY (§1.1 item 1), P6-GRAB-IS-TARGET (§1.1 item 2),
P6-UNIFPACK-OFF (§1.1 item 3), P6-ZERO-MIRROR (§1.1 item 4), P6-FLUSH-TWINS (INV §14 risk 2),
P6-T4-UPLOAD-DROP (risk 3), P6-FOG-BOOL (risk 4), P6-CT16S-ENCODE (risk 5), P6-PBO-NONDET (risk 6 — NEW
readback synchronous at scale 1), P6-CLUT-HEAL (risk 9 → `games/<t>/hle/workarounds` at integration; the
replay outcome must still match), P6-GRAB-FALLBACK (risk 10), P6-SHADER-TEXT-EXACT (risk 12).

## 8. HANDOFF template (each stage appends one to `PHASE6_IMPL.md`)

```
### HANDOFF (6<X> → 6<Y>)
Done: <files landed; tests added (ctest names) + results; flags consumed; INV sections transplanted>
Not done: <named remainder → 6<Y>>
Gate: cmake --build --preset macos-dev -j4 → 0 warnings; ctest --preset macos-dev → N/N; lints 0; getenv 1
Next command: <exact>
Findings: <P6-… rows>
```

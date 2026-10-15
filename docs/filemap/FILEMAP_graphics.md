# FILEMAP — Shard 6: GRAPHICS (`host_app/graphics/`)

File-by-file mapping of `host_app/graphics/` (40 files, 47,030 lines measured via
`wc -l`, /tmp/s116/f6/linecounts.txt) into the refactor plan. Sources: read-only scan
of the current tree 2026-09-23; destinations per ARCHITECTURE.md §3 tree / §4.4-§4.6
/ §6, MIGRATION_PLAN §1.1 (deletions), §1.3 (opengl_backend split), §1.4 (covering
clusters), §1.6 (engine/game split rows); flag dispositions per A2 §1 registry CSV
joined against a getenv census of this shard (/tmp/s116/f6/{getenv_sites,
flag_dispositions}.txt); classification per A6 §3 (GS evidence) and §7 rows B/C.

Outcome legend (per shard instructions): **NORM** = winning arm becomes the only
code path, var + losing arm deleted; **SETTING** = Config field (peak run values
become defaults: `PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60 PS2_MTVU=1
PS2_GL_FAST=1 PS2_GL_FAST2=1 PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1`); **DIAG** =
core/diag or core/triage, compile-time, default off; **TEST-ONLY** = harness hook;
**DELETE** = concluded experiment, both arms gone. Where a var's `disposition`
column (A2's) maps mechanically (keep-as-setting→SETTING, delete-losing-arm→NORM,
delete-both→DELETE, move-to-diagnostics→DIAG, move-to-test→TEST-ONLY), only
deviations are footnoted.

A note on gates in this shard: only **132 literal getenv sites** exist in these 40
files (90 in opengl_backend.cpp). The other ~30 gates these files *consume*
(`PS2_GL_FAST`/`PS2_GL_FAST2` family: EPOCH_KEY, UNIF_CACHE, BLOCK_DECODE, TEXSUB,
LAZY_RTSYNC, FASTHASH, PAR_DECODE, HASH_L2, ALT16_GPU, SYNC_RECT, LAZY_ZSYNC,
IDX_PBO, VSUV, RUNUPLOAD, VBO_RING, FRAMEFLUSH, VCONV_FAST, TEXCACHE, plus
PS2_GS_RESIDENT, PS2_GS_DEPOSIT_EEAPPLY, PS2_GS_CLASSRDR, PS2_GS_INTERP…) are
latched in `render.h`, `texture_decoder.h`, `gs_page_tracker.h`,
`gs_deposit_overlay.h`, `gs_class_renderer.h`, `gs_interp.h` (other shards) and
reach here as `r118::`/`r120::`/`gsresident::`/`gsovl::`/`classrdr::` helper calls.
Those vars are tabled in their latch-site files; this document tables what THIS
shard's files read, and calls out the two in-shard **mirror latches**
(gl_state_cache.h:75-89 BIND_CACHE/FAST2, and the duplicated in-file latches inside
opengl_backend.cpp) as hacks. All FAST/FAST2-family sub-gates are **NORM** per the
peak configuration.

---

## host_app/graphics/opengl/opengl_backend.cpp (25,598 lines)

- **Purpose**: The entire OpenGL rendering backend AND most of the GS-coherence
  policy layer in one TU. It executes RenderBatch draws against per-fbp target FBOs
  (`DrawBatch`), decides which physical surface serves every texture read of VRAM
  (`ResolveTextureSeam` + `FindTargetTexture(Impl)` — the ~17-implementation "ONE
  rule" of A3 §3), reads rendered pixels back into the 4 MB gs_vram mirror
  (SyncRT/SyncZ/PBO engine), refreshes FBOs from CPU uploads, composes scanout and
  presents, and hosts six native class renderers as textual `.inc` includes at its
  tail so they can see ~40 file-local gate/counter namespaces.
- **Public surface**: class `OpenGLBackend : GraphicsBackend` (declared in
  opengl_backend.h) — called by graphics_thread.cpp (queue consumer), by
  gs_class_renderer.h dispatch, by gs_state.cpp/render.h capture hooks
  (FlushPendingRTSyncs, MarkPagesCPUDirty, DepositUploadToLiveTargets), and by the
  savestate path (SyncAllRTsToVRAM, DestroyAllTargetFBOs). Exports `r162::Ps2Vert/
  FragSrcForTest()` (shader-source identity for tests) and census emitters.
- **Classification**: MIXED (A6 §3.3 row C: 145 code-level game-VRAM constant hits,
  28 distinct bases — 0x230000 ×71, 0x280000 ×69, 0x150000 ×43, 0x2F0000 ×31,
  0x380000 ×28, 0x370000 ×15…). The mechanisms (targets, resolver, readback,
  present, GL device) are GENERIC-PS2; the address pins and scene special-cases are
  GAME data → `RendererProfile.VramLayout` queries.
- **Refactor destination**: SPLIT per MIGRATION §1.3/§1.6 across four engine
  subsystems + one backend, one class per file (D4). Region map below is the split
  plan; every `fbp==0xNNNNNN` compare in any region becomes a
  `profile.vram.is(role, fbp)` query (game half → `games/<title>/
  renderer_profile.cpp`).

### REGION MAP (line ranges measured from the current file)

| # | Lines | Concern | Contents | Destination (one class per file) | Shape |
|---|---|---|---|---|---|
| R0 | 1–1246 | diagnostics preamble | includes; fwd-decls of the 6 `.inc` register hooks; ~20 gate/census namespaces (r121sync/r121attr/r122/r159/r172/r161/r147/r147cnt/r148cnt/g3/depcopy/depcopy2/g7c/g8par/g8cnt/g9/w14r5g10/g10cnt); probe parsers (S8TexdumpWindow :83, AtlasWindow :117, SH1_T8HPROBE :936, OCEAN_DUMP :1181, GL_DUMPS :1188) | gate latches die in the purge; durable censuses (r121sync flush reasons, r122 GL-object lifetime) → `engine/core/diag/`; one-off probes DELETE. The r121sync *reason enum* survives as `VramImage::MaterializePages(reason)` (ARCH §4.3) | dissolved |
| R1 | 1247–2845 | shader library + uniform cache | embedded `k_ps2_vert_src`/`k_ps2_frag_src` (:1247/:1312 — authoritative copies; shaders/ps2_*.glsl are reference mirrors); `r162::` test exports (:2259); CompileShader/LinkProgram (:2264); s103uvnative/s83idxscale/s83fognative shader-transform gates (:2367-2465); `R169TransformFragSrc` UNIFPACK source transform (:2500); CreatePS2Shaders (:2532); SetU1i/U1f/U2f/U3f shadow + FlushUnifPack (:2707-2797); CreateScanoutShaders/CreateFramebuffer (:2798-2845) | `engine/render/backends/gl/gl_shader_library.{h,cpp}` (class **GlShaderLibrary** — packed-uniform shader is the ONLY emitted source; transform folded at authoring time) + `gl_uniform_cache.{h,cpp}` (class **GlUniformCache** — pack shadow is the only path); pipeline keys per ARCH §6 cache contract | split + rewritten (fold R169/S83 transforms into the static source) |
| R2 | 2846–3406 | device lifecycle | Init (:2846 — context, extensions, class-renderer registration calls, gate self-report), Shutdown (:3204, S29-SHUTDOWN context-delete skip), InitVRAMBuffer/ShutdownVRAMBuffer (:3279 — compute SSBO, hardware-gated: always false on macOS, live on GL 4.3+) | `engine/render/backends/gl/gl_device.{h,cpp}` (class **GlDevice**: init/caps/teardown). InitVRAMBuffer/Shutdown VRAMBuffer DELETE with the compute experiment (MIGRATION §1.1 explicit row). Class-renderer registration moves to app/host boot via RendererProfile (§14.3) | split; compute arm deleted |
| R3 | 3407–3776 | alias coherence tracking | markDirtyRange; AliasBandFold/Get/Reset; AliasAuthorSet/Clear/ClearPage/Classify; AliasHotInsert/Contains; AliasDepSnapshot record/take/invalidate/consume (:3563-3732); TestAliasAuthor_BumpTargetGen (:3771, test seam) | absorbed by `engine/gs/vram/page_record.{h,cpp}` (**PageRecord**: author + dirty_band fields ARE this machinery, ARCH §4.3 — the five-tracker collapse) + `engine/gs/hle/targets/coherence_state.{h,cpp}` (**CoherenceState**) | rewritten (consolidation; behavior pinned by test_alias_rect + corpus) |
| R4 | 3777–4247 (+ :8597-8598, :23029-23030, :23184-23195, :24298-24299 branches) | compute-raster experiment | SyncCPUToGPU/SyncGPUToCPU (VRAM SSBO mirror), UploadBatchUniforms/UploadVertexData (UBO/SSBO), CompileComputeShaders (loads cs_*.glsl from disk with a hand-rolled `#include` preprocessor :3994-4105), DispatchRasterizer; `m_use_compute_vram` dispatch branches in DrawBatch/DrawScanout | **DELETE** — MIGRATION §1.1 explicit row ("no compute required — the compute-raster experiment does not survive"); ARCH §6 GL 4.1 floor | deleted |
| R5 | 4248–4336 | window resize | Resize() viewport bookkeeping | `engine/render/backends/gl/gl_device.cpp` | moves |
| R6 | 4337–8287 | texture source decision (upper half) | **ResolveTextureSeam** (3,951 ln): per-draw serve orchestration — RT-vs-decode decision, r172 ResMemo replay, R159 UV-bounded region decode, decode caches consult, shuffle classification (16to32/32to16/DepthToColor/ChannelShuffle), FMV staging special-cases (:6605 `frame_fbp==0x70000 && tbp==0x150000`, :6938 `is_fmv_blit=(tbp==0x150000)`), CLUT pairing pins (:6772 `(0x380000,0x380c00)/(0x370000,0x375400)`), TEXPOP probes | `engine/gs/hle/texsrc/texture_source.{h,cpp}` — **TextureSource** + the ONE `ResolveRead(view)→{owner, transform, freshness}` (ARCH §4.5); GL binding tail → `gl_draw_executor`; address pins → RendererProfile VramLayout | rewritten (exhaustive-match resolver; A3 §3 route list is the case list) |
| R7 | 8288–14398 | draw submission | **DrawBatch** (6,111 ln): S83/DRAW_SKIP diag filters; S9 armed-sequence pre-draw hook (:8330); SPLASH-COMPOSITE-SKIP `0x280000→0x70000` (:8559-8589); FOG_SKIP (:8652); vertex conversion (vconv) + fused bboxes; blend map (blend_ff_map) + sw-blend Cd-copy decisions (rt_copy_tracker); DATE/AFAIL multi-pass arms; fbmsk emulation; scissor; z-source routing (s9_shadow ZsrcOn); alt16 route decision; uniform packing; draw-run submit; post-draw extent/tracker updates; fog-lane FBMSK=0x3fff strips (:9419, :10850, :11601, :13974-14066) | policy half (pure GS values → blend/test/mask/route decisions) → `engine/gs/hle/ir/` consumption + `engine/gs/hle/targets/render_target.cpp` coherence updates; API half → `engine/render/backends/gl/gl_draw_executor.{h,cpp}` (class **GlDrawExecutor** implementing `IRenderBackend::Draw(DrawIR, TextureSource, …)` ARCH §6); scene skips/pins → RendererProfile | split + rewritten (backend consumes DrawIR + resolved TextureSource; never re-derives) |
| R8 | 14399–15440 | alt16 companion + alias refresh | SnapshotFBO/RestorePersistence; EnsureAlt16Companion/EnsureAlt16Program (:14513/:14608); RouteAliasRefreshFromGPU (:14668); ConsumeAliasDepositSnapshot (:14899); EnsureG7AlphaProgram (:15187); RefreshAlt16FromMain (:15237 — R113 main→companion 5551 coherence blit) | `engine/gs/hle/targets/render_target.{h,cpp}` (**RenderTarget.SurfaceViews**: alt16 + alt16_stale rect, ARCH §4.4) with pack/unpack via `IRenderBackend::CopyRect`; helper programs → GlShaderLibrary | split |
| R9 | 15441–15822 | target lifecycle | GetOrCreateTargetFBO(In) (:15449 — creation, grow-only rule, [S109(ii)] pre-destroy rescue deposit, [R113] class-tag carry, retag-keeps-content), DestroyTargetFBO/DestroyAllTargetFBOs | `engine/gs/hle/targets/render_target.{h,cpp}` (**RenderTarget**) + `target_cache.{h,cpp}` (class **RenderTargetCache**, keyed fbp, PCSX2 GSTextureCache::Target analogue) | split (TargetFBO 75-field god-struct → TargetIdentity/SurfaceViews/ReadbackEngine/CoherenceState per ARCH §4.4) |
| R10 | 15823–16169 | native-res resolve reads | NativeReadFBO (scale>1 resolve-pair NEAREST downsample + freshness seqs [GE1]) | `engine/gs/hle/targets/render_target.cpp` (SurfaceViews.resolve) using `IRenderBackend::CopyRect/Readback` | moves |
| R11 | 16170–17331 | GPU→mirror deposits (color) | DepositAlt16LaneToVRAM (:16170); **SyncRTToVRAM** (:16272 — drawn-since-sync clamp [R104], per-page [VRAM-EPOCH] arbitration, gsovl shadow routing, masked-5551 keep [MASKED-SYNC], S9 materialize hook); BumpPendingGen; RecordPendingRTSync/Remove/FlushPendingRTSyncs (:17118-17331 — the R121 lazy-deposit ledger) | `engine/gs/vram/vram_image.{h,cpp}` (**VramImage**: StageDeposit/CommitDeposits, ARCH §4.3 single-writer) + `engine/gs/hle/targets/readback_engine.{h,cpp}` (class **ReadbackEngine**); the pending ledger becomes MaterializePages reasons | rewritten (gsovl generalized; oracle = corpus + [GB4] zero-stale-at-save) |
| R12 | 17332–18156 | CPU→FBO refresh | EnsureAlphaQuadResources/AcquireAlphaQuadTex/AlphaQuadRefreshCT32 (:17332-17483 — alias alpha-quad refresh); **RefreshFBOFromVRAM** (:17484 — decode mirror → upload to FBO, alias_rect row-band scoping) | `engine/gs/hle/targets/target_refresher.{h,cpp}` (class **TargetRefresher**) via `IRenderBackend::Upload`; band math already in alias_rect.h → PageRecord | split |
| R13 | 18157–18539 | GPU→mirror deposits (Z) | **SyncZToVRAM** (z_drawn_since_sync clamp [R111], Z-format PSMZ encode, z PBO fast path) | ReadbackEngine z-side + VramImage (Z rows use PSMZ layout — gs/format PsmLayout.is_z contract) | moves into rewrite |
| R14 | 18540–18795 | scanout-from-RT + interp hooks | DrawScanoutFromRT (:18540 — native FBO present when gsresident::ScanoutResolve proves ownership [GB3]); InterpSnapshotBases/InterpReplayPresent (:18624/:18665 — [GS-INTERP] shadow-target replay entry points) | `engine/gs/hle/present/scanout_composer.{h,cpp}` (class **ScanoutComposer**); interp entries → `engine/gs/hle/interp/frame_interpolator.{h,cpp}` (class **FrameInterpolator**) | split |
| R15 | 18796–19411 | scanout-from-mirror | ScanoutFromVRAM (decode DISPFB footprint from mirror via texture_decoder; kScanout demand-flush first) | ScanoutComposer (decode path) — decode via `engine/gs/format/` | moves |
| R16 | 19412–21611 | texture source decision (owner match) | FindTargetTexture wrapper (:19412 — interp shadow-swap + SH5-BIND route probe, ~190 ln) + **FindTargetTextureImpl** (:19605, ~2,000 ln, 44 returns): the serve tiers COLORTEX/RESOLVE/ALT16/DEPTH fmt1-3/IDX fmt4-6/decline — [S32-RTKEEP] serve-keep, [CX8] read-footprint precise bits, [R162/R163-ZTEX] depth serves + zoff, [R164-IDXTEX]/[R177-IDX16LANE] indexed serves, [S107-FOGLANE-LIVE] lane-live predicate, [S109] rescue deposits before every decline | `engine/gs/hle/texsrc/texture_source.cpp` — the owner-match arm of **ResolveRead** (fmt cases keep the SH5-BIND route names per ARCH §4.5); freshness proof fields replace the `m_ftt_*` loose members; interp swap stays a decorator in FrameInterpolator | rewritten (44 returns → one exhaustive match; per-call outputs become TextureSource fields) |
| R17 | 21612–21674 + 21858–21981 | test seams | TestZTex_CreateDepthTarget/RegisterZWrite/FTT; TestIdx_CreateColorTarget/RegisterColorWrite/AgeTarget/SyncRT/RefreshFromVRAM/FTT; SyncAllRTsToVRAM (:21906 — savestate quiescence, NOT test-only) | Test* seams → `tests/` fixtures (TEST-ONLY); SyncAllRTsToVRAM → VramImage::MaterializeAll + `AssertNoStaleAtSave` (ARCH §4.3) | split |
| R18 | 21675–21857 | CLUT texture cache | EnsureClutTex — 256×1 prefolded CLUT-as-GL-texture, content-hash keyed, kick-snapshot sourced [R173-IDXFRESH], CSM1 grid walk | `engine/gs/hle/texsrc/clut_source.{h,cpp}` (class **ClutSource**) — swizzle math from `engine/gs/format/clut.*` (single source) | moves + dedup |
| R19 | 21982–22223 | vertex stream + run submit | RingUploadVertices (VBO ring), BeginDrawRun/EndDrawRun, StageRunUpload ([R169-GLRUN] run-batched upload/submit) | `engine/render/backends/gl/gl_vertex_stream.{h,cpp}` (class **GlVertexStream**) — ring + run submit is the ONLY path (RUNSUBMIT/RUNUPLOAD/VBO_RING NORM) | moves |
| R20 | 22224–22495 | texture pool + mips | UploadMipLevels ([S82-MIP]), PoolRetireTexture/PoolAcquireTexture/PoolDropAll, BindTexture | `engine/render/backends/gl/gl_texture_pool.{h,cpp}` (class **GlTexturePool**); mip upload → GlDrawExecutor upload helper (mip chain descriptors ride DrawIR per ARCH §4.6) | moves |
| R21 | 22496–22991 | decoded-texture caches | ReleaseTextureRecordKeepGL/RetireCacheEntryTexture; L2Lookup/DropEntry/MarkProbation/RescueProbation/EnforceSingleUse/EnforceBudget/Insert/DropAll; InvalidateTexture/All; DecodeCacheInsert; EvictDecodedTexturesInRange — L1 epoch/content-key + L2 content-hash tiers [R125-L2FIX] | `engine/gs/hle/texsrc/decode_cache.{h,cpp}` (class **DecodeCache** — address-free ContentKey, budget from Config or constant) | moves |
| R22 | 22992–23349 | page dirty marks + uploaded textures | MarkPagesCPUDirty/MarkPagesGPUValid/ArePagesCPUDirty (per-page CPU/GPU validity bitsets); DepositUploadToLiveTargets (upload lands in live FBO too); UploadVRAMTextureDirectly/FindUploadedTexture/Enforce/EvictUploadedTexturesInRange | bitsets → **PageRecord** (the one tracker); DepositUploadToLiveTargets → TargetRefresher; uploaded-texture cache → DecodeCache tier or TargetRefresher | rewritten (tracker collapse) |
| R23 | 23350–23593 | async readback engine | IssueZPBOCapture/IssueCPBOCapture (PBO rings + fences, falling-edge captures), MaterializePBOFences ([R154-GFX2] sweep) | `engine/gs/hle/targets/readback_engine.{h,cpp}` (**ReadbackEngine** staging rings behind `Caps.staging`, ARCH §4.4/§6) | moves |
| R24 | 23594–23955 | present | Present() — present-boundary pending-deposit convergence, next-frame z PBO pre-issue, drawlist-recorder PPM tap, GPUFENCE probe, pacing handoff, SDL swap | `engine/gs/hle/present/present_policy.{h,cpp}` (class **PresentPolicy**, consumes EmuDomain stamps ARCH §4.7) + `IRenderBackend::Present`; boundary flush → VramImage.CommitDeposits at BoundaryProtocol | split |
| R25 | 23956–24096 + 24097–24207 | scanout setup + compute scanout | BindFBO/UnbindFBO/Clear/UploadScanoutBuffer/SetScanoutFBP/SetScanoutDISPFB/SetScanoutContentDimensions/ApplyScanoutViewport (viewport_math consumer); DispatchScanout (compute — DELETE with R4) | ScanoutComposer + GlDevice; DispatchScanout deleted | split |
| R26 | 24208–25075 | scanout draw (mirror path) | DrawScanout — mirror-decode scanout quad; [BULLSEYE-FBODUMP] PPM bisection probe (:24222-…); [SCANOUT-STATE] stderr heartbeat (:24211) | ScanoutComposer; probes DELETE/DIAG | moves; probes stripped |
| R27 | 25076–25290 | display grabs + gauges | GrabDisplayRGBA/DumpDisplayBMP/DumpTargetBMP (PS2_SNAP/PS2_SNAP_RT consumers), UpdateMemGauges ([R122] RSS census via mach), census emitters (r147/r148/r159/r121attr) | `engine/core/diag/` (frame grab is also the triage screenshot mechanism → core/triage per ARCH §18) | moves to DIAG |
| R28 | 25291–25316 | TU-tail textual includes | the six `#include "*_native.inc"` | dissolved — each .inc becomes its own class pair (below); D4 bans the TU-tail pattern (ARCH §13 names it the counterexample) | deleted pattern |
| R29 | 25318–25598 | fog forensics probe | S106StageProbe (PS2_S106_STAGE/DIR — stage dumps of the fog chain) | **DELETE** (one-off, solved bug) | deleted |

- **Flags consumed here** (90 getenv sites, 72 unique vars — every site listed in
  /tmp/s116/f6/getenv_sites.txt; line numbers = latch sites):

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_S8_TEXDUMP (:83,:87) | num-if-set OFF | texture-dump probe frame window | diagnostic(one-off), delete-both | DELETE |
| PS2_R121_ATTR (:163) | OFF | SyncRT deposit attribution census by draw class | experiment, delete-both | DELETE |
| PS2_GL_L2_MB (:428) | numeric (default budget) | L2 decoded-texture cache budget MB | tuning-knob, delete-losing | NORM (proven budget becomes a constant in DecodeCache) |
| PS2_GL_RESID_DIAG (:484) | OFF | residency diag logging | diagnostic, move-to-diag | DIAG |
| PS2_GL_ALIAS_GPUROUTE (:652) | numeric | alias refresh routed on GPU vs CPU decode | tuning-knob, delete-losing | NORM |
| PS2_GL_ALIAS_DEPCOPY (:671) | OFF | W14 dep-copy experiment arm | experiment, delete-both | DELETE |
| PS2_GL_ALIAS_DEPCOPY2 (:677,:703 — double latch) | OFF | W14 dep-copy v2 arm | experiment, delete-both (mirrored) | DELETE |
| PS2_GL_G7C_FLUSH (:730) | numeric | G7C unpack-PBO flush policy | tuning-knob, delete-losing | NORM |
| PS2_GL_G7C_PBOUP (:737) | numeric | G7C PBO upload path | tuning-knob, delete-losing | NORM |
| PS2_GL_PAR_ENCODE (:781) | numeric | parallel deposit encode workers | tuning-knob, delete-losing | NORM |
| PS2_GL_CPBO_REISSUE (:804) | numeric | color-PBO reissue policy | tuning-knob, delete-losing | NORM |
| PS2_GL_MARKMEMO (:837) | numeric | mark-pages memoization | tuning-knob per code; A2 diagnostic, move-to-diag | NORM (perf memo, winner ON)¹ |
| PS2_GL_MARKMEMO_CHECK (:849) | OFF | memo checker (verifies memo vs recompute) | diagnostic, move-to-diag | TEST-ONLY (equivalence check) |
| PS2_SH1_T8HPROBE (:936,:19947) | OFF | T8H serve probe window | diagnostic(one-off), delete-both | DELETE |
| PS2_OCEAN_DUMP (:1181) | OFF | ocean-scene dump probe | diagnostic, move-to-diag | DELETE (scene-keyed one-off)¹ |
| PS2_GL_DUMPS (:1188) | OFF | master dump-dir switch for GL dumps | diagnostic, move-to-diag | DIAG |
| PS2_GL_UVNATIVE (:2370) | ON-unless-0 | [S103] requantize UVs to native centers on legacy-decode fall | tuning-knob, delete-losing | NORM (named in shard NORM list) |
| PS2_GL_IDXSCALE (:2383) | ON-unless-0 | [S83] idx-view UV scale injection in frag src | tuning-knob, delete-losing | NORM |
| PS2_GL_FOGNATIVE (:2412) | ON-unless-0 | [S83] fog alias destinations pinned native-res | tuning-knob, delete-losing | NORM (invariant #24) |
| PS2_MINIF_DIAG (:2466) | OFF | minification diag | diagnostic, move-to-diag | DIAG |
| PS2_R197_D2P (:5101,:6652,:19698,:19977 — quadruple latch) | OFF | R197 D2 probe | experiment, delete-both (mirrored ×4) | DELETE |
| PS2_TEXVAL (:5886) | OFF | texture-serve validation probe | tuning-knob, delete-losing | TEST-ONLY¹ |
| PS2_S83_ZGRID (:6238) | OFF | z-grid probe | experiment, delete-both | DELETE |
| PS2_S103_ZBAND (:6345) | OFF | z-band experiment | experiment, delete-both | DELETE |
| PS2_S83_SEQSCAN (:6427) | OFF | seq-scan probe | diagnostic(one-off), delete-both | DELETE |
| PS2_TEXBLKDUMP / _DIR (:6490/:6491) | OFF | texture block dumps | diagnostic, move-to-diag | DIAG |
| PS2_S32_GREENTEX (:7108,:12868,:19726,:21823 — quadruple latch) | OFF | paint RT-served texels green (visual bisector) | experiment, delete-both (mirrored ×4) | DELETE |
| PS2_TEXPOP_DIAG (:7806) | OFF | per-draw texture-serve attribution log | diagnostic, move-to-diag | DIAG |
| PS2_TEXPOP_ALL (:7816) | numeric | deposit-overlay scope widening | tuning-knob, delete-losing | NORM |
| PS2_TEXPOP_GLHASH (:7969) | OFF | GL-side content hashing probe | tuning-knob, delete-losing | DIAG¹ |
| PS2_TEXPOP_ZDUMP (:8018) | OFF | z dump on texpop events | diagnostic, move-to-diag | DIAG |
| PS2_S83_SKIP0221 (:8296) | OFF | skip ABE=1/CC=0 family (bisector) | experiment, delete-both | DELETE |
| PS2_S83_SKIPORD (:8306) | OFF | skip draw ordinal range (bisector) | experiment, delete-both | DELETE² |
| PS2_GS_S9RDR_DBGQUANT (:8334) | OFF | S9 quant-pass histogram | diagnostic, move-to-diag | DIAG |
| PS2_DRAW_SKIP (:8413) | OFF | skip draws by pattern | experiment, delete-both | DELETE² |
| PS2_DRAW_DIAG (:8439) | OFF | per-draw diagnostic lines | diagnostic, move-to-diag | DIAG |
| PS2_ARTDET_DIAG (:8452) | OFF | artifact-detector diag arm | diagnostic, move-to-diag | DIAG (artdet FIRE path itself → core/triage, ARCH §18) |
| PS2_FOG_SKIP (:8652) | num=0 | skip fog draws (bisector) | tuning-knob, delete-losing | NORM (winner=off ⇒ arm deleted) |
| PS2_SH5_BIND (:8900,:14579,:15256,:15522,:15717,:17806,:19438 + s9 inc — **8 latches**) | num-if-set OFF | bind-source route probe; its route names = the serve-tier taxonomy | experiment, delete-both; A2 finding #4 mirror hazard | DELETE (route names live on as ResolveRead case names) |
| PS2_SH7 (:9013) | OFF | SH7 experiment | experiment, delete-both | DELETE |
| PS2_CLEAR_DIAG (:9060) | OFF | clear-pass diag | diagnostic, move-to-diag | DIAG |
| PS2_SH7_DEC (:9255) | OFF | SH7 decode arm | experiment, delete-both | DELETE |
| PS2_R197_D1P (:10276) | OFF | R197 D1 probe | experiment, delete-both | DELETE |
| PS2_MIPCEN (:10332) | num-if-set OFF | mip census | tuning-knob per A2 | DIAG¹ (census by name/behavior) |
| PS2_CRYSENV_DIAG / _DUMPDIR (:10424/:10576) | OFF | crys-env serve dumps | diagnostic, move-to-diag | DELETE (solved: BUG-018/S111)¹ |
| PS2_FMV5_SRC (:10614) | numeric | FMV family-5 source select | tuning-knob, delete-losing | NORM |
| PS2_R198_RTPROBE (:10699,:20478) | OFF | RT-serve probe | diagnostic(one-off), delete-both | DELETE |
| PS2_S9_DIAG (:12341) | OFF | S9 diag lines | diagnostic(one-off), delete-both | DELETE |
| PS2_SH6_SWEEP (:12454 + s9 inc) | OFF | SH6 sweep probe | experiment, delete-both (mirrored) | DELETE |
| PS2_SH2_T8HTEX (:12683) | OFF | SH2 T8H texture experiment | experiment, delete-both | DELETE |
| PS2_SH2_DRAWDELTA (:12698) | OFF | SH2 draw-delta probe | experiment, delete-both | DELETE |
| PS2_SH2_T8HDUMP (:12744) | OFF | SH2 T8H dump | diagnostic(one-off), delete-both | DELETE |
| PS2_R197_D2S (:13026) | OFF | R197 D2S arm | experiment, delete-both | DELETE |
| PS2_GS_FOGLANE (:14066,:20680 — double latch) | ON-unless-0 | [S79] fog-lane encode fix (16-bit lane preserved vs enc=0 clobber) | tuning-knob, delete-losing | NORM (default-ON fix; kill the in-file mirror) |
| PS2_FOG_DIAG (:15470) | OFF | fog diag | diagnostic, move-to-diag | DIAG |
| PS2_SH1_LASTWRITER (:16379) | OFF | last-writer probe in SyncRT | experiment, delete-both | DELETE |
| PS2_S9_DEMOTE (:17130 + s9 inc :258) | OFF | force-demote S9 strips to color path | experiment, delete-both (mirrored) | DELETE |
| PS2_SH5_ALIASEPOCH (:17830) | OFF | alias-epoch probe | experiment, delete-both | DELETE |
| PS2_FMV5_SCAN (:18558) | OFF | FMV5 scan probe | diagnostic, move-to-diag | DIAG |
| PS2_INTERP_CENSUS (:18704) | OFF | interp match census | diagnostic, move-to-diag | DIAG |
| PS2_INTERP_DUMP / _DIR (:18743/:18750) | OFF | interp replay dumps | diagnostic, move-to-diag | DIAG |
| PS2_R197_D2V2 (:20722) | OFF | R197 D2 v2 arm | experiment, delete-both | DELETE |
| PS2_S8_RTDUMP (:21200) | OFF | RT dump probe | diagnostic(one-off), delete-both | DELETE |
| PS2_ZSERVE_DIAG (:21431) | OFF | z-serve diag | diagnostic, move-to-diag | DIAG |
| PS2_MIPDUMP (:22266) | OFF | mip-level dumps | diagnostic, move-to-diag | DIAG |
| PS2_GL_L2_VERIFY (:22564) | OFF | L2-hit vs fresh-decode live verifier | tuning-knob, delete-losing | TEST-ONLY¹ (equivalence tripwire → harness) |
| PS2_GPUFENCE (:23824) | OFF | present GPU-fence depth probe | experiment, delete-both | DELETE |
| PS2_S106_STAGE / _DIR (:25326/:25328,:25592) | OFF | fog-content stage dumps | experiment/diag, delete-both | DELETE (whole S106StageProbe region) |

  ¹ deviation from the mechanical A2 mapping, justified by code evidence (named in
  the row). ² draw-skip bisectors could alternatively become ONE generic
  `core/diag` draw-filter tool; the per-var forms delete either way.
  Also consumed via helper latches from other shards' files (not tabled here):
  the r118/r120 FAST/FAST2 family, gsresident (PS2_GS_RESIDENT → NORM),
  gsovl (PS2_GS_DEPOSIT_EEAPPLY → NORM), classrdr master (PS2_GS_CLASSRDR → NORM),
  gs_interp (PS2_GS_INTERP → SETTING video.interp default ON), s8_inrt, shuf_snap,
  idx_epoch_guard, foglane_live (PS2_GL_FOGLANE_LIVE → NORM), gs_mipmap
  (PS2_GS_MIP_ALLCLASS/LODREF → NORM).

- **Hacks / workarounds** (the load-bearing ones; each with the correct refactor
  implementation):
  1. **TU-tail `.inc` includes** (:25291-25316) so native renderers see file-local
     gate namespaces — banned by D4; correct form: gates die, renderers become
     classes registered via RendererProfile (`engine/gs/hle/classrdr/`).
  2. **~145 game VRAM address literals** (A6 §3.3: :941 `tbp==0x280000‖0x2f0000`,
     :6605, :6772-6775 CLUT pairs, :6938, :8559-8589 SPLASH-COMPOSITE-SKIP,
     fog-lane FBMSK=0x3fff sites :2193/:9419/:10850/:11601/:13974/:14047) — PS2
     rule: none; these are THIS game's VRAM map. Owner: `RendererProfile.VramLayout`
     (games/…/renderer_profile.cpp), queried as `layout.is(role, fbp)`.
  3. **Multi-latched gates in one file** (SH5_BIND ×8, S32_GREENTEX ×4, R197_D2P ×4,
     GS_FOGLANE ×2, ALIAS_DEPCOPY2 ×2…) — mirror-drift hazard (A2 finding #4).
     Correct: zero getenv outside core/config (ARCH §8); these all die anyway.
  4. **`m_ftt_*` loose members as implicit out-params** (opengl_backend.h:959-1013,
     reset-per-entry rule [W2/W3] enforced by discipline) — correct: fields of the
     returned `TextureSource` (ARCH §4.5 "the m_ftt_* idiom is abolished").
  5. **FindTargetTexture wrapper = ~190 lines of probe around a tail call**
     (:19412-19604) — probes in the hot path; correct: core/diag GenProbe-style
     instrumentation in diagnostics builds only.
  6. **Session-named namespaces as API** (r121sync, r147, r159, r161, r172, g3,
     g7c, g8par, g9, w14r5g10, depcopy2, s83fognative…) — ARCH §11 bans session
     tags; concepts get named homes (MaterializeReason, ResMemo dies, etc.).
  7. **Embedded shader + runtime source transforms** (R169TransformFragSrc :2500,
     S83InjectIdxScale :2541) — string surgery on GLSL at init; correct: the
     transformed source IS the only authored source in GlShaderLibrary; the
     shaders/ps2_*.glsl "reference copies" (a comment admits the embedded copy is
     authoritative) are deleted or regenerated from the single source.
  8. **Hand-rolled GLSL `#include` preprocessor** (:3994-4105) — dies with the
     compute experiment.
  9. **[SCANOUT-STATE] stderr heartbeat every 60 calls** (:24211) and
     **[BULLSEYE-FBODUMP]** (:24222…) — unconditional/diag noise in present path;
     correct: core/log category, compile-time off.
  10. **Compute-raster dual pipeline** (m_use_compute_vram branches in
      DrawBatch/DrawScanout/page marks) — a second whole renderer behind a
      hardware latch; MIGRATION deletes it; software oracle backend replaces its
      role (ARCH §6).
  11. **Narrative comment blocks** (hundreds of >15-line session narrations; A2
      counted 797 tree-wide) — history moves to docs/loop_state; code keeps
      one-line invariant comments.
- **Risks**: highest-risk file of the whole refactor. The serve-tier rewrite (R6/
  R16) and single-writer VRAM consolidation (R3/R11/R22) change the most-fixed code
  in the tree; every A3 §9 invariant (1-24, 31-37) lands here. Proof: gs_corpus 19
  captures replay tol=0 byte-identical at scale=1; PS2_SNAPVERIFY-equivalent
  oracle (reuses>0, mismatches=0); [GB4] zero-stale-at-save; test_ztex_equivalence
  (r162 exported sources), test_texture_shuffle, test_block_decode_equivalence;
  the in-process A/B idiom for each extracted class; frame-hash anchors on the
  ~40 savestates.

---

## host_app/graphics/opengl/opengl_backend.h (2,380 lines)

- **Purpose**: Declaration of `OpenGLBackend` plus every state structure the .cpp
  operates on: the ~75-field `TargetFBO` (:567 — GL objects, [GE1] scale + resolve
  pair, alt16 companion + stale rects, persistence/rt_copy + dirty tiles, PBO rings
  + fences, drawn rects ×3, coherence epochs/seqs), `TexResolveOut` (:131),
  `ScanoutParams` (:32), S9 ladder structs, per-class shader-uniform shadow structs
  (WslShadow/ShlShadow/SklShadow/PrtShadow/FvlShadow/S9StripShadow), the `m_ftt_*`
  per-call result members (:959-1013), decode/L2/uploaded/pooled texture cache
  entry types, alias tracking types, the [R169] UNIFPACK virtual-location pack
  (:2290-2325), and `r162::` test exports. Zero getenv; 65 PS2_* tokens are all in
  comments.
- **Public surface**: `class OpenGLBackend`, `struct ScanoutParams`; included by
  opengl_backend.cpp, graphics/graphics_backend.cpp (factory), and (via
  gpu_vertex/graphics_backend) the render thread. Test exports r162::*.
- **Classification**: MIXED (rides the .cpp's row C; the struct definitions are
  GENERIC mechanism, a few pinned comments/values are game census).
- **Refactor destination**: dissolved with the .cpp split. `TargetFBO` → the four
  one-class-per-file structs of ARCH §4.4: `engine/gs/hle/targets/
  {target_identity,surface_views,readback_engine,coherence_state}.h` behind
  `render_target.h`; `TexResolveOut` + `m_ftt_*` → `engine/gs/hle/texsrc/
  texture_source.h` fields; cache entry types → `decode_cache.h`; GL program/pack
  members → `engine/render/backends/gl/{gl_shader_library,gl_uniform_cache,
  gl_vertex_stream,gl_texture_pool}.h`; ScanoutParams → `gs/hle/present/`;
  compute members (m_cs_*, m_vram_ssbo, compute_data_layouts include at :8) DELETE.
  Headers become declarations-only (D4).
- **Flags consumed here**: none read (comment references only).
- **Hacks**: (a) the 75-field god-struct itself (A3 finding #8); (b) `m_ftt_*`
  members as cross-function implicit outputs — becomes TextureSource fields;
  (c) per-renderer uniform-shadow structs duplicated six times (Wsl/Shl/Skl/Prt/
  Fvl shadows) — one GlUniformCache serves all programs; (d) roundSprite mode-1
  convention documented at :170 as inherited — becomes TitleProfile.quirks data.
- **Risks**: savestate GSST/target reconstruction and interp shadow-target code
  touch these fields; prove with savestate round-trip on anchor states + corpus
  replay.

---

## host_app/graphics/opengl/world_tristrip_native.inc (1,300 lines)

- **Purpose**: [GD2] Native class renderer for `classrdr::kWorldTristrip` — the
  vs_family_tristrip shadow-volume strip shape (untextured, ABCD=(0,2,2,1) wrap
  blend, DATE=1, GEQUAL zmsk=1, CT16S frame, fbmsk=0xff000000). Statement-for-
  statement replica of the DrawBatch live path for this census-pinned shape, with
  a specialized warmed fragment shader (SPEC arm) and a generic-program control
  arm. Also hosts the `sh8::FixOn` zsrc-coordinate-map fix shared with s9.
- **Public surface**: `GsWorldRdrRegister(OpenGLBackend*)` (called from Init),
  `worldrdr::AcceptStrip` (reused by s9), `DrawWorldStripNative`; single-TU static
  linkage inside opengl_backend.cpp.
- **Classification**: MIXED (A6 §3.2 row 1): the tristrip idiom lowering is
  GENERIC; the Accept tuple (CT16S/fbmsk/DATE/GEQUAL census row), the
  `frame_fbp==0x230000` special-case (:323), and the World2 T8 handoff trigger are
  GAME.
- **Refactor destination**: MIGRATION §1.6 row — mechanism →
  `engine/gs/hle/classrdr/world_tristrip_renderer.{h,cpp}` (class
  **WorldTristripRenderer**, parameterized idiom detector); Accept census tuple +
  0x230000 pin + World2 handoff → `games/…/renderer_profile.cpp` (DrawShapeSpec
  row + VramLayout). Shape: split (no longer an .inc; D4).
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_CLASSRDR_WORLD_SPEC (:147) | ON-unless-0 | specialized warmed program vs generic control arm | tuning-knob, delete-losing | NORM (SPEC wins; control arm was the adjudication tool) |
| PS2_SH8 (:293) | ON-unless-0 | zsrc scaled-coordinate map fix (2x shadow dropout) | experiment per A2, delete-both | NORM¹ (default-ON proven fix; =0 is the defect arm — folding ON is the correct end state) |

  Also consumes worldrdr↔skinrdr World2Accept hook, classrdr per-class hatch
  (PS2_GS_CLASSRDR_WORLD via registry — latched in gs_class_renderer.h → NORM).
- **Hacks**: (a) statement-for-statement duplication of DrawBatch blocks (the
  whole METHOD) — dissolves when both sides consume DrawIR + TextureSource; the
  class renderer keeps only its pipeline/program specialization and its DECLINE
  path (ARCH §5.3c); (b) `frame_fbp==0x230000` (:323) → VramLayout.scene_rt;
  (c) forward-declared cross-.inc hooks (skinrdr::World2Accept seen before
  definition) — dies with the .inc pattern.
- **Risks**: class-dispatch bit-safety ([GD1]: fallback = untouched pre-dispatch
  call site) must survive the extraction. Prove: per-class byte-identity harness
  legs on slowspot/s57_walk captures (33.2%/53.6% class share), fb_* fallback
  counters unchanged.

---

## host_app/graphics/opengl/s9_composite_native.inc (2,273 lines)

- **Purpose**: [GD4] Semantic native renderer for the S9 blob-shadow seven-pass
  composite: native pass-A CLEAR, pass-E STRIPS as a stencil winding counter
  (INCR/DECR_WRAP + in-shader DATE/inverted-Z tests, no color writes), pass-D
  INVERT as a verified byte-NOT fold, and GsS9Materialize (32 stencil-masked
  quads reconstruct oracle bytes on the first non-strip touch). Passes B/C/F/G
  stay on the generic fallback. Encodes the stencil↔byte equivalence
  (byte==(8*stencil) mod 256) proven against the oracle.
- **Public surface**: `GsS9RdrRegister`, `GsS9HookPreDraw`/`GsS9HookTouch` (armed-
  sequence guards called from DrawBatch/SyncRTToVRAM), `GsS9Materialize`,
  DrawS9{Clear,StripOrWorld,Invert}Native; delegates color arm to
  DrawWorldStripNative.
- **Classification**: MIXED → mostly GAME (A6 §3.2: "Game idiom — TT's blob-shadow
  engine recipe"): stencil-winding/composite lowering + sequence-FSM mechanism is
  GENERIC; the seven-pass recipe, 0x78787878 clear, ZCOPY→0x268000, 0x230000/
  0x280000 pins (:781, :857-858) are GAME.
- **Refactor destination**: MIGRATION §1.6 row — mechanism →
  `engine/gs/hle/classrdr/stencil_composite_renderer.{h,cpp}` (class
  **StencilCompositeRenderer** + a declarative `SequenceRecipe` FSM engine);
  the S9 recipe + address pins → `games/…/renderer_profile.cpp` sequences/vram.
  Shape: split.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_S9_DEMOTE (:258) | OFF | force strips to color path (mirror of .cpp latch :17130) | experiment, delete-both | DELETE |
| PS2_GS_S9RDR_DBGPIX (:471,:591,:1868 — triple latch) | num-if-set OFF | per-pixel debug dumps | diagnostic, move-to-diag | DIAG |
| PS2_SH5_BIND (:854) | num-if-set OFF | bind-source probe (mirror #8 of the .cpp latches) | experiment, delete-both | DELETE |
| PS2_SH6_SWEEP (:1771) | OFF | sweep probe (mirror of .cpp :12454) | experiment, delete-both | DELETE |

- **Hacks**: (a) the materialize interlock reached by member-state checks planted
  inside DrawBatch/SyncRTToVRAM/FTT (`m_s9_seq_state != 0` guards at
  opengl_backend.cpp:8330/:16282) — correct form: SequenceRecipe engine owns its
  arm/touch lifecycle as explicit states on the RenderTarget, not scattered hooks;
  (b) oracle-adjudicated behavioral deltas (stencil fiction vs register-level
  color) documented in-file — keep as the SequenceRecipe contract + its corpus
  proof; (c) load-bearing per-strip mirror-sync enqueues (the [GD4] "235 differing
  frames" finding) — must be preserved verbatim as a VramImage deposit contract;
  (d) 0x230000‖0x280000 compares (:781,:857) → VramLayout.
- **Risks**: the subtlest renderer — stencil equivalence + materialize triggers +
  the S9RDR_HOLD interplay. Prove: test_s9_composite (pins shipping expressions),
  s57_walk + crystal-cavern corpus replays (the [GS-S9DEM] counters), bad_shadows
  capture.

---

## host_app/graphics/opengl/s9_composite.h (383 lines)

- **Purpose**: Pure decision/value logic for the S9 composite (GL-free): sequence
  states (kIdle/kPrimed/kArmed), stencil scheme constants (base 0x0F, mask 0x1F),
  the S9RDR master + HOLD + DBGBOTH gates, pass-detection helpers. Exists so
  tests/test_s9_composite.cpp pins the exact shipping expressions.
- **Public surface**: `namespace s9rdr` (On/HoldOn/DebugBothOn, SeqState, pass
  predicates); included by s9_composite_native.inc + tests.
- **Classification**: GAME (A6: "the pure decode of the seven-pass composite").
- **Refactor destination**: constants/predicates → the `SequenceRecipe` DATA in
  `games/…/renderer_profile.cpp`; the FSM state enum + hold policy → engine
  `stencil_composite_renderer.h`. Shape: split.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_S9RDR (:96) | ON-unless-0 | S9 native renderer registered at all | tuning-knob, delete-losing | NORM (named class-renderer family in shard NORM list) |
| PS2_GS_S9RDR_HOLD (:132) | ON-unless-0 | armed accumulator's own pending deposit held to the QUANT trigger (fixes stencil self-destruct on pure-CT16S targets) | tuning-knob, delete-losing | NORM (default-ON fix) |
| PS2_GS_S9RDR_DBGBOTH (:145) | OFF | run BOTH stencil and color arms ("never a shipping mode") | diagnostic, move-to-diag | DIAG |

- **Hacks**: none beyond the gates — this is the model header the refactor wants
  more of (pure logic + tests). The header/test pairing survives as the recipe's
  unit test.
- **Risks**: low. test_s9_composite must keep passing against the moved
  expressions.

---

## host_app/graphics/opengl/s9_shadow.h (254 lines)

- **Purpose**: Loop-21 blob-shadow pure logic: [S9-ABIT16] 16-bit-frame A-bit
  storage convention (FBO alpha = PS2 A-bit, not x2 rescale), [S9-ZSRC]
  color-authored Z routing predicate + z32 reconstruction from RGBA8, [S9-CLUTRAW]
  raw-alpha CLUT row selection for the T8H self-read invert, plus the [WN4-CT16Q]
  retail CT16 dither/truncation rule gate and the RTKEEP/ALT16UV gates. All
  default-ON fix hatches with test overrides.
- **Public surface**: `namespace s9shadow` (Abit16On/ZsrcOn/ClutRawOn/Alt16UvOn/
  RtKeepOn/Ct16QuantOn + pure value functions); included by opengl_backend.cpp,
  tests/test_s9_shadow.cpp, tests/test_vsuv_equivalence.cpp.
- **Classification**: GENERIC-PS2 (these encode GS hardware rules: A-bit
  semantics, Z-as-color aliasing, CLUT alpha domain, CT16 dither) — motivated by
  this game's shadow chain but stated as PS2 rules.
- **Refactor destination**: the VALUE LOGIC distributes to its owning format/
  policy homes: A-bit + CT16 quant/dither → `engine/gs/format/alpha.{h,cpp}` and
  the backend's 16-bit-frame store convention in `gl_draw_executor`; ZSRC routing
  → `engine/gs/hle/texsrc/texture_source.cpp` (DepthToColor owner case); CLUTRAW
  → `engine/gs/format/clut.{h,cpp}` (alpha-domain selection); RTKEEP predicate →
  ResolveRead serve-keep. Shape: split (gates deleted, logic inlined as the only
  path).
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_ABIT16 (:54) | ON-unless-0 | A-bit convention on CT16S frames | tuning-knob, delete-losing | NORM |
| PS2_GS_ZSRC (:62) | ON-unless-0 | color-authored Z depth-test routing | tuning-knob, delete-losing | NORM |
| PS2_GS_CLUTRAW (:70) | ON-unless-0 | raw CLUT alpha for T8H self-read invert | tuning-knob, delete-losing | NORM |
| PS2_GS_ALT16UV (:78) | ON-unless-0 | alt16 companion sizing/UV fix | proven-fix-hatch-ON, delete-losing | NORM |
| PS2_GS_RTKEEP (:86) | ON-unless-0 | [S32-RTKEEP] serve-keep predicate enabled | proven-fix-hatch-ON, delete-losing | NORM (invariant #15/#16) |
| PS2_GS_CT16_QUANT (:102) | ON-unless-0 | retail CT16 DTHE/DIMX dither + 5-bit truncation at fragment tail | tuning-knob, delete-losing | NORM (invariant #32 dither keys; BUG-020) |

- **Hacks**: none structural — exemplary pure-logic header. The
  `*OverrideForTest()` int&-singleton idiom is replaced by ordinary test
  injection once gates are gone (functions become unconditional).
- **Risks**: A-bit/quant conventions feed every 16-bit readback path; prove with
  test_s9_shadow + wallnoise (BUG-020) and shadow corpus captures.

---

## host_app/graphics/opengl/sprite_hud_native.inc (1,679 lines)

- **Purpose**: [GD5] Native class renderer for `classrdr::kSpritesHUD` —
  vs_sprite_text direct-MSCAL family (micro 0x1748): 1,012 textured glyph/HUD
  quads (T8+CLUT, MODULATE) + 1 untextured boot draw. Consumes FlushPrimitive's
  pre-expanded sprite quads (roundSprite mode-1 convention inherited from
  upstream, never re-derived). Texture resolution SHARED with DrawBatch (the
  [GD5-TEXSEAM]), not replicated.
- **Public surface**: `GsSpriteRdrRegister`, DrawSpriteHudNative; provides the
  TEXSEAM shared by skinned/particles/fmv.
- **Classification**: MIXED (A6 §3.2): sprite-quad idiom GENERIC; family bounds
  (micro 0x1748), census tuples, 0x230000 pin (:407) GAME; roundSprite mode=1 →
  TitleProfile.quirks.
- **Refactor destination**: mechanism → `engine/gs/hle/classrdr/
  sprite_hud_renderer.{h,cpp}` (class **SpriteHudRenderer**); Accept tuples +
  family bounds + pin → RendererProfile; round_sprite_mode=1 →
  TitleProfile.quirks (MIGRATION §1.6). Shape: split.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_SPRRDR (:183) | ON-unless-0 | renderer registered | tuning-knob, delete-losing | NORM |
| PS2_GS_SPRRDR_SPEC (:194) | ON-unless-0 | specialized program vs generic control arm | tuning-knob, delete-losing | NORM |

- **Hacks**: (a) DrawBatch statement replication (dissolves as world's); (b)
  `fbp==0x230000` (:407) → VramLayout; (c) reliance on TU-local TEXSEAM member
  hooks — becomes an explicit ResolveRead call.
- **Risks**: HUD/text is interp-exempt and byte-sensitive; prove with boot_menu +
  save_select captures, [GD1] fallback counters.

---

## host_app/graphics/opengl/skinned_native.inc (1,680 lines)

- **Purpose**: [GD7] Native class renderer for `classrdr::kSkinned`
  (vs_skinned_lit_strip STQ tristrips, iip=1 fst=0, cc=1) + the world family-2
  textured-T8 handoff arm (`skinrdr::World2Accept`) + the [GS-RAWSKIN] raw-other
  slot (stamp-free field-exact skinned-shape acceptance sharing the draw body).
  Field-exact census whitelist pins the one shape both classes share.
- **Public surface**: `GsSkinnedRdrRegister`, `GsRawSkinRegister`,
  `skinrdr::World2Accept` (forward-consumed by world .inc), DrawSkinnedNative;
  action counters g_skin_*.
- **Classification**: MIXED: skinned-mesh STQ idiom GENERIC; AcceptShape census
  tuple + 0x230000 pin (:517) GAME.
- **Refactor destination**: mechanism → `engine/gs/hle/classrdr/
  skinned_renderer.{h,cpp}` (class **SkinnedRenderer**; the raw-skin arm is the
  same detector registered on the raw class slot); tuples/pins → RendererProfile.
  Shape: split.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_SKINRDR (:202) | ON-unless-0 | renderer registered | tuning-knob, delete-losing | NORM |
| PS2_GS_SKINRDR_WORLD2 (:213) | ON-unless-0 | world family-2 textured arm | tuning-knob, delete-losing | NORM |
| PS2_GS_SKINRDR_SPEC (:224) | ON-unless-0 | specialized program arm | tuning-knob, delete-losing | NORM |
| PS2_GS_RAWSKIN (:238) | ON-unless-0 | raw-other-slot renderer | tuning-knob, delete-losing | NORM |

- **Hacks**: (a) cross-.inc forward declaration of World2Accept (single-TU static
  linkage dependency, include-order-sensitive) — dies with .inc pattern; (b)
  0x230000 pin (:517) → VramLayout; (c) hatch-stack (master→class→sub→SPEC
  four-level gating) — collapses to registry data.
- **Risks**: raw-skin accepts UNSTAMPED draws by field shape — a classifier
  behavior that must move into RendererProfile.raw verbatim; prove with island/
  s57 replays and fb_* counters.

---

## host_app/graphics/opengl/particles_native.inc (1,667 lines)

- **Purpose**: [GD8] Native class renderer for `classrdr::kParticlesUntex`
  (vs_poscolor_strip — actually textured T8+CLUT in every capture, 4,578 draws):
  one uniform state shape, blend×ate axes; built as the skinned template with
  three census-proven deltas (ztst=1, mmin=1, FF-blend rows).
- **Public surface**: `GsPartRdrRegister`, DrawParticlesNative.
- **Classification**: MIXED: particle idiom GENERIC; AcceptShape + census note +
  frame fbp=0x230000/zbp=0x150000 pins (:383) GAME.
- **Refactor destination**: `engine/gs/hle/classrdr/particles_renderer.{h,cpp}`
  (class **ParticlesRenderer**); tuples/pins → RendererProfile. Shape: split.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_PARTRDR (:166) | ON-unless-0 | renderer registered | tuning-knob, delete-losing | NORM |
| PS2_GS_PARTRDR_SPEC (:177) | ON-unless-0 | specialized program arm | tuning-knob, delete-losing | NORM |

- **Hacks**: template-copied from skinned (a third statement-replica of
  DrawBatch) — dissolves the same way; class name kParticlesUntex is a census
  misnomer (class is textured) — rename in the profile taxonomy.
- **Risks**: low; prove with the six-capture census shares + fallback counters.

---

## host_app/graphics/opengl/fmv_blit_native.inc (1,633 lines)

- **Purpose**: [GD8] Native class renderer for family F of
  `classrdr::kRawBlitFullscreen`: 820 fullscreen FMV column-quad DECAL blits of
  the IPU-decoded frame (staged at tbp=0x150000) onto the 0x70000 scanout target.
  Families A–E (scanout→CT16 copies, Z16 captures, Z32 copies, 0x230000 reads,
  S9 subtractive darkening) decline by design to the raw fallback.
- **Public surface**: `GsFmvRdrRegister`, DrawFmvBlitNative.
- **Classification**: MIXED: fullscreen-blit idiom GENERIC; the six-family
  decomposition, tbw∈{640,512} bounds, 0x230000/0x150000 pins (:366 + staging)
  GAME.
- **Refactor destination**: `engine/gs/hle/classrdr/fmv_blit_renderer.{h,cpp}`
  (class **FmvBlitRenderer**); family-F spec + staging addresses →
  RendererProfile sequences/vram (MIGRATION §1.6). Shape: split.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_FMVRDR (:168) | ON-unless-0 | renderer registered | tuning-knob, delete-losing | NORM |
| PS2_GS_FMVRDR_SPEC (:179) | ON-unless-0 | specialized program arm | tuning-knob, delete-losing | NORM |

- **Hacks**: (a) family decomposition encoded as accept-time literals — becomes
  RendererProfile data; (b) `tbp==0x150000` FMV-staging identity — VramLayout
  role fmv_stage.
- **Risks**: FMV present interacts with PresentPolicy FMV bypass; prove with
  fmv_attract + island_slot7 captures.

---

## host_app/graphics/opengl/gl_state_cache.h (917 lines)

- **Purpose**: [R120-GL2] Redundant-GL-bind shadow (`r120gl::` namespace):
  every glBindFramebuffer/ActiveTexture/BindTexture/TexParameteri/UseProgram/
  BindVertexArray/Delete* in the two GL TUs routes through these wrappers, which
  skip calls whose shadow already matches (GL-on-Metal render-pass-boundary cost).
  Also hosts the [R122-TRIAGE] GL object lifetime census (gen/del counters),
  [R166] draw-state cache, [R169-GLRUN] run-submit shadows, and the [R172]
  ResMemo env latch.
- **Public surface**: `namespace r120gl` (BindFB/BindTex2D/ActiveTexture/
  UseProgram/BindVertexArray/TexParam*/Gen*/Delete*/Invalidate*), counters;
  included by opengl_backend.cpp, graphics_thread.cpp, uv_bbox.h; tests
  test_gl_state_cache_equivalence/test_vsuv_equivalence/test_resmemo_equivalence/
  test_unifpack_equivalence.
- **Classification**: GENERIC-PS2 (host GL device state management; zero game
  tokens).
- **Refactor destination**: MIGRATION §1.4 row: "render/backends/gl/ device state
  members — rewrite" → `engine/render/backends/gl/gl_state_cache.{h,cpp}` (class
  **GlStateCache**, a member of GlDevice — no more inline-global header state;
  ARCH bans mutable globals in headers, and this header holds ~65 of A1's 184).
  Lifetime census → core/diag. Shape: rewritten (same skip logic as members).
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GL_BIND_CACHE (:82) | explicit wins, else PS2_GL_FAST2, else **ON** | bind-shadow skipping | experiment/delete-both per A2 — **contradicted by code** (3-level default-ON latch) | NORM (skip path is the only path) |
| PS2_GL_FAST2 (:84, fallback read) | peak run =1 | family master consulted as fallback | shipping-setting per A2 | NORM (family folds; var dies with all sub-gates) |
| PS2_GL_RUNSUBMIT (:286) | ON-unless-0 | [R169] run-batched submit (measured −5.0 ms/f) | proven-fix-hatch-ON, delete-losing | NORM |
| PS2_GL_STATE_CACHE (:399) | **ON** per code (A2 lists presence-gated) | [R166] draw-state cache | experiment/delete-both per A2 — contradicted by code | NORM |
| PS2_GL_RESMEMO (:809) | OFF, no master | [R172] per-draw resolve memo replay | tuning-knob, delete-losing | NORM (winner=OFF ⇒ memo arm deleted; its equivalence test retires with it)¹ |

  ¹ RESMEMO is default-OFF and absent from the peak run — the losing arm is the
  memo; if later profiling resurrects it, it must return as code, not a gate.
- **Hacks**: (a) **documented intentional mirror** of `r120::BindCacheOn`
  (render.h) at :75-89 to avoid an include chain — A2 finding #4's worst-case
  pattern; correct: one Config-derived constant, zero duplicate latches; (b)
  inline mutable globals in a header (shadow state + counters) — become GlDevice
  members; (c) deletion-semantics folding (GL 4.1 bind-reverts-to-0 on delete)
  encoded ad hoc — keep, as GlStateCache's documented contract.
- **Risks**: skip-correctness bugs resurface as stale-bind artifacts (the R36
  class). Prove: test_gl_state_cache_equivalence + full corpus replay with
  call-count census (c3 counters) unchanged.

---

## host_app/graphics/opengl/rt_neverstale.h (351 lines)

- **Purpose**: [S109-NEVERSTALE]/[CX8]/[S111-READVIEW] — the "RT-backed reads
  never consume stale mirror bytes" rule: serve-keep at read-footprint
  granularity with precise per-page write bits, rescue deposits before every
  decline, no-FBO compose-refresh of GPU-authored pages, and the READ-view STQ
  prescale fix (window dims, not target dims). Pure predicates + census atomics;
  the enforcement sites live in opengl_backend.cpp.
- **Public surface**: `namespace rtns` (On/ReadViewOn, decline/serve counters);
  included by opengl_backend.cpp, graphics_thread.cpp, gs_state.cpp.
- **Classification**: MIXED (A6 §3.2 support row): the rule is GENERIC (PCSX2
  paraphrase: stream-order readbacks make stale mirrors unsample-able); the
  motivating trigger (vs_crys_dual 0x280000 env lane) is GAME context.
- **Refactor destination**: MIGRATION §1.6 row: rule → `engine/gs/hle/texsrc/`
  ResolveRead freshness ladder (fields of **TextureSource.freshness** +
  `uv_prescale`; ARCH §4.5 carries [S111-READVIEW] explicitly); trigger context →
  profile. MIGRATION §1.4 also lists it with alias_rect/rt_copy_tracker →
  gs/hle/targets CoherenceState inputs. Shape: split (predicates into ResolveRead,
  counters into diag).
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GL_RT_NEVERSTALE (:110) | ON-unless-0 | the entire never-stale serve/rescue rule | tuning-knob, delete-losing | NORM (named in shard NORM list; invariant #15) |
| PS2_GL_RT_READVIEW (:150) | ON-unless-0 | STQ prescale by READ window dims | tuning-knob, delete-losing | NORM (named in shard NORM list; BUG-018) |
| PS2_FTT_CENSUS (:213) | OFF | decline/serve counters | diagnostic, move-to-diag | DIAG |

- **Hacks**: none — this is a rulebook header; its comments are the best A3-§3
  documentation in the tree. The `=0` legacy arms (the stale-serve defect paths)
  are the textbook delete-losing-arm case.
- **Risks**: the freshness ladder IS invariants #15/#16/#17; prove with
  crystal/cavern captures (CAVERN_CRYSTRIPES class), PS2_FTT_FORCE_DECLINE/
  FORCE_EVICT repro scenarios converted to tests.

---

## host_app/graphics/opengl/alias_rect.h (216 lines)

- **Purpose**: [HUB1-C2]/[W14-G1] rect-scoped alias refresh math: clamps the
  [VRAM-ALIAS] pre-draw RefreshFBOFromVRAM to the full-width row band covering
  the triggering draw's read window; page-clear rule (only fully-covered pages
  clear); optional dirty-band scope key. Header-pure; tests pin the math.
- **Public surface**: `namespace alias_rect` (RectOn/DirtyRectOn + band/coverage
  functions); included by opengl_backend.cpp + tests/test_alias_rect.cpp.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: MIGRATION §1.4: → `engine/gs/hle/targets/` coherence
  inputs; concretely the band math folds into `engine/gs/vram/page_record.{h,cpp}`
  (dirty_band field + MaterializePages scoping) — invariant #36 lives there.
  Shape: split (math verbatim, gates deleted).
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GL_ALIAS_RECT (:32) | ON-unless-0 | row-band-scoped refresh vs full refresh | proven-fix-hatch-ON, delete-losing | NORM |
| PS2_GL_ALIAS_DIRTYRECT (:58) | OFF-unless-1 | scope key = accumulated dirty band instead of draw rect | tuning-knob, delete-losing | NORM (winner=OFF per W14_PROFILE savedMB=0.0 evidence in-file ⇒ arm deleted) |

- **Hacks**: none; the in-comment A/B numbers (W14_PROFILE) move to the record.
- **Risks**: low; test_alias_rect + hub capture replay.

---

## host_app/graphics/opengl/rt_copy_tracker.h (256 lines)

- **Purpose**: [HUB1-C1] sw-blend Cd-copy elision: pure decision logic for
  skipping the pre-draw rt_copy when the existing copy already covers the draw's
  read rect (validity + dirty-union tracking; force_full for idx self-serves
  never elided; [W14R3] optional 4×4 dirty-tile grid granularity).
- **Public surface**: `namespace rt_copy` (LazyOn/Decide/ExtendDirty…); operates
  on TargetFBO-owned fields by reference; included by opengl_backend.cpp +
  tests/test_blendcp_lazy.cpp, test_rt_dirty_tiles.cpp.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: MIGRATION §1.4: → `engine/gs/hle/targets/` — the
  logic becomes methods of **CoherenceState**/**SurfaceViews** (rt_copy validity
  + dirty union are already §4.4 fields); backend does the CopyRect. Shape: split.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GL_BLENDCP_LAZY (:40) | ON-unless-0 | Cd-copy elision | proven-fix-hatch-ON, delete-losing | NORM |
| PS2_GL_CDTILES (:103) | numeric | 4×4 dirty-tile granularity vs union rect ("both sets kept; call sites gate which one Decide uses") | tuning-knob, delete-losing | NORM (adjudicate winner once via bench, delete the other set — dual bookkeeping is the hack) |

- **Hacks**: dual dirty-tracking (union rect AND tile grid maintained in
  parallel) — pick one, delete the other, keep the test.
- **Risks**: elision bugs = sw-blend sampling stale Cd; test_blendcp_lazy +
  corpus.

---

## host_app/graphics/opengl/blend_ff_map.h (249 lines)

- **Purpose**: [R178-BLENDSUB] the PS2 ALPHA (A−B)*C+D → GL fixed-function blend
  mapping as a pure function, including exact REVERSE_SUBTRACT for the
  subtractive combos 2001/2021 (title-screen portal root cause) and the
  [S8-WRAPBLEND] arm; self-defines GL enum ABI constants when no GL header
  preceded (off-GL unit testing).
- **Public surface**: `MapAbcdToGlFF(...)`, `BlendSubOn()`, `WrapBlendOn()`;
  included by opengl_backend.cpp, tests/test_blend_ff_map.cpp,
  test_s8_wrapblend.cpp.
- **Classification**: GENERIC-PS2 (GS blend semantics).
- **Refactor destination**: MIGRATION §1.4: "render/backends/gl/ blend mapping —
  verbatim" → `engine/render/backends/gl/blend_ff_map.{h,cpp}` (free-function
  concept pair per D4). The gate parameters drop (mapping unconditional); the
  legacy 11-arm approximation deletes. Shape: moves verbatim minus gates.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GL_BLEND_SUB (:182) | ON-unless-0 | exact REVERSE_SUBTRACT map vs legacy 11-arm approximation | tuning-knob, delete-losing | NORM (invariant #34) |
| PS2_GS_WRAPBLEND (:232) | ON-unless-0 | COLCLAMP=0 wrap-blend handling arm | proven-fix-hatch-ON, delete-losing | NORM |

- **Hacks**: #ifndef GL_* ABI constant self-definition for off-GL tests —
  acceptable; becomes unnecessary once the pair has a .cpp compiled in the
  backend and the test links it.
- **Risks**: minimal; test_blend_ff_map enumerates the combo space.

---

## host_app/graphics/opengl/gs_scale.h (70 lines)

- **Purpose**: [GE1] the ONE resolution-scale policy: integer factor 1–4 from
  PS2_GS_SCALE; per-target scale assignment with the S9-accumulator native pin
  (`IsPinnedNative(0x230000)`). Logical-vs-physical dims contract documented
  here.
- **Public surface**: `namespace gsscale` (Factor/On/IsPinnedNative/TargetScale);
  included by opengl_backend.cpp.
- **Classification**: MIXED — policy GENERIC; the 0x230000 pin is GAME (A6 §3.3).
- **Refactor destination**: MIGRATION §1.4: "gs/hle/targets/ scale policy —
  verbatim" → `engine/gs/hle/targets/scale_policy.{h,cpp}` (class **ScalePolicy**;
  Factor from `Config.video.scale`, pins from `RendererProfile.VramLayout`
  native-pin roles — [S83] alias destinations join the pin set there). Shape:
  moves + de-games.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_SCALE (:45) | 1 (peak run =2) | integer render scale 1–4 | shipping-setting, keep-as-setting | SETTING → `Config.video.scale` (default 2; scale=1 stays the byte-identity contract) |

- **Hacks**: address-pinned `fbp==0x230000` → VramLayout query.
- **Risks**: invariant #23 (scale==1 byte-identical) is the standing proof; keep
  the cmp gate.

---

## host_app/graphics/opengl/compute_data_layouts.h (127 lines)

- **Purpose**: std140/std430 mirror structs (BatchUniforms UBO, ComputeVertex
  SSBO) for the compute-raster pipeline.
- **Public surface**: structs BatchUniforms/ComputeVertex; included by
  opengl_backend.h (:8) and tests/test_integration_render_queue.cpp.
- **Classification**: GENERIC-PS2 (dead experiment).
- **Refactor destination**: **DELETE** — named in MIGRATION §1.1's explicit
  compute-raster row (with the test's mirrored blocks dropped in the same
  commit). Shape: deleted.
- **Flags consumed here**: none.
- **Hacks**: "must stay in sync with the GLSL" comment-enforced mirroring — the
  kind of textual contract the DrawIR/one-source design removes.
- **Risks**: none (path inactive on macOS; hardware-latched elsewhere).

---

## host_app/graphics/graphics_backend.h (174 lines)

- **Purpose**: The abstract `GraphicsBackend` interface: Init/Shutdown/DrawBatch/
  Present/Clear/Resize/UploadScanoutBuffer/DrawScanout/SetScanout*/MarkEEFrameEnd/
  BindFBO/UnbindFBO/UploadTexture/BindTexture + debug grabs (PS2_SNAP consumers).
  The current-tree ancestor of ARCH §6's IRenderBackend.
- **Public surface**: `class GraphicsBackend`; implemented by OpenGLBackend and
  VulkanBackend; consumed by graphics_thread.cpp, texture_cache.cpp,
  gs_class_renderer.h, the factory.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: MIGRATION §1.4: "absorbed by render/backend.h +
  gs/hle/queue consumer — rewrite" → `engine/render/backend.h` (**IRenderBackend**
  per ARCH §6: Caps/CreateSurface/Upload/Draw(DrawIR,TextureSource)/CopyRect/
  Readback/Present/fences). Today's fat surface (scanout setup, EE-frame marks,
  BMP dumps) redistributes: scanout composition → gs/hle/present (above the
  interface), frame marks → BoundaryProtocol, dumps → core/diag. Shape: rewritten.
- **Flags consumed here**: none (PS2_SNAP/PS2_SNAP_RT in comments only; latched
  elsewhere).
- **Hacks**: interface mixes policy (MarkEEFrameEnd semantics doc — the R93
  3-presents-per-game-frame lesson) with device ops — the Dolphin-lesson split
  fixes this.
- **Risks**: interface rewrite touches every consumer; staged via adapter during
  migration phases.

---

## host_app/graphics/graphics_backend.cpp (114 lines)

- **Purpose**: Backend factory with fallback: TryVulkan/TryOpenGL under
  RENDERER_* compile defines; picks per-platform order; exits fatally when none
  initialize.
- **Public surface**: `CreateGraphicsBackend(...)` (factory); called from the
  graphics thread bring-up.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: absorbed into `app/host` composition root (ARCH §3:
  the app REGISTERS the backend; selection from `Config.video.backend`). Shape:
  rewritten (trivial).
- **Flags consumed here**: none.
- **Hacks**: none.
- **Risks**: none.

---

## host_app/graphics/gpu_vertex.h (40 lines)

- **Purpose**: `GPUVertex` (32-byte GPU vertex layout: pos, texel-space UV, Q,
  RGBA8, fog) + PS2BlendParams/PS2DepthParams uniform mirrors.
- **Public surface**: structs; included by opengl_backend.h, vulkan_backend.h,
  two in-tree preservation tests.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: MIGRATION §1.4: split — the DrawIR vertex record
  (VertexRec, ARCH §4.6 keeps 12.4 raw UV + STQ) → `engine/gs/hle/ir/`; the
  GL-specific 32-byte layout → `engine/render/backends/gl/gl_vertex_stream.h`.
  PS2Blend/DepthParams die (DrawIR raster state supersedes). Shape: split.
- **Flags consumed here**: none.
- **Hacks**: none.
- **Risks**: vconv equivalence test covers the conversion.

---

## host_app/graphics/gs_c3_counters.h (79 lines)

- **Purpose**: [GS-C3] always-on draws-in vs GL-calls-out counters (the PCSX2-PR
  currency): g_draws_in/g_verts_in/g_glcalls_draw/g_merged_in — the regression
  currency emitted by bench and gs_replay.
- **Public surface**: `namespace c3` inline atomics; included by
  opengl_backend.cpp, gl_state_cache.h, gs_glbatch.h (graphics_thread).
- **Classification**: GENERIC-PS2.
- **Refactor destination**: MIGRATION §1.4: → `engine/core/diag/` (diagnostics
  build) — but note these counters feed the perf harness; under ARCH §18 the
  per-frame draw/call ratio is also PerfMonitor evidence → keep the counter
  POINTS compiled-in cheap (relaxed atomics) in `core/triage/perf_monitor`
  consumers, emitters in diag. Shape: moves.
- **Flags consumed here**: none (PS2_GS_GLBATCH referenced in a comment; latched
  in graphics_thread's gs_glbatch.h).
- **Hacks**: inline mutable globals in a header — becomes a small counters
  object.
- **Risks**: none.

---

## host_app/graphics/viewport_math.h (154 lines)

- **Purpose**: [U3-SCREENFIT] pure output-rect math for the scanout viewport:
  Stretch/Fit/Fill/Integer/Custom modes, aspect selector (auto from GS DISPLAY
  VCK geometry / 4:3 / 16:9 / PAR1:1), pinned rounding conventions, degenerate
  guards. No GL/SDL.
- **Public surface**: `namespace viewport_math`; included by opengl_backend.cpp
  (ApplyScanoutViewport), display_settings.cpp, tests/test_viewport_math.cpp.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: MIGRATION §1.4: "gs/hle/present/ fit/aspect math —
  verbatim" → `engine/gs/hle/present/viewport_math.{h,cpp}` (concept pair).
  Inputs become `Config.video.{fit_mode,aspect,custom_*}` (SETTINGs — already in
  the ARCH §4.8 schema). Shape: moves verbatim.
- **Flags consumed here**: none (settings arrive via display_settings, not env).
- **Hacks**: none — model header.
- **Risks**: none; test_viewport_math pins rounding.

---

## host_app/graphics/vconv.h (245 lines)

- **Purpose**: [R147-GFXRES] single-pass GSVertex→GPUVertex conversion with the
  two per-draw bounding boxes fused in (bb_tri strict-compare replica of the
  post-draw extent loop; bb_all for the copy-rect), mode-specialized on
  Untextured/UvFixed/Stq. Templated so the equivalence test runs production code
  off-GL. NaN caveat documented (legacy was already UB).
- **Public surface**: `namespace vconv` (UvMode, FusedBBoxes, ConvertVertices…);
  included by opengl_backend.cpp, tests/test_vconv_equivalence.cpp.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/render/backends/gl/vertex_convert.{h,cpp}`
  (concept pair consumed by GlVertexStream/GlDrawExecutor) — under ARCH §4.6 the
  conversion consumes DrawIR VertexRec; the fused-bbox outputs feed
  CoherenceState extent updates. PS2_GL_VCONV_FAST (latched in render.h) → NORM:
  the fast path IS the only path, legacy 3-pass loop deletes. Shape: moves.
- **Flags consumed here**: none read in-file (PS2_GL_VCONV_FAST / PS2_GL_VSUV
  comment references; latches elsewhere → both NORM).
- **Hacks**: none; the header's legacy-vs-fast dual arms in the .cpp die with the
  gate.
- **Risks**: test_vconv_equivalence; corpus replay.

---

## Shaders — host_app/graphics/opengl/shaders/ (11 files, 3,026 lines)

Two families with opposite fates. Family 1 (compute-raster, "unified-vram-
renderer"): loaded from disk at runtime by CompileComputeShaders via the
hand-rolled `#include` preprocessor — **all delete** with MIGRATION §1.1's
compute-raster row. Family 2 (reference copies of the embedded raster shaders).
Property tests (tests/test_{tfx,rasterization,texture_sampling,pixel_format,
blend}_properties.cpp) reference family-1 sources — those tests retarget the
gs/format C++ layer or retire with the experiment (same-commit rule as
test_integration_render_queue).

### opengl/shaders/cs_rasterize_tri.glsl (567 lines)
- **Purpose**: GL 4.3 compute rasterizer for tri/strip/fan with the full GS
  per-pixel pipeline against the VRAM SSBO. **Classification**: GENERIC-PS2
  (experiment). **Destination**: DELETE (MIGRATION §1.1 names it). Its VALUE:
  a documented GLSL transcription of the GS pixel pipeline — mine for the
  software backend's reference implementation before deleting. **Flags**: none.
  **Hacks**: none. **Risks**: none (inactive on macOS; delete row already
  covers the Windows 4.3+ activation via `m_use_compute_vram`).

### opengl/shaders/cs_rasterize_sprite.glsl (475 lines)
- Same row as cs_rasterize_tri: sprite variant (v1-color convention, bbox
  threads). DELETE.

### opengl/shaders/cs_rasterize_line.glsl (466 lines)
- Same row: Bresenham line/linestrip variant. DELETE.

### opengl/shaders/cs_scanout.glsl (97 lines)
- Same row: compute scanout (DISPFB decode from SSBO → image2D). DELETE with
  DispatchScanout.

### opengl/shaders/swizzle.glsl (250 lines)
- **Purpose**: GLSL port of texture_decoder's GetAddress* block tables/address
  math (CT32/Z32/CT16/Z16/Z16S/T8/T4). Included only by cs_* shaders — the
  FOURTH+ reimplementation of the PSM page/block math (A3 §2 duplicate census).
  **Destination**: DELETE with family 1. The refactor's single source is
  `engine/gs/format/psm_layout.*`; if a future backend needs GPU-side
  addressing it must be GENERATED from that table, not hand-mirrored.
  **Flags**: none. **Hacks**: the file IS the hack (a comment-enforced mirror
  of decoder tables). **Risks**: none.

### opengl/shaders/pixel_format.glsl (262 lines)
- GLSL VRAM read/write per PSM over the SSBO; includes swizzle.glsl. DELETE
  (family 1). Same mirror-hazard note.

### opengl/shaders/texture_sample.glsl (317 lines)
- GLSL sampleTexture port: FST/STQ, CLUT lookup, wrap modes, bilinear, TEXA.
  DELETE (family 1); TEXA behavior reference for the format layer's CT16 TEXA
  gap fix (A3 §2 inconsistency #1) before deletion.

### opengl/shaders/tfx.glsl (57 lines)
- GLSL TFX combine (MODULATE/DECAL/HIGHLIGHT/HIGHLIGHT2 + TCC). DELETE
  (family 1).

### opengl/shaders/blend.glsl (289 lines)
- GLSL alpha test/blend/ztest/fog/COLCLAMP/FBA/dither pipeline tail. DELETE
  (family 1); dither matrix handling is reference input for the DrawIR
  dthe/dimx fields (ARCH §4.6 closes the .gsdl dither gap).

### opengl/shaders/ps2_vert.glsl (62 lines)
- **Purpose**: REFERENCE COPY of the raster vertex shader; the authoritative
  source is the embedded `k_ps2_vert_src` (opengl_backend.cpp:1247) — stated
  in the file header. [R169-GLRUN P2.1 PS2_GL_VSUV] raw-attribute UV mode
  documented here (NORM: VS-side UV scaling becomes the only mode).
  **Destination**: superseded — GlShaderLibrary owns ONE source (embedded or
  a build-embedded .glsl asset, but exactly one). Shape: deleted as a
  duplicate, or becomes the single source that the .cpp embeds at build time
  (preferred: file → embedded via CMake, killing the two-copy drift hazard).
  **Flags**: none. **Hacks**: two-copy drift by design. **Risks**: none —
  test_ztex_equivalence compiles from the r162 exported (embedded) source.

### opengl/shaders/ps2_frag.glsl (184 lines)
- Same as ps2_vert: reference copy of `k_ps2_frag_src` (authoritative at
  opengl_backend.cpp:1312), pre-UNIFPACK-transform form. Same destination:
  single-source via build embedding into GlShaderLibrary; the R169 pack
  transform is folded into the authored source (NORM), deleting
  R169TransformFragSrc.

---

## Vulkan remnants — host_app/graphics/vulkan/ (6 files, 3,765 lines)

### vulkan/vulkan_backend.cpp (1,836 lines)
- **Purpose**: 2026-03-era Vulkan GraphicsBackend: instance/device/swapchain/
  render-pass/command-buffer scaffolding; DrawBatch/Present/texture methods
  explicitly stubbed ("tasks 8.2-8.4" never completed). Far behind the GL
  backend (no targets, no serve tiers, no deposits).
- **Public surface**: class VulkanBackend; compiled ONLY when
  RESOLVED_RENDERER=vulkan (CMakeLists.txt:499-503); factory TryVulkan.
- **Classification**: GENERIC-PS2 (stale).
- **Refactor destination**: **DELETE** — MIGRATION §1.1 row "graphics/vulkan/*
  (2,034 ln + shaders + CMake branch) … delete (rebuilt later per ARCHITECTURE
  §6)". The future Vulkan backend is a NEW IRenderBackend implementation with
  the ARCH §6 pipeline-cache contract; nothing here ports.
- **Flags consumed here**: none (zero getenv).
- **Hacks**: none relevant (pre-gate-era code).
- **Risks**: none; CMake branch removal is the only coupling.

### vulkan/vulkan_backend.h (198 lines)
- Declaration for the above. DELETE (same row).

### vulkan/shaders/ps2_vert.glsl (43) / ps2_frag.glsl (45) / scanout_vert.glsl (28) / scanout_frag.glsl (15)
- SPIR-V-compiled at build time under the vulkan CMake branch; primitive
  (single-quad scanout + basic blend uniform) — generations behind the GL
  pipeline. DELETE (same row). The rebuilt backend derives its shaders from
  the DrawIR state-space enumeration (ARCH §6), not from these.

---

## host_app/graphics/.DS_Store
- macOS Finder metadata, not code. Delete / ensure gitignored. (Excluded from
  all counts.)

---

# SHARD SUMMARY — graphics (40 files, 47,030 lines)

flags(N→…) = getenv latches IN the file, bucketed NORM/SETTING/DIAG/TEST/DELETE.
Cross-file mirror latches (SH5_BIND, SH6_SWEEP, S9_DEMOTE in both the .cpp and the
S9 .inc) are counted where latched, so shard flag-rows total 112 over 132 sites;
gates consumed via render.h/texture_decoder.h helpers are tabled in those files'
shards.

| file | lines | class | destination | shape | flags(N→NORM/SET/DIAG/TEST/DEL) | hacks(N) |
|---|---|---|---|---|---|---|
| opengl/opengl_backend.cpp | 25,598 | MIXED | split per REGION MAP: render/backends/gl/{gl_device,gl_shader_library,gl_uniform_cache,gl_draw_executor,gl_vertex_stream,gl_texture_pool} + gs/hle/texsrc/{texture_source,clut_source,decode_cache} + gs/hle/targets/{render_target,target_cache,readback_engine,target_refresher} + gs/vram/{vram_image,page_record} + gs/hle/present/{scanout_composer,present_policy} + gs/hle/interp/frame_interpolator; VRAM pins → games/…/renderer_profile.cpp; compute arm + probes deleted | split + rewritten | 72→14/0/20/3/35 | 11 |
| opengl/opengl_backend.h | 2,380 | MIXED | dissolved with the .cpp: TargetFBO→targets/{target_identity,surface_views,readback_engine,coherence_state}; TexResolveOut+m_ftt_*→texsrc/texture_source.h; caches→decode_cache.h; GL members→backends/gl/*; compute members deleted | split | 0 | 4 |
| opengl/world_tristrip_native.inc | 1,300 | MIXED | engine/gs/hle/classrdr/world_tristrip_renderer.{h,cpp}; Accept tuple + 0x230000 + World2 trigger → renderer_profile | split | 2→2/0/0/0/0 | 3 |
| opengl/s9_composite_native.inc | 2,273 | MIXED(→GAME recipe) | engine/gs/hle/classrdr/stencil_composite_renderer.{h,cpp} (+ SequenceRecipe engine); 7-pass recipe + 0x230000/0x280000/0x268000 → renderer_profile | split | 4→0/0/1/0/3 | 4 |
| opengl/s9_composite.h | 383 | GAME | recipe constants → renderer_profile; FSM enum/hold → stencil_composite_renderer.h | split | 3→2/0/1/0/0 | 1 |
| opengl/s9_shadow.h | 254 | GENERIC-PS2 | value logic → gs/format/{alpha,clut} + texsrc DepthToColor case + backend 16-bit store convention | split (gates deleted) | 6→6/0/0/0/0 | 0 |
| opengl/sprite_hud_native.inc | 1,679 | MIXED | engine/gs/hle/classrdr/sprite_hud_renderer.{h,cpp}; tuples/family/0x230000 → profile; roundSprite → TitleProfile.quirks | split | 2→2/0/0/0/0 | 3 |
| opengl/skinned_native.inc | 1,680 | MIXED | engine/gs/hle/classrdr/skinned_renderer.{h,cpp} (+ raw slot registration); tuples/pin → profile | split | 4→4/0/0/0/0 | 3 |
| opengl/particles_native.inc | 1,667 | MIXED | engine/gs/hle/classrdr/particles_renderer.{h,cpp}; tuples/pins → profile | split | 2→2/0/0/0/0 | 2 |
| opengl/fmv_blit_native.inc | 1,633 | MIXED | engine/gs/hle/classrdr/fmv_blit_renderer.{h,cpp}; family-F spec + 0x150000/0x70000 → profile | split | 2→2/0/0/0/0 | 2 |
| opengl/gl_state_cache.h | 917 | GENERIC-PS2 | engine/render/backends/gl/gl_state_cache.{h,cpp} as GlDevice member; lifetime census → core/diag | rewritten | 5→5/0/0/0/0 | 3 |
| opengl/rt_neverstale.h | 351 | MIXED | predicates → gs/hle/texsrc ResolveRead freshness ladder; counters → diag; crys trigger context → profile | split | 3→2/0/1/0/0 | 0 |
| opengl/alias_rect.h | 216 | GENERIC-PS2 | band/coverage math → gs/vram/page_record (invariant #36) | split | 2→2/0/0/0/0 | 0 |
| opengl/rt_copy_tracker.h | 256 | GENERIC-PS2 | gs/hle/targets coherence_state/surface_views methods | split | 2→2/0/0/0/0 | 1 |
| opengl/blend_ff_map.h | 249 | GENERIC-PS2 | engine/render/backends/gl/blend_ff_map.{h,cpp} | verbatim (gates dropped) | 2→2/0/0/0/0 | 1 |
| opengl/gs_scale.h | 70 | MIXED | engine/gs/hle/targets/scale_policy.{h,cpp}; factor→Config.video.scale; 0x230000 pin→profile VramLayout | verbatim + de-game | 1→0/1/0/0/0 | 1 |
| opengl/compute_data_layouts.h | 127 | GENERIC (dead) | DELETE (MIGRATION §1.1 compute-raster row) | deleted | 0 | 1 |
| graphics_backend.h | 174 | GENERIC-PS2 | engine/render/backend.h (IRenderBackend, ARCH §6) | rewritten | 0 | 1 |
| graphics_backend.cpp | 114 | GENERIC-PS2 | app/host composition root (backend selection from Config) | rewritten | 0 | 0 |
| gpu_vertex.h | 40 | GENERIC-PS2 | split: VertexRec→gs/hle/ir; GL layout→backends/gl/gl_vertex_stream.h | split | 0 | 0 |
| gs_c3_counters.h | 79 | GENERIC-PS2 | core/diag (emitters) + counter points feed core/triage PerfMonitor | moves | 0 | 1 |
| viewport_math.h | 154 | GENERIC-PS2 | engine/gs/hle/present/viewport_math.{h,cpp} | verbatim | 0 | 0 |
| vconv.h | 245 | GENERIC-PS2 | engine/render/backends/gl/vertex_convert.{h,cpp} | moves | 0 | 0 |
| shaders/cs_rasterize_tri.glsl | 567 | GENERIC (dead) | DELETE (compute-raster row) | deleted | 0 | 0 |
| shaders/cs_rasterize_sprite.glsl | 475 | GENERIC (dead) | DELETE | deleted | 0 | 0 |
| shaders/cs_rasterize_line.glsl | 466 | GENERIC (dead) | DELETE | deleted | 0 | 0 |
| shaders/cs_scanout.glsl | 97 | GENERIC (dead) | DELETE | deleted | 0 | 0 |
| shaders/swizzle.glsl | 250 | GENERIC (dead) | DELETE (mirror of gs/format tables) | deleted | 0 | 1 |
| shaders/pixel_format.glsl | 262 | GENERIC (dead) | DELETE | deleted | 0 | 1 |
| shaders/texture_sample.glsl | 317 | GENERIC (dead) | DELETE (TEXA reference first) | deleted | 0 | 0 |
| shaders/tfx.glsl | 57 | GENERIC (dead) | DELETE | deleted | 0 | 0 |
| shaders/blend.glsl | 289 | GENERIC (dead) | DELETE (dither reference first) | deleted | 0 | 0 |
| shaders/ps2_vert.glsl | 62 | GENERIC-PS2 | superseded by single-source GlShaderLibrary (build-embed or delete) | deleted/absorbed | 0 | 1 |
| shaders/ps2_frag.glsl | 184 | GENERIC-PS2 | superseded by single-source GlShaderLibrary (UNIFPACK form authored) | deleted/absorbed | 0 | 1 |
| vulkan/vulkan_backend.cpp | 1,836 | GENERIC (stale) | DELETE (MIGRATION §1.1; Vulkan rebuilt against IRenderBackend later) | deleted | 0 | 0 |
| vulkan/vulkan_backend.h | 198 | GENERIC (stale) | DELETE | deleted | 0 | 0 |
| vulkan/shaders/ps2_vert.glsl | 43 | GENERIC (stale) | DELETE | deleted | 0 | 0 |
| vulkan/shaders/ps2_frag.glsl | 45 | GENERIC (stale) | DELETE | deleted | 0 | 0 |
| vulkan/shaders/scanout_vert.glsl | 28 | GENERIC (stale) | DELETE | deleted | 0 | 0 |
| vulkan/shaders/scanout_frag.glsl | 15 | GENERIC (stale) | DELETE | deleted | 0 | 0 |

Shard flag totals: 112 latch rows / 132 getenv sites → **35 NORM · 1 SETTING ·
23 DIAG · 3 TEST-ONLY · 38 DELETE** in-shard, plus ~30 helper-consumed gates
(FAST/FAST2 family, GS_RESIDENT, GS_DEPOSIT_EEAPPLY, GS_CLASSRDR, GS_INTERP,
GL_FOGLANE_LIVE, GS_MIP_*) whose latches — and ledger rows — belong to other
shards but whose NORM/SETTING outcomes this shard's code embodies.

## TREE-GAPs and doc inconsistencies

1. **Naming discrepancy, not a gap**: MIGRATION §1.3's opengl_backend row says the
   six .inc files go to "gs/hle/classes/*.cpp", while §1.6 rows and the ARCH §3
   tree say `engine/gs/hle/classrdr/<name>_renderer.{h,cpp}`. This filemap follows
   §1.6/tree (the REV2 wording). §1.3 should be corrected.
2. **TREE-GAP (minor, propose)**: a declarative sequence-FSM engine file
   `engine/gs/hle/classrdr/sequence_recipe.{h,cpp}` (class SequenceRecipe engine
   executing RendererProfile.sequences — the S9 seven-pass + FMV family
   machinery). ARCH §14.3 defines the `SequenceRecipe` DATA type but the tree
   names no engine file that runs it; the stencil-composite renderer needs it as
   a separable class under D4.
3. **TREE-GAP (minor, propose)**: `engine/gs/hle/targets/scale_policy.{h,cpp}` —
   the tree's targets/ shows only render_target as a representative pair; scale
   policy ([GE1] + native-pin set) is its own concept/class. Consistent with the
   "representative pairs" convention but named here so INDEX can reverse-map
   gs_scale.h.
4. **Ambiguity to settle**: MIGRATION §1.3 mentions "in-TU test main() → tests/"
   for opengl_backend.cpp — **no `int main`/test-main exists in the current
   file** (grep verified). Either it referred to the TestZTex_/TestIdx_ seams
   (mapped here → tests/) or it is stale; INDEX should not expect a main.
5. **A2 registry corrections found while joining** (feed the FLAG_LEDGER):
   PS2_GL_STATE_CACHE and PS2_GL_BIND_CACHE are default-ON in code (A2 lists
   both as presence-gated experiments/delete-both); PS2_GS_RESIDENT,
   PS2_GL_RT_NEVERSTALE/READVIEW, PS2_GL_UVNATIVE, PS2_GS_MIP_ALLCLASS are
   NORM per the shard instructions' peak-config rule regardless of A2's
   delete-both/tuning rows. 23 vars latched in this shard are absent from the
   A2 CSV entirely (the render.h FAST2-family sub-gates listed in A2 finding
   #4's family table but not as first-class rows, plus PS2_FTT_FORCE_EVICT/
   FORCE_DECLINE, PS2_GS_CLASSRDR_{SPRITES,S9,RAWOTHER}, PS2_S32_FORCESTALL,
   PS2_GL_NO_FLUSH — /tmp/s116/f6/flag_dispositions.txt has the exact list);
   the master FLAG_LEDGER must add rows for them (FORCE_EVICT/FORCE_DECLINE →
   TEST-ONLY repro hooks; the CLASSRDR_* per-class severability hatches → NORM).

## Dead code (evidence-based)

- `graphics/vulkan/**` (2,034 code lines + 4 shaders): compiled only under
  `RESOLVED_RENDERER=vulkan` (host_app/CMakeLists.txt:499); stale since 2026-03,
  DrawBatch/Present stubbed in-file; MIGRATION §1.1 deletes.
- Compute-raster family: `compute_data_layouts.h`, `shaders/cs_*.glsl` and the
  five module .glsl they #include (swizzle/pixel_format/texture_sample/tfx/
  blend), plus opengl_backend.cpp regions R4/R25b — gated by
  `m_use_compute_vram = InitVRAMBuffer()` which hard-returns false on
  `__APPLE__` (opengl_backend.cpp:3279-3283) and requires GL 4.3+/extensions
  elsewhere; MIGRATION §1.1 deletes explicitly (with the test-file blocks).
- `shaders/ps2_vert.glsl` / `ps2_frag.glsl`: never loaded at runtime — the
  embedded `k_ps2_vert_src`/`k_ps2_frag_src` are authoritative (stated in
  ps2_frag.glsl's own header; no runtime file-load path references them —
  verified by grep, only the vulkan CMake branch compiles same-NAMED files from
  vulkan/shaders/).
- `graphics/.DS_Store`: Finder metadata.

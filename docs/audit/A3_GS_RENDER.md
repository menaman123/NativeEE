# A3 — GS / RENDER PIPELINE AUDIT

Audit stage A3 of the the title PS2 static-recomp runtime
(`$PS2R_OLD_ROOT`, read-only, 2026-09-22).
Scope: the entire GS/render pipeline as it actually is — data flow, format
layer, texture-source decision, VRAM mirror model, target lifecycle, class
renderers / draw IR, scale/interp/present — plus the essential-vs-accidental
verdict and the invariant list the refactor must encode as tests.

Vocabulary follows the GS skill (`.kiro/skills/GS_SKILL`): PSM, TBP/TBW/CBP,
FRAME/ZBUF/TEX0/TEXA/CLAMP/TEST/ALPHA, CLUT CSM1/CSM2, PATH1/2/3, contexts,
scanout (PMODE/DISPFB/DISPLAY).

## Size ground truth (measured)

| Unit | Size | Note |
|---|---|---|
| `graphics/opengl/opengl_backend.cpp` | 25,598 lines / 1.39 MB | single TU; #includes 6 native-renderer `.inc` files |
| `graphics/opengl/opengl_backend.h` | 2,380 lines | TargetFBO + backend class |
| `OpenGLBackend::DrawBatch` | ~6,111 lines (8288–14399) | one function |
| `OpenGLBackend::FindTargetTextureImpl` | ~1,999 lines (19605–21603) | one function, **44 return sites**; plus a ~190-line wrapper (19412–19604: SH5-BIND probe + interp shadow-swap, 5 returns) |
| `OpenGLBackend::ResolveTextureSeam` | ~3,951 lines (4337–8288) | one function |
| `gs_state.cpp` | 5,623 lines | register semantics + transfers + Flush |
| `gif.cpp` | 5,532 lines | GIFtag parse + FlushBatch + IR apply |
| `graphics_thread.cpp` | 4,341 lines | queue consumer + dispatch |
| `render.h` | 3,734 lines | RenderBatch/RenderJob/queue + epoch machinery |
| `texture_decoder.cpp` | 3,049 lines | canonical format layer |
| Distinct `PS2_*` env gates | **560** whole host_app / **282** in GS-render files | measured via grep census (`/tmp/s113/a3/*.txt`) |
| `docs/loop_state/` fix records | 634 files | the year's fix ledger |

---

## 1. DATA FLOW — GIF packet to pixel (current architecture, as-is)

```
EE thread (CPU)                                     GFX thread
═══════════════                                     ═══════════

VU1 XGKICK ──► PATH1 ─┐
VIF1 DIRECT ─► PATH2 ─┼─► GifArbiter (gif_arbiter.h/.cpp)
GIF DMA ch2 ─► PATH3 ─┘     priority drain P1>P2>P3; optional
       │                    "gifdrain" thread (PS2_GIF_DRAIN_THREAD)
       │  VU1-HLE typed kicks bypass GIFtag re-parse:
       │  gsdrawir::PacketToDrawIR (hle/gs_draw_ir.h)  [GS-DRAWIR]
       ▼
GIF::ParseSpan / GIF::ApplyHleDrawIR (gif.cpp)
       │ GIFtag walk: PACKED/REGLIST/IMAGE; A+D → ProcessReglist
       ▼
GSState register setters (gs_state.h/.cpp)          ┌───────────────────┐
  SetPrim/SetTex0/SetFrame/... (dual ctx 0/1)       │ IMAGE data path:  │
  KickVertex → vertex_queue → FlushPrimitive        │ WriteHWReg swizzle│
  → draw_buffer (GSVertex)                          │ writes → gs_vram  │
       │                                            │ (EE thread)       │
       ▼                                            └───────────────────┘
Flush sites (TWO, duplicated logic):
  GIF::FlushBatch (gif.cpp:1705)  +  GSState::Flush (gs_state.cpp:304)
  - BumpVramWriteEpochIfDirty()          [M1A-EPOCH]
  - build RenderBatch + BatchTextureInfo
  - capture VRAM/CLUT snapshots (r119::CaptureSharedSnapshot
    epoch-keyed shared buffers, or private memcpy)
  - stamp [GSDL-ID] src_hle_* identity
       │
       ▼
RenderQueue (render.h:3057; Push :3145, PushNoFlush :3176)
  RenderJob { type, batch, seq, src_frame, vram_epoch, args, upload_pixels }
  push sites: gif.cpp:3160 (TryMergePush), gs_state.cpp:902/4318/4407/... [COVERAGE-FIX 2026-09-22: was :893, a comment line; the TryMergePush call is :902]
       │  mutex+condvar; byte-budget pace ledger [R170-PACE]
       ▼
graphics_thread.cpp ProcessDrawBatchJob (:622)
  drawlist recorder tap (.gsdl)          [HLE-HARNESS]
  s103drawlim / s105skip diagnostic filters
  classrdr::Dispatch (gs_class_renderer.h)  — 10-class taxonomy
       │ native hit                    │ decline/fallback
       ▼                               ▼
  Native class renderers          OpenGLBackend::DrawBatch (6,111 ln)
  (6 .inc files in the same TU)     ├─ ResolveTextureSeam (3,951 ln)
  world_tristrip / s9_composite     │    └─ FindTargetTexture(Impl)
  sprite_hud / skinned /            │         serve tiers → §3
  particles / fmv_blit              ├─ vertex convert (vconv.h → GPUVertex)
       │                            ├─ blend map (blend_ff_map.h)
       └───────────► GL state ◄─────┘  uniform packing [R169]
                        │
                        ▼
      TargetFBO map (m_target_fbos, keyed frame_fbp)
        color_tex / depth_tex / alt16 companion / res_fbo pair
        persistence_tex / rt_copy_tex / z_pbo / c_pbo
                        │
        readback ladder ▼                       deposits
      SyncRTToVRAM (:16272) / SyncZToVRAM (:18157)
        └─► gsovl deposit overlay (gs_deposit_overlay.h)
             GFX writes 4 MiB SHADOW; EE commits at stream
             boundaries (ApplyDeposits) — single-writer model
                        │
                        ▼
      gs_vram (4 MB CPU mirror) ◄── EE uploads (WriteHWReg,
        5 page-granular trackers        local→local, HLE feeds)
        (§4)                            savestate restore
                        │
                        ▼
      Scanout: DrawScanout (:24208) / DrawScanoutFromRT (:18540)
        DISPFB/DISPLAY decode (texture_decoder DecodeScanoutBuffer
        or native FBO present via gsresident::ScanoutResolve [GB3])
                        │
                        ▼
      Present (:23594) → gs_interp.h [GS-INTERP] Phase-E replay
        (shadow m_interp_targets; K intermediate frames)
        → present_pace.h pacing → SDL swap → VBLANK raise
```

### Structs at each edge — field counts and mutators

| Struct | Where | ~Fields | Mutated by |
|---|---|---|---|
| `GSVertex` | gs_state.h | 14 | EE: KickVertexImpl/AssembleVertex only |
| `GSState` | gs_state.h | ~60 members (~150 scalars incl. per-ctx arrays) | EE thread only: GIF decode bodies + setters; `TryKickRunBulk/Native` (typed IR path) |
| `gsdrawir::Vert` | hle/gs_draw_ir.h | 15 | producer thread at conversion; immutable after |
| `gsdrawir::Item/Stream` | hle/gs_draw_ir.h | 6 / 2 | producer (VU1-HLE runtime); consumed by GIF::ApplyHleDrawIR |
| `BatchTextureInfo` | render.h | ~36 (incl. 2 snapshot vectors + 2 shared_ptr + mip vector) | EE at flush; GFX thread *materializes* `vram_snapshot` from `vram_snapshot_sp` at dispatch; decode-time `do_refresh` memcpys OVER it (GFX) |
| `RenderBatch` | render.h | ~55 top-level | EE at flush; GFX fills `r169_pre` (run collector) and reads the rest; `shuffle_*` set during resolve |
| `RenderJob` | render.h | 7 + args{4} | EE at push (seq/epoch stamps); GFX consumes |
| `RenderQueue` | render.h:3057 | — | EE pushes, GFX pops; merge/pace logic inside |
| `TargetFBO` | opengl_backend.h:567 | **~75** | GFX thread only |
| `TexResolveOut` | opengl_backend.h:131 | 15 | GFX: ResolveTextureSeam per draw |
| FTT per-call result slots (`m_ftt_depth_fmt`, `m_ftt_idx_fmt`, `m_ftt_idx_tex2`, `m_ftt_alt16_serve`, `m_ftt_uv_native`, ...) | opengl_backend.h:959–1013 | ~10 loose members | GFX: reset at every FTT entry ([W2] rule), consumed by same DrawBatch — implicit out-params as backend state |
| `DrawRec`/`VertexRec`/`JobRec` (.gsdl v2) | hle/drawlist_record.h | ~60 / 14 / 10 | recorder tap on GFX thread |
| Page trackers (5 families) | §4 | 512 pages each | mixed EE/GFX — see §4 |
| `gsovl` shadow overlay | gs_deposit_overlay.h | 4 MiB + state[512] | GFX encodes, EE commits |
| Snapshot share cache | render.h r119::CaptureSharedSnapshot | keyed (start,size) | EE thread only |
| `gsinterp` tick ring + shadow targets | gs_interp.h, m_interp_targets | — | GFX thread only |

Two flush paths (`GIF::FlushBatch`, `GSState::Flush`) both build
RenderBatch + snapshots — duplicated capture logic that must stay in
lockstep (both carry the same [M1A-EPOCH] hooks, footprint narrowing,
CLUT capture, identity stamping).

---

## 2. FORMAT LAYER — every PSM/swizzle/CLUT/TEXA implementation site

Canonical home: `texture_decoder.cpp`. But the layer is **re-implemented**
in at least three other places (write-side swizzle in gs_state.cpp, encode
side + shader-side aliasing in opengl_backend.cpp, page-geometry walks in
gs_vram_census.h). Table of every site (file:line, current tree):

| Concern | Site | Notes |
|---|---|---|
| Block tables CT32/Z32 | texture_decoder.cpp:263 / :275 | `block_table_PSMCT32[4][8]`, `block_table_PSMZ32[4][8]` |
| Block tables CT16/Z16/Z16S | texture_decoder.cpp:577 / :590 / :604 | 8×4 layouts |
| Block tables T8/T4 | texture_decoder.cpp:872 / :922 | 4×8 / 8×4 |
| PixelAddress (GetAddressPSM*) | texture_decoder.cpp:306 (CT32), :336 (Z32), :645 (CT16), :675 (Z16), :694 (Z16S), :964 (T8), :996 (T4) | page → block → column math; CT16S handled via CT16 variant table; PSMZ24 reuses Z32 |
| Block-LUT fast decode | texture_decoder.cpp ~:2300 (r121blk) | same tables, LUT-flattened; equivalence-tested |
| Decode footprint math | texture_decoder.cpp `ComputeDecodeFootprint` / `ComputeDecodeRowBand` | mirrors GetAddress page math; consumed by flush narrowing + texcache_key.h |
| **Write-side swizzle (duplicate #1)** | gs_state.cpp WriteHWReg / writePixel / local→local (23 GetAddress refs) | HWREG IMAGE uploads swizzle into gs_vram; PS2_XFER_FAST word-unrolled variant must stay byte-identical |
| **Encode-side swizzle (duplicate #2)** | opengl_backend.cpp SyncRTToVRAM/SyncZToVRAM/EncodeRTToVRAM (34 GetAddress refs) | FBO → mirror deposit in render PSM layout |
| **Shader-side aliasing (duplicate #3)** | opengl_backend.cpp fragment shaders (fmt 3/4/5 remaps), s9_composite_native.inc | closed-form byte-alias remaps of CT16-view-of-Z32, T8H-view-of-CT32/CT16-pair |
| CLUT CSM1 swizzle | texture_decoder.cpp:944 `CSM1Swizzle` | 8-entry half-block swap within 32 |
| CLUT read | texture_decoder.cpp:1146 `ReadCLUT32`, :1217 `ReadCLUT16` | CSM=0 → CSM1Swizzle, CSM=1 → linear (CSM2 partially: linear only) |
| CLUT-as-GL-texture | opengl_backend.h `EnsureClutTex` (m_clut_texes) | 256×1 prefolded; content-hash keyed; kick-snapshot sourcing [R173-IDXFRESH] |
| TEXA — CT24 | texture_decoder.cpp:502–553 DecodePSMCT24 | ta0 + AEM (RGB==0 → 0) applied ✔ |
| TEXA — CT16 **INCONSISTENCY** | texture_decoder.cpp:740–780 DecodePSMCT16 → Expand16to32(:1946) | alpha = MSB ? 255 : 0. **TA0/TA1/AEM not applied** — correct only for default-ish TEXA (ta1=0x80→255, ta0=0→0); non-default TEXA on 16-bit textures decodes wrong. opengl_backend.cpp:1395 comments acknowledge "no TEXA" |
| 16-bit expansion (zero-fill vs replicate) | Expand16to32 (texture_decoder.cpp:1946) = **bit-replicate**; opengl_backend.cpp ~:1620/:2157 RT-store paths use "the R162-proven 5551 expansion verbatim (bit-replicate + msb)" | replicate is the settled convention; any zero-fill variant is legacy and must not survive the refactor |
| Alpha domain scale | ScaleAlphaPS2ToGPU (texture_decoder.h) | PS2 0–128 → GPU 0–255, ≥128 clamps 255 |
| Z formats | DecodePSMZ32/Z16/Z16S (texture_decoder.cpp) + GetAddressPSMZ* | **rule**: Z reads/writes MUST use PSMZ helpers; PSMCT helpers on Z bytes silently corrupt co-paged data (texture_decoder.h contract comment) |
| Dual-format aliasing (CT16 view of Z32/Z16, T8H view of CT32/CT16-pair, 32↔16 shuffles) | FTT fmt=3 (Z16-view-of-Z32 remap + halfword select + 224-row zoff), fmt=4 (T8H=CT32 alpha byte), fmt=5 (CT16 pair partner at +h·fbw·2); ShuffleType enum (render.h:119) 16to32/32to16/DepthToColor/ChannelShuffle; [S83-FOGNATIVE] pins alias targets native-res | native-pixel-defined: requantization to native centers under scale ([S103-UVNATIVE]) |
| Dither | gs_state.h DTHE/DIMX capture [WN4-CT16Q] | folded into merge keys; applied at 16-bit frame writes |
| Page geometry (census walk) | gs_vram_census.h `PagedGeom` | page dims per PSM: CT32-class 64×32, CT16-class 64×64, T8 128×64, T4 128×128 — 4th reimplementation of page math (deliberately shared by tracker + census) |
| Equivalence tests | tests/swizzle/test_swizzle_equivalence.cpp, test_block_decode_equivalence, test_texture_shuffle, test_blend_ff_map | proof harnesses pinning decoder ↔ LUT ↔ rect-decode identity |

**Inconsistencies found:**
1. TEXA ignored in the CT16 decode path (above) — latent wrongness masked by
   the game's TEXA values.
2. Four independent implementations of PSM page/block math (decoder,
   write-side, encode-side, census PagedGeom) with equivalence tests
   covering only decoder↔LUT and decoder↔rect. Write-side and encode-side
   have no direct cross-proof; they agree today by discipline, not by
   construction.
3. CSM2 CLUT (csm=1) is treated as plain linear; CSM2's COU/CSA
   register-offset loading semantics are not modeled (safe for this game,
   undocumented as a scope cut).
4. `GSState::GetPixelSize` (gs_state.h) is yet another per-PSM bpp table
   separate from the decoder's.

---

## 3. TEXTURE SOURCE DECISION — FindTargetTexture + serve tiers

### The ONE rule

Every branch in this 4,000-line decision tree is an implementation of a
single sentence:

> **A texture read of VRAM must return the bytes that EE-stream order says
> are at that address, presented in the read's own view geometry
> (PSM/TBW/TW/TH), regardless of which physical surface (GL FBO attachment,
> companion, depth texture, decoded mirror, snapshot) currently holds the
> freshest version of those bytes.**

PCSX2 gets this for free by doing readbacks synchronously in stream order;
this runtime instead intercepts reads and routes them to the freshest owner,
with rescue deposits when no GL surface can serve.

### Serve tiers (the SH5-BIND probe's own route names, opengl_backend.cpp:19412–19605)

| Route | Meaning | Principal gate(s) |
|---|---|---|
| `COLORTEX` | live FBO color attachment served directly | [S32-RTKEEP] serve predicate: read-footprint upload epoch ≤ `last_draw_gfx_epoch` (per-page precise bits [CX8]); PS2_GL_RT_NEVERSTALE |
| `RESOLVE` | native-res resolve pair `res_color_tex` (scale>1 CPU-visible reads) | [GE1] PS2_GS_SCALE; NativeReadFBO freshness seqs |
| `ALT16` | 16-bit companion `alt16_tex` (bit-depth bifurcation) | [R105-ALT16] + [R113-ALT16-COHERENCE] RefreshAlt16FromMain of `alt16_stale` rect; [S9-ALT16UV] sizing flag |
| `DEPTH` fmt=1 | native PSMZ32 read of Z32 target → live depth_tex | [R162-ZTEX]/[R163-ZTEX2]; predicate incl. reader TBW |
| `DEPTH` fmt=2 | Z16-family read of Z16-native target (saturating chain) | [R163-ZTEX2] |
| `DEPTH` fmt=3 | PSMZ16 *view* of a Z32 target — byte-alias remap + halfword select + page-row zoff (live values 0 and 224) | [R163-ZTEX2]; `m_ftt_depth_zoff` set every draw [W3] |
| `IDX4` | PSMT8H over CT32-class render: index = alpha byte; self-read via forced-sw-blend rt_copy | [R164-IDXTEX]; CLUT predicate P5; [R173-IDXFRESH] vram_coherent_class=32 voucher |
| `IDX5` | PSMT8H over the CT16 pair (base + partner at +h·fbw·2), two samplers | [R164-IDXTEX]; [R177-IDX16LANE] lane predicate; idx_epoch_guard.h |
| `IDX6` | (probe-visible extension of the indexed family) | idx16_lane.h / foglane_live.h [S107-FOGLANE-LIVE]: fmt=5 lane serve requires lane author seq == surface author seq |
| `DECODE` (ret 0) | fall to CPU decode of snapshot/mirror — with rescue deposit first | [S109-NEVERSTALE] (i): SyncRTToVRAM footprint with epoch_override before decode; [CX8] (iii) no-FBO gsovl::ComposeRead refresh of GPU-authored pages |
| `NOFBO` | no target at tbp — plain decode of kick-time snapshot | CPU-authored pages: snapshot verbatim (TILE-RACE stream-order contract) |
| eviction rescue | grow-rebuild / DestroyTargetFBO deposits drawn-since-sync bytes before death | [S109] (ii); PS2_FTT_FORCE_EVICT repro hook |

Supporting per-call outputs: `m_ftt_uv_native` ([S103-UVNATIVE] requantize
UVs to native pixel centers when an indexed view falls to legacy decode at
scale>1), `TexResolveOut.uv_pre_w/h` ([S111-READVIEW] prescale by the READ's
texel window), `shuffle_type/shuffle_uv_scale_u` (32↔16 width doubling),
[R159-BODY] region sub-rect decode, [S82-MIP] mip chain, L1/L2 decoded-texture
caches keyed by texkey::ContentKey (address-free content key, [R125-L2FIX]).

### Path count

Counting distinct code paths that implement the ONE rule today:
**11 serve routes** (table above) **+ 3 rescue paths** (decline-deposit,
evict-deposit, no-FBO compose-refresh) **+ 2 decode cache layers** (L1
epoch-key/content-key, L2 content-hash) **+ the snapshot-share cache** on
the capture side ≈ **17 cooperating implementations**, spread across
FindTargetTextureImpl (~2,000 lines, 44 returns), ResolveTextureSeam,
SyncRTToVRAM, rt_neverstale.h, idx16_lane.h, foglane_live.h, texcache_key.h
and the flush-site capture logic. Each was added by a dated fix record; none
share a single resolver abstraction.

**Refactor target:** one `ResolveRead(view) → {surface, transform, freshness
proof}` function with an explicit owner table, where each current tier
becomes a *case* in one exhaustive match, and every gate becomes either a
deleted escape hatch or a test.

---

## 4. VRAM MIRROR MODEL — writers, epochs, designed staleness

### Who writes `gs_vram` (4 MB mirror)

| Writer | Thread | Sites |
|---|---|---|
| EE uploads: WriteHWReg (Host→Local IMAGE), local→local writePixel | EE | gs_state.cpp (46 direct write sites) |
| HLE direct feeds (hle_sceGsSendImage etc.) | EE | gs_hle.cpp (4) |
| GIF IMAGE processing | EE | gif.cpp (1) |
| GPU deposits: SyncRTToVRAM / SyncZToVRAM / DepositAlt16Lane / FMV blit | GFX | opengl_backend.cpp (20) — **routed through gsovl shadow when PS2_GS_DEPOSIT_EEAPPLY=on (default)** |
| gsovl::ApplyDeposits (commit shadow→mirror) | **EE** | gs_deposit_overlay.h:204 |
| Savestate restore | EE (drained) | savestate.cpp |
| Compute-raster readback (inactive on macOS) | GFX | explicitly NOT routed through overlay |

### The five page-granular tracking structures (all 512 × 8 KB)

1. `g_vram_page_epoch[512]` + `g_vram_epoch` — EE upload epochs, stamped at
   TRXDIR + re-stamped write-exact at boundaries ([M1A-EPOCH]
   `vram_write_min/max` + `vram_write_pages[8]` precise bits [CX8]).
2. `g_vram_deposit_epoch[512]` + `g_vram_deposit_seq` +
   `g_vram_deposit_inflight` — GFX deposit generations + in-flight bracket
   ([SH1-DEPEPOCH], [M1A-DEPBRACKET]); separate counter family, never
   compared against upload epochs.
3. `gsresident::` `g_author/g_author_fbp/g_epoch/g_stale/g_dirty_band` —
   residency tracker [GB2]: author ∈ {cold, CPU, GPU-target(fbp)}, stale =
   deposit elided; reader page set (census-seeded S9 CLUT band + dynamic).
4. `gsovl::g_state/g_epoch[512]` + 4 MiB shadow — deposit overlay
   state machine 0→2→1→3→0 (clean/encoding/pending/applying).
5. `GSState::vram_write_pages[8]` (512 bits) — precise per-boundary write
   bits feeding tracker #1's re-stamp.

Plus non-page-granular: r119 snapshot-share cache keyed (start,size) with
window epochs; TargetFBO `vram_coherent_epoch/class`; `m_vram_aliased_pages`
(alias_rect.h) in the backend.

### Staleness that is possible BY DESIGN

- 32-bit render content is **never deposited per-draw** ([S107-FOGLANE-LIVE],
  [GB2] texture-only residency): the mirror lags the FBO for GPU-authored
  pages until a demand flush (kDecode/kFTT/kRefresh/kPreDraw/kScanout/
  kDestroy) or present-boundary flush lands. Correctness depends on the §3
  serve tiers intercepting every read.
- EE captures (flush snapshots) can race a pending deposit by ≤1 frame under
  the PBO arm ([GB4] `c_b4_clut_lag` counted as envelope, not hazard).
- The savestate path force-reconciles ([GB4] ReconcileStaleAtSave) — ledger
  empty ⇒ no genuinely-stale page.

### The single-writer model the refactor should adopt

`gsovl` ([TEXPOP-FIX]) already IS the correct model — generalize it:

1. **One writer**: only the EE-stream applier mutates the mirror. GPU
   deposits always land in a shadow surface tagged with the batch's EE-epoch;
   commits happen at EE stream boundaries with per-page last-writer
   arbitration (`upload_epoch ≤ deposit_epoch` ⇒ commit, else drop).
2. **One tracker**: collapse the five structures into one per-page record
   `{author: CPU|GPU(fbp), upload_epoch, deposit_epoch, state, dirty_band,
   reader_flag}` — every current consumer's question is answerable from it.
3. **GPU residency as the default truth**: express "the FBO is the owner" as
   the page record's author field, and make the mirror an explicitly *lazy
   materialization* with a single `MaterializePages(span, reason)` API
   (today's demand-flush sites become reasons). CPU readers must go through
   a `ReadView(span)` that composes mirror + pending shadow (today's
   ComposeRead), never raw `gs_vram[]`.
4. Savestate = MaterializeAll + assert no page stale (today's invariant,
   kept as a test).

---

## 5. RENDER TARGET LIFECYCLE

Keyed by `frame_fbp` in `m_target_fbos` (real) and `m_interp_targets`
(shadow, interp only). TargetFBO carries **~75 fields** (GL objects, logical
dims + [GE1] scale, resolve pair, persistence/rt_copy, PBO pairs + fences +
rings, drawn rects ×3, alt16 companion + stale/drawn rects, lane/author
seqs, coherence epochs...).

- **Creation**: GetOrCreateTargetFBOIn (opengl_backend.cpp:15449) at first
  draw to an fbp; scale fixed per-fbp (gsscale::TargetScale; 0x230000 pinned
  native; [S83-FOGNATIVE] alias-destination pins).
- **Grow**: grow-only to running max — FRAME has no height field (SCISSOR
  bounds drawing), so 224↔225 scissor flips must NOT destroy content
  (the 0x230000 intro-buffer lesson). A genuine grow **destroys and
  recreates** (content lost by design, game redraws), but: (a) pending
  16-bit syncs flushed, (b) [S109 (ii)] drawn-since-sync bytes deposited
  BEFORE destroy, (c) [R113] format-class tags (has_32bit_content,
  was_32/16bit, render_psm/fbw) carried across the rebuild so bifurcation
  doesn't reset.
- **Format retag**: in-frame CT16→CT24/CT32 retag keeps content
  ([R102-NO-FORMAT-CLEAR]); `render_psm` tracks last FRAME PSM;
  `was_32bit/was_16bit` are sticky class bits.
- **Dual views**: [R105-ALT16] bit-depth bifurcation — 16-bit-class FRAME
  draws at a BP with 32-bit content bind the lazy `alt16_fbo/alt16_tex`
  companion; [R113] coherence rect `alt16_stale` mirrors main→companion
  (RGBA8→5551 pack→RGBA8) before any companion use. The depth attachment is
  a texture ([R162-ZTEX]) shared across views.
- **Resolve pair**: [GE1] scale>1 only — `res_fbo/res_color_tex/res_depth_tex`
  native-res NEAREST downsample for every CPU-visible read; freshness seqs
  skip redundant blits.
- **Eviction**: grow-rebuild (above) and DestroyTargetFBO/DestroyAllTargetFBOs
  (savestate restore, shutdown); kDestroy force-deposits pending syncs.
  PS2_FTT_FORCE_EVICT exists to reproduce eviction staleness.
- **Per-frame clear**: keyed on composite-consumption, not vsync ordinal
  (needs_frame_clear/ever_composited — the 3-syncV-per-game-frame lesson,
  R93 iceberg z-loss).

---

## 6. CLASS RENDERERS + TYPED DRAW IR

### What exists

- **EE-side draw IR** (`gsdrawir`, hle/gs_draw_ir.h): Item kinds kState /
  kPrim / kDrawRun / kAttr / kFlush / kSrcId / kSrcName over pre-decoded
  `Vert{x,y,z,s,t,q,u,v,rgba,f,mask,flags}` with masked attribute resolution.
  Exactness contract G-C1: store-for-store identical to register-op replay
  at every observation point; **verifier included** (verify::RunOps vs
  RunIR + live tripwire PS2_GS_DRAWIR_VERIFY) — this is the strongest
  proven-equivalence asset in the tree.
- **Consumer-side classification** (`classrdr`, gs_class_renderer.h):
  10 classes (world-tristrip, skinned, particles-untex, indexed-displace,
  sprites-HUD, hle-other, S9-shadow, raw-blit-fullscreen, raw-sprites-HUD,
  raw-other) classified from [GSDL-ID] identity stamp + state shape —
  same ClassOf language as the census/replay tooling. Registry + per-class
  hatches + warmup + counters; caller-runs-fallback keeps the raw
  DrawBatch path bit-identical by construction.
- **Six native renderers** as `.inc` files compiled *inside the
  opengl_backend.cpp TU*: world_tristrip, s9_composite (semantic 7-pass
  stencil pipeline), sprite_hud, skinned, particles, fmv_blit — each
  statement-for-statement replicas of the DrawBatch path for census-proven
  state shapes, plus specialized warmed shader programs.
- **The de-facto backend IR is RenderBatch** — but it is NOT backend-
  agnostic: it embeds GL-specific fields (r169_pre staging pointers/ring
  gen), a raw `const char*` registry name, GL texture id conventions, and
  its texture info carries decode *inputs* rather than resolved sources.

### What a backend-agnostic IR SHOULD contain (Vulkan / GXM-Vita / software)

Per draw record:
1. **Identity**: {hle_handler, image_hash, kick_pc, kick_ord, class} — keep
   [GSDL-ID] verbatim (it powers interp matching, census, replay diffing).
2. **Geometry**: prim topology, vertex span into a flat pool (pos.xyz float,
   STQ float, UV 12.4 raw, RGBA8, fog8, draw_kick) — exactly VertexRec;
   plus the resolved XYOFFSET and context id.
3. **Raster state** (pure GS values, no GL enums): FRAME{fbp,fbw,psm,fbmsk},
   ZBUF{zbp,psm,zmsk}, TEST{ate,atst,aref,afail,date,datm,zte,ztst},
   ALPHA{a,b,c,d,fix}, scissor, colclamp, pabe, fba, dthe+dimx, scanmsk,
   fogcol.
4. **Texture READ VIEW** (not a decode!): {tbp,tbw,psm,tw,th,tcc,tfx},
   CLUT{cbp,cpsm,csm,csa,cld}, TEXA, CLAMP, TEX1 filter/mip fields + mip
   chain descriptors — plus the *freshness token* (epoch tuple) instead of
   embedded snapshot bytes. Snapshot bytes move to a side pool referenced
   by hash (the .gsdl model already does this).
5. **Resolved-source annotation** (filled by the resolver, §3): which owner
   served the read {surface-id | decode-key}, the view transform
   (shuffle/idxscale/uvnative/readview prescale/zoff), so backends never
   re-derive aliasing math.
6. **NO GL types anywhere**; staging/upload slots live in per-backend
   sidecars keyed by draw seq.

The `.gsdl` DrawRec v2 is ~90% of this already — promote it from recording
format to the *in-flight* IR, and make GL/Vulkan/GXM consume it.

---

## 7. SCALE / INTERP / PRESENT PATH

- **Scale** ([GE1], gs_scale.h): one integer factor 1–4 (PS2_GS_SCALE) at
  the framebuffer-identity layer. Logical dims everywhere in bookkeeping;
  physical = logical×scale; rasterization scales via viewport only; every
  CPU-visible readback resolves native-first (NEAREST blit into res_fbo).
  Exemptions: 0x230000 S9 accumulator pinned native; [S83-FOGNATIVE]
  depth→16bit alias destinations pinned native; sprite class safe by
  integer-only contract. scale==1 is byte-identical (cmp-proven).
- **Interp** ([GS-INTERP] Phase E, gs_interp.h + backend hooks): default
  OFF; K∈[1..3] intermediates per sim tick. Records draws at the consumer
  tap; matches by (identity stamp, class, prim, vert count, target fbp) +
  occurrence order — never heuristic. Decomposed similarity lerp
  (Procrustes: translate/rotate/uniform-scale) + linear residual;
  endpoint-exact. HUD/raw/S9 exempt (replay as-is at newer tick). Replays
  into **shadow targets** (m_interp_targets) with all deposit/capture/memo
  hooks suppressed; FTT wrapper swaps plain color_tex serves to shadow
  ([S85-REPCEN] routes). Phase fixes: [S85-PHASEFIX] rotate-before-fire
  (pair N−1,N); [FL5-COHERENT] coherent pairing.
- **Present**: Present() (opengl_backend.cpp:23594) → scanout draw
  (DrawScanout :24208 mirror-decode path, or DrawScanoutFromRT :18540 native
  FBO present when gsresident::ScanoutResolve proves the FBO owns the
  display footprint [GB3], alias-hazard veto). present_pace.h pacing +
  quantum decimation grid (interp consults the same grid); VSync job raises
  VBLANK back to the EE (g_vsync_frame + condvar [R131-WAITCHAIN]).
  MaterializePBOFences sweep ([R154-GFX2]) and falling-edge PBO captures
  ride the present boundary.

---

## 8. ESSENTIAL vs ACCIDENTAL

**Essential (the GS semantics + the coherence algebra):**
- Dual-context register file, deferred flush, flush-before-state-change,
  batch-time TME snapshot; transfer-register latch-at-TRXDIR.
- PSM page/block/column addressing, CLUT CSM1, TEXA (both 24 and 16!), alpha
  0–128 domain, Z-format separate tables, dual-format alias remaps.
- Stream-order VRAM coherence: the ONE rule of §3, last-writer-wins per
  page, deposit-vs-upload arbitration, write-exact epochs.
- Target identity by fbp, grow-only, bit-depth bifurcation + coherence,
  drawn-rect-bounded readbacks (color and Z).
- (A−B)×C≫7+D blend mapping incl. REVERSE_SUBTRACT combos; COLCLAMP; DATE.
- PATH1>2>3 arbitration; XGKICK timing gamefix; roundSprite half-pixel
  normalization in FlushPrimitive.
- The typed draw IR + identity stamps + class taxonomy + equivalence
  verifiers; .gsdl record/replay/diff.

**Accidental (host-history artifacts to purge):**
- 560 env gates (282 render): nearly all are shipped-ON escape hatches for
  landed fixes; each is a frozen A/B experiment. Gate-purge = fold default-ON
  behavior in, delete the =0 arm, convert the record's proof to a test.
- Five page trackers where one record type suffices (§4).
- One 25.6K-line TU with 6 `.inc` renderers; 6.1K/4.0K/4.0K-line functions.
- ~17 cooperating serve/rescue paths instead of one resolver (§3).
- Diagnostic probes interleaved with semantics (the FTT wrapper is ~190
  lines of SH5-BIND logging around a tail call; DrawBatch/FTT are saturated
  with bounded-print probes and census counters).
- Duplicated flush/capture logic in gif.cpp vs gs_state.cpp; duplicated
  swizzle math in four places; duplicated bpp tables.
- RenderBatch as pseudo-IR with GL-specific staging fields inside.
- Legacy vestiges: SDL_Renderer-era fields (meshAddress/renderType/
  instances), Vulkan backend stub (1,836 lines, far behind GL), dead
  texture_cache.cpp/h, main_old.cpp, test binaries in-tree.

---

## 9. INVARIANT LIST — proven rules the refactor must encode as tests

Each: one-line rule + the record that proved it (docs/loop_state/* unless
noted; code tags cite the implementing comment).

1. A batch flushed at GIF EOP uses the TME captured at its first vertex
   kick, not a later write — [BATCH-STATE-SNAPSHOT] (gs_state.h; grey-ocean).
2. Pending vertices kicked under old state draw with old state: every state
   setter flushes the pending buffer first — FlushPendingBeforeStateChange
   (gs_state.h).
3. BITBLTBUF/TRXPOS/TRXREG latch at the TRXDIR kick; mid-transfer register
   writes never retarget in-flight pixels — [R199-FIX1] xfer_stage
   (R199_AUDIT_VRAM.md; ocean ghost-strip).
4. A snapshot observes the bytes it claims: upload epochs re-stamped from
   actual write extents at every observation boundary; oracle
   PS2_SNAPVERIFY must show reuses>0, mismatches=0 — [M1A-EPOCH]
   (MIDDLE_NATIVE_IMPL.md).
5. A page whose latest EE-stream writer is newer than a draw is never
   clobbered by that draw's writeback — [VRAM-EPOCH] page skip (render.h;
   F1641/42 crossfade class).
6. GPU→mirror deposits invalidate cached snapshot windows via a separate
   deposit-epoch family, never compared to upload epochs — [SH1-DEPEPOCH]
   (PLAYTEST_R1_IMPL.md; frozen-shadow).
7. Never reuse or cache a snapshot while a deposit encode is in flight;
   revalidate after decision — [M1A-DEPBRACKET] (MIDDLE_NATIVE_IMPL.md).
8. VRAM mirror is single-writer: GFX deposits land in shadow; EE commits at
   stream boundaries; a stream-newer upload wins unconditionally —
   [TEXPOP-FIX] gsovl (CAVERN_TEXPOP_IMPL.md; crystal pop/shimmer).
9. A target that has not drawn since its last sync writes back NOTHING;
   color writebacks clamp to drawn_since_sync — [R104-DRAWN-SINCE-SYNC]
   (water-CLUT flattening).
10. Depth writebacks clamp to z_drawn_since_sync; stale depth rows are never
    re-exported — [R111-TEXCACHE-INVAL] (F1567 logo-crossfade glyph z).
11. A 16-bit FRAME at a BP hosting 32-bit content is a separate target
    image — [R105-ALT16] bifurcation (fog-vs-scene sampling).
12. The 32-bit and 16-bit views of one BP are the same bytes: main-image
    writes mirror into the companion (alt16_stale) before companion use —
    [R113-ALT16-COHERENCE].
13. Targets grow-only; scissor-height flips (224↔225) never destroy
    content; format retag in-frame keeps content — [R102-NO-FORMAT-CLEAR] +
    grow rule (0x230000 intro buffer).
14. Format-class tags survive a grow rebuild (has_32bit_content etc.) so
    bifurcation cannot silently reset — [R113] carry_tags (R105 resurrection).
15. An RT-backed non-indexed read never consumes stale mirror bytes: serve
    the FBO unless a genuinely newer EE upload overlaps the READ's own
    pages; every decline deposits before decoding — [S109-NEVERSTALE]/[CX8]
    (CAVERN_CRYSTRIPES_IMPL.md; striped crystals).
16. Serve-keep arbitrates at read-footprint granularity with precise
    per-page write bits, not target-footprint watermarks — [CX8]
    (gs_state.h vram_write_pages).
17. FTT per-call result flags reset at every entry; depth/idx modes never
    leak into unrelated draws; zoff set on every draw — [R163-ZTEX2] W2/W3.
18. Texture content keys exclude raw addresses (tbp/cbp): decode output is
    base-independent; folding addresses splits identical content —
    [R125-L2FIX] (texcache_key.h; l2 hit=15 vs miss=34396 defect).
19. Same content key ⇒ byte-identical decode; DecodeRect == crop of full
    Decode — [R159-BODY] (test_block_decode_equivalence).
20. Z addressing must use PSMZ* helpers; PSMCT helpers on Z bytes corrupt
    co-paged data — texture_decoder.h contract.
21. Local→local source over GPU pages and TRXDIR=2 hazards are detected
    (census: game never issues TRXDIR=2) — [GB1]/[GB3] tiers.
22. Savestate serialize must see zero stale pages after SyncAllRTsToVRAM;
    empty pending ledger ⇒ residual stale marks are over-approximation and
    reconcile to zero — [GB4] ReconcileStaleAtSave / c_save_stale==0.
23. scale==1 is byte-identical to the unscaled pipeline; logical coords in
    all bookkeeping; CPU-visible reads resolve native-first — [GE1]
    (GS_PHASEE_IMPL.md cmp proof).
24. The S9 accumulator (0x230000) and depth→16bit alias destinations are
    native-res pinned; alias permutations are defined on native pixels —
    [GD9]/[S83-FOGNATIVE]/[S103-UVNATIVE].
25. Interp tick ring rotates BEFORE the pending intermediate fires (pair
    N−1,N on B(N−1) bases) — [S85-PHASEFIX] (CAVERN_PIXEL_IMPL.md; judder +
    doubled streaks).
26. Interp matching is identity-stamped only; raw/S9/HUD draws never lerp —
    [GS-INTERP] plan bindings (GS_PHASEE_IMPL.md).
27. Draw-IR apply is store-for-store identical to register-op replay at
    every observation point (kicks, state writes, flushes, final state) —
    G-C1 verifier (gs_draw_ir.h; test_gs_draw_ir + live tripwire).
28. Every flushed batch carries its producer identity stamp (or zero =
    raw) — [GSDL-ID] (GS_PHASEC_IMPL.md).
29. Class dispatch preserves submission order exactly; fallback executes
    the untouched pre-dispatch call site (bit-safe by construction) —
    [GD1] (gs_class_renderer.h).
30. PATH priority P1>P2>P3 drains texture uploads before draws that
    reference them — gif_arbiter.h header contract.
31. IMAGE re-activation and late HLE feeds re-stamp dest-page epochs —
    [VRAM-EPOCH-REACT] (shoreline-foam staleness).
32. Draws with different dither state (DTHE/DIMX) never merge —
    [WN4-CT16Q] (render.h merge keys).
33. The fmt=5 Z16-lane serve requires the lane draw to still be the LAST
    author of the sampled surface (lane seq == author seq) —
    [S107-FOGLANE-LIVE].
34. Full subtractive blends (abcd=2001/2021) map to REVERSE_SUBTRACT
    exactly, never the standard-alpha approximation — [R178-BLENDSUB]
    (PORTAL_ROOTCAUSE.md; title-screen portal).
35. Masked 16-bit writebacks preserve non-writable 5551 bits from current
    VRAM (sync_keep_mask16), not the FBO readback — [MASKED-SYNC].
36. Alias-refresh page clears only for pages FULLY covered by the refreshed
    band — alias_rect.h page-clear rule [HUB1-C2].
37. Per-game-frame target clears key on composite consumption, not vsync
    ordinal (3 syncV per game frame at 20fps) — R93 (TargetFBO
    needs_frame_clear/ever_composited).
38. XGKICK timing: kick deferred until MFLAG/FMAC drain (GameDB XGKick fix);
    roundSprite mode=1 half-pixel vertex rounding — runtime-environment
    steering + FlushPrimitive normalization (inherited upstream of class
    seam, tol=0 corpus diffs).

---

## 10. TOP-10 FINDINGS

1. **Monolith concentration**: opengl_backend.cpp is 25.6K lines with three
   multi-thousand-line functions (DrawBatch 6.1K, ResolveTextureSeam 4.0K,
   FTTImpl 2.0K/44 returns) plus six `.inc` native renderers compiled into
   the same TU for file-local gate access. This is the refactor's primary
   decomposition target.
2. **Gate debt**: 560 distinct PS2_* env gates (282 in render files), almost
   all default-ON escape hatches for landed fixes. Each is a frozen
   experiment; the purge procedure is mechanical (fold ON-arm, delete OFF-arm,
   port the record's acceptance evidence to a test) and enumerable from
   `/tmp/s113/a3/all_gates.txt`.
3. **Five page trackers, one job**: upload epochs, deposit epochs+bracket,
   residency tracker, overlay state machine, and precise write bits all
   track 512×8KB pages; each was added to patch a race the previous one
   missed. One per-page record answers every current query.
4. **The ONE rule has ~17 implementations**: 11 serve routes + 3 rescue
   paths + 2 decode caches + snapshot-share cache all enforce "a read
   returns stream-ordered bytes in the read's view geometry." A single
   resolver with an owner table is the correct refactor shape; the SH5-BIND
   route names are the ready-made case list.
5. **Designed mirror staleness**: gs_vram is not authoritative for
   GPU-authored pages (texture-only residency, [GB2]); correctness depends
   on read interception. The refactor must make this explicit: mirror =
   lazy materialization behind ReadView/MaterializePages, single EE-side
   writer (gsovl generalized).
6. **Format layer duplicated 4×**: canonical decoder tables + write-side
   swizzle (gs_state.cpp) + encode-side (opengl_backend.cpp) + census
   PagedGeom — only decoder↔LUT↔rect have equivalence proofs. Found real
   inconsistency: **CT16 decode ignores TEXA** (ta0/ta1/aem) while CT24
   applies it; correct only for default-ish TEXA values.
7. **The IR exists and is proven — but only EE-side**: gsdrawir has an
   exactness verifier and the .gsdl v2 DrawRec is ~90% of a
   backend-agnostic draw record. RenderBatch (the de-facto backend IR)
   embeds GL-specific staging state and decode inputs instead of resolved
   sources. Promote DrawRec-shape to the in-flight IR for Vulkan/GXM/soft
   backends.
8. **TargetFBO is a 75-field god-struct** mixing logical GS identity, GL
   objects, PBO capture machinery, retirement heuristics (120-present
   rings), and a dozen per-fix trackers. Split: TargetIdentity /
   SurfaceViews / ReadbackEngine / CoherenceState.
9. **Two flush paths duplicate capture logic**: GIF::FlushBatch and
   GSState::Flush both build RenderBatch + snapshots + epoch hooks and must
   be maintained in lockstep — merge into one flush routine with two entry
   points.
10. **Equivalence infrastructure is already strong**: .gsdl record/replay
    (gs_replay, drawlist_tool diff), PS2_SNAPVERIFY oracle, live IR
    tripwire, swizzle/blend/decode equivalence tests, preservation test
    suite, frozen corpus with byte-identical acceptance (tol=0 cmp). The A5
    differential harness should compose these, not invent new ones.

---

### Cross-references
- A1 (structure/dead code), A2 (config gates), A4 (CPU/VU/HLE/timing),
  A5 (verification assets) — this doc feeds ARCHITECTURE.md §render,
  MIGRATION.md gate-purge list, EQUIVALENCE.md invariant tests.
- Gate inventories preserved at /tmp/s113/a3/{all_gates,render_gates}.txt
  (regenerate with /tmp/s113/a3/census.sh).

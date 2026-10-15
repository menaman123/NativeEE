# A6 — Game-Specificity Audit & Engine/Title Decoupling

Audit stage A6 of the the title PS2 static-recomp runtime at
`$PS2R_OLD_ROOT` (read-only; no builds, no runs, no git writes).
Scope: every hand-written source under `host_app/`, `recompiler_tool/`, `iop_recompiler/`,
`tools/`, `EEAnalyze/` (excl. `third_party/`, `CMakeFiles/`). Date: 2026-09-23.
Method: scripted comment-stripped token census + manual adjudication of every load-bearing
file (scripts preserved under `/tmp/s114/`: `scan_tokens.py`, `code_only_scan.py`,
`classify2.py`, `vu1_quant.py`, `gs_consts.py`, `dispatch_hooks.py`, `emit_appendix.py`).

**The claim under verification** (user): *"some HLE is the title-specific — the VU
HLE and maybe the GS."*

---

## 0. Verdict and summary

**VERDICT: CONFIRMED — and understated.** The VU1 HLE is game-specific in exactly the way
the claim suspects (game-specific *library content* on a generic *dispatch mechanism*), the
GS "maybe" resolves to **yes, mixed** (generic class-renderer mechanism whose taxonomy,
trigger tables and ~220 embedded VRAM constants are this game's), and the *largest*
game-specific mass is one the claim doesn't name: the EE side — 222 hand-written function
bodies totalling 159,811 lines, plus 208+172 game-address hook installs in `main.cpp`.

| Class | Files | Lines | % of lines | Definition |
|---|---:|---:|---:|---|
| **GAME-SPECIFIC** | 234 | 170,312 | 40.3% | only meaningful for SLUS-12345 (keyed on its VU1 image hashes, EE addresses, VRAM layout, draw patterns, STREAM.IRX, argv, scene constants) |
| **MIXED** | 103 | 147,504 | 34.9% | generic mechanism with game tables/constants embedded (each listed with file:line in §7 and Appendix A) |
| **GENERIC-PS2** | 332 | 105,256 | 24.9% | PS2 hardware/OS/SDK semantics valid for any title |
| Total in scope | 669 | 423,072 | 100% | |

Headline numbers behind the verdict:

- **VU1 HLE** (§2): 6,207 lines of program library (GAME) vs **659 lines of mechanism**
  (registry + runtime dispatch, GENERIC/MIXED) — a 9.4:1 content:mechanism ratio.
  **137 registered entries over 120 distinct variant hashes**, all lifted from THIS game's
  microcode images; 15 native programs; ~40 of 62 helper functions encode the game's VU1
  data-memory register conventions.
- **GS** (§3): the class-renderer *framework* is generic, but its **classifier keys on the
  game's VU1-HLE program names** (`gs_class_renderer.h` `ClassifyFields`), every native
  renderer's `Accept()` is a census whitelist of THIS game's draw shapes, and **~220
  code-level (comment-stripped) game-VRAM-layout constants** sit in gs/render code —
  145 in `opengl_backend.cpp` alone (28 distinct base addresses: 0x230000 ×71 raw
  occurrences, 0x280000 ×69, 0x150000 ×43, …).
- **EE side** (§4): 222 `fun_*_body.inc` manual bodies = **159,811 lines** (93.8% of all
  GAME-class lines); `main.cpp` has **208 `install_dual_hook` sites + 172
  `recompiled_functions[...]` mutations** on game addresses; dedicated game-function HLE
  files exist for the loader poll-spin (`FUN_0017d6b8`), the frame-budget predicate
  (`FUN_00191bd0`), boot argv, and the 0x181490 anti-piracy check.
- **IOP/sound** (§5): only `STREAM.IRX` (TT custom) needed decompilation — exactly as the
  runtime-environment ground truth predicted; the SPU2 core is generic but its feed
  protocol is pinned to the STREAM.IRX decompilation at 15+ cited contract points.
- **Assets** (§6): every corpus/equivalence asset is SLUS-12345's: 19 GS captures,
  3,419+126 VU trace cases, ~40 anchor savestates, 22 bug-report bundles.

The decoupling map (§7) shows this is a *favourable* result for the refactor: the
game-specific mass is already concentrated in registrable content (VU1 programs, Accept
tables, hook/binding tables, manual bodies) rather than smeared through the mechanisms —
zero include cycles (A1 §2.2) means the `engine/` vs `games/title/` split in §8 is
mechanical, not architectural.

---

## 1. Method and classification rules

### 1.1 Rules

A file is classified by what its **code** (comments stripped) does, not by what its
comments mention:

- **GENERIC-PS2** — implements PS2 hardware, OS, or SDK semantics that hold for any title.
  Comment references to Crash scenes/sessions do not disqualify (they are history, and the
  refactor plan already moves history to `docs/loop_state/`). Title-*tuned defaults* that
  are behavior switches of generic mechanisms (XGKick deferral pacing, roundSprite mode 1)
  do not disqualify either — they become `TitleProfile.timing` flags (§8.1); the mechanism
  is generic.
- **GAME-SPECIFIC** — only meaningful for SLUS-12345: keyed on its VU1 microprogram image
  hashes, its EE addresses/functions, its texture/VRAM layout, its draw patterns, its
  STREAM.IRX, its save format, or its scene constants.
- **MIXED** — a generic mechanism with game tables/constants embedded in code. Every MIXED
  file's embedded constants are cited (Appendix A reasons; §7 decoupling rows; full cite
  dump preserved at `/tmp/s114/mixed_cites.txt`).

### 1.2 Token census

Two passes were run over all 669 files:

1. Raw token scan (`/tmp/s114/scan_tokens.py` → `token_census.tsv`): 379 files mention at
   least one game token *including comments* — an over-count kept only as an upper bound.
2. **Comment-stripped code-only scan** (`classify2.py` → `final_classification.tsv`),
   matching: EE text-range address literals (`0x100000`–`0x20FFFF`), `FUN_`/`func_00xxxxxx`
   names, the game's VRAM base constants (0x230000, 0x280000, 0x268000, 0x150000,
   0x370000/0x375400, 0x380000/0x380C00, 0x3C0000, 0x3D0000, 0x3F0000, 0x2F0000, 0x2F5400,
   …), game strings (`SLUS`, `20909`, `the title`, `DATASYS`, `STREAM.IRX`, `lti_`,
   `<elf-crc>`), and VU1-HLE program stamps (`vs_family_tristrip` …).

The code-only pass matters: `vu.cpp` (12,475 ln) has **zero** code-level game tokens — its
"the title"/XGKick mentions are comments — so it is GENERIC, where the naive scan
called it game-flagged. Conversely `main.cpp` keeps 596 code-level hits (496 EE address
literals + 96 FUN references) and `opengl_backend.cpp` keeps 159 (145 VRAM constants).

### 1.3 Totals per scope

| Scope | GENERIC | MIXED | GAME |
|---|---|---|---|
| host_app | 266 f / 96,901 ln | 55 f / 111,838 ln | 232 f / 170,079 ln |
| recompiler_tool | 0 | 26 f / 31,148 ln | 2 f / 233 ln |
| iop_recompiler | 2 f / 1,069 ln | 2 f / 672 ln | 0 |
| tools | 54 f / 5,173 ln | 20 f / 3,846 ln | 0 |
| EEAnalyze | 10 f / 2,113 ln | 0 | 0 |

`tests/` was audited by A5 and is out of numeric scope here, but note §6: its corpora and
fixtures are all this game's.

---

## 2. Evidence 1 — VU1 HLE: game-specific library, generic mechanism

Cluster: `host_app/hle/vu1_hle.h` (108 ln), `vu1_hle.cpp` (91), `vu1_hle_runtime.h` (23),
`vu1_hle_runtime.cpp` (437), `vu1_hle_programs.cpp` (6,207).

### 2.1 The mechanism (659 lines) is generic

- `vu1_hle.h` defines the contract: `Input` (16 KB micro + data memory, entry pc,
  TOP/TOPS/ITOP, entry registers, `image_hash`), `Output` (ordered raw GIF kicks +
  `data_writes` + optional full post-run register file `has_regs`), `Entry{name,
  image_hash, variant_hash, start_pc, fn}`, `Register()/Entries()/Lookup()`. Nothing in the
  contract is title-bound.
- `vu1_hle.cpp:19-31` `Lookup()`: exact image hash → variant hash → wildcard. Generic.
- `vu1_hle_runtime.cpp`: enable gates, per-image dispatch, interpreter fallback
  (`FALLBACK` reporting), the raw-packet → typed-op (`PS2_VU1_HLE_DIRECT`) → draw-IR
  (`PS2_GS_DRAWIR`) lowering ladder, arbiter submission. Generic dispatch plumbing; the
  only title-shaped element is the round-1 TPC exit contract (comment, `TPC = 0xA0`).

### 2.2 The game constants inside the mechanism (why vu1_hle.h/.cpp are MIXED)

| Constant | Site | Meaning |
|---|---|---|
| `kVariantBase = 0x1720` | vu1_hle.h:38 | first byte of the per-material variable region **in the the title image family** |
| `0x090E0003` (`ILW.x vi14,3(vi00)`) | vu1_hle.cpp:49 | the family dispatcher's first opcode — the signature `LooksLikeFamilyDispatcher()` matches |
| exit stubs `0x70` / `0x90` | vu1_hle.cpp:84 | the family dispatch chain's E-bit terminators |
| dispatch-chain walk (`data[3].x` node list, JALR targets ×8) | vu1_hle.cpp:71-88 | the game's linked-node dispatch convention |
| `prefix_is_family` | vu1_hle.h:96 / vu1_hle.cpp:44-51 | "prefix equals the known the title base" (learned lazily, but semantically the game family) |

These five items are precisely a per-title *microcode family signature* — §8.2 moves them
into the title's `Vu1LibraryDesc`.

### 2.3 The library (6,207 lines) is game-specific — quantified

From `/tmp/s114/vu1_quant.py` + `gs_and_vu_stats.py`:

- **137 registered entries**; **120 distinct 64-bit variant hashes**; **0 exact-image-hash
  entries** (every entry keys `image_hash=0` + a variant hash — i.e. the whole library is
  keyed on hashes of THIS game's per-material microcode variants).
- **15 native programs**: `vs_family_tristrip` (12 hashes), `vs_textured_tristrip` (15),
  `vs_poscolor_strip` (15), `vs_lit_tristrip` (17), `vs_skinned_lit_strip` (**40**),
  `vs_indexed_displace_bw/cw` (9), `vs_zero_kick_a/b/c` (+`_cont`) (16), `vs_sprite_text`
  (3), `vs_crys_dual` (2), `vs_crys_displace` (8).
- Program bodies are decompilations of the game's microcode: the file banner documents the
  dispatcher/emitter/clip-gate structure at the game's micro PCs (dispatcher pc 0, emitter
  0x1720, clip gate 0xB0, near clipper via QW5.y, sprite family entry 0x1748, skinning
  passes 0xCB0/0x1040/0x0B30/0x1AA0), and the data-memory register conventions (QW1.x =
  prebuilt GIF packet ptr, QW2.x = mesh list, QW3.x = node list, QW4.y = clip-gate
  address).
- **Helper split**: of ~62 file-local helpers, **~21 are generic VU float semantics**
  duplicating `vu.cpp` bit-for-bit (`CF`, `VuDiv`, `Ftoi0`, `ClipNf`, `XformEmit/Clip`,
  `GuardScale`, `Cross*`, `Fmin_raw/Fmax_raw`, `MacLane/ApplyMac*` — ~140 lines, L72-211)
  and **~40 encode game register conventions / micro structure** (`ClipGateB0` ×2,
  `RunEmitterCall`, `KickAt`, `Emit{,Lit,Fam,Crys}ClipFan`, `RunNearClipper`,
  `RunTexCall`, `RunEnvCall`, `RunEnv2Call`, `RunPosColorCall`, `RunLitCall`,
  `RunSkinCb0`, `RunSkin1040`, `RunClipMark0B30`, `RunSkinnedLitCall`, `RunSkinEnv1aa0`,
  `RunIndexPrepass`, `RunIdxCall`, `VsIndexedDisplace`, `VsZeroKick`, `RunCrysEnv1720`,
  `SeedGateRegs`, `ProbeMeshRing`, `UpdateGateRegsAtEnd`, …).
- The `[FP-CONTRACT]` note (vu1_hle_programs.cpp:~146) pins `std::fma` semantics because a
  two-rounding transform flipped a sign-of-zero backface verdict in THIS game's corpus —
  library content is proven only against this title.

**Quantified verdict for VU1**: mechanism 659 ln (of which 5 constants are game family
signature) vs library 6,207 ln (of which ~140 ln are displaced generic float helpers) —
i.e. **~88% of the VU1-HLE cluster's lines are game content, ~12% mechanism**, and the
mechanism needs only a 5-item signature struct extracted to be fully title-neutral.

---

## 3. Evidence 2 — GS: generic idiom machinery, game-derived taxonomy and tuning

### 3.1 The classifier is keyed on THIS game's VU1 programs

`host_app/gs_class_renderer.h` (585 ln, MIXED):

- Framework (generic): `ClassRenderer` interface (`Accept` pure / `Draw` all-or-nothing /
  `WarmUp`), per-class registry + hatch envs (`PS2_GS_CLASSRDR_<TOKEN>`), always-on
  counters, fallback ladder (`fb_norenderer/fb_disabled/fb_declined`).
- Taxonomy + classifier (game-derived): 10 classes
  (`kWorldTristrip, kSkinned, kParticlesUntex, kIndexedDisplace, kSpritesHUD, kHleOther,
  kS9Shadow, kRawBlitFullscreen, kRawSpritesHUD, kRawOther`), and `ClassifyFields()`
  (gs_class_renderer.h:152-186) branches on `strstr(hle_name, "skinned"|"family_tristrip"|
  "textured_tristrip"|"lit_tristrip"|"poscolor"|"indexed_displace"|"sprite_text")` — the
  names of this game's VU1-HLE programs — then on this game's S9 signature
  (`test_date` → S9; `frame_psm == 10` CT16S accumulator → S9; `tpsm == 27` T8H self-read
  → S9), then a ≥256×128 fullscreen-blit threshold. The *decision tree is the game's
  engine idiom census* (comment: "VERBATIM the strings gs_replay's ClassOf emits").

### 3.2 Per-renderer table — trigger predicate × game constants

All six native class renderers are `#include`d at `opengl_backend.cpp`'s tail (TU-tail
pattern, A1 §1.1). "Stamp" = the VU1-HLE program that produces the class's draws.

| Renderer (file, ln) | Class / stamp | Trigger predicate (`Accept`) | Names game constants in code? | Pattern generic-or-game? |
|---|---|---|---|---|
| `world_tristrip_native.inc` (1,300) | `kWorldTristrip` ← `vs_family/textured/lit_tristrip` | `AcceptStrip` (:156): tristrip, iip=0, fst=1, fog=0, untextured, abe=1 ABCD=(0,2,2,1), cc=0 wrap, pabe=0 fba=0 ate=0, DATE=1 datm=0, zte GEQUAL zmsk=1, **frame_psm=CT16S(0x0A), fbmsk=0xff000000** + `World2Accept` textured T8 handoff | :323 `frame_fbp == 0x230000` (target special-case) | Fields are generic GS axes; the pinned tuple is the game's shadow-volume strip census row |
| `s9_composite_native.inc` (2,273) | `kS9Shadow` (semantic, multi-pass FSM) | `AcceptStripShapeS9` (:297) + 7-pass sequence detector A..G (CLEAR 0x78787878 CT16S / SEED fbmsk=0x00FFFFFF / ZCOPY Z32→**0x268000** / INVERT T8H self-read HIGHLIGHT2 / STRIPS DATE=1 GEQUAL COLCLAMP=0 / quantize / Cd−Cs/4 composite) | :781 `fbp==0x230000`; :857-858 `0x230000‖0x280000` | **Game idiom** — TT's blob-shadow engine recipe (SHADOW2_IMPL loop-21); the stencil-winding lowering is generic, the recipe is the game's |
| `sprite_hud_native.inc` (1,679) | `kSpritesHUD` ← `vs_sprite_text` (direct-MSCAL family @ micro 0x1748) | `AcceptCommon/Textured/Untex/Sprite` (:211-261): textured glyph/HUD quads + untextured fades; roundSprite mode-1 half-pixel convention | :407 `fbp==0x230000` | Sprite-quad idiom generic; family + census bounds game's |
| `skinned_native.inc` (1,680) | `kSkinned` ← `vs_skinned_lit_strip` (+`kRawOther` raw-skin sibling) | `AcceptShape` (:261): one uniform shape — tristrip iip=1 fst=0(STQ) fog=0 abe=0 cc=1 pabe=0, fba∈{0,1}, ate∈{0,1} | :517 `fbp==0x230000` | Generic skinned-mesh idiom; tuple census-pinned to this game |
| `particles_native.inc` (1,667) | `kParticlesUntex` ← `vs_poscolor_strip` | `AcceptShape` (:201); census note: the class is actually textured T8+CLUT in every capture | :383 `fbp==0x230000` | Generic particle idiom; class content is game census |
| `fmv_blit_native.inc` (1,633) | `kRawBlitFullscreen`, family F only | `AcceptShape` (:198): fullscreen textured sprite blits, tbw∈{640,512}; 820 of the class's 6,831 draws (six semantic families censused) | :366 `fbp==0x230000`; staging source **0x150000** via backend | Fullscreen-blit idiom generic; family decomposition is game census |

Supporting GAME-classified GS headers: `s9_composite.h` (383) + `s9_shadow.h` (254) — the
pure decode of the seven-pass composite; `rt_neverstale.h` (351, MIXED) — a generic
"RT-backed reads never consume stale mirror bytes" rule that exists for the `vs_crys_dual`
0x280000 environment lane; `foglane_live.h` (MIXED) — generic live-attachment serve for
PSMT8H composites, motivated by the game's fog lane.

### 3.3 Game-address constants in gs/render code — counted

Comment-stripped, code-level:

- `opengl_backend.cpp`: **145 VRAM-constant hits**, 28 distinct base literals; raw
  occurrence counts topped by 0x230000 ×71, 0x280000 ×69, 0x150000 ×43, 0x2F0000 ×31,
  0x380000 ×28, 0x370000 ×15. Load-bearing examples: :941 `(ti.tbp==0x280000‖0x2f0000)`;
  :6605 `frame_fbp==0x70000 && ti.tbp==0x150000`; :6772-6775 CLUT pairing
  `(0x380000,0x380c00) / (0x370000,0x375400)`; :6938 `is_fmv_blit = (ti.tbp==0x150000)`;
  :8559-8589 SPLASH-COMPOSITE-SKIP on `0x280000→0x70000`.
- `gs_state.cpp`: 47 hits — :194 `kTbps[] = {0x370000,0x3c0000,0x3f0000}` (texture-tile
  bases); :3845/:3943 tile-atlas formula `0x380000 + tile*0x10000 (+0x8000)`;
  **:2909 `if (fbp==0x230000) g_cutscene_active=true`** — a game-scene latch inside the GS
  state machine; :2894 ktap on `{0x70000, 0x230000}`.
- `graphics_thread.cpp`: 6 — :161 `tbp==0x370000‖0x3c0000‖0x3f0000`; :1221 loading-logo
  special-case `tbp==0x370000`; :1352/:1411 base-layer tracking on 0x230000.
- Fog lane `FBMSK=0x3fff`: opengl_backend.cpp :2193, :9419, :10850, :11601, :13974
  (depth-shuffle strips into 0x280000/0x2f0000 with FBMSK=0x3fff), :14047.
- `gif.cpp` 4, `gif_arbiter.cpp` 2 (:1349 forced `zbuf[0].zbp=0x150000`),
  `texture_decoder.cpp` 7, `hle_heap.cpp` 1, native `.inc`s 8.

**Total ≈ 220 code-level game-VRAM references across gs/render code** — the game's VRAM
map (display 0x0/0x70000, scene RT 0x230000, fog/feedback 0x280000+0x2F0000, S9 zcopy
0x268000, FMV staging 0x150000, tile atlas 0x380000+i·0x10000 with CLUTs at +0xC00,
0x370000+0x5400, fonts 0x3F0000, ocean 0x3C0000/0x3D0000) is hard-wired where a per-title
`VramLayout` table should be (§7 row G, §8.3).

**GS verdict**: the *patterns* the renderers lower (tristrip strips, skinned STQ meshes,
sprite quads, fullscreen blits, stencil shadow volumes) are generic GS idioms; the
*taxonomy, trigger tuples, family decompositions, sequence FSMs, and address special-cases*
are this game's engine idioms, learned from this game's corpus and keyed on this game's
VU1 programs. MIXED, decoupled by a RendererProfile (§8.3).

---

## 4. Evidence 3 — EE side

| Piece | Class | Evidence |
|---|---|---|
| Recompiled output (`build/recompiled.*`, `vu_aot_programs*.inc` payloads) | GAME (generated) | generated from SLUS_123.45; out of hand-written scope but game-specific by construction |
| **222 `fun_*_body.inc` manual bodies** | **GAME, 159,811 ln** | each is a hand-written re-implementation of one game function (banner "MANUAL re-implementation", A1 §6); largest: fun_1017c8 11,564 ln, fun_00106ce8 6,278, fun_002a4b30 5,546 |
| `anim_pose_hle_bodies.inc` | GAME, 2,027 ln | 9 anim-pose mirrors of game functions (HOOK_RECLASSIFICATION recount) |
| `manual_switch_dispatchers.cpp` | MIXED, 2,303 ln (393 code hits) | generic install/registry/shadow-stack mechanism; funnels the body .incs; 3 jump-table re-impls (FUN_00203358 / FUN_002a13e0 / FUN_00255f30) |
| Hook tables (`main.cpp`) | MIXED, 14,691 ln (596 code hits) | **208 `install_dual_hook` + 172 `recompiled_functions[...]` writes**, all on game addresses (0x181400/0x181438/0x17D298/0x1a0640/0x186900/0x1ed448/0x194040 …); HOOK_RECLASSIFICATION's 747-row census is the companion inventory |
| S8-ARGV (`ps2_argv.h`, main.cpp:527-566) | GAME, 115 ln | writes `argc/argv` = `cdrom0:\SLUS_123.45;1` at the game parser's fixed layout `0x30a980` (FUN_0017d130/FUN_0017ce50 convention); also the boot double-init ROOT FIX (main.cpp:4192) |
| Budget predicate (`budget_math.h` + main.cpp:3226-3608) | GAME, 156 ln | `[S96-BUDGETEMU]` emu-domain arithmetic of game fn `FUN_00191bd0`; dual hook at 0x191bd0 (`[R145-LOCKSTORM]`) |
| Chunk-apply / loader HLE (`hle/loader_sync_hle.h` 224 ln, `hle/loader_oracle.h` 144 ln, main.cpp:6961+ `[CHUNK-APPLY-SCOPED]`) | GAME | HLE of the game loader `FUN_0017d6b8` poll-spin; `kSpinSiteRa = 0x17d724`; polls `FUN_002aa8d0`/`FUN_002ad110` chain; teardown-drain edges `FUN_00264948`/`FUN_001a1bb8` |
| Anti-piracy bypass | GAME (1 hook) | `sif_hle.cpp:710-732` `FUN_00181490` no-op registration at 0x181490 |
| Timing quirks (XGKick, roundSprite) | GENERIC mechanism, title-tuned | `vu.cpp` has **zero** code-level game tokens — XGKICK pacing/deferral (`[R31 XGKICKPACE]`, :762-764, :2447) is an unconditional runtime behavior; roundSprite mode-1 is a geometry convention upstream of the backend (opengl_backend.h:170, sprite_hud_native.inc:24). Both become `TitleProfile.timing` flags |
| `hle_heap.cpp` | MIXED, 3,995 ln | heap/libc HLE mechanism generic; re-enters game allocator fns at :719-768 (`0x1e2db0/0x1e2958/0x1e74a8`); cdvd file-table |
| `memory.cpp` | MIXED, 5,373 ln | memory model generic; hardcodes ELF path (:140 `game_files/example_title.rep/SLUS_123.45`) and the ELF section table (:166-170 `.vutext 0x2D9A10 …`) |
| `sce_hle/` (13 lib TUs + registry) | GENERIC, ~2,600 ln | SDK 3.0.x semantics by API; the per-title address binding lives in **generated** `sce_hle_bindings.inc` (307 ln, GAME — the 291 sce bindings of HOOK_RECLASSIFICATION) |
| `savestate.cpp` + format | MIXED, 2,450 ln (0 code hits — clean) | envelope/section mechanism generic; the *section set* carries title-tied payloads (IOPM = IOP RAM incl. STREAM.IRX state, STREAM recv buffer @EE 0x3B3000, MCHL libmc statics, SDRX libsdr) |
| `recompiler_tool/recompiler.cpp` | MIXED, 25,813 ln (1,373 code hits) | MIPS→C++ engine generic; hardcoded per-address emission special-cases at :903 (0x2d1a50), :935 (0x2044f8), :963 (0x204740), :987 (0x1ffdc0), :1008 (0x194040) …; `inject_fun_1017c8_helpers.{cpp,h}` (233 ln) is a per-game-function TU → GAME |
| `ps2_scheduler_old_fiber.cpp` | MIXED (DEAD, A1 §5.1) | 66 code-level EE-address hits inside superseded code — deleted by migration Phase 1, drops out of every count |

---

## 5. Evidence 3b — IOP and sound (STREAM.IRX vs libsd)

Exactly as the runtime-environment ground truth scoped it: **one custom IOP module**.

- `iop_recompiler/iop_recompiler.{cpp,h}` (1,069 ln) — GENERIC: a rabbitizer-based
  IOP-MIPS→C++ emitter, target-agnostic. `main.cpp` driver + `ExportIopFunctions.java`
  (672 ln) — MIXED: wired to STREAM.IRX inputs.
- `host_app/iop_loader.cpp` (451 ln) — GAME: locates and loads THIS game's
  `DATASYS/STREAM.IRX` from the ISO into IOP RAM.
- `host_app/iop_stubs.cpp` (2,422 ln) — MIXED: thbase/sifman/sysclib-shaped IOP kernel
  stubs (generic Sony API semantics) with stack/callback conventions tuned to the
  STREAM.IRX worker threads (:758, :1278, :1362, :1675).
- `host_app/sif_rpc_handlers.cpp` (6,131 ln) — MIXED: Sony module RPC HLE (pad, mc, cdvd,
  libsd — all stock SDK semantics, GENERIC by API) *plus* the STREAM.IRX/`lti_` custom
  protocol handlers and the EE recv buffer 0x3B3000 (GAME).
- `host_app/sound/` (5 TUs, 2,097 ln) — MIXED: SPU2 voice/ADSR/VOLX/NAX semantics are
  generic hardware; the *feed protocol* is pinned to the recompiled STREAM.IRX at cited
  contract points (sound_core.cpp:98 CD-worker watchdog `FUN_00003588` ENVX read, :105
  ramp stepper `FUN_00010d28` polls, :810 sub-chunk NAX pacing, :1122 savestate resume
  positions; voice.h:88 fade-in engine `FUN_00010b7c` cmd-96).
- `libsd`/`sdrdrv` HLE (`sce_hle/libsdr.cpp` + SDRX savestate section) — GENERIC (stock
  Sony), confirming the claim's implicit libsd-vs-STREAM split.

---

## 6. Evidence 4 — corpus and equivalence assets are all this game's

| Asset | Count/size | Content |
|---|---|---|
| `gs_corpus/` | 19 `.gsdl` captures (+CENSUS_MASTER.md, MANIFEST.md; 16 GB per A5) | boot_menu, fmv_attract, island_slot7, crystal, cavern_* (fog/gas/mipmap/streaks/checker/flicker/wallnoise), s57_walk, save_select, slowspot, load_transition, bad_shadows — all SLUS-12345 scenes |
| VU trace corpora | `vutrace_collect/` 3,293 bins; `vutrace_cases2/` 126; `vutrace_cases/` ~60 | per-case VU1 micro+data+entry captures from this game (the VU1-HLE oracle inputs) |
| Anchor savestates | `crash_tt_save_states/` 28 `.p2s` + `save_states/` 88 entries (~40 anchors per plan README) | this game's scenes (`begin-iceberg`, `beginning_splash`, `bulls_eye_fade_in`, …) |
| Bug bundles | `bug_reports/` 22 dirs (slowspot/crash captures, 2026-09-14..22) | in-game repro bundles |
| Title art fixtures | `ctt_coco_titles/` 21 PNGs | game title-screen references |
| PCSX2 ground truth | `pcsx2_video_groundtruth/` 20 entries; root `*.gs.zst`/`.png` pairs | this game's frames |

Consequence for §8.1: the corpus keys (image hashes, capture names, anchor names) must
live under `games/title/` (or be title-keyed externally) — the *harness* that consumes
them is engine.

---

## 7. Decoupling map — engine mechanism vs game data/plugin

For every MIXED cluster: what stays in `engine/`, what moves to `games/title/`, and
the seam. Ordered by leverage. (Aligned with MIGRATION_PLAN phases and D1/D2/D3; the rows
that D2 already retires are marked.)

| # | MIXED cluster (lines) | Engine mechanism (stays) | Game data/plugin (moves to `games/title/`) | Seam |
|---|---|---|---|---|
| A | VU1 HLE (6,866) | `vu1hle::{Entry,Register,Lookup,Analyze}` registry + runtime dispatch/fallback/DIRECT/DrawIR ladder; the ~140 ln float-semantics helpers get single-sourced with vu.cpp into `engine/vu/vu_float.h` | `vu1_library.cpp`: the 15 programs + 137 entries/120 hashes + ~40 game-convention helpers; the family signature {kVariantBase=0x1720, dispatcher op 0x090E0003, exits 0x70/0x90, TPC 0xA0} | `Vu1ProgramLibrary` interface (§8.2); title registers at boot; engine falls back to interpreter when library declines |
| B | GS classifier + class renderers (10,232 + 585) | ClassRenderer framework, registry, hatches, counters; the six *lowerings* (strip/skinned/sprite/particle/blit/stencil-composite) as parameterized idiom detectors | RendererProfile data: stamp→class map (VU1 program names), per-class Accept tuples (census rows), family decompositions (FMV family F), S9 seven-pass recipe as a declarative sequence, roundSprite mode | `RendererProfile` (§8.3): classifier reads the stamp map; Accept() evaluates a `DrawShapeSpec` table instead of hard-coded field chains |
| C | opengl_backend.cpp VRAM special-cases (145 hits) + gs_state.cpp (47) + graphics_thread.cpp (6) + gif/gif_arbiter (6) | target lifecycle, texture-source resolver, format layer, page trackers (per A3's ONE-rule consolidation) | `VramLayout` table: {display 0x0/0x70000, scene_rt 0x230000, fog 0x280000/0x2F0000, s9_zcopy 0x268000, fmv_stage 0x150000, tile_atlas base 0x380000 stride 0x10000 cluts (+0xC00, 0x370000+0x5400), fonts 0x3F0000, ocean 0x3C0000/0x3D0000, z 0x150000}; the gs_state.cpp:2909 cutscene latch becomes a title scene-hook | every `fbp==CONST`/`tbp==CONST` compare becomes `layout.is(scene_rt, fbp)`; latches move behind title callbacks |
| D | main.cpp hooks (208+172 sites) + manual_switch_dispatchers + 222 bodies + anim mirrors (164,141) | dual-hook/registry mechanism, shadow call stack | **retired by D1/D2** (HOOK_RECLASSIFICATION dispositions all 747 rows; bodies adjudicated out under the v2 proof gate). Whatever survives adjudication lands as SDK bindings (signature-keyed) or title patches in the manifest (§8.6) | SdkBindingManifest (already designed) + `TitlePatch` list |
| E | Loader/budget/argv/piracy HLE (~700) | scheduler, syscall, CDVD mechanisms | `hle/loader_sync.cpp` (FUN_0017d6b8 spin), `hle/budget.cpp` (FUN_00191bd0), `boot/argv.cpp` (0x30a980), patch `piracy_check_0x181490=nop` | title `hle/` folder + manifest patch entries |
| F | sce_hle bindings (307 generated) | 13 sce lib TUs (API semantics) | the generated address table (regenerate per title from its ELF imports — 22 for this game) | already generated; move output under `games/<title>/gen/` |
| G | STREAM.IRX chain: iop_loader (451, GAME) + iop_stubs (2,422) + sif_rpc_handlers (6,131) + sound feed contracts (2,097) | IOP kernel-stub semantics, SPU2 core, Sony RPC modules (pad/mc/cdvd/libsd) | STREAM.IRX recomp output + loader + `lti_` protocol handlers + feed-protocol contract constants (0x3B3000 recv buffer) | `IopModuleSet` in TitleProfile: stock modules by name/version (HLE), custom modules by recomp plugin |
| H | memory.cpp (11 hits) | 32MB RAM model, translate/read/write | ELF path + section table (derive from TitleProfile.elf at load, delete the hardcode) | TitleProfile.elf |
| I | savestate sections | envelope, section registry, anchors protocol | title-tied sections (IOPM/STREAM/MCHL/SDRX payload *semantics*) registered by the title module | section providers register like everything else; format version guarded by the standing phase gate |
| J | recompiler_tool (31,381) | v2 MIPS→C++ engine, VU AOT generator | per-address emission special-cases (recompiler.cpp:903-1008+), inject_fun_1017c8 TU, overrides ini — **retired by D1**; game inputs (ELF, switch tables) become per-title config | recompiler consumes `TitleProfile.recomp` inputs; no hardcoded addresses |
| K | Diagnostics probes (~54 files, host_app root) | ring log, perf profiler, censuses as engine services | scene-keyed probes (splash tiles, foglane_live triggers, s8_* …) — most are **diagnostics → deleted** per D2/A2 gate-purge; survivors become title-profile-scoped probes | config-driven probe registry |

Reading of the map: **rows D, J, K mostly *delete* under decisions already taken** (D1/D2 +
gate purge). What genuinely *moves* is rows A-C + E-I — the VU1 library, the renderer
profile, the VRAM layout, the title HLE folder, the bindings, and the STREAM.IRX chain.
That is the entire content of the `games/title/` tree in §8.1 — nothing else in the
runtime needs to know the title exists.

---

## 8. Design-doc revisions (deltas to apply)

This section extends the refactor plan. Each subsection names its target doc; apply as
marked `[REV 2026-09-23 A6]` blocks there. (This stage is docs-only and writes only A6;
the deltas are specified here verbatim so the design docs can absorb them.)

### 8.1 `engine/` vs `games/<title>/` split — target: ARCHITECTURE.md (module map)

Refines D3's subsystem-owned tree with one more axis: title-neutral vs title-bound.

```
engine/                          # title-neutral: PS2 semantics + host services
  ee/  vu/  gs/  gif/  vif/  dmac/  intc/  timers/  sif/  iop/  spu2/
    …each subsystem = state model + its own hle/ (SDK-signature-keyed, D2)
  gs/classrdr/                   # framework + parameterized idiom lowerings (§8.3)
  vu/hle/                        # registry + runtime dispatch ladder (§2.1)
  recomp/                        # v2 engine + VU AOT (no per-address cases)
  platform/                      # §8.7
  config/  log/  savestate/  verify/
games/
  title/                     # the title (US)
    title_profile.cpp            # the TitleProfile literal (below)
    vu1_library.cpp              # §2.3 content: 15 programs, 137 entries
    renderer_profile.cpp         # §8.3 data: stamps, Accept tuples, S9 recipe, VramLayout
    hle/                         # loader_sync, budget, argv (§7 row E)
    iop/stream_irx/              # recomp output + lti_ protocol handlers
    gen/                         # sce_hle_bindings, recompiled EE/VU output
    mods/manifest.toml           # §8.6 (empty by default)
    corpus/                      # or title-keyed external paths (§6)
docs/bugs/                       # §8.8
```

Build rule: `engine/` must link with **zero** references into `games/`; a title registers
everything through interfaces at boot (same pattern the code already uses for
`classrdr::Register` and `vu1hle::Register`). CI greps `engine/` for the game-token
regexes of §1.2 — the §3.3/§4 constants are the seed denylist and must trend to zero.

**Concrete TitleProfile for SLUS-12345** (every field sourced from this audit or the
runtime-environment ground truth):

```cpp
// games/title/title_profile.cpp
const TitleProfile kExampleTitleUS = {
  .serial       = "SLUS-12345",
  .crc          = 0x<elf-crc>,
  .elf          = { .path="SLUS_123.45", .entry=0x00100008, .gp=0,
                    .load_lo=0x00100000, .load_hi=0x0020A0B8 },
  .sdk          = { .version="3.0.1", .ioprp="IOPRP300", .ee_imports=22 },
  .iop_modules  = { /* 22 stock IOPRP300 modules by name/version → engine HLE */
                    /* disc IRX: sio2man 2.03, padman 2.04, mcman 2.07, mcserv 2.01,
                       libsd 1.05, sdrdrv 1.01, cdvdstm 1.01 → engine HLE */
                    .custom = { {"STREAM.IRX", IopPlugin_StreamIrx} } },
  .timing       = { .xgkick_defer=true, .round_sprite_mode=1 },
  .boot         = { .argv="cdrom0:\\SLUS_123.45;1", .args_base=0x0030a980 },
  .vu1_library  = &title::Vu1Library,        // §8.2
  .renderer     = &title::RendererProfile,   // §8.3 (owns VramLayout)
  .hle          = { title::LoaderSyncHle, title::BudgetHle },
  .patches      = { {.at=0x00181490, .kind=Patch::NopCall,
                     .why="anti-piracy check", .record="HOOK_RECLASSIFICATION"} },
  .save         = { .mc_dir="BASLUS-12345", .stream_recv=0x003B3000 },
  .mods         = "games/title/mods/manifest.toml",
};
```

### 8.2 `Vu1ProgramLibrary` interface — target: ARCHITECTURE.md (three-IR design)

The existing `vu1hle::Entry` registry is already the right mechanism (§2.1). The delta is
ownership + the family signature:

```cpp
struct Vu1FamilySignature {            // was hard-coded in vu1_hle.{h,cpp} (§2.2)
  uint32_t variant_base;               // 0x1720
  uint32_t dispatcher_op0;             // 0x090E0003 (ILW.x vi14,3(vi00))
  uint16_t exit_stubs[2];              // 0x70, 0x90
  uint32_t family_exit_tpc;            // 0xA0
};
struct Vu1ProgramLibrary {
  Vu1FamilySignature family;           // drives Analyze()'s chain walk + prefix check
  std::span<const vu1hle::Entry> entries;   // name, image/variant hash, start_pc, fn
};
```

Engine behavior unchanged: `Analyze()` takes the signature as a parameter; `Lookup()` miss
⇒ cached-interpreter fallback (the existing tier); replay oracle (`vu1_replay --hle`)
stays the per-entry proof gate. A second title with different microcode simply registers a
different library — or none, and runs fully interpreted.

### 8.3 `RendererProfile` interface — target: ARCHITECTURE.md (GS DrawIR section)

```cpp
struct DrawShapeSpec {                 // one census row, data not code
  // every axis Accept() pins today: prim, iip, fst, fog, tex on/off+psm,
  // abe+ABCD+FIX, cc, pabe, fba, ate, DATE/datm, zte/ztst, zmsk,
  // frame psm/fbmsk, scanmsk … each tri-state {require, forbid, any}
};
struct RendererProfile {
  // stamp → class (replaces ClassifyFields' strstr chain, §3.1)
  std::span<const std::pair<const char* /*hle program*/, classrdr::Class>> stamps;
  // per-class Accept tables (replaces hard-coded field chains, §3.2)
  std::span<const std::pair<classrdr::Class, DrawShapeSpec>> accepts;
  // sequence recipes (S9 seven-pass composite; FMV family decomposition)
  std::span<const SequenceRecipe> sequences;
  VramLayout vram;                     // §7 row C table
  // fallback taxonomy for unstamped draws (S9 signature, blit threshold …)
  RawClassifierSpec raw;
};
```

The six lowerings (strip / skinned / sprite / particle / blit / stencil-composite) stay in
`engine/gs/classrdr/` as idiom detectors parameterized by the spec; **every decline still
falls back to the generic DrawBatch path** — the [GD1] contract already guarantees a
second game renders correctly (slowly) with an *empty* profile, which is the right
bring-up default.

**Second-game checklist** (target: MIGRATION_PLAN.md, new appendix):

1. Dump ELF identity → fill `TitleProfile{serial, crc, elf, sdk}`; verify `gp` and import
   count (this game: 0 and 22 — do not assume).
2. Enumerate IOP modules from the boot log; map stock modules to engine HLE; list customs
   (each custom = an `IopPlugin` recomp project; this game had exactly one).
3. Boot with empty VU1 library + empty RendererProfile: interpreter + generic DrawBatch
   must render correctly (the mods-off/profile-off leg is the correctness baseline).
4. Capture VU trace corpus → `vu1_replay --catalog` → lift hottest images into the title's
   library, entry by entry, oracle-gated (the §2.3 workflow, unchanged).
5. Capture GS corpus → census → write stamp map + Accept tuples as data; enable class
   renderers one hatch at a time with the byte-identity harness.
6. Fill VramLayout from FRAME/TEX0 census (not by copying 0x230000 et al.).
7. Adjudicate any needed title HLE (loader spins, budget predicates) through the
   HOOK_RECLASSIFICATION five-class protocol — signature-bound, never raw addresses.
8. Savestate sections: title module registers its providers; anchor set recorded fresh.
9. Equivalence: two-binary harness against a reference emulator capture for the new title
   (EQUIVALENCE_PLAN applies verbatim; the ladder is title-agnostic).

### 8.4 One-class-per-file + CI lint — target: ARCHITECTURE.md (naming/size budgets)

Extends the standing budgets (files ≤ ~1,500 ln, functions ≤ ~300) with a structural rule:

- **Rule**: one *primary* type (class/struct with methods, or one free-function family
  namespace) per `.h`/`.cpp` pair, file named after it (`gs_class_renderer.h` →
  `ClassRenderer` + its registry is today's counterexample: 10 responsibilities in one
  header). Allowed without counting: nested types, private-to-TU helpers in anonymous
  namespaces, plain-data PODs ≤ ~30 lines declared adjacent to their owner.
- **Exemptions** (explicit, listed in the lint config, never implicit): generated files
  (`gen/`, `recompiled.*`, bindings), `cpu_state.h` register PODs, vendored code.
- **CI lint**: `tools/ci/lint_one_class.py` — libclang (preferred; fall back to a
  regex-AST hybrid) walks each non-exempt file, counts top-level record declarations with
  ≥1 method plus top-level namespaces containing ≥2 unrelated free functions; >1 primary
  ⇒ fail with the offending decl list. Registered as a ctest (`ctest -R LintOneClass`)
  next to the existing 407 gates, plus the engine/ game-token greps of §8.1 in the same
  script family (`lint_engine_purity.py`, seeded with the §3.3/§4 constants).
- Applied *during* migration phase moves, not as a big-bang: a file passes the lint when
  its cluster lands in the new tree (MIGRATION_PLAN per-file dispositions gain a "lint
  clean" column).

### 8.5 Modding hooks — target: ARCHITECTURE.md (new section) + MIGRATION_PLAN late phase

Principle: **mods are intentional, declared surfaces — never workarounds.** Anything a mod
needs that isn't a declared surface is a feature request for a new hook, not a patch. And
the correctness story is unconditional: **mods-off is the only configuration the
equivalence harness certifies**; CI asserts the mods-off build is byte-identical with the
mod host compiled in but empty.

Declarative per-title manifest (`games/<title>/mods/manifest.toml`):

```toml
[mod]                      # one table per mod, load-ordered
id = "hd-ui"
version = "1.0"
requires_engine = ">=2.0"

[[mod.texture]]            # asset replacement — keyed by content, never by frame timing
match = { tbp = 0x370000, psm = "PSMT8", hash = "93cb32d7" }
replace = "textures/logo_hd.png"     # decoded via the ONE texture-source resolver

[[mod.vu1_program]]        # render override — keyed by variant hash (the §2.3 key)
variant_hash = "0xd4264095dc680c85"
handler = "plugins/hd_strips.dll:vs_family_tristrip_hd"

[[mod.audio]]              # bank/voice replacement — keyed by STREAM.IRX bank id
bank = 12
replace = "audio/theme.ogg"

[[mod.detour]]             # code hook — SDK-manifest symbol or adjudicated game symbol
symbol = "title::UpdateCamera"   # named export of the title module; NEVER a raw addr
kind = "pre"                          # pre | post | replace
handler = "plugins/freecam.dll:on_update_camera"

[[mod.config]]             # config overlay (the ~32-field Config, whitelisted keys only)
key = "render.scale"
value = 2
```

Engine side: a `ModHost` that (a) loads manifests only when `--mods` is passed, (b)
resolves each entry against the declared hook registries (texture resolver, VU1 library,
sound banks, exported title symbols, config whitelist), (c) hard-fails on any unresolved
entry (no silent partial loads), (d) stamps the window title + logs "MODDED — equivalence
void". Detour symbol surface = the title module's exported functions, which exist only for
addresses that survived HOOK_RECLASSIFICATION adjudication — the mod surface can never
resurrect the raw-address hook pattern D2 just retired.

### 8.6 Per-target platform folders + `Platform` interface — target: ARCHITECTURE.md (backend section)

```
engine/platform/
  platform.h            # the ONE interface; no #ifdef outside this tree
  linux/  macos/  windows/  psvita/  wii/  gamecube/
```

```cpp
struct Platform {                      // capability-honest, like the GPU caps contract
  // threads/fibers: CPU thread, graphics thread, PS2 cooperative threads
  Fiber   (*fiber_create)(void (*fn)(void*), void*, size_t stack);
  void    (*fiber_switch)(Fiber);      // win32 fibers / ucontext / custom asm
  // time: the single TimeDomain source (A4's 9-clock consolidation)
  uint64_t(*now_ns)();
  // vfs: ISO + config + savestates (Vita: app0:/ux0:; consoles: DVD/SD)
  File    (*open)(const char* path, Mode);
  // video surface + input + audio sink (SDL2 on desktop; SceGxm/SceCtrl on Vita)
  Surface (*create_surface)(const VideoConfig&);
  void    (*poll_input)(InputState&);
  AudioSink(*audio_open)(const AudioSpec&);
  const PlatformCaps caps;             // memory budget, gles_level, has_fibers, page_size
};
```

Honest feasibility (per target — this table is the commitment, not marketing):

| Target | Verdict | Grounds |
|---|---|---|
| windows | Supported (original platform) | Win32 fibers native; Vulkan backend currently STALE (A1 §5.4) — GL or rebuilt Vulkan per plan |
| macos | Supported (current dev platform) | GL 4.1-on-Metal today; Apple-silicon FP contract already handled ([FP-CONTRACT], §2.3) |
| linux | Feasible, low risk | SDL2/GL path ports directly; fibers → ucontext/boost.context; CI-able |
| psvita | **Feasible with major effort** — spike-gated | 512 MB RAM holds guest 32 MB + runtime; needs GXM backend (caps contract already anticipates it), ARM32 codegen of recompiled output, custom fibers, perf headroom unproven on 4×444 MHz A9. A4 lists 12 concrete blockers; U2_VITA_FEASIBILITY.md is the standing study. Ship criterion: the §8.3 empty-profile leg at full speed before any native classes |
| wii | **Not feasible for the full game** — stated plainly | 729 MHz single-core PPC750 + 88 MB total RAM cannot host 32 MB guest + recompiled text + VU interpretation + GS lowering at speed; even the mods-off software path is projected >10× over budget. Folder exists only if someone funds a research spike (menu-scene demo at best); no roadmap claim |
| gamecube | **Not feasible** | 24 MB main RAM (+16 MB slow A-RAM) < the guest's 32 MB alone. Physically excluded; folder is a tombstone with this sentence in its README |

Rule: a platform folder may exist only with a written feasibility note (like the above)
and a CI-buildable stub; no aspirational empty dirs.

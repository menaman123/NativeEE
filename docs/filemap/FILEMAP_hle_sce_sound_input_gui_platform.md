# FILE MAP — Shard 7: hle/ + sce_hle/ + sound/ + input/ + gui/ + platform/

Session s116, shard f7 (2026-09-23). Scope: every hand-written file under
`host_app/hle/` (13), `host_app/sce_hle/` (16), `host_app/sound/` (9),
`host_app/input/` (13 incl. `default_bindings.ini`), `host_app/gui/` (10),
`host_app/platform/` (9 + `.DS_Store`). Destinations use the ARCHITECTURE.md §3
tree (`engine/`, `games/<title>/`, `app/`, `tests/`).
Flag dispositions follow A2_CONFIG_DIAG.md; the peak run_play.sh configuration
(PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60 PS2_MTVU=1 PS2_GL_FAST=1
PS2_GL_FAST2=1 PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1) is NORM: everything it turns
on becomes the only code path.

Path shorthand: `games/<title>/` = `games/<title>/`.

---

## host_app/hle/ — 13 files

### host_app/hle/vu1_hle.h (108 lines)
- **Purpose**: The VU1-HLE contract header. Defines what an HLE program IS: a
  native replacement for one VU1 microprogram run (MSCAL entry → E-bit exit)
  that receives the 16 KB data memory + VIF TOP/TOPS/ITOP + entry registers and
  produces the raw GIF PATH1 packet bytes the micro would have XGKICKed.
  Declares the registry (`Entry`, `Register`, `Entries`, `Lookup`), the image
  analysis result (`ImageInfo`: image/prefix/variant hashes, extent, dispatch
  chain), and the `Input`/`Output` structs (incl. the [VU1-HLE-R2] `has_regs`
  full-register-file contract for zero-kick programs).
- **Public surface**: `namespace vu1hle` — `Input`, `Output`, `Fn`, `Entry`,
  `Register()`, `Entries()`, `Lookup()`, `ImageInfo`, `Analyze()`,
  `HashBytes()`, `ShortHash()`, `ReferenceInterpreter()`; constants
  `kMicroSize`, `kDataSize`, `kVariantBase=0x1720`. Included by
  vu1_hle.cpp, vu1_hle_programs.cpp, vu1_hle_runtime.cpp, tests/vu1_replay.
- **Classification**: MIXED (A6 §2.2, row A) — the registry/contract mechanism
  is GENERIC; `kVariantBase=0x1720` and the "the title image family" identity
  notes are game constants.
- **Refactor destination**: split.
  - Contract + registry declarations → `engine/vu/hle/contract.h` (verbatim,
    MIGRATION §1.2 row) with the registry class as `vu1_registry.h/.cpp` =
    `Vu1Registry` (one class per file); `ImageInfo`+`Analyze` decl →
    `image_analyzer.h` = `Vu1ImageAnalyzer`, **parameterized by
    `Vu1FamilySignature`** (ARCH §14.2).
  - The five family constants {kVariantBase 0x1720, dispatcher op 0x090E0003,
    exit stubs 0x70/0x90, family exit TPC 0xA0, prefix_is_family} →
    `games/<title>/vu1_library/vu1_library.cpp` family-signature literal.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - `kVariantBase = 0x1720` (vu1_hle.h:36) — a game constant compiled into the
    engine mechanism. Correct implementation: field of `Vu1FamilySignature`,
    registered by the title (ARCH §14.2).
- **Risks**: every registration site and Analyze caller re-plumbs through the
  signature parameter; the `vu1_replay --hle` corpus sweep (3,419+126 cases,
  FAIL=0) is the proving gate, plus `--catalog` output identity.

### host_app/hle/vu1_hle.cpp (91 lines)
- **Purpose**: Registry + image analysis implementation. Keeps the static
  entry table, implements the exact-image → variant → wildcard `Lookup` order,
  FNV image hashing, and `Analyze()`: extent scan, prefix/variant hashing,
  family-dispatcher detection (`lower0 == 0x090E0003`), and the QW3-rooted
  dispatch-chain walk terminating at the 0x70/0x90 E-bit stubs.
- **Public surface**: definitions of `vu1hle::Register/Entries/Lookup/
  HashBytes/ShortHash/Analyze`. Static state: entry `Table()`, lazily-learned
  `s_family_prefix`.
- **Classification**: MIXED (A6 §2.2) — generic walk mechanism, game opcode/
  stub constants inline.
- **Refactor destination**: split → `engine/vu/hle/vu1_registry.cpp`
  (`Vu1Registry`: Register/Entries/Lookup) + `engine/vu/hle/image_analyzer.cpp`
  (`Vu1ImageAnalyzer::Analyze(micro, data, const Vu1FamilySignature&)`). The
  constants 0x090E0003 / 0x70 / 0x90 and the lazily-learned family prefix move
  into the signature the title registers.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - `LooksLikeFamilyDispatcher` hard-codes `0x090E0003` (vu1_hle.cpp:50) and
    the chain walk hard-codes exit stubs `0x70 || 0x90` (:83) — game family
    constants in engine code. Correct: `Vu1FamilySignature.dispatcher_op0` /
    `.exit_stubs[]`.
  - `s_family_prefix` lazily learned from the first matching image (:45) — an
    implicit global latch; correct: explicit signature-provided prefix hash or
    prefix check within the analyzer instance.
- **Risks**: Lookup precedence (exact > variant > wildcard, first-match within
  tier) must be preserved bit-for-bit — order changes silently re-route images.
  Proof: full vutrace corpus sweep FAIL=0 + live [VU1-HLE-RT] runs/fallbacks
  census identical on the anchor legs.

### host_app/hle/vu1_hle_runtime.h (23 lines)
- **Purpose**: Declares the live dispatch entry: `Enabled()` (the PS2_VU1_HLE
  latch) and `TryExecute(ctx, start_addr, micro_gen)` which vu.cpp's XGKICK/
  MSCAL site calls through a function-pointer hook.
- **Public surface**: `namespace vu1hle_rt` — `Enabled()`, `TryExecute()`.
  Consumed by vu.cpp via the `g_vu1_hle_try_execute` pointer.
- **Classification**: GENERIC-PS2 (mechanism; the gate comment is history).
- **Refactor destination**: `engine/vu/hle/runtime.h` = `Vu1HleRuntime`
  (declaration-only header, one class). `Enabled()` dies with the env gate
  (NORM); the hook pointer becomes a direct registered tier in the vu/tiers
  dispatch ladder.
- **Flags consumed here**: none (declares the gate implemented in the .cpp).
- **Hacks / workarounds**: the static-init function-pointer install pattern
  (see .cpp) — replaced by explicit registration from `app/host` boot.
- **Risks**: minimal; header-only rename.

### host_app/hle/vu1_hle_runtime.cpp (437 lines)
- **Purpose**: The live VU1-HLE dispatch ladder. Per MSCAL: identifies the
  image (generation-keyed hash memo over micro_gen + g_vu1_micro_extgen),
  memoizes `Analyze()` per distinct image, looks up the registry, runs the
  program, applies `data_writes` to VU1 data memory, then submits each XGKICK
  through three tiers in order: DrawIR (`gsdrawir::PacketToDrawIR` →
  `submitHleIR`) → typed ops (`gsdirect::PacketToOps` → `submitHleOps`) → raw
  packet (`submitOwned(PATH1)`). Installs post-run registers (has_regs) or the
  family exit TPC=0xA0. Declines fall back to the interpreter tier with a
  per-(image,pc) census. Also hosts the [GS-DRAWIR-VERIFY] per-kick dual-
  converter equivalence tripwire.
- **Public surface**: `vu1hle_rt::Enabled/DirectEnabled/DrawIREnabled/
  DrawIRVerifyEnabled/TryExecute`; installs `g_vu1_hle_try_execute` at static
  init. Reads `vu1_code_memory`/`vu1_data_memory`, `g_vif`, `g_gifArbiter`,
  `g_vu1_micro_extgen`, `g_cut_xgkick`.
- **Classification**: GENERIC-PS2 mechanism (A6 row A: dispatch/fallback/
  DIRECT/DrawIR ladder). The TPC=0xA0 exit constant is family data.
- **Refactor destination**: split → `engine/vu/hle/runtime.cpp`
  (`Vu1HleRuntime`: hash memo + lookup + run + data-writes + register
  install) + `engine/vu/hle/kick_lowering.h/.cpp` (`KickLowering`: the
  DrawIR→ops→raw three-tier submit, incl. the kSrcId/kSrcName identity stamp)
  + the verify tripwire → `tests/` (the G-C1 oracle already covers it; keep a
  `PS2R_DIAGNOSTICS`-build hook in `core/diag` if wanted). TPC 0xA0 rides
  `Vu1FamilySignature.family_exit_tpc`. Stderr census lines become core/log
  category `vu1hle` counters surfaced by core/triage FallbackDetector
  (TRIAGE_CAPTURE §4).
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_VU1_HLE | ON (unset⇒on) | HLE native route vs cached-tier | shipping-setting | **NORM** — peak config; ladder always on, per-image fallback to cached interpreter is automatic ([GD1]); losing arm (=0 forced cached route) deleted |
  | PS2_VU1_HLE_DIRECT | ON (unset⇒on) | typed-op conversion vs raw-packet submit | shipping-setting | **NORM** — peak config; raw submit survives only as the per-kick decline fallback, not a mode |
  | PS2_GS_DRAWIR | ON ([GC3]) | DrawIR lift vs register-op path | tuning-knob | **NORM** — instructions list it; op path survives only as per-kick decline fallback |
  | PS2_GS_DRAWIR_VERIFY | OFF | live dual-converter equivalence tripwire | tuning-knob | **TEST-ONLY** — the ctest G-C1 oracle owns this proof; live tripwire retires to the harness |
  | PS2_VU1_HLE_HASHEPOCH | ON | generation-keyed hash memo vs hash-every-entry | tuning-knob | **NORM** — memo is the only path; =0 arm deleted |
  | PS2_VU1_HLE_DEBUG | OFF | first-400 miss log | tuning-knob (de facto diagnostic) | **DIAG** — becomes a core/log `vu1hle` category line; the miss census itself feeds core/triage FallbackDetector |
- **Hacks / workarounds**:
  - Static-init hook install `s_install = (g_vu1_hle_try_execute = …)`
    (vu1_hle_runtime.cpp:430-437) — link-order-dependent global wiring.
    Correct: explicit tier registration in `vu/tiers/dispatch` at app/host
    boot.
  - Raw stderr `fprintf` telemetry in the hot dispatch path (:70, :105, :250,
    :270, :420 — rate-limited but unconditional formatting on trigger).
    Correct: core/log categories + core/triage counters.
  - `s_memo.size() > 512 ⇒ clear()` "paranoia bound" (:196) — silent cache
    flush; correct: sized LRU or assert, owned by `Vu1HleRuntime`.
  - `kOpSrcName` carries a `const char*` as uint64 (same-process pointer
    smuggling, :345) — correct: handler-id → name table owned by the DrawIR
    stream (`kName` record pattern the recorder already uses).
  - Comment narrating session evidence ("s84 dv2 evidence", "[GC3] default
    flipped ON at Phase-C close") — history to docs/loop_state; the shipped
    code keeps one-line invariant comments only.
- **Risks**: tier order and per-kick fallback slotting are the G-C3 order
  contract — any reorder breaks byte identity. Proof: vu1_replay --hle corpus
  (FAIL=0), the 880k-kick live equivalence leg, drawlist census identity, and
  frame-hash anchors.

### host_app/hle/vu1_hle_programs.cpp (6,207 lines)
- **Purpose**: THE game program library — native reimplementations of Crash
  the title's VU1 microprograms, bit-exact to the interpreter (proven per
  entry by the vutrace oracle). One anonymous namespace holding shared
  float-semantics helpers + eight family namespaces + ~30 static-initializer
  registration blocks binding 137 entries over 120 variant hashes.
- **Public surface**: none (anonymous namespace); its entire effect is the
  static `vu1hle::Register(...)` calls. Consumers: the registry.
- **Classification**: GAME-SPECIFIC content on the MIXED cluster (A6 §2.3):
  the ~21 float helpers (~140 ln) are GENERIC duplicates of vu.cpp semantics;
  everything else is this title's.
- **Refactor destination**: split (MIGRATION §1.6 row A / ARCH §14.2):
  - Generic float/bit helpers `CF, VuDiv, Ftoi0, ClipNf, XformEmit, XformClip,
    GuardScale, CrossZSign, CrossXYZ, Fmin_raw, Fmax_raw, Rsq, bits2f/f2bits,
    QwPtr, V4, ChopGuard` (lines 72-296, 1380-1393, plus the ~9 duplicated
    ChopGuard definitions) → single-sourced `engine/vu/vu_float.h/.cpp`
    (`VuFloat` free-function group), shared with vu.cpp — the [FP-CONTRACT]
    std::fma note rides along as the documented invariant.
  - Program bodies + registrations → `games/<title>/vu1_library/`, ONE FAMILY
    PER FILE (D4): see per-namespace sub-entries below. Registration
    static-init booleans (`s_reg_*`) collapse into the declarative
    `Vu1LibraryEntry[]` table in `games/<title>/vu1_library/vu1_library.cpp`,
    each entry carrying its `oracle_case_ids`.
- **Namespace sub-entries** (program bodies → `games/<title>/vu1_library/`;
  helpers → engine `vu_float` or stay with their family file):
  - **anon/"fam" family top-level** (lines 297-640): `vs_family_tristrip`
    (:545) — the shared per-material tristrip emitter at 0x1720 (dispatcher →
    clip gate 0xB0 → transform/FTOI/backface → XGKICK, QW1 double-buffer
    rotation); 9+ registrations (bbe25e46/713e4401/98e2b462 + siblings). The
    [CV3C] near-clip "fam" target for image 0x713e44016d5f4840 is handled here
    via ttx::GateRegs threading. → `games/<title>/vu1_library/
    family_tristrip.h/.cpp` (`FamilyTristripProgram`). Helpers `WhyNote(f)`,
    `CrysRingFixEnabled`, `MeshRingBound`, `CrysGateLiftEnabled`,
    `ProbeMeshRing` (:212-296) are library-internal mechanism → a small
    `games/<title>/vu1_library/library_common.h` (decline diagnostics wire to
    core/triage instead of stderr).
  - **ttx** (297-334 + 679-2103): `vs_textured_tristrip` (:1968) + the shared
    `ClipGateB0` near-clipper state (`GateRegs`) + `LatestOfClass`, `Rsq`,
    `Fmin_raw/Fmax_raw`; 13 registrations. → `textured_tristrip.h/.cpp`
    (`TexturedTristripProgram`); `ClipGateB0`+`GateRegs` →
    `clip_gate.h/.cpp` (`ClipGate`) since fam/lit/crys share it; Rsq/Fmin/Fmax
    → engine `vu_float`.
  - **pcs** (2146-2422): `vs_poscolor_strip` (:2275) — position+color strips;
    15 registrations. → `poscolor_strip.h/.cpp` (`PoscolorStripProgram`).
  - **lit** (2478-2945): `vs_lit_tristrip` (:2841) — lit tristrips; 15
    registrations. → `lit_tristrip.h/.cpp` (`LitTristripProgram`).
  - **skn** (3016-3687): `vs_skinned_lit_strip` (:3564) + `LoadBone`/bone
    palette walk + `RunSkin1040`/`RunScalePhase`-class helpers (game VU1
    data-memory conventions); 15 registrations + the [CS9a] displace-prefix
    0x0/0x90 rows (:6198). → `skinned_lit_strip.h/.cpp`
    (`SkinnedLitStripProgram`).
  - **idx** (3788-4011): `vs_indexed_displace_bw/cw` (:3982/:3985) — indexed
    displacement (winding variants), first ChopGuard dup (:3932); 9
    registrations. → `indexed_displace.h/.cpp` (`IndexedDisplaceProgram`).
  - **zk** (4055-4203, 4310-4657, 4703-4995 — THREE fragmented blocks):
    `vs_zero_kick_a/b/c` + `_a_cont/_c_cont` — zero-kick register-setup
    programs on the [VU1-HLE-R2] has_regs contract (full register file
    reproduction); lambda registrations `s_reg_zk_all`/`s_reg_zk_cont`. →
    ONE `zero_kick.h/.cpp` (`ZeroKickProgram`) — kills the three-block
    fragmentation (A4 §3.5).
  - **spr** (5110-5322): `vs_sprite_text` (:5122) — sprite/text quads (start
    pc 0x1748 direct entry); `s_reg_spr_all`. → `sprite_text.h/.cpp`
    (`SpriteTextProgram`).
  - **crys** (3011-3014 fwd + 5386-6205 — TWO blocks): `vs_crys_dual` (:5637),
    `vs_crys_displace` (:5767, multi-entry 0x12a0/0x1400/0x15a0) — crystal/
    fog-scene dual-pass + displacement programs; registration blocks
    `s_reg_crys_all` (:6083), `s_reg_fog_all` (:6131), `s_reg_f20442_all`
    (:6174), `s_reg_cs9a_disp` (:6198) binding the [CS4]/[CS7]/[CS9a] sibling
    images. → ONE `crys_programs.h/.cpp` (`CrysPrograms`) — kills the
    two-block fragmentation; sibling-registration evidence comments compress
    to one line + RecordId each.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_VU1_HLE_WHY | OFF | decline-reason stderr notes (WhyNote/WhyNotef :214/:225) | experiment, delete-both | **DELETE** — decline reasons become core/triage FallbackDetector bundle content, not stderr |
  | PS2_VU1_CRYS_RING | ON | data-driven mesh-ring bound 4096 vs legacy fixed 64 (:253) | diagnostic (A2), de facto proven-fix | **NORM** — data-driven walk + 4096 safety bound is the only path; 64-arm deleted |
  | PS2_VU1_CRYS_GATE | ON | [CS6] pre-scan decline lifted vs restored (:267) | tuning-knob | **NORM** — lift is oracle-proven; losing arm deleted |
- **Hacks / workarounds**:
  - ~9 duplicated `ChopGuard` FE_TOWARDZERO RAII definitions (:3932, :4093,
    :4324, :4708, :5130, :5640, :5774, :5812, :5956) + repeated float-bit
    helpers per namespace — correct: ONE copy in `engine/vu/vu_float.h`.
  - zk in three blocks, crys in two (fragmented by lift session order) —
    correct: one file per family (D4).
  - Static-initializer registration booleans (`s_reg_* = (Register(...),
    true)`) — link-order-dependent side effects; correct: the declarative
    `Vu1ProgramLibrary.entries` table registered by TitleProfile at boot.
  - `WhyNote` getenv latches + stderr in decline paths (:212-244) — probes in
    hot paths; correct: FallbackDetector counters (core/triage).
  - Long session-evidence comment blocks (e.g. :6067-6082, :6100-6130
    narrating /tmp/s75, /tmp/s81 analyses) — history; compress to one-line
    invariant + RecordId per D4 comment rules.
  - `MeshRingBound()` 4096 "generous" bound (:258) — a safety watchdog
    constant; correct: named constant in the family file with the PS2 rule
    stated (ring must terminate via its own IBNE condition; bound exists only
    for pathological data).
- **Risks**: THE crown jewel — any float-semantics drift (fma contraction,
  clamp order, FTOI saturation) flips backface verdicts / vertex bits. Every
  moved family must re-pass its `oracle_case_ids` via vu1_replay (FAIL=0
  per-entry gate, ARCH §14.2); single-sourcing helpers with vu.cpp must be
  proven by the same corpus (the [FP-CONTRACT] two-rounding trap is
  documented). Registration-table conversion proven by `--catalog` diff
  (137 entries / 120 hashes identical).

### host_app/hle/gs_packet.h (572 lines)
- **Purpose**: Self-contained, host_app-dependency-free GIF PATH1 packet
  decoder / builder / differ (`gsp::`). Lowers PACKED / REGLIST / PRE-PRIM /
  A+D forms to ONE canonical `(gs_register, u64)` write sequence so the HLE
  oracle can diff an HLE program's packets against the interpreter's with
  per-register tolerances (XYZ 12.4 units, RGBA steps, ST/Q floats), NOP-state
  normalization, and packet/kick counting. Also the `Builder` used by HLE
  programs to construct packets.
- **Public surface**: `gsp::` — GS register enum (REG_*), `RegName`, `Write`,
  `TagInfo`, `Stream`, `Decode`, `Builder`, `Tolerance`, `Normalize`, `Diff`,
  `DiffResult`. Included by vu1_hle_programs.cpp, tests/test_gs_packet.cpp,
  tests/vu1_replay.cpp.
- **Classification**: GENERIC-PS2 (pure GIF/GS packet semantics).
- **Refactor destination**: `engine/gs/hle/sce/gs_packet.h/.cpp` per MIGRATION
  §1 ("gs_direct_ops.h, gs_packet.h → gs/hle/sce packet builders — verbatim").
  Shape: split the single header into `gs_packet.h/.cpp` (`GsPacket` decode/
  build) + `packet_diff.h/.cpp` (`PacketDiff`: Tolerance/Normalize/Diff — a
  test-side concern also usable by verify/), honoring D4 headers-declarations-
  only (today it is a 572-line header-only TU — function bodies must move to
  a .cpp; the harness links it fine either way).
- **Flags consumed here**: none.
- **Hacks / workarounds**: none of substance — the header-only form (function
  definitions in a .h) violates D4 and is the one structural fix.
- **Risks**: the canonicalization rules (Q latch on ST, NOP-state
  normalization exempting vertex/PRIM regs) are the oracle's comparison
  semantics — behavior must not shift while moving. Proof: tests/test_gs_packet
  + the full vu1_replay corpus rerun.

### host_app/hle/gs_direct_ops.h (151 lines)
- **Purpose**: The [VU1-HLE-DIRECT] typed GS op stream. `gsdirect::Op` is the
  semantic payload of one GIF item pre-split by mode (PACKED-layout /
  REGLIST-A+D / kOpFlush EOP boundary / kOpSrcId+kOpSrcName identity metadata);
  `PacketToOps` mirrors GIF::ParseSpan's tag semantics over one complete
  XGKICK packet, declining losslessly-unrepresentable shapes (IMAGE/DISABLE,
  truncation) so the caller falls back to the raw path.
- **Public surface**: `gsdirect::Op`, `kModeReglist`, `kOpFlush`, `kOpSrcId`,
  `kOpSrcName`, `PacketToOps()`. Consumers: vu1_hle_runtime.cpp, gif.cpp
  (ApplyHleDirectOps), gif_arbiter.h, gs_draw_ir.h, tests.
- **Classification**: GENERIC-PS2 (GIF tag semantics; identity codes are
  mechanism).
- **Refactor destination**: `engine/gs/hle/ir/direct_ops.h/.cpp`
  (`DirectOps`) — it is the middle tier of the same lowering family as DrawIR,
  so it belongs beside `gs/hle/ir/` rather than sce/ (MIGRATION groups it with
  packet builders; the ir/ placement keeps the three-tier ladder in one
  directory — flagged as a minor doc delta, not a TREE-GAP). Move
  PacketToOps' body to the .cpp (D4).
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - `kOpSrcName.lo = uintptr_t(name)` pointer smuggling (documented
    same-process-only) — correct: handler-id keyed name table (the drawlist
    kName record), resolved at recording/consumption.
- **Risks**: conversion must stay ParseSpan-bug-compatible (NLOOP=0 EOP rule,
  REGLIST QW padding, A+D/NOP reglist no-ops). Proof: test_gs_draw_ir's
  op-reference interpreter + corpus byte-identity.

### host_app/hle/gs_draw_ir.h (638 lines)
- **Purpose**: [GS-DRAWIR] Phase-C1 draw-level IR. Lifts the op stream to
  typed draw runs: `Item::kDrawRun` = contiguous vertices under one PRIM with
  pre-decoded KickVertexImpl inputs + resolved attribute masks (RGBA/STQ/UV/
  uv-order/fog), kAttr boundary items, kPrim/kState pass-throughs; the
  EXACTNESS CONTRACT documents store-for-store identity with the op replay.
  Contains `PacketToDrawIR` and the `gsdrawir::verify` dual-converter
  canonicalizer the tripwire and ctest oracle share.
- **Public surface**: `gsdrawir::` — mask/flag enums, `Vert`, `Item`,
  `Stream`, `PacketToDrawIR()`, `verify::VerifyKickEquivalence()`. Consumers:
  vu1_hle_runtime.cpp, gif.cpp (ApplyHleDrawIR), gs_state, gif_arbiter,
  tests/test_gs_draw_ir.
- **Classification**: GENERIC-PS2 (DrawIR record definitions = ARCH §4.6 seed).
- **Refactor destination**: `engine/gs/hle/ir/` (MIGRATION §1: "gs_draw_ir.h →
  gs/hle/ir/"): split → `draw_ir.h/.cpp` (`DrawIr` record types +
  PacketToDrawIR) + `draw_ir_verifier.h/.cpp` (`DrawIrVerifier` — the G-C1
  verifier the tree names) — and bodies out of the header (D4).
- **Flags consumed here**: none (its gates live in vu1_hle_runtime.cpp).
- **Hacks / workarounds**:
  - "bug-compatible with the op path, which assigns the fog BYTE to GSState's
    bool" (kMaskFOGF note) — a deliberate preserved wrongness; keep, but
    record as an accepted-difference candidate row (EQUIVALENCE §6) with the
    PS2 rule (FOG is an 8-bit coefficient) and owning class `GsRegisters`.
- **Risks**: this IS Phase-D's input format; field/mask semantics changes
  invalidate recorded .gsdl reasoning. Proof: test_gs_draw_ir reference-
  interpreter equivalence + corpus sweep + live 880k-kick identity leg.

### host_app/hle/drawlist_record.h (241 lines)
- **Purpose**: The .gsdl capture format — the permanent versioned recording of
  every RenderJob the EE pushes (GS_NATIVE_PLAN Phase A: "the recording IS the
  IR seed and the oracle artifact"). Defines FileHeader/RecordHeader/JobRec/
  VertexRec/DrawRec (v2 adds the HLE draw-identity stamp {handler, image,
  kick pc/ordinal, per-frame instance}, GS context, decoded XYOFFSET, kName
  records) and declares recorder controls.
- **Public surface**: `drawlist::` — kMagic/kVersion, `Kind`, the packed
  record structs, `kDrawRecSizeV1`, `HandlerId()`, `WantFrameImage()`,
  `StartRecording()`, `StopRecording()`; consumed by drawlist_record.cpp,
  drawlist_reader.h, vu1_hle_runtime.cpp ([GSDL-ID]), graphics thread,
  tests/drawlist_tool + gs_replay.
- **Classification**: GENERIC-PS2 (capture format; the identity stamp fields
  reference the HLE registry mechanism, not game data).
- **Refactor destination**: `engine/gs/hle/ir/gsdl_format.h` (record structs =
  the versioned on-disk format, declarations only) + recorder decls into
  `core/diag/drawlist_recorder.h` (`DrawlistRecorder`) per MIGRATION §1
  ("gs_draw_ir.h + drawlist_record.{h,cpp} + drawlist_reader.h → gs/hle/ir/ +
  core/diag/recorder").
- **Flags consumed here**: none (documented gates live in the .cpp).
- **Hacks / workarounds**: none — format history (v1/v2) is a legitimate
  versioning note and stays.
- **Risks**: on-disk layout is pinned by existing 16 GB of corpus captures —
  structs must move byte-identical (static_asserts + reader round-trip test
  prove it).

### host_app/hle/drawlist_record.cpp (403 lines)
- **Purpose**: The .gsdl recorder implementation: env-configured one-shot
  `Open`, the `g_on_push` RenderQueue hook (`OnPush`) serializing jobs/draws/
  frames under a mutex with attribution bucket timers, sampled FNV hashing of
  upload payloads and shared snapshot buffers, kName-once-per-handler
  emission, per-frame PPM dump, and a GUI-callable `StartRecording`.
- **Public surface**: `drawlist::StartRecording/StopRecording/WantFrameImage`;
  static installer wiring `g_on_push`.
- **Classification**: GENERIC-PS2 diagnostics.
- **Refactor destination**: `core/diag/drawlist_recorder.h/.cpp`
  (`DrawlistRecorder`), compiled in `PS2R_DIAGNOSTICS` builds (ARCH §9);
  triggered by Config/diag registry and the triage/manual GUI rather than env.
  The `g_on_push` global hook becomes an explicit RenderQueue observer
  registration.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_DRAWLIST_RECORD | unset | recording target path (master gate) | diagnostic | **DIAG** — core/diag recorder arg (config/CLI), PS2R_DIAGNOSTICS builds |
  | PS2_DRAWLIST_RECORD_VERTS | 1 | include vertex arrays | diagnostic | **DIAG** — recorder option |
  | PS2_DRAWLIST_RECORD_PIXELS | 0 | include upload pixel payloads | diagnostic | **DIAG** — recorder option |
  | PS2_DRAWLIST_RECORD_PPM | 0 | per-frame backbuffer PPM | diagnostic | **DIAG** — recorder option |
  | PS2_DRAWLIST_RECORD_FRAMES | all | frame range a-b | diagnostic | **DIAG** — recorder option |
- **Hacks / workarounds**:
  - Static-initializer env configure + `std::atexit(Shutdown)`
    (drawlist_record.cpp:96-108,166-170) — correct: explicit lifecycle owned
    by the diag registry.
  - `g_on_push` single global function pointer — correct: observer list on
    the RenderQueue (gs/hle/queue).
- **Risks**: recorder must stay producer-side-cheap (the G-A3 overhead
  instrument); proof = test_drawlist_record + a recorded-vs-baseline .gsdl
  byte diff on an anchor scene.

### host_app/hle/drawlist_reader.h (115 lines)
- **Purpose**: Header-only in-memory loader for .gsdl recordings (v1+v2):
  Frame/Draw/Job/Recording structs, name-table resolution, order-preserving
  interleave, [GA2] pixel payload loading for gs_replay re-upload.
- **Public surface**: `drawlist::Draw/Job/Frame/Recording`, `Load()`. Used by
  tests/gs_replay, tests/drawlist_tool, test_drawlist_record, test_gs_replay.
- **Classification**: GENERIC-PS2 (test/tool infrastructure).
- **Refactor destination**: `tests/` support lib (or `verify/`):
  `tests/lib/gsdl_reader.h/.cpp` (`GsdlReader`) — MIGRATION groups it with the
  ir/recorder cluster; only test binaries consume it, so it homes with the
  replay tools. Bodies leave the header (D4).
- **Flags consumed here**: none.
- **Hacks / workarounds**: none.
- **Risks**: none beyond format pinning (shared structs from gsdl_format.h).

### host_app/hle/loader_oracle.h (144 lines)
- **Purpose**: [LDR-ORACLE] event-trace oracle for the loader sync-wait HLE.
  Records the loader pipeline's causal event trace (enq/dlv/drn/rel/cns/yld/
  fcm lines, one mutex-serialized monotone sequence) in baseline and HLE legs;
  tools/loader_oracle_check.py machine-checks the S57 drain-first ordering
  contract offline.
- **Public surface**: `loader_oracle::On/Event/Enqueue/Deliver/ForceComplete/
  Release/Consume/Yield/Drain…` (header-only, env-gated). Called from
  hle_heap.cpp, main.cpp hooks, loader_sync_hle.h.
- **Classification**: MIXED — trace mechanism GENERIC; the event vocabulary is
  the loader-model contract (title HLE model, A6 row E).
- **Refactor destination**: split per MIGRATION §1 ("oracle check remains a
  tests/ tool"): the event-trace emitter → `tests/`/`verify/` harness
  instrumentation compiled into PS2R_DIAGNOSTICS builds as
  `core/diag/loader_trace.h/.cpp` (`LoaderTrace`), armed by the harness legs
  that prove `cdvd/hle/loader` changes; the checker script stays
  `tools/loader_oracle_check.py` → `engine/tools/diagnostics/`.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_LOADER_ORACLE | OFF | event-trace capture on | experiment, delete-both | **TEST-ONLY** — the ordering contract is proven and becomes a standing harness leg; the live env gate deletes (A2 says delete-both; the *capture mechanism* survives only inside the harness) |
  | PS2_LOADER_ORACLE_DIR | "." | trace output dir | tuning-knob | **DELETE** — harness passes an explicit path argument |
- **Hacks / workarounds**:
  - Header-only global FILE*/mutex/seq singletons (:70-101) — correct: a
    LoaderTrace class instance owned by the harness.
  - `(s_seq & 0x3ff)==0 ⇒ fflush` "legs get killed at span end" (:118) —
    harness-lifecycle leakage into runtime code; correct: harness owns
    flush/close.
- **Risks**: the ordering invariants (i)-(iv) are the ONLY proof the loader
  HLE never fakes status — the harness leg must be wired before the loader
  HLE moves (MIGRATION row E, phase 7). Proof: loader_oracle_check.py green on
  baseline+HLE legs of the load-span anchor.

### host_app/hle/loader_sync_hle.h (224 lines)
- **Purpose**: [LDR-HLE] the sanctioned sync-wait HLE of the game's loader
  poll-spin (FUN_0017d6b8 lap-1 spin at ra=0x17d724): when the only blocker is
  provably gate-held/in-flight (five engage-guards g1-g5, thread-local poll
  note + read-only re-verify peeks), answer "no headroom" so the spin exits
  through the game's own path — removing wall-quantized dead spin iterations
  (~1.76 ms/op, 71.6% of load-span wall) without ever faking completion
  status. Telemetry stats + 2 s emit.
- **Public surface**: `loader_sync::kSpinSiteRa/HleOn/PollOutcome/NotePoll/
  ShouldYieldPollSlice/EmitIfDue/EmitFinal/g_stats` (header-only). Called from
  the FUN_00191bd0 dispatch surface (main.cpp/budget path) and the
  FUN_002ad110 hook chain (hle_heap.cpp).
- **Classification**: MIXED (A6 row E): the completion-visibility yield
  MECHANISM is engine (cdvd timing surface); the site constants
  (ra 0x17d724, the FUN_0017d6b8 lap model, $s0 register convention) are title
  model parameters.
- **Refactor destination**: split (MIGRATION §1.6 row E):
  - Mechanism (hold/decision core, blocker-note protocol, peeks contract,
    stats) → `engine/cdvd/hle/loader.h/.cpp` (`LoaderCompletionModel`,
    HARDWARE/TIMING HLE per HOOK §3.6) consuming ee/timing clocks.
  - The FUN_0017d6b8-shaped parameters {spin-site ra 0x17d724, lap-1-only
    engagement, GPR r16 ($s0) lap-progress convention, HELD_MODULE decline
    policy} → `games/<title>/hle/loader_sync.cpp` (`TitleHleModule` row
    registered via TitleProfile.hle).
  - Telemetry → core/log category + core/triage PerfMonitor attribution.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_LOADER_HLE | ON | sync-wait HLE vs recompiled spin | tuning-knob, delete-losing-arm | **NORM** — shipped-ON proven fix (oracle green); recompiled-spin arm remains reachable only as the decline path of the engage guards, not an env mode |
- **Hacks / workarounds**:
  - 224-line header with full implementation + inline globals (`g_stats`,
    `tl_note`) — D4 violation; correct: class split above.
  - Hard-coded guest ABI facts (ra site, `GPR.r[16]` s0 check,
    loader_sync_hle.h:96/:186) in a header the engine includes — correct:
    title-module data consumed through the TitleHleModule interface.
  - stderr telemetry every 2 s beside loader_prof (:150-170) — core/log.
- **Risks**: the g2 lap-1-only rule is a LIVENESS invariant (yielding at the
  lap≥2 site defers consumption a frame — documented regression). Proof:
  loader-oracle harness leg (invariants i-iv), load-span wall-time budget
  check, and the boot/load anchor states.

---

## host_app/sce_hle/ — 16 files

Cluster context (HOOK_RECLASSIFICATION §3.3, MIGRATION §1 row "sce_hle/*"):
the 291 bindings are already signature-bound (sce_lib_finder → SDK band
0x2b6fd8-0x2d6880) — the exact D2 rule. Each lib TU's API semantics move to
the owning subsystem's `hle/`; the generated address table becomes
SdkBindingManifest rows regenerated per title into `games/<title>/gen/`
(build dir), never the source tree.

### host_app/sce_hle/sce_hle.h (44 lines)
- **Purpose**: The registry contract: `Handler = void(*)(CpuContext&)`
  (args in GPR a0-a3, result sign-extended into v0), `Entry`,
  `register_sce_hle_overrides()` (boot-time binder), and the shared
  `log_call()` trace helper.
- **Public surface**: `sce_hle::Handler/Entry/register_sce_hle_overrides/
  log_call`. Included by every lib TU + main.cpp.
- **Classification**: GENERIC-PS2 (mechanism).
- **Refactor destination**: `engine/ee/hle/sdk_binding.h/.cpp`
  (`SdkBindingManifest` — ARCH §4.9): the Handler ABI note survives as the
  binding-row call contract; `log_call` dies into core/log category `scehle`
  (first-call FAKED banner becomes a log-once helper).
- **Flags consumed here**: none.
- **Hacks / workarounds**: "FAKED" banner + per-name unordered_map counters in
  `log_call` (registry.cpp:45-64) — logging state in the dispatch path;
  correct: core/log once-per-site category.
- **Risks**: none beyond ABI documentation fidelity.

### host_app/sce_hle/sce_hle_bindings.inc (307 lines, AUTOGENERATED)
- **Purpose**: The generated `SCE_HLE_BINDING(addr, name, lib)` X-macro table:
  291 unambiguous SDK-band matches (38 ambiguous skipped, 2 out-of-band
  rejected with the false-positive rationale in the header comment).
- **Public surface**: textual include into sce_hle_registry.cpp.
- **Classification**: GAME-SPECIFIC data (this ELF's addresses) produced by a
  GENERIC tool.
- **Refactor destination**: regenerated per title into
  `games/<title>/gen/sce_hle_bindings` (BUILD DIR only — MIGRATION: "bindings
  .inc generated into build dir, never source tree"); consumed as
  SdkBindingManifest rows. The checked-in copy deletes. Generated files are
  exempt from the D4 no-.inc rule, but the build-dir redirect is mandatory.
- **Flags consumed here**: none.
- **Hacks / workarounds**: checked-in generated artifact regenerated into the
  SOURCE tree at configure time (host_app/CMakeLists.txt:69-82) — correct:
  emit to ${CMAKE_BINARY_DIR}/games/<title>/gen/.
- **Risks**: regeneration parity — row-set diff old vs new must be empty; the
  boot BOUND/TRAP census line is the runtime check.

### host_app/sce_hle/generate_bindings.py (139 lines)
- **Purpose**: Generator: reads crash_matches_full.json (sce_lib_finder
  output), emits the binding table; implements the OUT-OF-BAND band filter
  (cluster by address, keep the largest band, default max-gap 64 KB) that
  rejects signature false positives landing in game code — with the two
  documented real incidents (0x287988, 0x1818b0).
- **Public surface**: CLI (--input/--output/--max-gap/--no-band-filter). Run
  by host_app/CMakeLists.txt at configure time.
- **Classification**: GENERIC mechanism (band filter) — inputs/outputs are
  per-title.
- **Refactor destination**: `engine/tools/sce_lib_finder/generate_bindings.py`
  (moves beside its producer; ARCH tree: sce_lib_finder is "the ONLY way an
  HLE row acquires an address"). Output path → `games/<title>/gen/`.
- **Flags consumed here**: none (argparse only).
- **Hacks / workarounds**: none — the band filter IS the documented rule.
- **Risks**: none; row-set parity test as above.

### host_app/sce_hle/sce_hle_registry.cpp (604 lines)
- **Purpose**: Boot-time binder. Forward-declares every implemented handler
  per lib namespace, builds the name→handler map, walks the bindings table:
  implemented ⇒ install via `update_function_impl` (or fresh
  recompiled_functions[] entry), unimplemented ⇒ install a hard-abort trap
  stub (loud, with lib/name/addr). Logs a BOUND/TRAP census.
- **Public surface**: `register_sce_hle_overrides()` (called from main.cpp),
  `log_call`. Mutates `recompiled_functions`, `function_ranges`.
- **Classification**: GENERIC-PS2 mechanism (the D2-conform binder).
- **Refactor destination**: `engine/ee/hle/sdk_binding.cpp`
  (`SdkBindingManifest::Install`) — the manifest rows carry {addr (from the
  per-title gen table), name, lib, handler}, install through the SAME frozen
  `update_function_impl` seam; the trap stub becomes `SdkTrapStub` (one
  class), its abort routed through core/log + core/triage crash path. Per-lib
  forward-decl blocks dissolve: each subsystem's hle/ registers its own rows
  (pad/hle registers scePad*, mc/hle sceMc*, …) into the manifest at boot —
  no central 600-line TU.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - Fresh-insert branch "Recompiler had no entry (Ghidra missed the
    function)" (registry.cpp:552-556) — a legacy-recompiler artifact; under
    D1 v2 discovery the address always exists ⇒ the branch deletes (install
    becomes update-only, missing address = manifest error).
  - Dual registration of `sceSifFreeIopHeap` under two names to paper over a
    manifest mislabel (registry.cpp:289-297 comment) — correct: fix the
    signature adjudication in sce_lib_finder output (one name, one row) and
    record the ambiguity in the manifest generator, not the runtime.
  - `std::function` wrapper per binding + map mutation — correct: plain
    function-pointer rows in a const manifest array.
- **Risks**: install order vs recompiler-injected seams (registry installs
  last and wins — HOOK §3.5 table); the Phase-8 SdkBinding migration must
  keep that precedence. Proof: boot census identical (291 BOUND, same TRAP
  set), full boot anchor.

### host_app/sce_hle/libkernl.cpp (765 lines)
- **Purpose**: libkernel.a surface (~52 handlers): host-fd table for the
  SLUS_123.45 disc-check open/close idiom, SIF RPC init/bind/call/
  CheckStatRpc short-circuits (IOP fully HLE'd), IOP heap alloc/free RPC,
  reboot/sync no-ops, TLB init no-ops, timer-system stubs (SetT2,
  iGetTimerSystemTime…), thread/crt0 supplements, sbrk, write() routed to the
  log, kCopy/kFindAddress kernel helpers.
- **Public surface**: `sce_hle::libkernl::*` handlers (bound by registry).
- **Classification**: GENERIC-PS2 by API (stock SDK semantics); the
  disc-check fast-path comments cite game functions but the code keys on the
  path string tail (SLUS_123.45) — one game constant.
- **Refactor destination**: split → `engine/ee/hle/` as one-concept files:
  `kernl_file_io.h/.cpp` (`KernlFileIo`: sceOpen/sceClose/get_iob/write fd
  table), `kernl_sif_rpc.h/.cpp` (`KernlSifRpc`: sceSif* rows — handler
  BODIES home in `sif/hle` per HOOK §3.3 note), `kernl_boot.h/.cpp`
  (`KernlBoot`: InitTLB*/InitExecPS2/crt0/reboot/sync), `kernl_timer.h/.cpp`
  (`KernlTimer`: SetT2/InitTimer/InitAlarm/... over ee/intc+ee/timing).
  Every row becomes an SdkBinding manifest entry.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - Case-insensitive tail-match on "SLUS_123.45" (libkernl.cpp:335) for the
    disc-check fd — game string in engine code; correct: match against
    `TitleProfile.boot.argv`/elf path data.
  - Fake-fd counter base 1000 (`s_next_fake_fd`) — fine as mechanism, but
    the fd table should live in one class with documented no-overlap vs IOP
    ioman fds.
  - sceSifFreeIopHeap buffer constant 0x3B4480 / STREAM.IRX server comments
    (:613) — the RPC contract points are title data (A6 row G); the handler
    mechanism stays engine, the buffer constants ride the title's
    stream_irx feed-contract table.
- **Risks**: boot-order sensitive (crt0/InitSys run before everything);
  proof = cold-boot anchor + the 22-EE-import call census unchanged.

### host_app/sce_hle/libmc.cpp (887 lines)
- **Purpose**: libmc.a over a host directory (`mc0/`): full persistent
  memory-card model — fd table with real fstreams, GetDir with '*' patterns
  filling real sceMcTblGetDir entries, Mkdir/Delete/Chdir/Format, the
  async-latency shim (commands held BUSY for N sceMcSync polls), and the
  kick-then-poll pending-command state machine the game's save UI expects.
- **Public surface**: `sce_hle::libmc::*` (14 handlers).
- **Classification**: GENERIC-PS2 by API; comments cite the game's bullseye
  state machine but code is generic MC semantics. The `mc0/` host dir name
  and dir layout interact with TitleProfile.save.mc_dir ("BASLUS-12345").
- **Refactor destination**: `engine/mc/hle/` split: `mc_host_store.h/.cpp`
  (`McHostStore`: path mapping, fd table, dir enumeration),
  `mc_command_model.h/.cpp` (`McCommandModel`: pending cmd/busy-poll/Sync
  protocol), `libmc_bindings.cpp` (the 14 SdkBinding rows). Host root comes
  from TitleProfile.save.mc_dir via Config, not a literal.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_MC_PERSIST | ON | host-dir persistent card vs legacy zero-facade (libmc.cpp:96) | tuning-knob, delete-losing-arm | **NORM** — persistence is the shipped behavior (save bug fix); facade arm deleted |
  | PS2_MC_LATENCY | 4 polls | sceMcSync busy-poll count before done (:196) | shipping-setting (A2: keep-as-setting; Config [loads]) | **SETTING** — `Config.loads.mc_latency_polls` default 4 (documented divergence: real MCSERV takes multi-frame) |
- **Hacks / workarounds**:
  - Instant-done-is-wrong latency shim [R200-MC] (:196-210) — a timing model,
    not a hack per se, but the constant must carry its PS2 rule (MCSERV ops
    span frames; poll loops mishandle first-poll completion) in
    `McCommandModel`.
  - Hardcoded `kMcRpcBoundFlag = 0x003c85a4` (:51) — game data address
    (libmc's internal bound flag) written directly; correct: the SDK struct
    offset belongs to the binding row's documented layout (SDK 3.0.1 libmc
    internal), kept as a named constant with the SDK cite in mc/hle.
  - Fixed fake timestamp table `dt[8]` year 2026 (:143-146) — invented
    metadata; correct: derive from host file mtime (documented divergence
    either way).
- **Risks**: save/load round-trip + the save-select UI flow; proof =
  mc save/verify harness leg (save, reboot, load) + GetDir pattern unit test.

### host_app/sce_hle/libpad.cpp (933 lines)
- **Purpose**: libpad.a reader surface implemented directly against the
  PADMAN DMA-buffer layout: scePadPortOpen replicates the RPC success-path
  writes into the libpad status tables (0x3c6e90/0x3c6ea0/0x3c6f80),
  scePadRead/GetDmaStr snapshot the freshest 128-byte double-buffer slot,
  GetState/InfoAct/InfoMode/ReqState/ActAlign/ActDirect/ButtonMask/PressMode
  model the DS2 protocol state machine; plus the [R183-INLAT] consumption
  handshake, stick-trace serve hook, and analog-edge probes.
- **Public surface**: `sce_hle::libpad::*` (12 handlers).
- **Classification**: MIXED — scePad protocol semantics GENERIC (SDK 3.0.1
  layouts); the status-table addresses are THIS ELF's libpad .bss (SDK-band
  data, derived from FUN_002c27c0 disassembly).
- **Refactor destination**: `engine/pad/hle/` split (MIGRATION row
  "pad_input.{cpp,h} | split: scePad protocol → pad/hle/; device polling →
  platform/sdl/input"): `pad_protocol.h/.cpp` (`PadProtocol`: slot tables,
  double-buffer select, state machine), `libpad_bindings.cpp` (SdkBinding
  rows). The .bss table addresses (0x3c6e90/0x3c6ea0/0x3c6f80) are SDK-band
  data symbols — they belong in the binding manifest as data-symbol rows
  (same signature-keyed channel), not hand constants. Probes → core/diag.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_PAD_SCRIPT | unset | [VISREG-TMP] served-bytes probe while a pad script is active (libpad.cpp:374; the script itself lives in pad_input/input_system) | diagnostic | **TEST-ONLY** — the probe deletes ("Remove after investigation" is in-source); the script grammar survives as core/triage InputLog / verify replay |
  | PS2_INLAT_ANALOG | OFF | [S29-A3] analog-serve edge probe arming T5 (:416) | diagnostic | **DIAG** — input-latency census moves to core/diag (PS2R_DIAGNOSTICS), riding the inlat module |
- **Hacks / workarounds**:
  - `g_sceGsSyncV_count` pad-cadence coupling via gs_hle.h include (:15) —
    cross-subsystem global; correct per MIGRATION: frame counter published by
    ee/timing, consumed by pad/hle.
  - Probe stack in the serve path (VISREG-TMP :373-386, sticktrace :393-403,
    INLAT analog :405-440) — hot-path probes; correct: core/diag category
    with one latched test, or deletion (VISREG-TMP says remove).
  - Pad-id formula `port*2+slot+1` (:121) — invented id; document as
    divergence (real PADMAN returns RPC ids) in pad_protocol.
- **Risks**: byte-exact slot layout (the +0x02/+0x03 button order, +0x58
  counter-last publish) is pinned by test_pad_buffer — that test moves to
  tests/ and gates the split; title-screen input is the live anchor.

### host_app/sce_hle/libsdr.cpp (438 lines)
- **Purpose**: libsdr.a (EE-side sound remote): sceSdRemoteInit +
  sceSdRemote command decode (rSd* codes 0x8000-0x81D0) forwarding to the
  host sound core (SetParam/GetParam/SetAddr/…); the virtual SPU2
  block-transfer model for rSdBlockTrans/rSdBlockTransStatus — advancing
  address ramp + double-buffer index so FMV/stream pollers see live progress,
  with [S45-FMVAUD] real-consumption pacing (block completes when the mixer
  actually consumed a ring-half) replacing the wall-clock model when FMV PCM
  flows.
- **Public surface**: `sce_hle::libsdr::sceSdRemoteInit/sceSdRemote`.
- **Classification**: GENERIC-PS2 (stock libsd remote semantics; PS2SDK-cited
  contract). The pacing constants interact with the STREAM.IRX feed protocol
  (title data).
- **Refactor destination**: `engine/sound/hle/` split: `sdr_remote.h/.cpp`
  (`SdrRemote`: command decode → sound core), `sd_block_trans.h/.cpp`
  (`SdBlockTransModel`: the virtual transfer + FMV pacer). SdkBinding rows in
  `libsdr_bindings.cpp`. SDRX savestate section provider registers beside it.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_SDR_BLOCK_MS | 8 ms | wall-clock virtual block duration (libsdr.cpp:113) | tuning-knob, delete-losing-arm | **NORM** — 8 ms constant (fallback path only; real-consumption pacing is primary); env dies |
  | PS2_SND_FMVPACE | ON | real-consumption pacing vs pure wall-clock (:143) | tuning-knob, delete-losing-arm | **NORM** — real pacing is the only path; wall-clock stays only as the documented no-stream fallback branch |
  | PS2_SND_FMVQ_DIV | 2 | ring fraction per completion (:153) | tuning-knob, delete-losing-arm | **NORM** — constant 2 (AutoDMA half-ring cadence), named with its rule |
- **Hacks / workarounds**:
  - `#define kVirtBlockDuration VirtBlockDuration()` (:117) — a macro
    masquerading as a constant; dies with the env read.
  - Wall-clock model itself is a documented invention (addresses are not
    observable on real HW) — keep, with the divergence comment per §11 rules.
- **Risks**: FMV A/V sync (BUG-002 family); proof = FMV anchor legs
  (fmv_attract capture) + audio ring hash in the harness.

### host_app/sce_hle/libcdvd.cpp (402 lines)
- **Purpose**: libcdvd.a surface: init/media-mode/disk-ready/sync/clock
  no-op-with-correct-shape handlers, and the CD streaming family
  (sceCdStInit/Start/Seek/Read/Stop/Stream/Sync) bridged to host-side direct
  ISO readers (cdvd_stream.cpp) since the SIF-RPC pipe doesn't exist.
- **Public surface**: `sce_hle::libcdvd::*` (18 handlers).
- **Classification**: GENERIC-PS2 by API; streaming bridge is engine cdvd.
- **Refactor destination**: `engine/cdvd/hle/libcdvd_bindings.cpp` rows over
  the cdvd engine (stream bridge already migrating there per MIGRATION
  sif_rpc_handlers row); clock via `libscf`-shared RTC helper.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none of substance (honest stubs with SDK-shaped
  returns).
- **Risks**: stream-read pacing interacts with the loader completion model;
  proof = load-transition corpus capture + STREAM.IRX audio legs.

### host_app/sce_hle/libipu.cpp (266 lines)
- **Purpose**: libipu.a shims: sceIpuInit (double-reset leaving g_ipu clean —
  seed-table pushes deliberately skipped), sceIpuSync/StopDMA/RestartDMA over
  the IPU MMIO + DMAC ch3/ch4 model. The game only touches IPU at boot;
  cinematics bypass via sceGsSendImage HLE.
- **Public surface**: `sce_hle::libipu::*` (4 handlers).
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/ipu/hle/libipu_bindings.cpp` rows (the
  MIGRATION mapping "libmpeg+libipu→ipu/hle").
- **Flags consumed here**: none.
- **Hacks / workarounds**: skipped quant-table seeding (documented — decoder
  stub doesn't consume them) — keep with a `// diverges:` note per §11.
- **Risks**: only FMV boot path; proof = fmv_attract anchor.

### host_app/sce_hle/libmpeg.cpp (815 lines)
- **Purpose**: libmpeg.a: sceMpegInit (DMAC ch3/4 stop + chain to
  sceIpuInit), Create/Reset/Delete calling the RECOMPILED MIPS bodies
  directly (FUN_002b70a8 et al. — avoids duplicating ~70 struct inits),
  demux callbacks (AddCallback/AddStrCallback/DemuxPss/DemuxPssRing) bridged
  to mpeg_hle's PSS demuxer, GetPicture/GetPictureRAW8/IsEnd over the host
  decoder, stream-type key/template tables mirroring the ELF's 0x2e7de0
  tables.
- **Public surface**: `sce_hle::libmpeg::*` (17 handlers).
- **Classification**: MIXED — sceMpeg API semantics GENERIC; direct
  `extern void FUN_002bxxxx(CpuContext&, uint32_t)` calls and the 0x2e7de0/
  0x2e7de8 table addresses are this ELF's SDK-band internals.
- **Refactor destination**: `engine/ipu/hle/` split: `mpeg_api.h/.cpp`
  (`MpegApi`: the sceMpeg row handlers over the FMV pipeline),
  `libmpeg_bindings.cpp` (rows). The recompiled-body calls stay legal (they
  are SDK-band functions reached through the frozen dispatch seam) but must
  route via the dispatch table (`update_function_impl`-installed address
  call), not link-time `extern FUN_*` symbols — that extern pattern is the
  A6 §1.2 game-token regex and would fail LintEnginePurity; the address
  comes from the binding manifest row.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - `extern void FUN_002b70a8(...)` link-time couplings (libmpeg.cpp:24-29) —
    correct: call through the dispatch table by manifest address.
  - Stream-key table addresses 0x2e7de0/0x2e7de8 hard-coded (:497-510) —
    SDK-band data symbols → manifest data rows.
  - `kSlotOffsets[6]` mirror of the game's callback-slot layout (:412) —
    SDK struct layout; keep as documented SDK 3.0.1 constant.
- **Risks**: FMV playback end-to-end (video + [S45] audio gate); proof =
  fmv_attract .gsdl + audio ring hash + IsEnd/skip behavior on the logo leg.

### host_app/sce_hle/libgraph.cpp (123 lines)
- **Purpose**: libgraph.a wrappers: sceGsSyncV/sceGsResetGraph forward to
  gs_hle handlers; sceGsSyncVCallback fully HLE'd — maintains the GsGParam
  struct at 0x2e7ff0 (+0x08 fn, +0x0C handler id) and inserts/removes the
  VBlank-start handler directly in g_intc_queues[INTC_VBON] (bypassing the
  AddIntcHandler syscall because the 5th stack arg isn't available at HLE
  entry).
- **Public surface**: `sce_hle::libgraph::*` (3 handlers).
- **Classification**: MIXED — API semantics GENERIC; 0x2e7ff0 GsGParam is
  this ELF's libgraph .bss (SDK-band data).
- **Refactor destination**: `engine/gs/hle/sce/libgraph_bindings.cpp` rows
  (MIGRATION: gs_hle sync/reset surfaces land in gs/hle/sce; the thin
  wrappers here "fold into the same rows" per the gs_hle row). SyncVCallback
  → `gs/hle/sce/gs_callbacks.h/.cpp` (`GsCallbacks`) writing GsGParam via a
  manifest data-symbol row and registering through the ee/intc public API
  (add an arg-carrying registration entry point so the syscall bypass note
  dies).
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - Direct `g_intc_queues` container surgery (:75-118) — reaches into
    another subsystem's internals; correct: ee/intc registration API
    (Add/RemoveHandler with explicit arg).
  - `kGsGParamAddr = 0x002e7ff0` (:60) — manifest data row, not a literal.
- **Risks**: VBlank callback ordering (flag=-1 back-of-queue) drives pad
  cadence; proof = boot anchor + pad-input title-screen leg.

### host_app/sce_hle/libdma.cpp (82 lines)
- **Purpose**: libdma.a: sceDmaGetChan returning a synthesized per-channel
  descriptor block in reserved EE RAM (0x01CFF000, zeroed on first use),
  PutEnv/Reset no-ops, memclr fill helper.
- **Public surface**: `sce_hle::libdma::*` (4 handlers).
- **Classification**: GENERIC-PS2 by API; the reserved-RAM base is an
  invented host convention.
- **Refactor destination**: `engine/dmac/hle/libdma_bindings.cpp` rows.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - `kBaseAddr = 0x01CFF000` invented descriptor arena in guest RAM (:25) —
    squats on top-of-RAM the game could theoretically touch; correct: the
    real libdma keeps its channel structs in its own .bss — resolve the
    actual `sceDmaGetChan` data symbol via the manifest (SDK band) and use
    it, or document the arena as a `// diverges:` with a guard.
- **Risks**: low (callers only read zeros); proof = boot anchor.

### host_app/sce_hle/libdev.cpp (19 lines)
- **Purpose**: libdev.a VIF/VU reset helpers — instant no-ops returning 0
  (reset latency not modeled).
- **Public surface**: `sce_hle::libdev::*` (4 handlers).
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/ee/hle/libdev_bindings.cpp` rows (per
  MIGRATION mapping libdev→ee/hle; they poke FBRST — arguably vif/vu
  surfaces, but the SDK groups them as EE dev-control; keep with a
  `// diverges: reset latency not modeled` note).
- **Flags consumed here**: none. **Hacks**: none. **Risks**: none observed;
  boot anchor covers.

### host_app/sce_hle/netcnfif.cpp (23 lines)
- **Purpose**: sceNetcnfifCheck — "no pending network-config RPC" (returns
  0), silent because hot (143 calls/run).
- **Public surface**: `sce_hle::netcnfif::sceNetcnfifCheck`.
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/sif/hle/netcnfif_bindings.cpp` row
  (MIGRATION: netcnfif→sif/hle). NOTE: the bindings.inc header records
  0x1818b0 as an OUT-OF-BAND rejection — this handler binds only if a
  legitimate in-band row exists; verify at migration that the handler is
  actually reachable (it may be a dead handler kept for the manifest name).
- **Flags consumed here**: none. **Hacks**: none.
- **Risks**: if genuinely unbound today, moving it is a no-op; check the
  boot BOUND census.

### host_app/sce_hle/libscf.cpp (107 lines)
- **Purpose**: libscf.a system-config: tobcd/frombcd, IsT10K (retail=0),
  GetRomName (writes "SCPH-50000"), sceScfGetLocalTimefromRTC from host
  clock, AdjustTime/GetSummerTime/GetTimeZone stubs.
- **Public surface**: `sce_hle::libscf::*` (8 handlers).
- **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/ee/hle/libscf_bindings.cpp` rows
  (MIGRATION: libscf→ee/hle). Host-clock RTC is nondeterministic — the
  deterministic profile (ARCH §8) must pin it (Config-provided fixed epoch),
  same treatment as every wall-clock read.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - `std::localtime` host-time read (:70-80) — nondeterminism leak into the
    deterministic profile; correct: TimeDomain-provided RTC (fixed in the
    blessed profile).
  - "SCPH-50000" plausible ROM name (:53) — invented but harmless; keep with
    `// diverges:`.
- **Risks**: none beyond determinism; proof = two-run hash identity in the
  deterministic profile.

Coverage check: all 16 sce_hle files have entries above — sce_hle.h,
sce_hle_bindings.inc, generate_bindings.py, sce_hle_registry.cpp, libkernl,
libmc, libpad, libsdr, libcdvd, libipu, libmpeg, libgraph, libdma, libdev,
netcnfif, libscf.

---

## host_app/sound/ — 9 files

Cluster context (A6 §5/§7 row G; MIGRATION §1.6): SPU2 voice/ADSR/VOLX/NAX
semantics are GENERIC engine (`engine/sound/`); the feed-protocol contract
points pinned to the recompiled STREAM.IRX (sound_core.cpp:98/:105/:810/:1122,
voice.h:88) are title data → `games/<title>/iop/stream_irx/` feed contract.

### host_app/sound/sound_core.h (171 lines)
- **Purpose**: The host SPU2 core contract: 2 MB byte-addressed SPU RAM, 48
  voices (2×24), libsd register-addressing entry codes (SetParam/GetParam/
  SetSwitch/SetAddr/SetCoreAttr/VoiceTrans*), lock-free readback publication
  (NAX/ENVX/ENDX/VOLX atomics), MixInto (audio-callback-only), the [S45-SNDC]
  full-core savestate codec (Serialize/Restore/Validate), and the [S45-FMVAUD]
  FMV PCM SPSC stream source (push/gate/flush/consumed).
- **Public surface**: `sound::` — Enabled, Init/Shutdown, the libsd surface,
  VoiceTransHost/Status, GUI accessors (master volume/mute/config), stats,
  MixInto, OnSavestateLoad, SerializeState/RestoreState/ValidateState,
  Fmv* family. Consumers: iop_stubs (LIBSD.IRX exports), sce_hle/libsdr,
  mpeg_hle, gui/sound_section, mixer, savestate.
- **Classification**: MIXED (A6 §5): model GENERIC; the eager-hold and
  volx readback semantics exist BECAUSE of STREAM.IRX watchdog/ramp behavior
  (contract points).
- **Refactor destination**: `engine/sound/sound_core.h/.cpp` (`SoundCore`) —
  MIGRATION: "sound/ → sound/ (+ platform audio sink)". The header splits per
  D4: `sound_core.h` (SoundCore class), `spu_ram.h` if desired (kept nested
  is fine — PODs beside owner). Feed-protocol contract points get named
  constants + a `games/<title>/iop/stream_irx/feed_contract.h` documenting
  which STREAM.IRX functions rely on them (the code itself stays engine —
  the CONTRACT documentation is the title artifact).
- **Flags consumed here**: none (declares gates; reads in .cpp).
- **Hacks / workarounds**: none in the header; contract comments citing
  FUN_00003588/FUN_00010d28 stay as one-line invariants with RecordIds (they
  are the durable "why").
- **Risks**: savestate SNDC codec layout is pinned (VoiceState 119 bytes);
  proof = test_sound_* suites + SNDC round-trip hash (PS2_SND_STATEHASH
  quotable) + save/load anchor.

### host_app/sound/sound_core.cpp (1,168 lines)
- **Purpose**: SoundCore implementation: command ring (MPSC) applied by the
  audio callback; eager KON NAX/ENVX publish with the [S59 FIX-3] hold
  counter (fixes the STREAM.IRX CD-worker watchdog reading ENVX==0 on a
  just-keyed voice — the save-select soft-lock); [S47 FIX-2] VOLX readbacks
  for the music ramp stepper; SdInit's silence block at 0x5000; the
  [S41-SND] self-test tone; MixInto (voices + FMV PCM after-mix scaled by
  BVOL, turbo duck, master/mute); SNDC serialize/restore under the device
  lock; FMV FIFO (16 MB, sized to the largest PSS audio body).
- **Public surface**: definitions of the sound_core.h API.
- **Classification**: MIXED (as header).
- **Refactor destination**: `engine/sound/sound_core.cpp` + split the FMV
  stream source into `engine/sound/fmv_stream.h/.cpp` (`FmvStream`: FIFO/
  gate/consumed counters) — one class per file; the SNDC codec into
  `engine/sound/sound_state_codec.h/.cpp` (`SoundStateCodec`) registered as a
  savestate SectionSpec. PERF window stats → core/triage PerfMonitor
  channels.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_PERF | OFF | [SND-PERF] window stats on stderr (sound_core.cpp:174) | shipping-setting | **SETTING** — the perf surface becomes core/triage PerfMonitor (always compiled, TRIAGE_CAPTURE §4.2) + `Config.diag.perf_hud`; the local latch mirror deletes (one of PS2_PERF's 21 latch sites — A2 §2.3 mirror hazard) |
  | PS2_SOUND | ON | whole audio core + device (:187) | shipping-setting | **SETTING** — `Config.audio.enabled=true` ([audio] enabled, A2 §6 schema) |
  | PS2_SND_SELFTEST | OFF | audibility self-test tone (:237) | test-hook | **TEST-ONLY** — moves to tests/ (unit leg keys the tone through the public API) |
  | PS2_SND_KONLOG | OFF | KON parameter-table probe (:337/:473) | diagnostic | **DIAG** — core/diag category `snd.kon` (PS2R_DIAGNOSTICS) |
  | PS2_SND_EAGER_HOLD | ON | eager-publish hold vs pre-fix clobber (:353) | proven-fix-hatch-ON | **NORM** — hold protocol is the only path; clobber arm deleted (BUG-005 family root fix) |
- **Hacks / workarounds**:
  - Local PS2_PERF latch mirror "kept local so the core stays free of
    host_app includes" (:169-177) — gate mirror (A2 §2.3); correct: Config/
    PerfMonitor injection at construction.
  - 16 MB FMV FIFO sized by an offline scan of all 22 disc files ("SPYRO.PSS
    = 82.3 s") (:38-45) — a title-derived capacity constant in engine code;
    correct: `TitleProfile`-provided capacity hint (title data), engine
    default smaller with drop-oldest already handled.
  - Silence-block convention at 0x5000 (SdInit) — that IS libsd's documented
    system-area behavior; keep (rule stated).
  - stderr fprintf telemetry — core/log.
- **Risks**: lock-free protocols (eager-hold seq_cst ordering, ring
  drop-oldest CAS) are concurrency-proof-carrying code — move verbatim,
  re-run test_sound_ring/test_sound_voice/core suites + the save-select
  soft-lock repro leg; SNDC layout byte-pinned.

### host_app/sound/voice.h (199 lines)
- **Purpose**: One SPU2 voice: KON/KOFF, pitch-stepped ADPCM playback from
  SPU RAM, loop flags (START latches LSAX, END jumps NAX→LSAX + ENDX latch,
  REPEAT keeps envelope), linear-interp resampling, phase-1 linear ADSR
  approximation, [S47 FIX-2] hardware volume-sweep mode (bit15) that the
  STREAM.IRX fade-in engine requires, VOLX readbacks, and the packed
  `VoiceState` (119 B) savestate mirror. PURE: no globals/locks/IO.
- **Public surface**: `sound::Voice`, `VoiceState`, `EnvPhase`,
  `kSpuRamSize`. Consumer: sound_core.
- **Classification**: MIXED (A6 cites voice.h:88 fade-in contract) — model
  GENERIC, sweep-mode necessity documented against STREAM.IRX cmd-96.
- **Refactor destination**: `engine/sound/voice.h/.cpp` (`Voice`) verbatim;
  `VoiceState` stays adjacent (POD-beside-owner allowance). ADSR curve
  exactness is a recorded phase-3 item (documented approximation —
  divergence comments per §11).
- **Flags consumed here**: none.
- **Hacks / workarounds**: linear-vs-gaussian interp + linear ADSR are
  DOCUMENTED approximations (keep `// diverges:` notes); no gates.
- **Risks**: none beyond codec pinning; unit vectors in tests pin decode.

### host_app/sound/voice.cpp (245 lines)
- **Purpose**: Voice implementation: block fetch/decode via VagDecoder, loop
  flag consumption, envelope stepping, sweep convergence, sample synthesis.
- **Public surface**: Voice methods. **Classification**: MIXED (as header).
- **Refactor destination**: `engine/sound/voice.cpp` verbatim.
- **Flags**: none. **Hacks**: none. **Risks**: as voice.h.

### host_app/sound/mixer.h (29 lines) / mixer.cpp (85 lines)
- **Purpose**: SDL2 audio output: one 48 kHz stereo float device (512-sample
  buffer), callback = sole ring consumer calling `MixInto`;
  MixerStart/Stop/Running; also implements MixerLock/UnlockAudioImpl for the
  SNDC codec.
- **Public surface**: `sound::MixerStart/MixerStop/MixerRunning`.
- **Classification**: GENERIC host glue (SDL).
- **Refactor destination**: split across the platform seam: the SDL device
  open/callback → `engine/platform/sdl/audio_sink.h/.cpp` (`SdlAudioSink`
  implementing Platform.audio_open, ARCH §16); the engine keeps
  `engine/sound/mixer.h/.cpp` (`Mixer`) as the sink-agnostic pump wiring
  SoundCore::MixInto to whatever AudioSink app/host registered.
- **Flags consumed here**: none (PS2_SOUND honored upstream).
- **Hacks / workarounds**: none.
- **Risks**: device buffer size participates in A/V latency (40-80 ms
  target); keep 512 as the Config-defaulted value. Proof: audio ring hash +
  FMV sync leg.

### host_app/sound/ring.h (88 lines)
- **Purpose**: Bounded MPSC command ring: consumer lock-free bounded-CAS Pop,
  producer-only mutex, drop-oldest overflow with counter. Contract pinned by
  tests/test_sound_ring.cpp.
- **Public surface**: `sound::CmdRing<T,N>`. **Classification**: GENERIC.
- **Refactor destination**: `engine/core/threads/cmd_ring.h` (template —
  `.inl` allowance applies; it is a general primitive other subsystems can
  use) — TREE-NOTE: core/threads is the tree's queue home; keeping it
  sound-local (`engine/sound/ring.h`) is also defensible; prefer
  core/threads for reuse. Flagged as a soft placement choice, not a
  TREE-GAP.
- **Flags**: none. **Hacks**: none.
- **Risks**: concurrency contract — test_sound_ring must move+pass first.

### host_app/sound/vag_decoder.h (56) / vag_decoder.cpp (56)
- **Purpose**: PURE SPU2 ADPCM ("VAG") block decoder: 16-byte blocks,
  shift/filter nibbles, two-tap IIR predictor with cross-block history,
  flag semantics (END/REPEAT/START), out-of-range clamping pinned by unit
  tests. Clean-room from public format docs.
- **Public surface**: `sound::VagHistory/DecodeVagBlock/kVagBlock*`,
  `kFlag*`. **Classification**: GENERIC-PS2.
- **Refactor destination**: `engine/sound/vag_decoder.h/.cpp` (`VagDecoder`)
  verbatim.
- **Flags**: none. **Hacks**: none. **Risks**: none (unit-pinned).

---

## host_app/input/ — 13 files (12 + default_bindings.ini)

Cluster context: MIGRATION §1 maps "input/ (mapper, backends, system) →
platform/sdl/input/"; the two test TUs → tests/ (verbatim, "live, unlike the
§1.1 dead root set"). The L1/L2/L3 layering (backends → mapper → pad wire) is
already the target shape; only homes and env gates change.

### host_app/input/input_backend.h (48 lines)
- **Purpose**: L1 contract: `NormalizedInput` (device-agnostic sticks [-1,1],
  ACTIVE-HIGH `LogicalButton` bitmask in DS2 wire order, mouse deltas) and
  the `InputBackend` interface (Poll/IsActive/Name; MAIN thread only).
- **Public surface**: `input::NormalizedInput`, `LogicalButton`,
  `InputBackend`. **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/sdl/input/input_backend.h`
  (interface header; declarations only — already conformant). The
  LogicalButton wire-order enum is pad-subsystem vocabulary shared with
  pad/hle — keep it here; pad/hle includes it through the platform-neutral
  `InputState` in Platform (ARCH §16 `poll_input(InputState&)`), i.e.
  NormalizedInput generalizes into the Platform `InputState`.
- **Flags**: none. **Hacks**: none. **Risks**: none.

### host_app/input/input_mapper.h (95) / input_mapper.cpp (318)
- **Purpose**: L2: bindings config (INI with baked-in default; whole-file
  rejection on any malformed line) and NormalizedInput → `PS2PadState`
  translation: key/mouse buttons, WASD digital stick with opposite-key
  cancellation, mouse→stick accumulate/decay (time-based), PCSX2-style
  gamepad sensitivity, `StickToByte` scale (pinned so -1→0x00, 0→0x80,
  +1→0xFF), key-name resolution (SDL names + aliases).
- **Public surface**: `input::PS2PadState`, `Bindings`, `InputMapper`
  (LoadConfig/LoadConfigText/Map/ButtonsFromKeys/KeyStickFromKeys/
  LogicalForScancode), `StickToByte`, `ScancodeFromName`, `ParseMouseName`.
  Consumers: input_system, kbm backend, tests.
- **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/sdl/input/input_mapper.h/.cpp`
  (`InputMapper`) verbatim; PS2PadState stays the pad-facing POD (bridges to
  pad/hle). Bindings file path/config integrate with core/config (the INI
  remains a user file; `Config.input.*` holds the defaults).
- **Flags consumed here**: none (SDL scancode resolution only).
- **Hacks / workarounds**: none — deliberate design notes (StickToByte scale
  rationale) stay.
- **Risks**: mapping is user-visible feel; test_input_mapper T1-T5 pin it.

### host_app/input/input_backend_kbm.h (34) / input_backend_kbm.cpp (91)
- **Purpose**: L1 keyboard+mouse backend: polls SDL key state through the
  mapper's tables, relative mouse capture, selftest key-injection overlay
  hook (per-frame scancode overrides).
- **Public surface**: `input::KeyboardMouseBackend` (+ HandleEvent,
  injection seam). **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/sdl/input/kbm_backend.h/.cpp`
  (`KeyboardMouseBackend`) verbatim.
- **Flags**: none directly (selftest spec parsed in input_system).
- **Hacks / workarounds**: `PS2_INPUT_NUM_SCANCODES 512` ABI-pin macro with
  static_assert guard — acceptable, documented.
- **Risks**: none.

### host_app/input/input_backend_gamepad.h (103) / input_backend_gamepad.cpp (423)
- **Purpose**: L1 gamepad backend: opens EVERY connected controller,
  gamecontrollerdb.txt extra mappings + generic fallback, Auto/None/specific
  selection with reconnect-by-GUID + hotplug, thread-safe device snapshot
  for the GUI, and the PURE `GamepadConvert` (SDL layout == DS2 layout;
  PCSX2 circular-deadzone-snap + per-axis sensitivity 1.33; triggers past
  25%).
- **Public surface**: `input::GamepadBackend` statics (Snapshot/SetSelection/
  SetPreferredGuid/SetDeadzone/SetSensitivity/…), `GamepadRaw`,
  `GamepadConvert`. Consumers: input_system, gui, tests.
- **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/sdl/input/gamepad_backend.h/.cpp`
  (`GamepadBackend`); `GamepadConvert` + `GamepadRaw` →
  `gamepad_convert.h/.cpp` (pure, unit-tested with no pad — keeps the test
  linking without SDL devices).
- **Flags**: none.
- **Hacks / workarounds**: static-mutable class-level selection atoms (GUI
  coupling) — acceptable as the documented thread contract; formalize as one
  `GamepadBackend` instance owned by the platform layer, GUI reads through
  it.
- **Risks**: hotplug/selection state machine; test tables + manual pad check.

### host_app/input/input_system.cpp (393) / input_system.h (55)
- **Purpose**: The main-thread facade: Init (env gates + device config INI
  load), HandleEvent, `Frame()` = Poll backends → merge (gamepad priority,
  kbm merge) → selftest overlay → Map → publish; GUI surface (UI capture
  suppression, kbm toggle, SaveDeviceConfig persisting pad GUID/deadzone/
  sensitivity), `DebugLastPadState` for the GUI tester; PS2_INPUT_SELFTEST
  spec parser (frame-windowed scripted key presses).
- **Public surface**: `input::Init/Shutdown/HandleEvent/Frame/Enabled/
  SetUICapture/UICaptured/SetKbmEnabled/KbmEnabled/SaveDeviceConfig/
  DebugLastPadState`. Consumers: main.cpp loop, gui.cpp, pad_input.
- **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/sdl/input/input_system.h/.cpp`
  (`InputSystem`) — becomes the implementation behind
  `Platform.poll_input(InputState&)` (ARCH §16); app/host constructs it and
  registers it. input_device.ini keys fold into the Config schema
  (`Config.input.pad`, `.kbm`, `.deadzone`, `.sensitivity`) with the INI as
  the user file.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_INPUT | ON | disable L1/L2 entirely (neutral pad; script overlay still runs) (input_system.cpp:214) | tuning-knob, delete-losing-arm | **NORM** — input always on; the "neutral pad" configuration survives only as the harness's no-input default (headless legs), selected by the harness, not env |
  | PS2_INPUT_SELFTEST | OFF | scripted key injection windows (:127) | test-hook | **TEST-ONLY** — the harness/acceptance legs own it; grammar folds into the InputLog/replay toolchain (core/triage + verify) |
- **Hacks / workarounds**:
  - Function-local `State` singleton + atomics for GUI coupling — fine;
    formalizes as the InputSystem instance.
  - Selftest default "300-500:Y" boot-window literal (:130) — a
    title-boot-specific test constant; moves to the title's verify scripts.
- **Risks**: input feel + title-screen acceptance; test_input_mapper +
  test_pad_buffer + a scripted boot leg prove.

### host_app/input/input_latency.h (55 lines)
- **Purpose**: [R183-INLAT] input-latency census shared gate/clock: the
  T1(SDL)→T2(wire)→T3(scePadRead)→T4(boot-SM edge)→T5(displayed) chain
  probes; atomics for the pending-edge frame publication ("kept as a
  permanent diagnostic (task mandate)").
- **Public surface**: `inlat::On/NowUs/EdgePendingEeFrame/EdgeT4Us/EdgeT4Vf`.
  Consumers: graphics_thread, pad_input, input_system, libpad, main.
- **Classification**: GENERIC diagnostics.
- **Refactor destination**: `core/diag/input_latency.h/.cpp`
  (`InputLatencyCensus`) — compile-time diag category (PS2R_DIAGNOSTICS),
  default off; the T5 display-relative measurement doc is the durable "why"
  (BUG-027 input-lag pipeline references this chain).
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_INPUT_LAT | OFF | latency chain probes on (input_latency.h:18) | tuning-knob (de facto diagnostic) | **DIAG** — core/diag category `input.lat`; env dies, diag registry arms it |
- **Hacks / workarounds**: header-only globals-in-functions — becomes the
  diag module's owned state.
- **Risks**: none (measurement only).

### host_app/input/default_bindings.ini (37 lines)
- **Purpose**: The baked-in default bindings text (mouse=right stick camera,
  WASD=left stick, buttons, gamepad deadzone/sensitivity defaults) — parsed
  at startup through the same INI path as user files (proving the default
  parseable).
- **Public surface**: embedded/read by InputMapper.
- **Classification**: GENERIC host data (a note records the user's original
  ask vs the corrected standard scheme — history).
- **Refactor destination**: `engine/platform/sdl/input/default_bindings.ini`
  (installed beside the exe) with its values mirrored as `Config.input.*`
  defaults in the schema; the historical NOTE paragraph moves to the
  directory README.
- **Flags**: n/a. **Hacks**: none. **Risks**: none.

### host_app/input/test_input_mapper.cpp (645 lines)
- **Purpose**: Unit tests T1/T2/T3/T5 for the mapper + the pure gamepad
  conversion tables (runs with NO pad; SDL linked only for
  SDL_GetScancodeFromName). Custom main() (repo convention).
- **Public surface**: test binary `test_input_mapper` (tests/CMakeLists:2151).
- **Classification**: GENERIC test.
- **Refactor destination**: `tests/input/test_input_mapper.cpp` verbatim
  (MIGRATION: tests/ — live). Registered in ctest as today.
- **Flags**: none. **Hacks**: none.
- **Risks**: none — this test gates the input move itself.

### host_app/input/test_pad_buffer.cpp (698 lines)
- **Purpose**: Unit test T4: PS2PadState → 128-byte PADMAN slot image,
  linking the REAL pad_input.cpp + memory.cpp; asserts every byte of the
  wire contract (ACTIVE-LOW inversion, +0x02/+0x03 order fix, 0x79/0x73 mode
  ids, mask bytes, +0x58 counter-last publish via the write-recorder seam,
  DISCONN→CONFIG→READY staging, script overlay after L2, press latch,
  aligned u16 stores).
- **Public surface**: test binary. **Classification**: GENERIC test (pins
  scePad wire semantics).
- **Refactor destination**: `tests/pad/test_pad_buffer.cpp` verbatim —
  becomes the proof gate for the pad/hle + platform/input split (it links
  both sides of the seam).
- **Flags**: none read (exercises the PS2_PAD_SCRIPT overlay via seams).
- **Hacks / workarounds**: `pad_input_test::WriteRecorder` production-null
  test seam — a sanctioned TEST-ONLY hook; survives as the explicit test
  seam on the pad wire writer.
- **Risks**: none — it is the safety net.

---

## host_app/gui/ — 10 files

Cluster context (MIGRATION §1 "App & GUI → app/"): panels move to `app/gui/`
verbatim, reading Config + the diag/triage registries instead of env/globals;
bug_report_section becomes the "Triage" panel over core/triage (TRIAGE_CAPTURE
§12); imgui vendors to `app/gui/third_party/`.

### host_app/gui/gui.h (34 lines)
- **Purpose**: GUI facade + threading contract: MAIN thread owns SDL events
  (Toggle/SetOpen/PushEvent/PublishDisplayMetrics); GRAPHICS thread owns the
  ImGui context (RenderImGui before swap, ShutdownRenderer). OpenGL-only.
- **Public surface**: `gui::Toggle/SetOpen/IsOpen/PushEvent/
  PublishDisplayMetrics/RenderImGui/ShutdownRenderer`. Consumers: main.cpp,
  opengl_backend.
- **Classification**: GENERIC host (app shell).
- **Refactor destination**: `app/gui/gui.h` (`Gui`) verbatim; the
  render-backend hook formalizes against `render/backend.h`'s overlay hook so
  a rebuilt Vulkan backend can host it too (today: silent no-op on Vulkan).
- **Flags**: none. **Hacks**: none. **Risks**: none.

### host_app/gui/gui.cpp (753 lines)
- **Purpose**: (1) cross-thread plumbing (open atomic, bounded copied
  SDL_Event queue, display metrics); (2) a minimal ImGui SDL2+GL4.1 backend
  implementing the 1.92 dynamic-texture protocol; (3) the settings window:
  Controller (device list/selection/deadzone/sensitivity via GamepadBackend),
  Keyboard/Mouse toggle, live Input test, Display (scaling/aspect via
  display_settings), then delegates to the Savestates / Bug reports /
  VU Microcode / Sound sections; persists via input::SaveDeviceConfig +
  sound config.
- **Public surface**: gui.h definitions.
- **Classification**: GENERIC host.
- **Refactor destination**: split → `app/gui/gui_host.h/.cpp` (`GuiHost`:
  plumbing), `app/gui/imgui_backend_gl.h/.cpp` (`ImGuiBackendGl`),
  `app/gui/settings_window.h/.cpp` (`SettingsWindow` calling per-section
  panels). Sections read Config fields (render.scale, video.fit_mode,
  input.*, audio.*) through core/config instead of scattered module statics.
- **Flags consumed here**: none directly (PS2_GUI_OPEN is read in main.cpp —
  out of this shard; noted for the ledger as SETTING → `Config.gui.open_at_start`).
- **Hacks / workarounds**:
  - `#ifdef RENDERER_OPENGL` compile-fork with no-op stubs — the platform/
    backend seam should provide the overlay capability flag instead of
    ifdefs (ARCH §16 no-#ifdef-outside-platform rule; render backend cap
    `has_gui_overlay`).
- **Risks**: graphics-thread-only ImGui context rule; proof = GUI open/close
  + input capture smoke leg (PS2_GUI_TEST_* successors in the harness).

### host_app/gui/savestate_browser.h (249 lines)
- **Purpose**: PURE browser logic: filename-pattern taxonomy
  (slot/slot-alias/slowspot/artdet/gui/arbitrary), 32-byte header decode
  (authoritative vf), filter/sort, row model — unit-testable with no
  ImGui/SDL (test_savestate_browser).
- **Public surface**: `ssbrowse::Kind/Row/ParseU32At/ClassifyName/...` +
  `DrawSavestatesSection()` decl. Consumers: gui.cpp, savestate_browser.cpp,
  tests.
- **Classification**: GENERIC host.
- **Refactor destination**: `app/gui/savestate_browser.h/.cpp`
  (`SavestateBrowser`; pure core kept header-separable for the test — split
  pure logic into `savestate_rows.h/.cpp` (`SavestateRows`) consumed by the
  panel). Filename taxonomy gains the triage-ring names when core/triage
  lands (SavestateRing slots, TRIAGE_CAPTURE §4.4).
- **Flags**: none. **Hacks**: none (patterns document their sources).
- **Risks**: pattern set must track the new `games/<title>/triage/` layout —
  coordinate with core/triage phase; test pins parsing.

### host_app/gui/savestate_browser.cpp (506 lines)
- **Purpose**: The panel: background scan thread over save_states/, lazy
  header validation, load/save buttons wiring GuiRequestLoad/RequestSaveNamed,
  status lines, and the [U4-TEST] env hooks firing the EXACT button code
  paths headlessly.
- **Public surface**: `ssbrowse::DrawSavestatesSection()`.
- **Classification**: GENERIC host.
- **Refactor destination**: `app/gui/savestate_browser.cpp` (panel half of
  the split above); scan thread stays detached-worker (never blocks draw).
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_GUI_TEST_LOAD | unset | headless GUI-path load firing `path@frame[,…]` (savestate_browser.cpp:245) | test-hook | **TEST-ONLY** — acceptance harness drives the same panel entry points via a test driver seam |
  | PS2_GUI_TEST_SAVE | unset | headless GUI-path save at frames (:263) | test-hook | **TEST-ONLY** — same |
- **Hacks / workarounds**: env test hooks inside the draw (need GUI open) —
  correct: a `GuiTestDriver` (tests/) invoking panel commands directly.
- **Risks**: load/save re-entrancy with the savestate quiescence protocol;
  proof = U4 acceptance legs re-targeted.

### host_app/gui/bug_report_section.h (16) / bug_report_section.cpp (159)
- **Purpose**: "Bug reports" panel: manual "Report bug now" (detached bundle
  worker via bugreport::StartManualBundle), include-savestate toggle,
  slowspot-bundling toggle + per-run cap display, polled result lines with
  the acceptance-quotable [U5-GUI] stderr mirror; [U5-TEST] env hooks firing
  the exact button path.
- **Public surface**: `bugrsec::DrawBugReportSection()`.
- **Classification**: GENERIC host (diagnostics UI).
- **Refactor destination**: `app/gui/triage_section.h/.cpp` (`TriageSection`)
  — MIGRATION/TRIAGE_CAPTURE §12: becomes the "Triage" panel over
  core/triage (per-class counts/toggles, manual button = TriageHost manual
  trigger, last result). bugreport:: API calls re-target TriageHost/
  TriageWriter.
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_BUGREPORT_TEST_MANUAL | unset | fire manual bundle at frames (bug_report_section.cpp:80) | test-hook | **TEST-ONLY** — triage gate (MIGRATION Phase 4) drives TriageHost directly |
  | PS2_BUGREPORT_TEST_MANUAL_STATE | unset | pre-set include-state (:92) | test-hook | **TEST-ONLY** — same |
- **Hacks / workarounds**: env hooks in draw (as U4) — same GuiTestDriver
  fix. `PS2_BUGREPORT_SLOWSPOT_MAX` is only DISPLAYED here (read in
  bug_report.cpp — other shard); the label re-words to the triage Config
  field.
- **Risks**: none beyond re-targeting; triage gate (i)-(iv) covers.

### host_app/gui/sound_section.h (15) / sound_section.cpp (51)
- **Purpose**: "Sound" panel: master volume slider, mute, voice/kon/underrun/
  drop stats; disabled-state notes (PS2_SOUND=0 / no device).
- **Public surface**: `sndsec::DrawSoundSection()`.
- **Classification**: GENERIC host.
- **Refactor destination**: `app/gui/sound_section.h/.cpp` (`SoundSection`)
  verbatim; "Disabled (PS2_SOUND=0)" text becomes "audio.enabled=false"
  (Config).
- **Flags**: none read. **Hacks**: none. **Risks**: none.

### host_app/gui/vu_microcode_section.h (22) / vu_microcode_section.cpp (488)
- **Purpose**: [U6-VUCTRL] "VU Microcode" panel controlling the VU-AOT tier:
  corpus-dir scan (vutrace_cases2 counts/bytes), vu_aot.ini read (mode/
  collect settings), regenerate dry-run on a detached worker, registry count;
  in [NOAOT] builds degrades to a status-only view (banner + read-only ini).
  Env test hooks mirror the buttons.
- **Public surface**: `vumcsec::DrawVuMicrocodeSection()`.
- **Classification**: GENERIC host UI over a DELETED subsystem — the tree
  retires the AOT tier outright ("AOT deleted (NOAOT)", ARCH §3 vu/tiers).
- **Refactor destination**: **DELETE** with the AOT tier (MIGRATION: VU-AOT
  machinery dies in the CMake rewrite; the [NOAOT] build is already the
  shipped shape). The useful residue — "which VU1 images are hot / covered"
  — is superseded by the vu1_replay --catalog workflow and the triage
  fallback bundles; if a VU coverage panel is wanted later it reads the
  Vu1ProgramLibrary + FallbackDetector counters (new, small, in
  app/gui/vu_section).
- **Flags consumed here**:
  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_VUAOT_INI | vu_aot.ini | ini path override (:79/:163) | tuning-knob | **DELETE** — dies with AOT |
  | PS2_VUAOT_TEST_SETMODE | unset | scripted mode set (:339) | test-hook | **DELETE** — dies with AOT |
  | PS2_VUAOT_TEST_SETCOLLECT | unset | scripted collect toggle (:348) | test-hook | **DELETE** |
  | PS2_VUAOT_TEST_REGEN | unset | scripted regenerate (:360) | test-hook | **DELETE** |
- **Hacks / workarounds**: the whole `#if defined(PS2_VU_AOT_DISABLED)`
  dual-body TU (full section kept "byte-identical when the tier is compiled
  in") — a compile-time legacy+current dual path; resolved by deletion.
- **Risks**: none — [NOAOT] is already default; deleting removes a dead
  limb. Proof: build without the TU + GUI smoke.

---

## host_app/platform/ — 9 files (+ .DS_Store)

Cluster context (ARCH §16, MIGRATION §1 "Platform seams"): ONE `Platform`
interface, per-target folders, no #ifdef outside the tree. MIGRATION maps the
compat headers → platform/ common + platform/sdl verbatim, and sentences
`platform/*/fiber_*` to DELETE with ps2_scheduler_old_fiber.

### host_app/platform/platform.h (28 lines)
- **Purpose**: The OLD fiber abstraction: ConvertThreadToFiber/CreateFiber/
  SwitchToFiber/DeleteFiber over an opaque handle, implemented per-OS.
- **Public surface**: `platform::FiberHandle` + 4 functions. ONLY consumers:
  ps2_scheduler_old_fiber.{h,cpp} — which are NOT in any CMakeLists
  (verified: no build references; the std::thread PS2 threading replaced
  them, A4 §2.3/B5).
- **Classification**: GENERIC host — DEAD CODE.
- **Refactor destination**: **DELETE** (MIGRATION §1.1 row: "superseded by
  ps2_threading; 0 build refs"). The FUTURE `Platform.fiber_create/
  fiber_switch` entries (ARCH §16, for no-thread targets like Vita) are a
  new seam designed then — not a move of this code (its ucontext/Win32
  semantics may inform the implementation; the docs note suffices).
- **Flags**: none. **Hacks**: none.
- **Risks**: none — deletion of never-built consumers; link test proves.

### host_app/platform/macos/fiber_macos.cpp (149 lines)
- **Purpose**: ucontext implementation of platform.h (deprecated-API
  suppression, heap stacks, thread-local current-fiber).
- **Public surface**: platform.h impls. NOTE: compiled today via
  `file(GLOB platform/macos/*.cpp)` (host_app/CMakeLists.txt:491) but has no
  live caller — dead weight in the binary.
- **Classification**: DEAD CODE. **Refactor destination**: **DELETE** (as
  platform.h). **Flags**: none. **Risks**: none (glob removal + link test).

### host_app/platform/win32/fiber_win32.cpp (58 lines)
- **Purpose**: Win32 Fibers implementation of platform.h. Same GLOB-compiled
  dead-code status.
- **Classification**: DEAD CODE. **Refactor destination**: **DELETE**.
- **Flags**: none. **Risks**: none.

### host_app/platform/crash_handler.h (23 lines)
- **Purpose**: Platform crash/exit diagnostics interface:
  InstallCrashHandlers (raw-fd crash report + fatal-signal handlers),
  InstallExitTraceLogger (atexit attribution).
- **Public surface**: `platform::InstallCrashHandlers/InstallExitTraceLogger`.
  Called from main.cpp startup.
- **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/crash_handler.h` — folds into
  the ONE Platform interface (§16) as platform-provided services; app/host
  installs at boot. Crash output re-targets core/triage's crash bundle path
  (TRIAGE_CAPTURE E2: crash sentinel → next-launch bundle).
- **Flags**: none. **Hacks**: none. **Risks**: signal-safety rules must move
  intact (no malloc/ofstream in handlers).

### host_app/platform/macos/crash_handler_macos.cpp (175 lines)
- **Purpose**: POSIX implementation (moved VERBATIM from main.cpp):
  sigaction+SA_SIGINFO fault address, execinfo backtraces, ring-log tail +
  binary dump, first-fault-only atomic_flag, async-signal-safe write
  helpers, getrusage census (ps2_get_proc_usage), exit-trace atexit.
- **Public surface**: crash_handler.h + proc_stats.h impls.
- **Classification**: GENERIC host (macOS/POSIX target code).
- **Refactor destination**: `engine/platform/macos/crash_handler.cpp`
  (`CrashHandler` per-target) — already target-foldered; rename to drop the
  suffix (folder carries the target per §16). Reads of ps2_threading/
  ring_log re-point to core/log ring + core/triage.
- **Flags**: none. **Hacks**: none (signal-context constraints documented).
- **Risks**: crash-path regressions are invisible until a crash — keep the
  deliberate-SIGSEGV smoke test (tests) that asserts a report lands.

### host_app/platform/win32/crash_handler_win32.cpp (189 lines)
- **Purpose**: Win32 mirror: CRT signal() + SetUnhandledExceptionFilter
  (fault address via SEH), same report format, no native backtrace;
  GetProcessTimes census. Header documents its verification class:
  mingw-w64 -fsyntax-only cross-checked only, never executed on real
  Windows.
- **Public surface**: crash_handler.h + proc_stats.h impls (win32).
- **Classification**: GENERIC host (windows target).
- **Refactor destination**: `engine/platform/windows/crash_handler.cpp`.
- **Flags**: none.
- **Hacks / workarounds**: the honesty note ("never executed on a real
  Windows machine") — keep; the windows platform folder's feasibility/CI
  note (§16 rule) must record the untested-runtime status until a Windows CI
  leg exists.
- **Risks**: handler-vs-WER ordering unverified; flagged, not fixable here.

### host_app/platform/backtrace_compat.h (31 lines)
- **Purpose**: execinfo backtrace() family where available
  (PS2_HAVE_BACKTRACE on macOS/glibc), inert zero-frame fallbacks elsewhere —
  callers' loops degrade to 0 iterations by pattern.
- **Public surface**: `backtrace/backtrace_symbols(_fd)` shims +
  PS2_HAVE_BACKTRACE. Consumers: crash handler, diagnostics probes.
- **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/backtrace_compat.h` (common
  platform header) verbatim — one of the few sanctioned #if trees since it
  IS the platform layer.
- **Flags**: none. **Hacks**: none. **Risks**: none.

### host_app/platform/proc_stats.h (16 lines)
- **Purpose**: `Ps2ProcUsage` (cpu_s, minflt/majflt, nvcsw/nivcsw) +
  `ps2_get_proc_usage()` — cross-platform process census for perf probes
  (Win32 leaves non-CPU fields 0).
- **Public surface**: struct + function; implemented in the per-target
  crash_handler TUs. Consumers: main.cpp [W13R7] probe.
- **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/proc_stats.h` with per-target
  impls moving into each target folder (or a dedicated proc_stats.cpp per
  target — one concept per file; implementing it inside crash_handler.cpp is
  a mild concept-mixing to fix). Consumer becomes core/triage PerfMonitor's
  capture-cost audit.
- **Flags**: none. **Hacks**: impl location (inside crash handler TU) —
  split per target. **Risks**: none.

### host_app/platform/win_compat.h (112 lines)
- **Purpose**: POSIX/Win32 shims: ps2_fsync/ps2_pread (OVERLAPPED positioned
  read preserving the pread guarantee hle_heap relies on)/ps2_setenv(+
  default)/ps2_unsetenv/ps2_mkdir/ps2_fseek64/ps2_ftell64/ps2_ssize_t — the
  LLP64 64-bit seek/tell rationale documented.
- **Public surface**: the shim set. Consumers: file IO sites repo-wide.
- **Classification**: GENERIC host.
- **Refactor destination**: `engine/platform/file_compat.h` (common) —
  though the END STATE per §16 is the `Platform.open/File` interface
  absorbing raw FILE*/fd usage; the shims survive the migration as the
  common-layer implementation detail and shrink as call sites move to
  Platform file IO. The `ps2_setenv*` family becomes vestigial once the
  env-gate era ends (only the config loader and test harness set env) —
  expect deletion in the gate-purge terminal state.
- **Flags consumed here**: none as gates — `getenv` at win_compat.h:77 is
  inside the Windows `ps2_setenv_default` shim (set-if-unset semantics), not
  a flag read; it exits with the env era.
- **Hacks / workarounds**: none. **Risks**: pread semantics under Win32
  OVERLAPPED — the documented guarantee must hold for ISO reads; covered by
  the loader harness on a future Windows leg.

### host_app/platform/.DS_Store (binary)
- macOS Finder artifact, not source. **DELETE** (add to .gitignore).

---

## SHARD SUMMARY

Flags column: N→outcomes as NORM/SETTING/DIAG/TEST/DELETE counts.
Class: G=GENERIC-PS2, GS=GAME-SPECIFIC, M=MIXED, D=dead, T=test.

| file | lines | class | destination | shape | flags | hacks |
|---|---:|---|---|---|---|---:|
| hle/vu1_hle.h | 108 | M | engine/vu/hle/contract.h + vu1_registry.h + image_analyzer.h; family consts → games/<t>/vu1_library | split | 0 | 1 |
| hle/vu1_hle.cpp | 91 | M | engine/vu/hle/vu1_registry.cpp + image_analyzer.cpp (signature-param) | split | 0 | 2 |
| hle/vu1_hle_runtime.h | 23 | G | engine/vu/hle/runtime.h | verbatim (gate dies) | 0 | 1 |
| hle/vu1_hle_runtime.cpp | 437 | G | engine/vu/hle/runtime.cpp + kick_lowering.* ; verifier → tests | split | 6 → 4 NORM / 1 TEST / 1 DIAG | 5 |
| hle/vu1_hle_programs.cpp | 6,207 | GS-on-M | helpers → engine/vu/vu_float.*; 8 family files + registration table → games/<t>/vu1_library/ | split (one family per file) | 3 → 2 NORM / 1 DELETE | 6 |
| hle/gs_packet.h | 572 | G | engine/gs/hle/sce/gs_packet.* + packet_diff.* | split (bodies out of header) | 0 | 1 |
| hle/gs_direct_ops.h | 151 | G | engine/gs/hle/ir/direct_ops.* | move (body → .cpp) | 0 | 1 |
| hle/gs_draw_ir.h | 638 | G | engine/gs/hle/ir/draw_ir.* + draw_ir_verifier.* | split | 0 | 1 |
| hle/drawlist_record.h | 241 | G | engine/gs/hle/ir/gsdl_format.h + core/diag/drawlist_recorder.h | split | 0 | 0 |
| hle/drawlist_record.cpp | 403 | G | core/diag/drawlist_recorder.cpp | move | 5 → 5 DIAG | 2 |
| hle/drawlist_reader.h | 115 | G/T | tests/lib/gsdl_reader.* | move | 0 | 0 |
| hle/loader_oracle.h | 144 | M | core/diag/loader_trace.* (harness-armed) + tools checker | split | 2 → 1 TEST / 1 DELETE | 2 |
| hle/loader_sync_hle.h | 224 | M | engine/cdvd/hle/loader.* + games/<t>/hle/loader_sync.cpp | split | 1 → 1 NORM | 3 |
| sce_hle/sce_hle.h | 44 | G | engine/ee/hle/sdk_binding.h | rewritten (manifest contract) | 0 | 1 |
| sce_hle/sce_hle_bindings.inc | 307 | GS (gen) | games/<t>/gen/ (build dir) | regenerate; checked-in copy deleted | 0 | 1 |
| sce_hle/generate_bindings.py | 139 | G | engine/tools/sce_lib_finder/ | move | 0 | 0 |
| sce_hle/sce_hle_registry.cpp | 604 | G | engine/ee/hle/sdk_binding.cpp (+ per-subsystem row registration) | rewritten (same install seam) | 0 | 3 |
| sce_hle/libkernl.cpp | 765 | G | engine/ee/hle/kernl_{file_io,sif_rpc,boot,timer}.* (sceSif bodies → sif/hle) | split | 0 | 3 |
| sce_hle/libmc.cpp | 887 | G | engine/mc/hle/mc_host_store.* + mc_command_model.* + bindings | split | 2 → 1 NORM / 1 SETTING | 3 |
| sce_hle/libpad.cpp | 933 | M | engine/pad/hle/pad_protocol.* + bindings; probes → core/diag | split | 2 → 1 TEST / 1 DIAG | 3 |
| sce_hle/libsdr.cpp | 438 | G | engine/sound/hle/sdr_remote.* + sd_block_trans.* | split | 3 → 3 NORM | 2 |
| sce_hle/libcdvd.cpp | 402 | G | engine/cdvd/hle/libcdvd_bindings.cpp | move | 0 | 0 |
| sce_hle/libipu.cpp | 266 | G | engine/ipu/hle/libipu_bindings.cpp | move | 0 | 1 |
| sce_hle/libmpeg.cpp | 815 | M | engine/ipu/hle/mpeg_api.* + bindings | split | 0 | 3 |
| sce_hle/libgraph.cpp | 123 | M | engine/gs/hle/sce/ (rows + gs_callbacks.*) | split | 0 | 2 |
| sce_hle/libdma.cpp | 82 | G | engine/dmac/hle/libdma_bindings.cpp | move | 0 | 1 |
| sce_hle/libdev.cpp | 19 | G | engine/ee/hle/libdev_bindings.cpp | move | 0 | 0 |
| sce_hle/netcnfif.cpp | 23 | G | engine/sif/hle/netcnfif_bindings.cpp | move (verify reachability) | 0 | 0 |
| sce_hle/libscf.cpp | 107 | G | engine/ee/hle/libscf_bindings.cpp | move | 0 | 2 |
| sound/sound_core.h | 171 | M | engine/sound/sound_core.h | move (D4 splits below) | 0 | 0 |
| sound/sound_core.cpp | 1,168 | M | engine/sound/sound_core.cpp + fmv_stream.* + sound_state_codec.* | split | 5 → 1 NORM / 2 SETTING / 1 DIAG / 1 TEST | 4 |
| sound/voice.h | 199 | M | engine/sound/voice.h | verbatim | 0 | 0 |
| sound/voice.cpp | 245 | M | engine/sound/voice.cpp | verbatim | 0 | 0 |
| sound/mixer.h | 29 | G | engine/sound/mixer.h + platform/sdl/audio_sink.h | split | 0 | 0 |
| sound/mixer.cpp | 85 | G | engine/sound/mixer.cpp + platform/sdl/audio_sink.cpp | split | 0 | 0 |
| sound/ring.h | 88 | G | engine/core/threads/cmd_ring.h (.inl) | move | 0 | 0 |
| sound/vag_decoder.h | 56 | G | engine/sound/vag_decoder.h | verbatim | 0 | 0 |
| sound/vag_decoder.cpp | 56 | G | engine/sound/vag_decoder.cpp | verbatim | 0 | 0 |
| input/input_backend.h | 48 | G | engine/platform/sdl/input/input_backend.h | verbatim | 0 | 0 |
| input/input_mapper.h | 95 | G | engine/platform/sdl/input/input_mapper.h | verbatim | 0 | 0 |
| input/input_mapper.cpp | 318 | G | engine/platform/sdl/input/input_mapper.cpp | verbatim | 0 | 0 |
| input/input_backend_kbm.h | 34 | G | engine/platform/sdl/input/kbm_backend.h | verbatim | 0 | 1 |
| input/input_backend_kbm.cpp | 91 | G | engine/platform/sdl/input/kbm_backend.cpp | verbatim | 0 | 0 |
| input/input_backend_gamepad.h | 103 | G | engine/platform/sdl/input/gamepad_backend.h + gamepad_convert.h | split | 0 | 1 |
| input/input_backend_gamepad.cpp | 423 | G | engine/platform/sdl/input/gamepad_backend.cpp + gamepad_convert.cpp | split | 0 | 0 |
| input/input_system.h | 55 | G | engine/platform/sdl/input/input_system.h | verbatim | 0 | 0 |
| input/input_system.cpp | 393 | G | engine/platform/sdl/input/input_system.cpp (behind Platform.poll_input) | move | 2 → 1 NORM / 1 TEST | 2 |
| input/input_latency.h | 55 | G | core/diag/input_latency.* | move | 1 → 1 DIAG | 1 |
| input/default_bindings.ini | 37 | G | engine/platform/sdl/input/ + Config defaults | move | 0 | 0 |
| input/test_input_mapper.cpp | 645 | T | tests/input/ | verbatim | 0 | 0 |
| input/test_pad_buffer.cpp | 698 | T | tests/pad/ | verbatim | 0 | 1 |
| gui/gui.h | 34 | G | app/gui/gui.h | verbatim | 0 | 0 |
| gui/gui.cpp | 753 | G | app/gui/gui_host.* + imgui_backend_gl.* + settings_window.* | split | 0 | 1 |
| gui/savestate_browser.h | 249 | G | app/gui/savestate_rows.* + savestate_browser.h | split | 0 | 0 |
| gui/savestate_browser.cpp | 506 | G | app/gui/savestate_browser.cpp | move | 2 → 2 TEST | 1 |
| gui/bug_report_section.h | 16 | G | app/gui/triage_section.h | rewritten (over core/triage) | 0 | 0 |
| gui/bug_report_section.cpp | 159 | G | app/gui/triage_section.cpp | rewritten (over core/triage) | 2 → 2 TEST | 1 |
| gui/sound_section.h | 15 | G | app/gui/sound_section.h | verbatim | 0 | 0 |
| gui/sound_section.cpp | 51 | G | app/gui/sound_section.cpp | verbatim | 0 | 0 |
| gui/vu_microcode_section.h | 22 | G/D | — | **deleted** (AOT tier retired) | 0 | 0 |
| gui/vu_microcode_section.cpp | 488 | G/D | — | **deleted** (AOT tier retired) | 4 → 4 DELETE | 1 |
| platform/platform.h | 28 | D | — | **deleted** (old-fiber consumer never built) | 0 | 0 |
| platform/macos/fiber_macos.cpp | 149 | D | — | **deleted** | 0 | 0 |
| platform/win32/fiber_win32.cpp | 58 | D | — | **deleted** | 0 | 0 |
| platform/crash_handler.h | 23 | G | engine/platform/crash_handler.h | verbatim | 0 | 0 |
| platform/macos/crash_handler_macos.cpp | 175 | G | engine/platform/macos/crash_handler.cpp | move | 0 | 0 |
| platform/win32/crash_handler_win32.cpp | 189 | G | engine/platform/windows/crash_handler.cpp | move | 0 | 1 |
| platform/backtrace_compat.h | 31 | G | engine/platform/backtrace_compat.h | verbatim | 0 | 0 |
| platform/proc_stats.h | 16 | G | engine/platform/proc_stats.h (+ per-target .cpp) | split | 0 | 1 |
| platform/win_compat.h | 112 | G | engine/platform/file_compat.h (shrinks into Platform.open) | move | 0 | 0 |
| platform/.DS_Store | — | junk | — | **deleted** (gitignore) | 0 | 0 |

Totals: 71 files (69 source + 1 ini + 1 junk), ~21,474 lines.
Flag reads found in shard files: 40 distinct env vars → 13 NORM, 3 SETTING,
9 DIAG, 9 TEST-ONLY, 6 DELETE. (PS2_PERF and PS2_SOUND counted once each here;
both have reads outside this shard too — master ledger reconciles.)

## TREE-GAPS and placement deltas (for the INDEX consolidator)

1. **`engine/platform/sdl/` shared-implementation folder** — MIGRATION_PLAN
   uses `platform/sdl/input/` (rows 82, 189) but the ARCHITECTURE §16 tree
   lists only per-target folders (linux/macos/windows/…) + `_template/`. The
   SDL-backed input/audio/window code is shared by three desktop targets.
   Proposal: `engine/platform/sdl/` as a common implementation library that
   the linux/macos/windows target folders compose. TREE-GAP (small): add the
   sdl/ folder to the §3/§16 tree.
2. **`gs_direct_ops.h` placement** — MIGRATION says "gs/hle/sce packet
   builders"; this map proposes `gs/hle/ir/` beside DrawIR because it is the
   middle tier of the same lowering ladder. Doc-level delta, either home
   works; ir/ keeps the three converters together.
3. **`sound/ring.h` → `core/threads/cmd_ring.h`** — the tree's core/threads
   ("queues, parks") is the natural home for a reusable MPSC ring; the
   alternative (stay in sound/) is also legal. Soft choice, not a gap.
4. **games/<title>/vu1_library/ file naming** — the tree sketches ONE
   `vu1_library.cpp`; A4 §3.5 + D4 one-class-per-file argue one family per
   file (family_tristrip, textured_tristrip, poscolor_strip, lit_tristrip,
   skinned_lit_strip, indexed_displace, zero_kick, sprite_text,
   crys_programs + clip_gate + library_common + vu1_library.cpp registration
   table). The tree already says "(one family per file, D4)" — no gap, just
   the concrete file list recorded here.
5. **netcnfif reachability** — bindings.inc header lists sceNetcnfifCheck's
   only match (0x1818b0) as OUT-OF-BAND-rejected; the handler may be
   unreachable today. Verify the boot BOUND census before porting effort.

## Dead code found (with evidence)

| file | evidence |
|---|---|
| platform/platform.h + platform/macos/fiber_macos.cpp + platform/win32/fiber_win32.cpp | only consumers are ps2_scheduler_old_fiber.{h,cpp} (grep: no other includers); ps2_scheduler_old_fiber appears in NO CMakeLists (grep of host_app/tests/root CMake = 0 hits); the fiber TUs themselves compile only via `file(GLOB platform/<os>/*.cpp)` (host_app/CMakeLists.txt:489-491) with no live caller. MIGRATION §1.1 already sentences them to delete. |
| gui/vu_microcode_section.{h,cpp} | live only as the [NOAOT] status-stub branch (`PS2_VU_AOT_DISABLED` build is the shipped shape); the controlled subsystem (VU AOT tier) is retired by the tree ("AOT deleted (NOAOT)"). Deletion rides the AOT retirement, not this shard. |
| platform/.DS_Store | macOS Finder artifact. |

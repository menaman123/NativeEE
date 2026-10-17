# `engine/vu/`

**Phase:** 5  
**Purpose:** Vector units: register state, ONE decode, flag pipeline, stall model, XGKICK, single-sourced PS2-float helpers; vu0/ macro helpers; tiers/; hle/ mechanism.

## Belongs here

- `Vu1State`/`Vu0State`, `VuUnit`, `VuDecoder` (+ `VuDecodedSlot`, the VUK kinds), `SpanClassifier`, `FlagPipeline`,
  `StallModel`, `XgKick`, `VuFloat`, `VuChopRoundGuard`, `VuImageHash`, `Vu1Case`; `tiers/` (interp, cached, block, dispatch, MTVU),
  `vu1_aot/` (the generic run-time VU1 recompiler: verified images on (kind, dest)-specialised block tables -- see
  `vu1_aot/README.md`), `vu0/` (libvu0 fingerprint HLE), `hle/` (the VU1 HLE mechanism; the library itself is title data)

## Does not belong here

- the VU1 program LIBRARY (games/<title>/vu1_library/); generated code of any kind (the AOT tier generates none: its
  native form is a table of bodies instantiated at engine build time); the AOT cache's files and threads (app/host/)

## Transplanted from (OLD, read-only)

- `vu.{h,cpp}, vu_decode_slot.h, vu_span_classify.h, cpu_state.h float helpers, vu1_hle_programs.cpp generic helpers`

## Files

| File | Class / types | OLD source (read-only) |
|---|---|---|
| `vu1_state.h` | `Vu1State` POD (`VUregs regs`, `alignas(16) micro[16384]`, `data[16384]`, `mem_mask`, per-run scratch, `micro_generation`); `using Vu0State = Vu1State` (VU0 = `mem_mask 0x0FFF`, low 4 KB reachable) | `host_app/vu.h:464-1167` (members), `host_app/memory.h:17-20`, `host_app/vu.cpp:717, :761-775` |
| `vu_chop_round_guard.h/.cpp` | `VuChopRoundGuard` — RAII `FE_TOWARDZERO`, the ONE copy (the six library copies collapse onto it) | `host_app/vu.cpp:54-69` |
| `vu_float.h/.inl/.cpp` | `VuFloat` — `Bits2F/F2Bits` (memcpy), `ClampBits1/ClampFloat/IsNormalExp/ClampStoreACC/WriteMaskClamp`, `MacFlags1/PackMacBits`, `kLaneMask[16][4]`, `CommitQStatus`, `FpMax/FpMin/ApplyMinMaxRaw`, `VuDiv`, `FtoiSat/Ftoi0/4/12/15`, `Itof0`, `ClipNf` (the CLIP judgement on raw bits); the tiers' four-lane kernels `ClampVec/Add4/Sub4/Mul4/Fma4/Fms4/Splat4/ApplyMinMax4/Ftoi4Scaled/Itof4Scaled` (scalar = the interpreter's per-lane expressions; NEON twins with `ClampBits4/MacFlags4` under `PS2R_VU_NEON` = `PS2R_SIMD` on AArch64, all `PS2R_FORCE_INLINE`) | `host_app/vu_fast_exec.h:38-47, :121-145, :158-222, :357-360, :1807-1833`; `host_app/vu.cpp:1814-1829, :1861-1879`; `host_app/hle/vu1_hle_programs.cpp:72-123, :1388-1393` |
| `vu_image_hash.h/.cpp` | `VuImageHash::Fast` — the 4-lane interleaved FNV-1a (cache key AND every registry/catalog image/prefix/variant hash) | `host_app/vu_hash_fnv.h:22-50` |
| `vu1_case.h/.cpp` | `Vu1Case` FROZEN POD (`VUC1`, v1, 33,456 bytes, offsets pinned) + `vu1_case::ReadCase/WriteCase` | `host_app/vu_trace_case.h:24-79` |
| `vu_decoder.h/.cpp` | `VuDecoder` — `DecodeVuSlot(lower, upper)`, `DecodeStallStatics`, `ClassifyFastKinds`, `VuUpper/Lower/SlotCanTouchHardwired`, `IsBranchKind`; `VuDecodedSlot` (60-byte POD), `VUK_U_*` / `VUK_L_*` kinds, `vu_dest` / `vu_status` / `vu_mac` bit namespaces — the ONE decode both tiers consume | `host_app/vu.h:61-200, :255-321`; `host_app/vu_decode_slot.h:21-217, :219-378, :380-470, :537-647` |
| `span_classifier.h/.cpp` | `SpanClassifier` — `ComputeRunLenTouchHw / ComputeStallClasses / ComputeFoldClasses / ComputeSpanRem` (static passes for the cached tier's superblock; `kFmacWbLatencyShared = kIaluLwLatencyShared = 4`) | `host_app/vu_span_classify.h:34-195` |
| `flag_pipeline.h/.inl/.cpp` | `FlagPipeline` — owner of the ONE VU clock (the hot MAC-ring entries `UpdateMACFlags/PushMac/PushMacRaw/ApplyMacToStatus` inlined in the `.inl`); CLIP ring (linear 8, `kClipDelay 4`, `clip_latest`, `SetClipImmediate` for FCSET), MAC/STATUS circular ring 8 (`kMacDelay 4`) with BOTH representations compiled — eager `PushMac/ApplyMacToStatus`, lazy `PushMacRaw/AccumulateLazyEntry/MaterializeMacStatus` (NEON fold under `PS2R_SIMD`, scalar OR-fold otherwise), `kDefaultLazy = PS2R_SIMD`; `RetireDue/RetireMacStatus/FlushMacPipe/UpdateMACFlags`; Q latch (`PushQ/VisibleQ/FlushQPipe`, 7/7/13); P latch (`PushP/VisibleP/ReleasePEarly/FlushPPipe`, the 13 EFU latencies); case-record latch accessors | `host_app/vu.h:625-633, :654-687, :731-737, :873-888`; `host_app/vu.cpp:797-829, :872-935, :950-1107, :1109-1161, :1228-1258, :1897-1908`; `host_app/vu_fast_exec.h:259-330` |
| `stall_model.h/.inl/.cpp` | `StallModel` — the retire cycle of the last write per VF lane (`fmac_retire_[32][4]`) and per VI (`ialu_retire_[16]`) + the newest of each (the OLD writeback rings of depth 8 / 4 collapsed onto what a reader observes: a reader takes the max retire over its lanes if still ahead of the clock; a lane the clock passed is inert, so nothing is pruned and the ring's never-reachable drop rule is gone — proven identical on the 720 + 126 case corpora), the ONE stall body `ApplyBody<M>` (`.inl`) behind `Apply` (dynamic), `ApplySpec<M>` (static class mask) and `ApplyClassed` (class table 1-14, 15-18 = branch bundles with the `kBranch` step); `FlushFmacWb/FlushIaluWb` (clock to the newest, lanes forgotten) / `Reset`; the cached tier's `ApplyPrunePush` (fold-16 body: the pushes) and `MaterializeFmac` / the block tier's `MaterializeFmacIssued` (span close: the last min(executed, 3) slots' writebacks at their issue cycles) | `host_app/vu_fast_exec.h:337-352, :387-388, :393-639`; `host_app/vu.cpp:616-663, :1198-1214, :2801-2818` (runtime twin `:1261-1584` not written) |
| `xg_kick.h/.cpp` | `XgKick(GifArbiter&, bool paced_drain)` — `PacketSize` tag walk, `Arm/Pace/Flush/Reset`, quantum 32, 1 QW / 2 cycles, second-kick stall `remaining_qw*2`, `0x4000` cancel, `0x40000` runaway, `& 0x3FFF` wrap, PATH1 `submitOwned`; `paced_drain=false` = whole-packet copy at the instruction | `host_app/vu.h:930-1000`; `host_app/vu.cpp:11359-11384, :11566-11732, :11855-11860, :12233-12251` |
| `vu_unit.h/.cpp` | `VuUnit` — owns `Vu1State` (heap), `FlagPipeline`, `StallModel`, `XgKick` (VU1 only); `WriteDataMem/WriteMicroMem/ReadDataMem` masked byte loops, `BumpMicroEpoch`, `Reset`, `SetVifTop`, `LoadCase/SaveCase` (XITOP fed from the case's `vif1_itops`); ctor `lazy_flags` selects the MAC ring; owns a `VuTierDispatch` — `Execute(start_pc)` / `ResumeIfWaiting()` run the ladder, `dispatch()` exposes mode / HLE settings and the tiers | `host_app/vu.cpp:714-775, :1598-1602, :1792-1798, :1804-1810, :4320-4332, :8095-8099, :12416-12418` |
| `tiers/vu_interpreter_tier.h/.cpp` + `vu_interpreter_tier_upper.cpp` + `vu_interpreter_tier_lower.cpp` | `VuInterpreterTier` — the reference: `Run(VuUnit&, start_pc[, RunOptions{cycle_limit}])` bundle loop (I/E bits, `discard_lower`/`hide_upper`, I written after the upper, VF0/VI0 re-pin, delay slot + `TPC = end + 16`, program-end drain of every pipe + XGKICK flush, NOP-sled stop, XTOP yield, VU0 self-spin park + `ResumeIfWaiting`, VU0 4 KB edge, `bundles()`); `RunUpper` (every func 0x00-0x2F, `std::fmaf` MADD/MSUB/OPMSUB), `RunSpecial2Upper` (ACC family, ITOF/FTOI, ABS, CLIP, legacy 0x18-0x1F), `RunLower` (LSU/IALU/flag/branch), `RunLowerSpecial` (0x40 group), `RunEfu`; FDIV bodies shared with the legacy upper encodings | `host_app/vu.cpp:5203-5210, :5300-5301, :5737-5760, :5990-7910, :8095-8103, :8104-11314` (probe forest `:4334-4999, :5027-5195` not written) |
| `tiers/vu_cached_tier.h/.cpp` + `vu_cached_tier_exec.cpp` | `VuCachedTier` — `VuCachedProgram` table (`kTableCap = 64`, evict min `exec_count`, epoch fast path, `VuImageHash::Fast` key), `Acquire`, `DecodeProgram` (ONE decode + the four `SpanClassifier` passes over the unit's `(mem_mask+1)/8` slots), `Execute(unit, pc[, cycle_limit])` / `ResumeIfWaiting`, the bundle loop over predecoded slots (`ApplyClassed`, fly-decode of a misaligned pc) and the superblock run body (entry `run_len >= 3`, warmup 4, fold 15/16 → `ApplyPrunePush`/nothing, spans `>= 4` without the model + `MaterializeFmac` close, the replay close on a cap cut, unmasked in-run pc, TPC at run exits), `ExecUpperFast/ExecLowerFast` (the interpreter's expressions per kind; GENERIC → `RunUpper/RunLower`), `bundles()` | `host_app/vu.h:218-252, :503-560`; `host_app/vu.cpp:2228-2459, :2462-2563, :2566-2665, :2696-2721, :2801-2846, :2848-2866, :2905-3675, :3676-3877`; `host_app/vu_fast_exec.h:111-117, :663-1426, :1442-1790` (RUNFLAT `:2330-2392, :2926-3069`, SPANFOLD2 `:3135-3345`, AOT, census and the eager pre-retire `:318-334` not written) |
| `tiers/vu_block_tier.h/.cpp` | `VuBlockTier` — `VuBlockProgram` table (same key / epoch / eviction rules as the cached tier) with lazily built straight-line `VuBlock`s per entry address: `VuBlockOp` = resolved upper / lower body (the cached tier's `UpperFastFn / LowerFastFn` tables), stall-model entry per static class + stall kind (None / Push / Model), hazard flags, `s_delta` (the static stall schedule from `Schedule` / `StaticStall`); terminals = branch kinds, E bit, NOP pair (and the bundle before one), the image's last bundle, the 128-op cap; the delay slot merged (`has_ds`), `fall` / `jump` successors; `Execute(unit, pc[, cycle_limit])` runs blocks with the model until two consecutive agreements at or past op 4 then by addition, `MaterializeFmacIssued` on leaving static mode, a 3-op guard after a post-issue clock advance; `Stats {blocks_run, ops_static, ops_model, blocks_built}`; VU1 only | NEW (no OLD counterpart: OLD's RUNFLAT / SPANFOLD2 are the cached tier's run body) |
| `vu1_aot/vu1_aot_tier.h/.cpp` + `vu1_aot/vu1_aot_verifier.h/.cpp` | `Vu1AotTier` (status map Unverified / Verified / Refused per image hash, `Execute` = verified → the block table re-resolved to `VuCachedTier::UpperFastFnD / LowerFastFnD` (`ResolveSpecialized`) else kick observer + fallback Block / Interp; `Stats`, `set_assume_verified`), `Vu1AotVerifier` (`Verify(cases)`: aot vs interp on fresh units — regs, data, clock, bundles, kicked PATH1 bytes; `ImageHashOf`, `CorpusHashOf`); the cached tier's bodies gained the `D` (dest) template parameter and the (kind, dest) tables | OLD `host_app/vu_aot_registry.h`, `vu_aot_status.h`, `recompiler_tool/vu_aot_gen.cpp` (the DESIGN: per-image native functions verified by the corpus compare; the codegen replaced by build-time template instantiation — `vu1_aot/README.md`) |
| `tiers/vu_tier_dispatch.h/.cpp` | `VuTierDispatch` — the ladder `Execute(unit, pc)`: HLE hook (VU1, `hle_enabled`, slot set) → MTVU hook (VU1, slot set) → `vu1_mode` Cached / Interp / Block / Aot (Block, Aot: VU1 on that tier, VU0 on the cached tier); `ResumeIfWaiting` through the ladder; one `cycle_cap` for every tier; `bundles()` = every tier; `VuSettings {vu1_mode = Cached, hle_enabled = false}`, `VuTierMode {Cached, Interp, Block}`, `HleHook` / `MtvuHook` plain callbacks (null = absent) | `host_app/vu.h:30, :503-515`; `host_app/vu.cpp:364-407, :456-466, :4318-4332, :4808-4816, :5000-5008, :8095-8099` (`Native` stub, the vutrace veto, the `[VU-MODE]` announce, per-unit `PS2_VU0/1_MODE` and the oracle arms `:5010-5195` not written) |
| `hle/vu1_hle_contract.h`, `hle/vu1_family_signature.h`, `hle/vu1_library_entry.h` | PODs: `Vu1HleInput` / `Vu1HleOutput` / `Vu1HleFn` / `Vu1ImageInfo`, `Vu1FamilySignature {variant_base, dispatcher_op0, exit_stubs[2], family_exit_tpc}`, `Vu1LibraryEntry` / `Vu1ProgramLibrary` (entries in registration order), `kVu1AnyEntry` | `host_app/hle/vu1_hle.h:28-108` (no `kVariantBase` — title data) |
| `hle/vu1_registry.h/.cpp` | `Vu1Registry(const Vu1ProgramLibrary&)` — `Lookup` (pc filter → first exact image → first variant → wildcard), `Append`, `Entries`, `DistinctVariantHashes` | `host_app/hle/vu1_hle.cpp:9-31` |
| `hle/vu1_image_analyzer.h/.cpp` | `Vu1ImageAnalyzer(const Vu1FamilySignature&)` — image / prefix / variant hashes (`VuImageHash::Fast`), extent, the family prefix learned per instance, the dispatch chain to an exit stub, `ShortHash` | `host_app/hle/vu1_hle.cpp:33-89` |
| `hle/vu1_hle_runtime.h/.cpp` | `Vu1HleRuntime(library, GifArbiter& | KickLowering&)` — `Hook` (the `VuTierDispatch::HleHook` shape) / `TryExecute`: hash memo on (micro generation, external epoch), analyze memo (512), lookup, run, data writes then kicks then regs / `TPC = family_exit_tpc`; `SetVifTops`, `BumpExternalGeneration`, `SetIdentityObserver` (per-kick `KickIdentity` before the submit, null after the last — invariant 28's source), `census()` | `host_app/hle/vu1_hle_runtime.cpp:160-423` (raw kick tier; `:427-437` static installer not written) |
| `hle/kick_lowering.h/.cpp` | `KickLowering(GifArbiter&[, GsSink&])` — one kick → one PATH1 slot; two producer shapes delivered direct (`submitDirectOwned`): the `{ST,RGBAQ,XYZ2/XYZF2}` geometry packet and the one-descriptor A+D register block (through `DispatchGifRegister`); everything else raw (`submitOwned`), censused by producing program | `host_app/hle/vu1_hle_runtime.cpp:295-399` (tier 3) |
| `hle/gif_packet_builder.h/.cpp` | `GifPacketBuilder` — PACKED GIF emission (`BeginTag/EndTag/QW/PRIM/RGBAQ/ST/UV/XYZ2/XYZF2/AD/Take`) | `host_app/hle/gs_packet.h:351-403` |
| `vu0/lib_vu0_sin_cos.h/.cpp` | `LibVu0SinCos` — `Matches` (three SDK instruction bundles at VU0 0xF0) + `TryExecute` (libm sin/cos of VF20.x/z into VF31, pins, `TPC = 0x1C0`; no pipe or clock change) | `host_app/vu.cpp:5540-5607` (interp arm; `:2570-2600` cached arm identical) |
| `vu0/vu0_registry.h/.cpp` | `Vu0Registry` — the macro-program census mechanism: injected `Descriptor {vu_id, entry_pc, name, Kind, fingerprint_lo/hi}` tables (`AddEntry/AddSubroutine`, `Lookup/LookupSubroutine`), `NoteEntry(vu, pc, micro, size)` → `Note {Event FirstKnown/FirstUnknown/Periodic(1k/10k/100k/1M)/None, fingerprint_mismatch (once per (vu, pc)), call_number, live bytes}`; zero title rows (the table is `games/…/vu1_library/vu0_programs`) | `host_app/vu_function_registry.h:39-80`, `vu_function_registry.cpp:66-88`; the census logic `host_app/vu.cpp:5395-5520` (the `g_logFile` lines become the caller's `PS2R_LOG(VU, …)`) |
| `macro/vu_macro_bridge.h/.cpp` | `VuMacroBridge` — the EE COP2 ↔ `VuUnit` hand-over behind the generated code's VCALLMS / VCALLMSR / CMSAR1 sites; DEFINES the row-27 seam bodies `Vu0Macro::Execute` / `Vu0Macro::ResumeIfWaiting` / `Vu1Window::Execute` (`memcpy` the 608-byte `VUregs` in → `vu0.Execute(start)` → out; VU1 owns its file; unbound = the Phase-3 `vu_micro_absent` park); `Bind/Unbind`, `Stats()` census, optional `Vu0Registry` notes → `PS2R_LOG(VU, …)`; see `macro/README.md` | `host_app/vu.cpp` (the VU0 interpreter ran ON `ctx.vuRegs`; here the unit owns its file and the bridge copies), `host_app/recomp_seams.cpp` (the seam names) |

File names follow the lint's snake_case(primary type) rule, so the plan's `decode`, `chop_round_guard`,
`span_classify`, `xgkick`, `tiers/interp_tier` and `vu0/libvu0_sincos` rows are `vu_decoder`, `vu_chop_round_guard`,
`span_classifier`, `xg_kick`, `tiers/vu_interpreter_tier` and `vu0/lib_vu0_sin_cos` here (one class may span several `.cpp`
files: the interpreter's upper and lower bodies are TU-private halves of the same class).

## Decode / float notes (OLD behaviour carried verbatim)

- `DecodeVuSlot` takes the LOWER word first: at byte address `pc` the lower is `[pc..pc+3]` and the upper
  `[pc+4..pc+7]`, both little-endian.
- ABS encoded with func 0x3C (bc 0) registers only a read of fs and no VF write / hazard; only the func 0x3D
  encoding writes ft in the decode. MOVE with ft == 0 is the lower NOP and enters no set. VF0 never enters a
  read or write set; `rd_regs` keeps counting past the 5-entry read table.
- The clamp maps NaN/Inf to +/-MAX_FLOAT and denormals to +/-0 (sign kept); -0 survives. MAC flags are judged
  on the RAW result bits: S from the sign bit (so -0 sets Z and S), Z for zero and denormal, U for denormal, O
  for Inf/NaN patterns. `CommitQStatus` replaces D/I (bits 4-5) and ORs DS/IS (10-11); MAC commits own bits
  0-3 and 6-9 — the two never overlap. MAX/MINI order raw bits as signed integers (-0 below +0, no clamp).
- FTOI saturates when the scaled source's exponent field is >= 0x4F000000 (|x| >= 2^31 or NaN) by the sign
  bit, else truncates toward zero; `Ftoi0` clamps its source first (the library form; identical for scale 1),
  `Ftoi4/12/15` scale the raw source (the interpreter form). ITOF converts the raw integer.
- `VuDiv`: divisor +/-0 (after the clamp, so denormal divisors too) yields +/-MAX_FLOAT with sign = XOR of
  the operand signs; the interpreter's DIV adds the D/I status bits on top.
- The image hash ignores a trailing partial 8-byte word; the tail's whole words feed lane 0 only.

## Pipeline / stall / XGKICK notes (OLD behaviour carried verbatim)

- `FlagPipeline` owns `vu_cycle`; `StallModel` and the tiers advance it through `cycle()`, `XgKick` takes a
  `uint64_t&`. `Reset` never touches the clock, the registers, the memories or the CLIP/MAC/Q rings.
- The MAC ring's eager and lazy forms are bit-identical at every reader (`RetireMacStatus`, `FlushMacPipe`):
  eager commits each retired entry (Z/S/U/O replaced from the MAC nibbles, sticky ORed, FDIV bits 4-5/10-11
  kept), lazy ORs the sticky evidence and packs the newest entry on materialise. A ring overflow commits
  (eager) or folds (lazy) the oldest entry at push time. `VuLazyFlagsAlgebra` pins the equivalence.
- Q: `PushQ` clamps the value and, if a quotient is still pending, waits for and commits it first; `VisibleQ`
  returns the CLAMPED register (the register keeps raw bits). P: `PushP` does not clamp; a second EFU op
  releases the pending P at `retire - 1`, program end at `retire`.
- Stall body order: FMAC prune (only when `min_retire <= cycle`), IALU prune (lower-bearing bundles), FMAC
  read stall (lane overlap, VF0 never), IALU branch stall, EFU release, FDIV wait+commit, then this bundle's
  VF and VI writebacks at `+4`; a full ring drops its soonest-retiring entry (last entry moves into the slot,
  `min_retire`/`regs` stay stale lower bounds/supersets).
- XGKICK copies bytes AT DRAIN TIME (a store issued after the kick but ahead of the drain pointer is
  transferred); the kick itself banks one cycle; `Pace` acts only once `cycle >= lastcycle + 33`; a tag walk
  reaching 0x4000 cancels the kick but submits what was already staged; staging past 0x40000 drops the chain.

## Interpreter notes (OLD behaviour carried verbatim; kept divergences named)

- Every upper op reads CLAMPED copies of VF[fs]/VF[ft] and re-clamps ACC in place (NOP included); MAC flags are
  judged on the RAW result, then the written lanes are clamped; a VF00 write is dropped but its flags issue.
  MADD/MSUB/MADDA/MSUBA/OPMSUB lanes are `std::fmaf` (single rounding) — the transform ground-truth test
  discriminates this, so `-ffp-contract` cannot move a result. MAX/MINI compare raw bits as signed ints.
  ITOF/FTOI/ABS read raw lanes; MOVE/MR32/LQ/SQ/MFIR move raw bits.
- Same-bundle rules: the upper runs first and reads the OLD `I`; `hide_upper` shows the lower the pre-upper
  VF; `discard_lower` skips the WHOLE lower op (its VI side effects too — kept divergence); `I` is written
  after the upper; VF0/VI0 re-pinned after every bundle. Stalls run before both ops, XGKICK pacing between.
- Control: a branch or E bit acts after one delay-slot bundle; TPC is set only at the normal advance
  (`(pc+8) & 0x3FFF`) and at program end (`E-bit address + 16`), never on a redirect; a branch or E bit
  INSIDE a delay slot is ignored; an E bit on a taken-branch bundle wins over the branch. Four all-zero
  bundles end a run; a VU1 run that reads XTOP > 1024 times without kicking yields at the bundle boundary;
  a VU0 self-branch gated by the same VI pair twice parks the unit (`suspended`, `ResumeIfWaiting`); VU0's
  pc is masked with 0x3FFF and a run ends silently past its 4 KB micro memory; the cycle cap counts bundles.
- Lower details: XITOP reads the ITOPS shadow; FCSET drops the CLIP ring; FCGET does not retire the MAC
  ring; RGET presents R as stored; JALR reads its target before writing the link; DIV by zero yields
  ±MAX by XOR of the operand signs with D/DS (I/IS for 0/0); EEXP is the only clamped EFU result; the EFU
  sum of squares is the fused chain `fmaf(z, z, fmaf(x, x, y*y))`.
- `LibVu0SinCos` runs libm under the run's round-toward-zero guard with sin and cos of one value back to
  back (the compiler may pair them into one sincos call; a lone cos differs in the last bit for huge
  arguments — the pin test mirrors the call shape).
- Special-2 table rows 7/8/9 follow the VU user's manual column by column: func 0x3C is the Q column
  (`MULAq` ACC = fs × Q, `ADDAq` ACC = fs + Q, `SUBAq` ACC = fs − Q — the ADDAbc/SUBAbc/MULAbc tail with the
  visible Q as the scalar), 0x3D is `ABS` / `MADDAq` / `MSUBAq`, 0x3E the I column, 0x3F `CLIP` / `MADDAi` /
  `MSUBAi`. OLD (`vu.cpp:8631-8634`) ran the 0x3D bodies for 0x3C — a divergence from the manual that is NOT
  carried (BUG-075; no live word of the title uses the Q column of those rows, `scan_fd00.py`); pinned by
  `VuSpecial2QRows`.

## Cached tier notes (OLD behaviour carried verbatim)

- The cached tier is the interpreter loop over a predecoded image plus three elisions, each the identity on
  registers / data memory / clock / kicks: fast bodies per decoded kind (same `VuFloat` kernels, same
  `std::fmaf` forms), the classed stall model (`ApplyClassed` → `ApplySpec<M>`), and the superblock run body
  (fold-15/16 bundles past the 4-bundle warmup skip the model; a span of ≥ 4 foldable bundles runs without it
  and `MaterializeFmac` rebuilds the ring at the close from the last 3 slots; a cap cut inside a span with
  < 4 executed replays `ApplyPrunePush` per slot at its issue cycle). `VuCachedSuperblock` and
  `VuCachedVsInterpCorpus` prove cached == interp (regs, data, clock, bundles, kicked op stream).
- The MAC flag push in the fast bodies is `UpdateMACFlags` exactly as the interpreter's (the eager
  pre-retire OLD performed before a push is unobservable and was not carried).
- A misaligned `pc` executes the bundle decoded on the fly (dynamic stall class, never a run); a decoded
  image is keyed by the whole-memory hash and re-validated only when the unit epoch moves.
- `VuSettings::vu1_mode` is the FALLBACK tier (Cached by default; Block = the pre-resolved block tier, selected
  by the composition); `hle_enabled` is false until the HLE runtime installs its hook (5D); the Config keys are
  copied in at integration. `VuBlockTier` and `VuCachedVsInterpCorpus`'s block leg prove block == interp.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

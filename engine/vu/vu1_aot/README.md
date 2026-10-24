# `engine/vu/vu1_aot/` -- the generic VU1 AOT tier (run-time recompiler)

**Purpose:** `engine.vu1_mode=aot`: a VU1 microprogram image (keyed by `VuImageHash::Fast`, the key every table
uses) runs NATIVE once it is VERIFIED, and on the fallback tier (the block tier by default, the interpreter by
`engine.vu1_aot_fallback=interp`) until then. Generic: nothing here knows a title; the title library (HLE) sits
above it in the ladder (`VuTierDispatch`: HLE hook -> MTVU hook -> aot -> fallback).

## What "native" is -- two steps
1. **The specialised table** (always, in-process, no toolchain): the image's block table (`VuBlockTier`, the
   pre-resolved straight-line blocks with the static stall schedule) re-resolved to the (kind, dest)-SPECIALISED
   instruction bodies `VuCachedTier::UpperFastFnD(kind, dest)` / `LowerFastFnD(kind, dest)` -- the body templates
   with the dest mask a template constant (`ExecUpperK<K, D>`, `ExecLowerK<K, D>`, `tiers/vu_cached_tier_exec.inl`),
   so the lane loops, the lane-select load, the ACC merge and the MAC mask are constants of the code. Built in
   memory at the image's first verified kick (`VuBlockTier::SetResolver`); proven the identity by
   `Vu1AotSpecializer` (every encoding x every dest) and by the corpus compares.
2. **The generated code** (optional; `engine.vu1_aot_codegen`, on by default, needs a C++ compiler on the host):
   `Vu1AotEmitter` emits ONE C++ FUNCTION PER BLOCK of the image's block table, the text of each by
   `Vu1AotBlockGen` -- a REGISTER-ALLOCATING generator: the VU register file lives in LOCALS for the whole function
   (`VR_reg vN` per VF the function touches, `uint16_t iN` per VI, `acc`; VF00 / VI00 constants; loaded at entry,
   written back at the exits), the host compiler does the SSA and the register allocation, and the generator does
   what the compiler cannot prove -- the CLAMP CACHE (`vNc` = the clamped view of vN, recomputed only when vN
   changes and is not known clamped; ACC re-clamped once per block), the STALL MODEL folded (ops 0..2 of a block
   read the model's lanes and push their writes; from op 3 on the in-block read-after-write stall is
   `max(cycle, c<j> + 4)` on the issue-cycle local of the most recent in-block writer and the writes stay in
   locals until the block's edges / exits materialise the last <= 3 -- `StallModel::ApplyBody` step by step with
   the slot's facts folded, the EFU / FDIV waits and the IALU side as the model does them), the MAC flag pushes
   DEFERRED (results in `m<i>` locals, delivered in issue order before a STATUS / MAC reader or a GENERIC word and
   at the exits: `RetireDue` first, a result issued >= 4 ops back folds directly, the rest push with their issue
   cycle) and the ring arm (lazy / eager, `FlagPipeline::kDefaultLazy`) decided at generation (recorded in the
   exports, checked by the installer). Each op's text is the `ExecUpperK` / `ExecLowerK` body of its kind with
   the operands named: the `VuFloat` lane kernels, `vu_cached_exec`'s helpers (`WriteMaskClampD<D>`, `QwAddr`,
   ...), the `FlagPipeline` entries, `XgKick::Arm`, the interpreter's inline judgements for CLIP (`ClipJudge`)
   and DIV (`DivQuotient`); a GENERIC word (no fast kind in the decoder) gets a native body of the interpreter's
   expression where the generator has one (`ClassifyGenericUpper` / `ClassifyGenericLower`: the Q / I FMAC and
   ACC forms, OPMULA / OPMSUB, ABS, MAXi; MFP, WAITP, the lane-arithmetic EFU ops) and the interpreter's body with
   a flush / reload around it otherwise. The same-bundle rules are kept op for op. A
   function covers a REGION: its block and, within `kRegionBlockCap` / `kRegionOpCap`, the blocks it reaches over
   fall-through / static targets, each a labelled scope with `goto` edges -- a block whose branch targets its own
   head loops inside the function with its registers resident (the default caps keep the regions to the block
   itself + its self-loop: growing them over the chain cut the hot bench 10 % but grew the text 4x and cost 9 % in
   the game, where the instruction fetch of a different program every kick is the bound). The block table STAYS
   the dispatcher: the loop sets `Vu1State::native_budget` (the cycle cap), the function reports the block whose
   END it reached (`native_exit_pc`; `taken` / `target` are that block's verdict) or whose HEAD it stopped at for
   lack of budget (`native_resume`), and the loop re-syncs there (JR / JALR targets, the E bit, the wrapped delay
   slot, the cycle cap and the NOP sled are the loop's as before) -- no giant switch; only the blocks the corpus
   reached are emitted (`ReachableBlocks`). The composition root's `app/host/vu1_aot_builder.*` compiles the TU at
   run time on its own thread, loads the library, and the tier installs the functions at the image's next kick
   (`InstallNative`, VU thread only) -- after the gate below has passed AGAIN on the loaded code, and (the test
   `Vu1AotNativeCorpus`) under a sweep of cycle caps (the budget exits and the loop's re-sync).

## Verification (the user's rule)
`Vu1AotVerifier::Verify(cases[, exports])`: for EVERY corpus case of the image, a fresh unit on the AOT tier (image
assumed verified; the exports table installed when given) and a fresh unit on the interpreter must leave identical
registers (the whole 608-byte file, TPC included), data memory, VU clock, bundle count and kicked PATH1 bytes
(hashed at the arbiter's submit tap). One divergence refuses the image (its first differing field is named); no
corpus case = not verified. Generated code passes the same gate a second time, in-process, before the tier ever
sees it; a library that diverges is refused and deleted. The corpus is the engine's capture format (`Vu1Case`),
collected at first run by the composition root's `app/host/vu1_aot_cache.*` through the tier's kick observer and
kept under the user data dir with the manifest of verdicts (keyed by image hash + engine build token), so a
verified image is native from its first kick of the next run and a new engine build re-verifies everything (the
generated LIBRARIES are keyed by the ABI key below instead, so they survive a rebuild that leaves the VU alone).

The corpus gate is NECESSARY, not sufficient: it replays single kicks from their entries. The in-game oracle is the
lockstep A/B (`engine/vu/tiers/vu_lockstep.h`, Config `diag.vu1_lockstep`): every kick on the tier AND the
interpreter, the end states diffed, the first divergence bisected to its bundle. It found E385 -- a GENERIC lower in
a branch's delay slot ran the interpreter's body with the region's verdict locals by reference, and that body begins
with `branch_taken = false`: the taken branch was cleared before the block end read it (the block tier is immune: it
captures the verdict right after the branch op). The generator now hands the verdict to the branch op alone. The
ABI key covers the generator's own sources since (CMake `--also`): a generator fix retires the libraries and packs
the old generator emitted, which pass the corpus gate and would otherwise be loaded forever.

## Files
| File | Class | Notes |
|---|---|---|
| `vu1_aot_tier.h/.cpp` | `Vu1AotTier(VuBlockTier&, VuInterpreterTier&)` | `Execute`: Acquire -> status (memo on (hash, epoch), the memo carries the image's exports pointer) -> native (`ResolveSpecialized` installed once per table; `InstallNative` when the builder has published; steady-clock timed) or observer + fallback; `Status {Unverified, Verified, Refused}`, `SetStatus` / `SetNativeExports` (thread-safe), `Fallback {Block, Interp}`, `set_assume_verified` (the harness / bench knob, never the play default), `Stats {native_kicks, fallback_kicks, native_bundles, native_ns, specialized, native_installs, native_blocks}`, `native_refused` (tables whose blocks did not match the live structure) |
| `vu1_aot_verifier.h/.cpp` | `Vu1AotVerifier` | `Verify(cases, exports = null, cycle_cap = 0, oracle_block = false) -> Result {identical, cases, image_hash, corpus_hash, bundles, native_blocks, native_ops, block_pcs, divergence}`, `ImageHashOf`, `CorpusHashOf` |
| `vu1_aot_emitter.h/.cpp` | `Vu1AotEmitter` | `ReachableBlocks(prog, entry_pcs, cap)`, `Emit(prog, block_pcs, {engine_build, note}) -> {source, entry_symbol, blocks, ops}` (the slots of every block once, `S_<pc4>_<k>`, then one region function per block), `EmitBlock(prog, blk, fn, emitted)`, `EmitBlockSlots`, `EmitSlot` (every field by name in a constexpr lambda), `EntrySymbol(hash)` = `vu1_aot_exports_<16 hex>`. Pure: strings out, no files |
| `vu1_aot_block_gen.h/.cpp` | `Vu1AotBlockGen` | `Region(prog, head, emitted)`, `EmitFunction(prog, head, fn, emitted)`, `SlotName`: the register-allocating function text (above) -- two passes (the touched / written / clamped-read sets, then the text) |
| `vu1_aot_exports.h` | PODs | the library ABI: `Vu1AotBlockExport {pc, n_ops, fn}`, `Vu1AotImageExports {abi_version, slot_size, image_hash, engine_build, compiler, block_count, blocks, flags_lazy}`, `kVu1AotAbiVersion` (2) |
| `vu1_aot_prelude.h` | -- | the ONE include of a generated TU; its include closure (22 engine headers, listed by the compiler's `-MM`) is staged by `tools/vu1_aot_stage_prelude.py` into `<build>/vu1_aot_prelude/` and copied under the cache by the builder |
| `vu1_aot_abi_key.h/.cpp` | `Vu1AotAbiKey` | `Compute(prelude_dir, defines)` / `ComputeForThisBuild` -> `abi-<16 hex>`: SHA-256 over the staged closure (MANIFEST order: relpath, `\n`, bytes, `\0`) + sorted `define=NAME=VAL` lines -- the identity a generated library is binary-compatible with. The staging script writes the same key into `<build>/generated/vu1_aot_abi_key.h` (`kVu1AotAbiKey`); the libraries, the cache dir and a shipped pack are keyed by it (a rebuild with the same VU sources keeps them); `tests/app/test_vu1_aot_pack` pins python == C++ |

Composition side: `app/host/vu1_aot_cache.h/.cpp` (manifest, corpus dirs, collector, verifier thread, the run.meta
`vu1_aot=` row), `app/host/vu1_aot_builder.h/.cpp` (toolchain discovery, the per-image search shipped pack -> user cache
-> emit / compile; load / re-verify / publish, `native.txt`, the run.meta `vu1_aot_codegen=` row),
`app/host/vu1_aot_pack.h/.cpp` (the pre-built pack's manifest `pack.txt` + library paths, the default location next to the
executable, the platform token). `tools/vu1_aot_pack` builds / checks a pack from a corpus. Config: `engine.vu1_mode=aot`,
`engine.vu1_aot_corpus=<dir>`, `engine.vu1_aot_verify=on|off`, `engine.vu1_aot_fallback=block|interp`,
`engine.vu1_aot_codegen=on|off`, `engine.vu1_aot_compiler=<c++>|none`, `engine.vu1_aot_prelude=<dir>`,
`engine.vu1_aot_opt=1|2`, `engine.vu1_aot_pack=<dir>`.

## The generated code (one block, abridged)
```cpp
static constexpr VuDecodedSlot S_0960_0 = [] { VuDecodedSlot s{}; s.upper = 0x1e0683cu; s.lower = 0x1ef380bu; ...; return s; }();
// region of 0960: 1 block(s) 0960
int Blk_0960(VuUnit& unit, VuInterpreterTier& g, bool& taken, uint32_t& target) {
    Vu1State& st = unit.state(); VUregs& regs = st.regs; FlagPipeline& pipes = unit.flags(); StallModel& stalls = unit.stalls();
    XgKick* const xgk = unit.xgkick(); uint64_t& cycle = pipes.cycle(); uint8_t* const data = st.data; const uint32_t mask = unit.mem_mask();
    const uint32_t budget = st.native_budget; int done = 0; taken = false; bool t = false; uint32_t tg = 0;
    uint64_t pace_due = (xgk != nullptr && xgk->armed()) ? xgk->next_due() : kNever;
    VR_reg v6 = regs.VF[6]; VR_reg v6c; ... VR_reg v27 = regs.VF[27]; uint16_t i1 = regs.VI[1]; uint16_t i7 = regs.VI[7]; VR_reg acc = regs.ACC;
    auto store_back = [&]() { regs.VF[1] = v1; ... regs.VI[2] = i2; regs.ACC = acc; st.native_resume = false; };
L_0960: {
        if (PS2R_UNLIKELY(done != 0 && static_cast<uint32_t>(done) + 8u > budget)) { store_back(); st.native_resume = true; st.native_exit_pc = 0x960u; return done; }
        t = false;
        // op 0 @ 0960  upper=0x1e0683c lower=0x1ef380b  ADDA_BC.xyzw | LQ
        ++cycle;
        stalls.ApplySpec<1>(S_0960_0, regs, pipes);  // the model's read stall
        const uint64_t c0 = cycle;
        if (PS2R_UNLIKELY(cycle >= pace_due)) { xgk->Pace(st.data, cycle); pace_due = xgk->armed() ? xgk->next_due() : kNever; }
        VR_reg m0;
        {
            VuFloat::ClampStoreACC(acc);
            v13c = VuFloat::ClampVec(v13);
            m0 = VuFloat::Add4(v13c, VuFloat::Splat4(kVf0.F[0]));
            vu_cached_exec::WriteMaskClampD<15>(acc, m0, 15);
            { const uint32_t a = vu_cached_exec::QwAddr(i7, 11, mask); std::memcpy(v15.I, data + a, 16); }
        }
        // op 1 @ 0968  MADD.xyzw | MOVE   ... v10c = ClampVec(v10); v17c = ClampVec(v17); m1 = Fma4(v10c, v17c, acc); WriteMaskClampD<15>(v27, m1, 15); v27c = v27; v1 = v19;
        // op 3 @ 0978  MSUB_BC.x | SQ     ... (op 3 on: no model call -- `if (c<j> + 4 > cycle) cycle = c<j> + 4;` on an in-block writer when one exists)
        // op 6 @ 0990  MADDA_BC.xyzw | IBEQ ... stalls.ApplySpec<64>(S_0960_6, regs, pipes);  // the model's branch stall
        //                                     if (static_cast<uint16_t>(0) == i1) { t = true; tg = 0x9c8u; }
        // op 7 @ 0998  MADD_BC.xyzw | FSAND  ... pipes.RetireDue(regs); FoldLazyRetired(m0.I, 15); ... PushMacRawAt(m6.I, 15, c6);  [the pending results, in order]
        //                                     pipes.RetireMacStatus(regs); i2 = static_cast<uint16_t>((regs.status_flag & 0xFFFu) & 0x2u);
        done += 8;
        pipes.RetireDue(regs); pipes.PushMacRawAt(m7.I, 15, c7);
        regs.TPC = 0x9a0u;
        if (t) { stalls.QueueFmac(11, 1, c5); stalls.QueueFmac(19, 15, c7); store_back(); st.native_exit_pc = 0x960u; st.cur_bundle_pc = 0x998u; taken = t; target = tg; return done; }
        regs.TPC = 0x9a0u;
        ... the same exit for the fall-through (0x9a0 is outside this region)
}
}
```
The first title's main image: 328 blocks / 2302 ops -> 4.5 MB of C++, 0.55 MB of library text (the stage-1 shape: 0.83 MB),
~7-9 s of `clang++ -O2` on an idle M4. Bench (`test_vu1_capture_replay --bench`, same minute, host load 12): hot
7-case loop 12.2 -> 8.8-9.1 ns per bundle, the 720-case corpus 13.8 -> 11.1, the rotation (a different case every
replay: the game's shape) 16.5 -> 15.0; in the game (the MTVU worker's `block_ns_per_bundle`, same hour) 18.0-19.2
-> 12.0-12.4.

## Does not belong here
Title data (a program library is `games/<title>/vu1_library/`), the instruction bodies (`tiers/vu_cached_tier_exec.inl`
owns them), file I/O, processes and threads (the composition root's cache and builder; the platform's
`ProcessRun` / `DynamicLibrary*`).

## Rules
One class per `.h`/`.cpp` pair; no getenv (the toolchain is found through Config and the platform's PATH spawn);
generated code and libraries live ONLY under the user data dir; identity proven, never assumed -- twice for
generated code. macOS hardened runtime: a locally built, unsigned `nativeee` loads the unsigned library it just
compiled; a notarised release build would need `com.apple.security.cs.disable-library-validation` (or to sign the
artefact) -- documented, not solved.

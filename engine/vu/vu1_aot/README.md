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
   `Vu1AotEmitter` emits ONE C++ FUNCTION PER BLOCK of the image's block table -- the block tier's `RunOps` over
   that block with every slot a `static constexpr VuDecodedSlot` and every body the engine's own template
   instantiation called with the constant slot (`VuCachedTier::ExecUpperK<K, D>` / `ExecLowerK<K, D>`,
   `StallModel::ApplySpec<M>` / `Apply` / `ApplyPrunePush`, `VuFloat::ClampStoreACC`, `VuBlockTier::PinHardwired`,
   the kick pacing compare, the XTOP yield), so the compiler folds registers, immediates, masks, hazard arms and
   the stall class into straight-line code with no per-op dispatch and no indirect call. NO op semantics are
   re-implemented: the emitted text names the bodies the loop would call, arm for arm (I bit / hide-upper /
   plain). The block table STAYS the dispatcher: a function runs one block and returns the ops it executed; the
   loop reads the branch verdict and picks the successor (JR / JALR targets, the E bit, the delay slot, the cycle
   cap and the NOP sled are the loop's as before) -- no giant switch, no full-extent seeding; only the blocks the
   corpus reached are emitted (`ReachableBlocks`: the closure of the cases' entry pcs and the blocks the
   verifier's native legs built, over fall-through / static target / the wrapped delay slot). The composition
   root's `app/host/vu1_aot_builder.*` compiles the TU at run time on its own thread, loads the library, and the
   tier installs the functions at the image's next kick (`InstallNative`, VU thread only) -- after the gate below
   has passed AGAIN on the loaded code.

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

## Files
| File | Class | Notes |
|---|---|---|
| `vu1_aot_tier.h/.cpp` | `Vu1AotTier(VuBlockTier&, VuInterpreterTier&)` | `Execute`: Acquire -> status (memo on (hash, epoch), the memo carries the image's exports pointer) -> native (`ResolveSpecialized` installed once per table; `InstallNative` when the builder has published; steady-clock timed) or observer + fallback; `Status {Unverified, Verified, Refused}`, `SetStatus` / `SetNativeExports` (thread-safe), `Fallback {Block, Interp}`, `set_assume_verified` (the harness / bench knob, never the play default), `Stats {native_kicks, fallback_kicks, native_bundles, native_ns, specialized, native_installs, native_blocks}`, `native_refused` (tables whose blocks did not match the live structure) |
| `vu1_aot_verifier.h/.cpp` | `Vu1AotVerifier` | `Verify(cases, exports = null) -> Result {identical, cases, image_hash, corpus_hash, bundles, native_blocks, native_ops, block_pcs, divergence}`, `ImageHashOf`, `CorpusHashOf` |
| `vu1_aot_emitter.h/.cpp` | `Vu1AotEmitter` | `ReachableBlocks(prog, entry_pcs, cap)`, `Emit(prog, block_pcs, {engine_build, note}) -> {source, entry_symbol, blocks, ops}`, `EmitBlock`, `EmitSlot` (every field by name in a constexpr lambda), `EntrySymbol(hash)` = `vu1_aot_exports_<16 hex>`. Pure: strings out, no files |
| `vu1_aot_exports.h` | PODs | the library ABI: `Vu1AotBlockExport {pc, n_ops, fn}`, `Vu1AotImageExports {abi_version, slot_size, image_hash, engine_build, compiler, block_count, blocks}`, `kVu1AotAbiVersion` |
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
static constexpr VuDecodedSlot Blk_2e20_s0 = [] { VuDecodedSlot s{}; s.upper = 0x2ffu; s.lower = 0x800106bcu; ...; return s; }();
int Blk_2e20(VuUnit& unit, VuInterpreterTier& g, bool& taken, uint32_t& target) {
    Vu1State& st = unit.state(); VUregs& regs = st.regs; FlagPipeline& pipes = unit.flags();
    StallModel& stalls = unit.stalls(); XgKick* const xgk = unit.xgkick(); uint64_t& cycle = pipes.cycle();
    taken = false; bool t = false; uint32_t tg = 0; constexpr uint64_t kNever = ~0ull;
    uint64_t pace_due = (xgk != nullptr && xgk->armed()) ? xgk->next_due() : kNever;
    // op 0 @ 2e20  upper=0x2ff lower=0x800106bc
    ++cycle;
    if (cycle >= pace_due) { xgk->Pace(st.data, cycle); pace_due = xgk->armed() ? xgk->next_due() : kNever; }
    VuFloat::ClampStoreACC(regs.ACC);                                   // the upper NOP arm
    VuCachedTier::ExecLowerK<45, -1>(g, unit, Blk_2e20_s0, 0x2e20u, t, tg);   // XTOP
    if (st.xtop_exit_requested) return 1;
    // op 1 ... op 3 (the branch: taken = t; target = tg;), op 4 = the delay slot
    return 5;
}
```
Sly 2's main image: 328 blocks / 2302 ops -> 3.5 MB of C++, 0.9 MB library, ~7-9 s of `clang++ -O2` (the default;
-O1 compiles in about the same CPU time and runs 5-10 % slower) on an idle M4 (the OLD generator's shape -- one function per entry over the whole extent with a JR switch -- cost ~90 ms per
slot and ~20 minutes for the same image).

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

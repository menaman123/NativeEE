# `engine/vu/vu1_aot/` -- the generic VU1 AOT tier (run-time recompiler)

**Purpose:** `engine.vu1_mode=aot`: a VU1 microprogram image (keyed by `VuImageHash::Fast`, the key every table
uses) runs NATIVE once it is VERIFIED, and on the fallback tier (the block tier by default, the interpreter by
`engine.vu1_aot_fallback=interp`) until then. Generic: nothing here knows a title; the title library (HLE) sits
above it in the ladder (`VuTierDispatch`: HLE hook -> MTVU hook -> aot -> fallback).

## What "native" is
The image's block table (`VuBlockTier`, the pre-resolved straight-line blocks with the static stall schedule) re-resolved
to the (kind, dest)-SPECIALISED instruction bodies: `VuCachedTier::UpperFastFnD(kind, dest)` / `LowerFastFnD(kind,
dest)` -- the same body templates as the dynamic tables with the dest mask a template constant (`ExecUpperK<K, D>`,
`ExecLowerK<K, D>`), so the lane loops, the lane-select table load, the ACC merge and the MAC mask are constants of the
code (OLD's `vu_aot` folded the whole decoded slot into a generated C++ function per entry; the engine's templates fold the
two fields whose value changes the code's shape, at engine build time, so there is no code to generate, compile, sign or
load at the user's machine -- the recon's path C). Built in memory at the image's first verified kick
(`VuBlockTier::SetResolver`); the specialisation is proven the identity by `Vu1AotSpecializer` (every encoding x every
dest) and by the corpus compares (`VuCachedVsInterpCorpus` aot leg, the title's verifier).

## Verification (the user's rule)
`Vu1AotVerifier::Verify(cases)`: for EVERY corpus case of the image, a fresh unit on the AOT tier (image assumed verified)
and a fresh unit on the interpreter must leave identical registers (the whole 608-byte file, TPC included), data memory,
VU clock, bundle count and kicked PATH1 bytes (hashed at the arbiter's submit tap). One divergence refuses the image (its
first differing field is named); no corpus case = not verified. The corpus is the engine's capture format (`Vu1Case`),
collected at first run by the composition root's `app/host/vu1_aot_cache.*` through the tier's kick observer and kept
under the user data dir with the manifest of verdicts (keyed by image hash + engine build token), so a verified image is
native from its first kick of the next run and a new engine build re-verifies everything.

## Files
| File | Class | Notes |
|---|---|---|
| `vu1_aot_tier.h/.cpp` | `Vu1AotTier(VuBlockTier&, VuInterpreterTier&)` | `Execute`: Acquire -> status (memo on (hash, epoch)) -> native (`ResolveSpecialized` installed once per table; steady-clock timed) or observer + fallback; `Status {Unverified, Verified, Refused}`, `SetStatus` (thread-safe), `Fallback {Block, Interp}`, `set_assume_verified` (the harness / bench knob: every image native, never the play default), `Stats {native_kicks, fallback_kicks, native_bundles, native_ns, specialized}`, `native_ns_per_bundle` |
| `vu1_aot_verifier.h/.cpp` | `Vu1AotVerifier` | `Verify(const std::vector<const Vu1Case*>&) -> Result {identical, cases, image_hash, corpus_hash, bundles, divergence}`, `ImageHashOf`, `CorpusHashOf` (FNV-1a over the records, order-dependent) |

Composition side: `app/host/vu1_aot_cache.h/.cpp` (manifest, corpus dirs, collector, verifier thread, the run.meta
`vu1_aot=` row). Config: `engine.vu1_mode=aot`, `engine.vu1_aot_corpus=<dir>` (an extra corpus read at start),
`engine.vu1_aot_verify=on|off`, `engine.vu1_aot_fallback=block|interp`.

## Does not belong here
Title data (a program library is `games/<title>/vu1_library/`), the instruction bodies (`tiers/vu_cached_tier_exec.cpp`
owns them), file I/O and threads (the composition root's cache).

## Rules
One class per `.h`/`.cpp` pair; no getenv; no generated code anywhere in the tree; identity proven, never assumed.

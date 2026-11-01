#pragma once
// vu1_aot_prelude.h -- the ONE include of a GENERATED VU1 image TU (the output
// of Vu1AotEmitter). It names exactly the engine headers a generated block
// needs: the block tier's types and PinHardwired, the cached tier's bodies
// (vu_cached_tier_exec.inl through vu_cached_tier.h: ExecUpperK / ExecLowerK),
// the unit (state, flags, stalls, the kick), the interpreter tier (the GENERIC
// bodies the templates delegate to), the VU float kernels, the exports ABI.
// Everything else arrives transitively. The builder stages this header and
// the transitive closure of what it includes (the compiler's own dependency
// listing of this file, so the set is never hand-maintained) next to the
// cache, under <cache>/<build>/include/, and compiles the generated TU with
// that directory as the include root: the TU compiles outside the tree with
// the headers of the engine build that generated it.
//
// The generated TU is compiled with the engine's VU semantic flags (-O1 or
// -O2, -fno-strict-aliasing -fwrapv, -ffp-contract=off, the PS2R_* definitions
// of the ps2r_vu target): the bodies are bit-identity code and must see the
// same rules as the engine's own instantiations. The re-verification gate
// (Vu1AotVerifier with the exports installed) is what proves it, per image.
#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/tiers/vu_cached_tier.h"
#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu1_aot/vu1_aot_exports.h"
#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

// Block chaining (Vu1AotBlockGen::Policy::chain): a block function hands over
// to its successor's function with a GUARANTEED tail call -- a chain of
// hundreds of blocks per kick must not grow the stack. A compiler without the
// attribute cannot build a chained TU (the generator emits the #error guard
// when it chains; the pack tool / the builder choose the policy).
#if defined(__clang__)
#define VU1_AOT_MUSTTAIL [[clang::musttail]]
#define VU1_AOT_HAS_MUSTTAIL 1
#elif defined(__GNUC__) && __GNUC__ >= 15
#define VU1_AOT_MUSTTAIL [[gnu::musttail]]
#define VU1_AOT_HAS_MUSTTAIL 1
#else
#define VU1_AOT_MUSTTAIL
#define VU1_AOT_HAS_MUSTTAIL 0
#endif

// The compiler's own version string for the exports table.
#if defined(__VERSION__)
#define VU1_AOT_COMPILER __VERSION__
#elif defined(_MSC_FULL_VER)
#define VU1_AOT_COMPILER "msvc"
#else
#define VU1_AOT_COMPILER "unknown"
#endif

// STAGE PERF-1: the COLD calls of a block function. Every op of a block may
// call XgKick::Pace (the paced XGKICK drain) -- an out-of-line call at 30+
// sites per function made the register allocator spill the live register
// file on the HOT path (a store before every cold branch) to have it in memory
// for the call. Under clang the wrappers below carry the `preserve_all`
// calling convention: the callee saves every register it touches (the SIMD
// file included), so the caller keeps its values in registers across the call
// and the saving cost moves to the cold path. Elsewhere a plain call (the
// code is identical in effect; only the register traffic differs). The clock
// goes by VALUE and comes back in the result, so the caller's local never
// escapes.
#if defined(__clang__)
#if __has_attribute(preserve_all)
#define VU1_AOT_COLD __attribute__((preserve_all, noinline))
#endif
#endif
#if !defined(VU1_AOT_COLD)
#define VU1_AOT_COLD
#endif
struct Vu1AotClockResult {
    uint64_t cycle;
    uint64_t pace_due;
};
VU1_AOT_COLD inline Vu1AotClockResult Vu1AotXgkPace(XgKick* xgk, const uint8_t* data, uint64_t cycle) {
    xgk->Pace(data, cycle);
    return Vu1AotClockResult{cycle, xgk->armed() ? xgk->next_due() : ~0ull};
}
VU1_AOT_COLD inline Vu1AotClockResult Vu1AotXgkArm(XgKick* xgk, const uint8_t* data, uint32_t vi_addr, uint64_t cycle) {
    xgk->Arm(data, vi_addr, cycle);
    return Vu1AotClockResult{cycle, xgk->armed() ? xgk->next_due() : ~0ull};
}

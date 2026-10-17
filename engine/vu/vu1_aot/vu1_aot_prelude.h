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

// The compiler's own version string for the exports table.
#if defined(__VERSION__)
#define VU1_AOT_COMPILER __VERSION__
#elif defined(_MSC_FULL_VER)
#define VU1_AOT_COMPILER "msvc"
#else
#define VU1_AOT_COMPILER "unknown"
#endif

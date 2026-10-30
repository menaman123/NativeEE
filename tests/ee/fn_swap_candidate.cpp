// The tier-2 test candidate (tests/ee/test_fn_swap.cpp dlopens it): a body for the generated leaf
// FUN_001ae150 (v0 = [0x2f6388] - [0x2f6384]) that computes the same result and ALSO writes one byte
// the generated body never touches -- the "one byte differently" arm of the swap test. The symbol is
// the swap's default (FnSwap::kDefaultSymbol); the generated-code ABI symbols it uses
// (ps2r_recomp_abi::g_bound_memory through memory::) resolve against the loading executable
// (-undefined dynamic_lookup, like the VU1 AOT image libraries).
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/recomp_abi/host_app/memory.h"

extern "C" void recomp_candidate(CpuContext& ctx, uint32_t) {
    const uint32_t hi = memory::read<uint32_t>(0x2f6388u);
    const uint32_t lo = memory::read<uint32_t>(0x2f6384u);
    ctx.cpuRegs.GPR.r[2].SD[0] = static_cast<int32_t>(hi - lo);
    memory::write<uint8_t>(0x00300040u, 0x5au);   // the one byte the generated body does not write
}

extern "C" const uint32_t recomp_candidate_addr = 0x001ae150u;

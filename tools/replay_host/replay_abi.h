#pragma once
// replay_abi.h -- the prologue of a tier-1 CANDIDATE translation unit (precompiled: <build>/tools/replay_host/
// replay_abi.pch). A candidate is one C++ file defining `void fn_<addr>(CpuContext& ctx, uint32_t addr)` (or
// `FUN_<addr>`, the generated spelling, or `extern "C" recomp_candidate` directly), compiled against this header
// exactly as the generated shards are compiled (same compiler, -std=c++17 -O2 -fno-strict-aliasing -fwrapv
// -ffp-contract=off, PS2R_TRACE_ACCESS so every load and store is observed), linked as a shared object with
// unresolved symbols left to the host, and dlopen'd by ps2r_replay_host. tools/tier1.py in recomp-agent drives that
// from the recipe CMake writes next to the PCH, and wraps the body in THE ONE candidate format tier 2 loads too:
// `extern "C" void recomp_candidate(CpuContext&, uint32_t)` + `extern "C" const uint32_t recomp_candidate_addr`.
// Everything below is header-only (inline) or resolves against the loading executable -- the replay host and
// nativeee both export the generated surface -- so the same .dylib runs in both.
//
// What a candidate body sees (all of it is what a generated body sees; see engine/ee/recomp_abi/host_app/):
//   CpuContext ctx            ctx.cpuRegs.GPR.r[n].UD[0] / SD[0] / UL[0] / SL[0] (n: 4..7 = a0..a3, 2..3 = v0/v1,
//                             29 = sp, 31 = ra), ctx.fpuRegs.fpr[n].f, ctx.cpuRegs.HI / LO
//   memory::read<T>(addr), memory::write<T>(addr, v), memory::read_quad / write_quad   (guest addresses)
//   ps2_float_clamp, ps2_div_s, ps2_sqrt_s, ps2_rsqrt_s                               (the EE's float rules)
//   recomp_call(id, addr, ctx)                                                        (a generated call site)
//   ps2r_call(addr, ctx)                                                              (the same, by address)
//   FUN_<addr>(ctx, 0)  prototypes of every generated body (recompiled.h)
#include "recompiled.h"
#include "../host_app/cpu_state.h"
#include "../host_app/memory.h"
#include "../host_app/syscalls.h"
#include "../host_app/sif_hle.h"
#include "../host_app/gs_hle.h"
#include "../host_app/hle_heap.h"
#include "../host_app/ps2_threading.h"
#include "../host_app/cdvd_stream.h"
#include "../host_app/timer_hle.h"
#include "../host_app/vu.h"
#include "../host_app/sif_rpc_handlers.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <fstream>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <set>
#include <map>
#include <functional>

extern std::ofstream g_logFile;

// Call the function at guest address `addr` through the engine's dispatch (the probe sees it as a callee; the
// replay host serves it from the recording or runs its generated body). For hand-written bodies that do not want
// to spell dense ids.
inline void ps2r_call(uint32_t addr, CpuContext& ctx) {
    const uint32_t id = recomp_fn_id_for(addr);
    if (id == 0xFFFFFFFFu) { recomp_dispatch_fallback(ctx, addr); return; }
    recomp_call(id, addr, ctx);
}

// Register accessors a hand-written body may prefer over the r[n] spelling.
namespace ps2r {
inline uint32_t a0(const CpuContext& c) { return c.cpuRegs.GPR.r[4].UL[0]; }
inline uint32_t a1(const CpuContext& c) { return c.cpuRegs.GPR.r[5].UL[0]; }
inline uint32_t a2(const CpuContext& c) { return c.cpuRegs.GPR.r[6].UL[0]; }
inline uint32_t a3(const CpuContext& c) { return c.cpuRegs.GPR.r[7].UL[0]; }
inline uint32_t t0(const CpuContext& c) { return c.cpuRegs.GPR.r[8].UL[0]; }
inline uint32_t t1(const CpuContext& c) { return c.cpuRegs.GPR.r[9].UL[0]; }
inline uint32_t sp(const CpuContext& c) { return c.cpuRegs.GPR.r[29].UL[0]; }
// v0/v1 as the EE holds them: a 32-bit result is sign-extended to 64 bits.
inline void ret_s32(CpuContext& c, int32_t v) { c.cpuRegs.GPR.r[2].SD[0] = v; }
inline void ret_u32(CpuContext& c, uint32_t v) { c.cpuRegs.GPR.r[2].SD[0] = static_cast<int32_t>(v); }
inline void ret_v1_u32(CpuContext& c, uint32_t v) { c.cpuRegs.GPR.r[3].SD[0] = static_cast<int32_t>(v); }
inline void ret_u64(CpuContext& c, uint64_t v) { c.cpuRegs.GPR.r[2].UD[0] = v; }
}  // namespace ps2r

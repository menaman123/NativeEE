// bench_ee_codegen: the EE-thread codegen roofline (STAGE PERF-1, track ee).
//
// The recompiled EE code is memory-to-memory: every guest register is a field of
// CpuContext reached through a reference, every guest load/store goes through
// memory::read/write -> RecompAbi::Memory() -> FastMem (two pointer loads, a
// bounds compare, a memcpy). Because the RAM store is a char-typed store, the
// compiler must assume it may alias the context and the two pointers, so every
// register value is reloaded after every guest store. This tool measures, on the
// host it runs on:
//   peak      -- scalar integer adds: dependent chain (latency) and 8 independent
//                chains (throughput), ns per op -> the instruction roofline
//   gen       -- a 12-instruction guest basic block in the generated code's exact
//                shape, three emissions:
//                  as_emitted : `CpuContext& ctx`, memory::read/write
//                  restrict   : `CpuContext& __restrict ctx` (the context never
//                               overlaps guest RAM), memory::read/write
//                  hoisted    : restrict + the RAM base hoisted once per body into
//                               a local const pointer (one bounds check, one load
//                               per access)
//                ns per guest instruction, and the ratio to `peak` = the gap
//   call      -- the per-call dispatch sequence of recomp_raw_dispatch (table load,
//                probe byte, shadow-stack push/pop, an atomic trace flag, the raw
//                indirect call) around an empty body: ns per call
// Portable: no platform code; the timer is std::chrono::steady_clock. Output is one
// line per row, `name ns_per_op=... ops=...`, for the notes' roofline table.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_handler.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"

#if defined(_MSC_VER) && !defined(__clang__)
#define BENCH_RESTRICT __restrict
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_RESTRICT __restrict__
#define BENCH_NOINLINE __attribute__((noinline))
#endif

namespace {

using Clock = std::chrono::steady_clock;

double Seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

// A sink the optimiser cannot see through.
volatile uint64_t g_sink;

struct NullMmio : MmioHandler {
    uint64_t Read(uint32_t, unsigned) override { return 0; }
    void Write(uint32_t, unsigned, uint64_t) override {}
};

// ---- peak -----------------------------------------------------------------------------------------
double PeakDependent(uint64_t n) {
    uint64_t x = 1;
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < n; i++) {
        x = x + (x >> 7) + 1;   // one dependent add chain (plus a shift to defeat strength reduction)
    }
    const auto t1 = Clock::now();
    g_sink = x;
    return Seconds(t0, t1) * 1e9 / static_cast<double>(n);
}

double PeakIndependent(uint64_t n) {
    uint64_t a0 = 1, a1 = 2, a2 = 3, a3 = 4, a4 = 5, a5 = 6, a6 = 7, a7 = 8;
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < n; i++) {
        a0 += i; a1 += i ^ 1; a2 += i ^ 2; a3 += i ^ 3; a4 += i ^ 4; a5 += i ^ 5; a6 += i ^ 6; a7 += i ^ 7;
    }
    const auto t1 = Clock::now();
    g_sink = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
    return Seconds(t0, t1) * 1e9 / (8.0 * static_cast<double>(n));
}

// ---- gen: the 12-instruction block in three emissions ---------------------------------------------
// The guest block (a typical object-update inner loop):
//   lw   v1, 0x30(s0)      ; addiu a0, s0, 0x40 ; sll  v0, v1, 2   ; addu a1, a0, v0
//   lw   t0, 0(a1)         ; addiu t0, t0, 1    ; sw   t0, 0(a1)   ; lw  t1, 0x34(s0)
//   addu t1, t1, t0        ; sw   t1, 0x34(s0)  ; addiu s0, s0, 0x10; sltiu v0, s0, 0x4000
#define GEN_BLOCK(R, RD, WR)                                                                              \
    R(3).SD[0] = static_cast<int32_t>(RD(uint32_t, static_cast<uint32_t>(R(16).UL[0]) + 0x30));            \
    R(4).SD[0] = static_cast<int32_t>(static_cast<int32_t>(R(16).SL[0]) + 0x40);                           \
    R(2).SD[0] = static_cast<int32_t>(static_cast<uint32_t>(R(3).UL[0]) << 2);                             \
    R(5).SD[0] = R(4).SD[0] + R(2).SD[0];                                                                  \
    R(8).SD[0] = static_cast<int32_t>(RD(uint32_t, static_cast<uint32_t>(R(5).UL[0]) + 0x0));              \
    R(8).SD[0] = static_cast<int32_t>(static_cast<int32_t>(R(8).SL[0]) + 0x1);                             \
    WR(uint32_t, R(5).UL[0] + 0x0, R(8).UL[0]);                                                            \
    R(9).SD[0] = static_cast<int32_t>(RD(uint32_t, static_cast<uint32_t>(R(16).UL[0]) + 0x34));            \
    R(9).SD[0] = R(9).SD[0] + R(8).SD[0];                                                                  \
    WR(uint32_t, R(16).UL[0] + 0x34, R(9).UL[0]);                                                          \
    R(16).SD[0] = static_cast<int32_t>(static_cast<int32_t>(R(16).SL[0]) + 0x10);                          \
    R(2).UD[0] = (static_cast<uint32_t>(R(16).UL[0]) < 0x4000u) ? 1 : 0;

#define REG(n) ctx.cpuRegs.GPR.r[n]
#define RD_ABI(T, a) memory::read<T>(a)
#define WR_ABI(T, a, v) memory::write<T>(a, v)

BENCH_NOINLINE void BlockAsEmitted(CpuContext& ctx, uint32_t iters) {
    for (uint32_t i = 0; i < iters; i++) {
        GEN_BLOCK(REG, RD_ABI, WR_ABI)
        if (!(ctx.cpuRegs.GPR.r[2].UD[0] & 1)) ctx.cpuRegs.GPR.r[16].SD[0] = 0x1000;   // the loop's back-edge test
    }
}

BENCH_NOINLINE void BlockRestrict(CpuContext& BENCH_RESTRICT ctx, uint32_t iters) {
    for (uint32_t i = 0; i < iters; i++) {
        GEN_BLOCK(REG, RD_ABI, WR_ABI)
        if (!(ctx.cpuRegs.GPR.r[2].UD[0] & 1)) ctx.cpuRegs.GPR.r[16].SD[0] = 0x1000;
    }
}

// The hoisted emission: the RAM base read once per body; the fast path is one compare + one access.
template <typename T>
inline T RdHoisted(const uint8_t* BENCH_RESTRICT ram, uint32_t addr) {
    const uint32_t phys = MainMemory::Phys(addr);
    if (static_cast<uint64_t>(phys) + sizeof(T) <= MainMemory::kSize) {
        T v;
        std::memcpy(&v, ram + phys, sizeof(T));
        return v;
    }
    return RecompAbi::Memory().ReadSlow<T>(addr);
}
template <typename T>
inline void WrHoisted(uint8_t* BENCH_RESTRICT ram, uint32_t addr, T v) {
    const uint32_t phys = MainMemory::Phys(addr);
    if (static_cast<uint64_t>(phys) + sizeof(T) <= MainMemory::kSize) {
        std::memcpy(ram + phys, &v, sizeof(T));
        return;
    }
    RecompAbi::Memory().WriteSlow<T>(addr, v);
}
#define RD_H(T, a) RdHoisted<T>(ram, a)
#define WR_H(T, a, v) WrHoisted<T>(ram, a, v)

BENCH_NOINLINE void BlockHoisted(CpuContext& BENCH_RESTRICT ctx, uint32_t iters) {
    uint8_t* BENCH_RESTRICT ram = RecompAbi::Memory().Ram().Data();
    for (uint32_t i = 0; i < iters; i++) {
        GEN_BLOCK(REG, RD_H, WR_H)
        if (!(ctx.cpuRegs.GPR.r[2].UD[0] & 1)) ctx.cpuRegs.GPR.r[16].SD[0] = 0x1000;
    }
}

template <typename F>
double TimeBlock(F f, CpuContext& ctx, uint32_t iters) {
    ctx.cpuRegs.GPR.r[16].SD[0] = 0x1000;
    f(ctx, 1000);   // warm
    ctx.cpuRegs.GPR.r[16].SD[0] = 0x1000;
    const auto t0 = Clock::now();
    f(ctx, iters);
    const auto t1 = Clock::now();
    g_sink = ctx.cpuRegs.GPR.r[9].UD[0];
    return Seconds(t0, t1) * 1e9 / (12.0 * static_cast<double>(iters));
}

// ---- call: the recomp_raw_dispatch sequence -------------------------------------------------------
struct Frame { uint32_t func_addr, ra_at_entry; };
Frame g_stack[256];
uint32_t g_depth = 0;
std::atomic<bool> g_trace{false};
uint8_t g_probe[8] = {};
uint32_t g_addrs[8] = {0x100000, 0x100100, 0x100200, 0x100300, 0x100400, 0x100500, 0x100600, 0x100700};
void EmptyBody(CpuContext& ctx, uint32_t) { ctx.cpuRegs.GPR.r[2].UD[0] += 1; }
void (*g_raw[8])(CpuContext&, uint32_t) = {EmptyBody, EmptyBody, EmptyBody, EmptyBody, EmptyBody, EmptyBody, EmptyBody, EmptyBody};

BENCH_NOINLINE void RawDispatch(uint32_t id, uint32_t target, CpuContext& ctx) {
    const uint32_t addr = g_addrs[id];
    const uint32_t entry_ra = ctx.cpuRegs.GPR.r[31].UL[0];
    if (g_probe[id]) { g_sink = 1; return; }
    bool pushed = false;
    if (g_depth < 256) { g_stack[g_depth].func_addr = addr; g_stack[g_depth].ra_at_entry = entry_ra; ++g_depth; pushed = true; }
    if (g_trace.load(std::memory_order_relaxed)) g_sink = 2;
    g_raw[id](ctx, target);
    if (pushed) --g_depth;
}

BENCH_NOINLINE void DirectBody(CpuContext& ctx, uint32_t) { ctx.cpuRegs.GPR.r[2].UD[0] += 1; }

double TimeCalls(CpuContext& ctx, uint32_t iters, bool dispatch) {
    const auto t0 = Clock::now();
    if (dispatch) {
        for (uint32_t i = 0; i < iters; i++) RawDispatch(i & 7, 0x100000, ctx);
    } else {
        for (uint32_t i = 0; i < iters; i++) DirectBody(ctx, 0x100000);
    }
    const auto t1 = Clock::now();
    g_sink = ctx.cpuRegs.GPR.r[2].UD[0];
    return Seconds(t0, t1) * 1e9 / static_cast<double>(iters);
}

double Median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    const int rounds = argc > 1 ? std::atoi(argv[1]) : 5;
    const uint32_t iters = 20u * 1000u * 1000u;

    MainMemory ram;
    Scratchpad spad;
    AddressMap map(ram, spad);
    NullMmio null_mmio;
    MmioRouter mmio(null_mmio);
    FastMem fast(ram, map, mmio);
    RecompAbi::BindMemory(&fast);
    alignas(16) CpuContext ctx{};
    std::memset(&ctx, 0, sizeof(ctx));

    std::vector<double> dep, ind, as_emitted, restr, hoisted, call_direct, call_dispatch;
    for (int r = 0; r < rounds; r++) {
        dep.push_back(PeakDependent(100u * 1000u * 1000u));
        ind.push_back(PeakIndependent(50u * 1000u * 1000u));
        as_emitted.push_back(TimeBlock(BlockAsEmitted, ctx, iters));
        restr.push_back(TimeBlock(BlockRestrict, ctx, iters));
        hoisted.push_back(TimeBlock(BlockHoisted, ctx, iters));
        call_direct.push_back(TimeCalls(ctx, iters, false));
        call_dispatch.push_back(TimeCalls(ctx, iters, true));
    }
    const double peak_dep = Median(dep), peak_ind = Median(ind);
    std::printf("bench_ee_codegen rounds=%d iters=%u (median of rounds)\n", rounds, iters);
    std::printf("peak.dependent_add   ns_per_op=%.3f  (latency-bound chain: ~1 cycle -> ~%.2f GHz effective)\n", peak_dep, 1.0 / peak_dep);
    std::printf("peak.independent_add ns_per_op=%.3f  (8 chains: throughput, %.2f ops/ns)\n", peak_ind, 1.0 / peak_ind);
    std::printf("gen.as_emitted       ns_per_guest_instr=%.3f  (x%.1f the dependent-add peak)\n", Median(as_emitted), Median(as_emitted) / peak_dep);
    std::printf("gen.restrict         ns_per_guest_instr=%.3f  (x%.1f; %.0f%% of as_emitted)\n", Median(restr), Median(restr) / peak_dep, 100.0 * Median(restr) / Median(as_emitted));
    std::printf("gen.hoisted          ns_per_guest_instr=%.3f  (x%.1f; %.0f%% of as_emitted)\n", Median(hoisted), Median(hoisted) / peak_dep, 100.0 * Median(hoisted) / Median(as_emitted));
    std::printf("call.direct          ns_per_call=%.3f\n", Median(call_direct));
    std::printf("call.raw_dispatch    ns_per_call=%.3f  (table + probe + shadow push/pop + trace flag + indirect)\n", Median(call_dispatch));
    return 0;
}

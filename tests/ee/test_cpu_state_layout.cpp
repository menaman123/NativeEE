// EeCpuStateLayout: sizeof/offsetof of the serialized CpuContext members equal
// the original's values (member offsets follow from the original cpu_state.h
// declaration order; the sizeof guard is the THRD section's ctx_size field,
// which the original wrote as sizeof(CpuContext) into every anchor state).
//
// Two ctest entries share this binary:
//   no argument      -> EeCpuStateLayout: the static layout pins (1600 is the
//                       value measured over all 11 frozen anchors, cited here
//                       and re-read by the second entry).
//   <anchor.ps2st>   -> EeCpuStateLayoutAnchor: parses the frozen anchor with
//                       the Phase-1 envelope reader, walks the THRD records
//                       and requires every ctx_size == sizeof(CpuContext).
//                       Exit 77 (ctest SKIP) when the anchor file is absent.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/savestate/savestate_format.h"
#include "engine/ee/cpu/cpu_context_init.h"
#include "engine/ee/cpu/cpu_state.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

// THRD record header as the original serialised it (packed, 80 bytes):
//   i32 tid; u32 entry, stack, stackSize, gp, priority, attr, option, arg;
//   i32 status, waitType, waitId, wakeupCount, currentPriority, suspendCount;
//   u8 started, has_ctx, is_boundary, pad0; u32 heapBase, heapEnd;
//   u32 ctx_size; u32 chain_depth;   then [ctx_size bytes if has_ctx] then
//   chain_depth x {u32 func, u32 ra}.
constexpr size_t kThreadRecHdrSize = 80;
constexpr size_t kOffHasCtx = 61;
constexpr size_t kOffCtxSize = 72;
constexpr size_t kOffChainDepth = 76;

uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Returns the number of thread records checked, or -1 on a malformed payload.
int CheckAnchorThrd(const std::vector<uint8_t>& thrd) {
    if (thrd.size() < 4) return -1;
    const uint32_t count = Le32(thrd.data());
    size_t p = 4;
    for (uint32_t t = 0; t < count; ++t) {
        if (p + kThreadRecHdrSize > thrd.size()) return -1;
        const uint8_t has_ctx = thrd[p + kOffHasCtx];
        const uint32_t ctx_size = Le32(&thrd[p + kOffCtxSize]);
        const uint32_t chain_depth = Le32(&thrd[p + kOffChainDepth]);
        std::printf("  THRD record %u: has_ctx=%u ctx_size=%u chain_depth=%u\n", t, has_ctx, ctx_size,
                    chain_depth);
        CHECK(ctx_size == sizeof(CpuContext));
        p += kThreadRecHdrSize;
        if (has_ctx) p += ctx_size;
        p += static_cast<size_t>(chain_depth) * 8;
        if (p > thrd.size()) return -1;
    }
    return static_cast<int>(count);
}

int RunAnchorMode(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::printf("SKIP EeCpuStateLayoutAnchor: anchor not present: %s\n", path);
        return 77;
    }
    std::fclose(f);
    ps2r::savestate::FileMeta meta;
    std::vector<ps2r::savestate::Blob> sections;
    std::string err;
    if (!ps2r::savestate::ReadStateFile(path, &meta, &sections, &err)) {
        std::printf("FAIL EeCpuStateLayoutAnchor: %s: %s\n", path, err.c_str());
        return 1;
    }
    const ps2r::savestate::Blob* thrd = nullptr;
    for (const auto& s : sections)
        if (s.tag == ps2r::savestate::TAG_THRD) thrd = &s;
    if (!thrd) {
        std::printf("FAIL EeCpuStateLayoutAnchor: no THRD section in %s\n", path);
        return 1;
    }
    const int n = CheckAnchorThrd(thrd->payload);
    CHECK(n > 0);
    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS EeCpuStateLayoutAnchor: %d THRD records in %s carry ctx_size == sizeof(CpuContext) == %zu\n", n,
                path, sizeof(CpuContext));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) return RunAnchorMode(argv[1]);

    // The anchor-state guard value: THRD ctx_size measured 1600 in all 11
    // frozen anchors (verify/baseline legs; EeCpuStateLayoutAnchor re-reads one).
    CHECK(sizeof(CpuContext) == 1600);

    // cpuRegs: 32 GPRs (512) + HI/LO/HI1/LO1 (64) + CP0 (128) + sa + pc.
    CHECK(offsetof(CPURegs, GPR) == 0);
    CHECK(offsetof(CPURegs, HI) == 512);
    CHECK(offsetof(CPURegs, LO) == 528);
    CHECK(offsetof(CPURegs, HI1) == 544);
    CHECK(offsetof(CPURegs, LO1) == 560);
    CHECK(offsetof(CPURegs, CP0) == 576);
    CHECK(offsetof(CPURegs, sa) == 704);
    CHECK(offsetof(CPURegs, pc) == 708);
    CHECK(sizeof(CPURegs) == 720);

    // fpuRegs follows at 720: fpr (128) + fprc (128) + ACC + ACCflag.
    CHECK(offsetof(EmotionEngineState, fpuRegs) == 720);
    CHECK(offsetof(FPUregs, fprc) == 128);
    CHECK(offsetof(FPUregs, ACC) == 256);
    CHECK(offsetof(FPUregs, ACCflag) == 260);
    CHECK(sizeof(FPUregs) == 264);

    // vuRegs is 16-aligned -> 992.
    CHECK(offsetof(EmotionEngineState, vuRegs) == 992);
    CHECK(offsetof(VUregs, VI) == 512);
    CHECK(offsetof(VUregs, ACC) == 544);
    CHECK(offsetof(VUregs, Q) == 560);
    CHECK(offsetof(VUregs, P) == 564);
    CHECK(offsetof(VUregs, I) == 568);
    CHECK(offsetof(VUregs, R) == 572);
    CHECK(offsetof(VUregs, status_flag) == 576);
    CHECK(offsetof(VUregs, mac_flag) == 578);
    CHECK(offsetof(VUregs, clip_flag) == 580);
    CHECK(offsetof(VUregs, TPC) == 584);
    CHECK(offsetof(VUregs, CMSAR1) == 600);
    CHECK(sizeof(VUregs) == 608);

    // GPR element views: UD[0] is the low dword, UL[0] the low word.
    GPR_reg r{};
    r.UD[0] = 0x1122334455667788ull;
    r.UD[1] = 0x99AABBCCDDEEFF00ull;
    CHECK(r.UL[0] == 0x55667788u && r.UL[1] == 0x11223344u);
    CHECK(r.US[0] == 0x7788u && r.UC[0] == 0x88u);
    CHECK(r.UQ.lo == 0x1122334455667788ull && r.UQ.hi == 0x99AABBCCDDEEFF00ull);

    // CP0 by name == by number.
    CP0regs cp0{};
    cp0.n.Count = 7;
    cp0.n.Status = 9;
    CHECK(cp0.r[9] == 7 && cp0.r[12] == 9);

    // Power-on reset: zero, pc/sp set, VF0.w = 1, Q = 1.
    CpuContext ctx;
    CpuContextInit::Reset(ctx, 0x1000u, 0x01FFFF00u);
    CHECK(ctx.cpuRegs.pc == 0x1000u);
    CHECK(ctx.cpuRegs.GPR.r[29].UD[0] == 0x01FFFF00u);
    CHECK(ctx.cpuRegs.GPR.r[29].UD[1] == 0);
    CHECK(ctx.vuRegs.VF[0].w == 1.0f && ctx.vuRegs.VF[0].x == 0.0f);
    CHECK(ctx.vuRegs.Q == 1.0f);
    CHECK(ctx.vuRegs.VI[0] == 0);

    // COP2 control register rules.
    ctx.vuRegs.WriteControlReg(16, 0xFFFF);
    CHECK(ctx.vuRegs.status_flag == 0xFC0);  // low 6 bits are read-only
    ctx.vuRegs.WriteControlReg(20, 0xFFFFFFFFu);
    CHECK(ctx.vuRegs.R == 0x7FFFFFu);  // 23-bit LFSR
    ctx.vuRegs.WriteControlReg(3, 0x12345678u);
    CHECK(ctx.vuRegs.ReadControlReg(3) == 0x5678);  // VI is 16-bit, zero-extended
    ctx.vuRegs.VI[5] = 0xFFFF;
    CHECK(ctx.vuRegs.ReadControlReg(5) == 0xFFFF);  // never sign-extended
    CHECK(ctx.vuRegs.ReadControlReg(0) == 0);
    ctx.vuRegs.WriteControlReg(28, 0xFFFF);
    CHECK(ctx.vuRegs.FBRST == 0x0C0C);

    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS EeCpuStateLayout: sizeof(CpuContext)=%zu == anchor THRD ctx_size 1600\n", sizeof(CpuContext));
    return 0;
}

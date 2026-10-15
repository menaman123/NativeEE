// Bug072SpBalance -- the pin for BUG-072: the retail function at 0x178470 begins
// `lhu v0,0x1c(a3); andi v0,v0,1; bne v0,zero,0x17859c; addiu sp,sp,-0x10` (the
// prologue sits in the branch delay slot, executed on BOTH paths) and ends with
// one `addiu sp,sp,0x10; jr ra`. The MIPS calling convention: `jr ra` sees the
// sp the function was entered with. A generation that starts a second function
// at the delay slot (0x17847c) and enters it at the fall-through 0x178480 without
// a case for that address re-runs the prologue, and sp drifts by 0x10 per call.
//
// Rig: real guest memory (MainMemory + Scratchpad + an empty MmioRouter), zeroed,
// the four pointer arguments in RAM so every load/store of the body lands; the
// body is a leaf (no jal/jalr), float geometry over [a1], [a2], [a3] and the
// flag word [a3+0x1c]. Both paths are run; the fall-through path (bit 0 clear)
// is the one BUG-072 leaks on.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"

namespace {
int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

// The test rig (the file's one primary type): bound guest memory + one call of
// the retail function with a chosen flag bit, reporting the sp delta.
class TestBug072SpBalance {
public:
    static constexpr uint32_t kFunction = 0x00178470;  // retail entry (the lhu)
    static constexpr uint32_t kA0 = 0x00300000;        // object: lw [a0+0x1234]
    static constexpr uint32_t kA1 = 0x00310000;        // float triple
    static constexpr uint32_t kA2 = 0x00320000;        // float triple
    static constexpr uint32_t kA3 = 0x00330000;        // float triple + flag word at +0x1c
    static constexpr uint32_t kSp = 0x00500000;
    static constexpr uint32_t kRa = 0x00242878;        // a caller's return site (any)

    void BindMemory() {
        static MainMemory ram;
        static Scratchpad spad;
        static AddressMap map(ram, spad);
        static MmioSink sink;
        static MmioRouter mmio(sink);
        static FastMem fm(ram, map, mmio);
        RecompAbi::BindMemory(&fm);
    }

    // Returns sp_after - sp_before for one call with [a3+0x1c] = flag.
    int64_t Call(uint16_t flag) {
        memory::write<uint16_t>(kA3 + 0x1c, flag);
        CpuContext ctx{};
        std::memset(&ctx, 0, sizeof ctx);
        ctx.cpuRegs.GPR.r[4].UL[0] = kA0;
        ctx.cpuRegs.GPR.r[5].UL[0] = kA1;
        ctx.cpuRegs.GPR.r[6].UL[0] = kA2;
        ctx.cpuRegs.GPR.r[7].UL[0] = kA3;
        ctx.cpuRegs.GPR.r[29].UL[0] = kSp;
        ctx.cpuRegs.GPR.r[31].UL[0] = kRa;
        DispatchTable::Invoke(kFunction, ctx);
        return static_cast<int64_t>(ctx.cpuRegs.GPR.r[29].SL[0]) - static_cast<int64_t>(kSp);
    }
};
}  // namespace

int main() {
    TestBug072SpBalance rig;
    rig.BindMemory();
    DispatchTable::Initialize();
    // (No IsHooked check: the generated hooked[] table is all-true by design until
    // the generator's hook-list post-pass runs; the sp deltas below prove the
    // generated body executed.)

    const int64_t d_taken = rig.Call(1);
    std::printf("sp delta, [a3+0x1c]&1 == 1 (branch taken to 0x17859c): %lld\n", static_cast<long long>(d_taken));
    CHECK(d_taken == 0, "taken path: sp after return == sp before");

    const int64_t d_fall = rig.Call(0);
    std::printf("sp delta, [a3+0x1c]&1 == 0 (fall-through to 0x178480): %lld\n", static_cast<long long>(d_fall));
    CHECK(d_fall == 0, "fall-through path: sp after return == sp before (BUG-072: -0x10 on the split generation)");

    // Ten calls on the leaking path: the drift is per call, so the sum must stay 0.
    int64_t sum = 0;
    for (int i = 0; i < 10; ++i) sum += rig.Call(0);
    std::printf("sp delta summed over 10 fall-through calls: %lld\n", static_cast<long long>(sum));
    CHECK(sum == 0, "ten fall-through calls: no accumulated sp drift");

    std::printf("%s\n", g_fail ? "Bug072SpBalance: FAIL" : "Bug072SpBalance: PASS");
    return g_fail ? 1 : 0;
}

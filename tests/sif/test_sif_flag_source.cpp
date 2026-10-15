// tests/sif/test_sif_flag_source.cpp -- SifFlagSingleSource: the SIF flag words
// have ONE owner, the SifState block on the router. The sceSifSetReg/GetReg
// syscalls (SyscallTable, indices 1..6) read and write the physical word through
// memory::read/write and keep no mirror of their own, so what the IOP side
// raises through SifState::SetSmflagBits (SceSif::InitRpc's CMDINIT|BOOTEND
// reply) is visible to the game's sceSifGetReg(SMFLAG) poll -- including after
// a guest sceSifSetReg wrote SMFLAG earlier (a mirror would have shadowed the
// IOP-side bits with the stale guest value). The MSFLAG ack rule
// (sceSifSetReg(MSFLAG, SIFINIT|CMDINIT) raises the same bits on SMFLAG) lands
// in the same word.
#include <cstdio>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/sif/hle/rpc_router.h"
#include "engine/sif/hle/sce_sif.h"
#include "engine/sif/sif_state.h"

static int g_fail = 0;
#define CHECK(c, msg)                                                                          \
    do {                                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; }      \
    } while (0)

namespace {
constexpr uint32_t kSmflagAddr = SifState::kBase + SifState::kSmflag;
constexpr uint32_t kMsflagIndex = 3, kSmflagIndex = 4;  // sceSifSetReg/GetReg physical indices
constexpr int kSetRegSyscall = 121, kGetRegSyscall = 122;

uint32_t GetReg(SyscallTable& st, uint32_t index) {
    CpuContext c{};
    c.cpuRegs.GPR.r[29].UL[0] = 0x01FFFF00;
    c.cpuRegs.GPR.r[4].UL[0] = index;
    st.Dispatch(kGetRegSyscall, c);
    return c.cpuRegs.GPR.r[2].UL[0];
}
void SetReg(SyscallTable& st, uint32_t index, uint32_t value) {
    CpuContext c{};
    c.cpuRegs.GPR.r[29].UL[0] = 0x01FFFF00;
    c.cpuRegs.GPR.r[4].UL[0] = index;
    c.cpuRegs.GPR.r[5].UL[0] = value;
    st.Dispatch(kSetRegSyscall, c);
}
}  // namespace

int main() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static SifState sif;
    sif.Register(mmio);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);

    RpcRouter router;
    SceSif::Bind(SdkBindingManifest::Instance(), router, sif, SceSif::IopHeap{});
    auto& st = SyscallTable::Instance();
    st.ResetForTest();

    // A guest sceSifSetReg(SMFLAG) first: the word is the SifState word, nothing else.
    SetReg(st, kSmflagIndex, 0x00000001u);
    CHECK(sif.Smflag() == 0x1u, "sceSifSetReg(SMFLAG) writes the SifState word");
    CHECK(GetReg(st, kSmflagIndex) == 0x1u, "sceSifGetReg(SMFLAG) reads it back");

    // The IOP side's reply (SceSif::InitRpc -> SetSmflagBits(CMDINIT|BOOTEND)) is
    // visible to the very next sceSifGetReg(SMFLAG) poll, guest bit kept.
    CpuContext ctx{};
    ctx.cpuRegs.GPR.r[29].UL[0] = 0x01FFFF00;
    SceSif::InitRpc(ctx);
    CHECK(ctx.cpuRegs.GPR.r[2].UL[0] == 1, "sceSifInitRpc returns 1");
    CHECK(sif.Smflag() == 0x60001u, "SifState holds CMDINIT|BOOTEND plus the guest bit");
    CHECK(GetReg(st, kSmflagIndex) == 0x60001u, "sceSifGetReg(SMFLAG) sees the IOP-side bits after the guest SetReg");
    CHECK((GetReg(st, kSmflagIndex) & SifState::kCmdInit) != 0, "the CMDINIT poll succeeds at once");
    CHECK(memory::read<uint32_t>(kSmflagAddr) == 0x60001u, "a plain memory::read of 0x1000F230 agrees");

    // The MSFLAG ack rule lands in the same word: SIFINIT|CMDINIT raised on SMFLAG,
    // the existing bits kept.
    SetReg(st, kMsflagIndex, SifState::kSifInit | SifState::kCmdInit);
    CHECK(sif.Msflag() == 0x30000u, "sceSifSetReg(MSFLAG) stores MSFLAG");
    CHECK(sif.Smflag() == 0x70001u, "the ack ORs SIFINIT into the shared SMFLAG word");
    CHECK(GetReg(st, kSmflagIndex) == 0x70001u, "sceSifGetReg(SMFLAG) reads the acked word");

    // A later guest sceSifSetReg(SMFLAG) replaces the word (plain store, as the
    // original) and the syscall reads the replacement -- no shadow copy anywhere.
    SetReg(st, kSmflagIndex, 0);
    CHECK(sif.Smflag() == 0 && GetReg(st, kSmflagIndex) == 0, "a guest store replaces the word for both faces");
    sif.SetSmflagBits(SifState::kBootEnd);
    CHECK(GetReg(st, kSmflagIndex) == SifState::kBootEnd, "an IOP-side raise after the store is visible again");

    // Non-flag indices and the system registers are untouched by the change.
    SetReg(st, 1, 0x12345678u);
    CHECK(sif.Mscom() == 0x12345678u && GetReg(st, 1) == 0x12345678u, "MSCOM round trip through SifState");
    CHECK(GetReg(st, 0) == 0 && GetReg(st, 7) == 0, "indices outside 1..6 read 0");
    SetReg(st, 0x80000002u, 7);
    CHECK(GetReg(st, 0x80000002u) == 7, "the kernel-side system registers keep their own words");
    CHECK(mmio.UnhandledPageCount() == 0, "no SIF access fell to the sink");

    std::printf("%s: %d failure(s)\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}

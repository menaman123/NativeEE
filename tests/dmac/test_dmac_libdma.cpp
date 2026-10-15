// DmacLibdmaRows: the sce libdma.a bodies over the Dmac register page --
// sceDmaGetChan hands back the ten channel register blocks (no RAM
// descriptor), sceDmaReset zeroes the library-owned channels through the page
// (the SIF channels are the kernel's; both IPU pages are writable,
// globals other than PCR/STAT drop writes), sceDmaPutEnv decodes the 20-byte sceDmaEnv with
// the -1..-4 validation, memclr zero fills RAM and scratchpad and counts
// dropped bytes -- plus the manifest Bind (6 rows, thunks over the instance)
// and the two sync seams (sceDmaSync / sceGifSyncDma flush the parked VIF1
// kick, -> 0), driven directly and through their manifest rows.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/dmac/dmac.h"
#include "engine/dmac/hle/libdma_bindings.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

struct TestDmacLibdma {
    MainMemory ram;
    Scratchpad spad;
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0;
    VuUnit vu1;
    Vif vif;
    Dmac dmac;
    LibdmaBindings libdma;
    TestDmacLibdma()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter), libdma(dmac, ram, spad) {}
};
using Rig = TestDmacLibdma;

void SetArgs(CpuContext& ctx, uint32_t a0, uint32_t a1 = 0) {
    ctx.cpuRegs.GPR.r[4].UD[0] = a0;
    ctx.cpuRegs.GPR.r[5].UD[0] = a1;
    ctx.cpuRegs.GPR.r[2].UD[0] = 0xDEADBEEFDEADBEEFull;  // stale v0
}
int64_t V0(const CpuContext& ctx) { return ctx.cpuRegs.GPR.r[2].SD[0]; }

bool AllZero(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (p[i] != 0) return false;
    return true;
}

const uint32_t kBases[10] = {0x10008000u, 0x10009000u, 0x1000A000u, 0x1000B000u, 0x1000B400u,
                             0x1000C000u, 0x1000C400u, 0x1000C800u, 0x1000D000u, 0x1000D400u};

void FillChannel(Dmac& dmac, int ch, uint32_t seed) {
    DmacChannelRegs& r = dmac.channel(ch);
    r.chcr = seed | 0x100;   // STR set: a reset must clear it without kicking anything
    r.madr = seed + 0x10;
    r.qwc = seed + 0x20;
    r.tadr = seed + 0x30;
    r.asr0 = seed + 0x40;
    r.asr1 = seed + 0x50;
    r.sadr = seed + 0x80;
}

bool ChannelIs(const DmacChannelRegs& r, uint32_t chcr, uint32_t madr, uint32_t qwc, uint32_t tadr, uint32_t asr0,
               uint32_t asr1, uint32_t sadr) {
    return r.chcr == chcr && r.madr == madr && r.qwc == qwc && r.tadr == tadr && r.asr0 == asr0 && r.asr1 == asr1 &&
           r.sadr == sadr;
}

void TestGetChan() {
    Rig rig;
    CpuContext ctx{};
    // Fill a RAM stripe and snapshot the whole RAM: GetChan writes nothing anywhere.
    std::memset(rig.ram.Data() + 0x01CFF000u, 0xCC, 0x1000);
    std::vector<uint8_t> before(rig.ram.Data(), rig.ram.Data() + MainMemory::kSize);
    std::vector<uint8_t> spad_before(rig.spad.Data(), rig.spad.Data() + Scratchpad::kSize);
    for (int ch = 0; ch < 10; ++ch) {
        SetArgs(ctx, static_cast<uint32_t>(ch));
        rig.libdma.SceDmaGetChan(ctx);
        CHECK(V0(ctx) == static_cast<int64_t>(static_cast<int32_t>(kBases[ch])));
        CHECK(LibdmaBindings::ChannelBase(ch) == kBases[ch]);
    }
    // Every base is a register-page address, never RAM.
    for (int ch = 0; ch < 10; ++ch) CHECK((kBases[ch] & 0xFFFF0000u) == 0x10000000u);
    for (uint32_t bad : {10u, 0xFFFFFFFFu, 0x80000000u, 11u}) {
        SetArgs(ctx, bad);
        rig.libdma.SceDmaGetChan(ctx);
        CHECK(V0(ctx) == 0);
    }
    CHECK(LibdmaBindings::ChannelBase(-1) == 0 && LibdmaBindings::ChannelBase(10) == 0);
    CHECK(std::memcmp(before.data(), rig.ram.Data(), MainMemory::kSize) == 0);
    CHECK(std::memcmp(spad_before.data(), rig.spad.Data(), Scratchpad::kSize) == 0);
    // The Dmac register file is untouched too.
    for (int ch = 0; ch < 10; ++ch)
        CHECK(ChannelIs(rig.dmac.channel(ch), 0, 0, 0, 0, 0, 0, 0));
    CHECK(LibdmaBindings::GetChanCalls() >= 14);
}

void TestReset() {
    Rig rig;
    CpuContext ctx{};
    // Pre-set every channel and the globals directly (the page drops most global writes).
    for (int ch = 0; ch < 10; ++ch) FillChannel(rig.dmac, ch, 0x1000u * static_cast<uint32_t>(ch + 1));
    rig.dmac.globals().ctrl = 0x0000037Eu;   // DMAE clear; RELE, MFD, STS, STD, RCYC set
    rig.dmac.globals().stat = 0x00FF03FFu;
    rig.dmac.globals().pcr = 0x12345678u;
    rig.dmac.globals().sqwc = 0x00110022u;
    rig.dmac.globals().rbsr = 0x0000FFF0u;
    rig.dmac.globals().rbor = 0x00400000u;
    SetArgs(ctx, 1);
    rig.libdma.SceDmaReset(ctx);
    CHECK(V0(ctx) == 0);   // DMAE read at entry was clear
    // Channels whose write page the Dmac reaches: CHCR/MADR/TADR/ASR0/ASR1/SADR zero, QWC kept.
    for (int ch : {0, 1, 2, 3, 4, 8, 9}) {
        const uint32_t seed = 0x1000u * static_cast<uint32_t>(ch + 1);
        CHECK(ChannelIs(rig.dmac.channel(ch), 0, 0, seed + 0x20, 0, 0, 0, 0));
    }
    // The SIF channels are the kernel's: the body never touches them.
    for (int ch : {5, 6, 7}) {
        const uint32_t seed = 0x1000u * static_cast<uint32_t>(ch + 1);
        CHECK(ChannelIs(rig.dmac.channel(ch), seed | 0x100, seed + 0x10, seed + 0x20, seed + 0x30, seed + 0x40,
                        seed + 0x50, seed + 0x80));
    }
    // PCR is cleared by the environment. STAT acknowledges the SDK's 0xFF1F
    // status subset and toggles existing masks off; other globals stay unchanged.
    CHECK(rig.dmac.globals().pcr == 0);
    CHECK(rig.dmac.globals().ctrl == 0x0000037Eu);
    CHECK(rig.dmac.globals().stat == 0x00E000E0u);  // SIF channel CIS/CIM5..7 are excluded by the SDK
    CHECK(rig.dmac.globals().sqwc == 0x00110022u && rig.dmac.globals().rbsr == 0x0000FFF0u &&
          rig.dmac.globals().rbor == 0x00400000u);
    CHECK(rig.dmac.Read(0x1000E000u, 4) == 0x0000037Eu && rig.dmac.Read(0x1000E010u, 4) == 0x00E000E0u);
    // DMAE never latches through the page: mode 1 twice still returns 0; mode 0 too.
    SetArgs(ctx, 1);
    rig.libdma.SceDmaReset(ctx);
    CHECK(V0(ctx) == 0);
    SetArgs(ctx, 0);
    rig.libdma.SceDmaReset(ctx);
    CHECK(V0(ctx) == 0);
    // Returns the DMAE bit read at entry (a restored D_CTRL with bit 0 set).
    rig.dmac.globals().ctrl = 0x1u;
    SetArgs(ctx, 0);
    rig.libdma.SceDmaReset(ctx);
    CHECK(V0(ctx) == 1);
    // A cold page: 0.
    Rig cold;
    SetArgs(ctx, 1);
    cold.libdma.SceDmaReset(ctx);
    CHECK(V0(ctx) == 0);
    for (int ch = 0; ch < 10; ++ch) CHECK(ChannelIs(cold.dmac.channel(ch), 0, 0, 0, 0, 0, 0, 0));
    CHECK(LibdmaBindings::ResetCalls() >= 5);
}

void WriteEnv(MainMemory& ram, uint32_t at, uint8_t sts, uint8_t std_ch, uint8_t mfd, uint8_t rcyc,
              uint16_t pcr_hi, uint16_t pcr_lo, uint16_t sqwc_lo, uint16_t sqwc_hi, uint32_t rbor, uint32_t rbsr) {
    uint8_t* p = ram.Pointer(at, 20);
    p[0] = sts;
    p[1] = std_ch;
    p[2] = mfd;
    p[3] = rcyc;
    std::memcpy(p + 4, &pcr_hi, 2);
    std::memcpy(p + 6, &pcr_lo, 2);
    std::memcpy(p + 8, &sqwc_lo, 2);
    std::memcpy(p + 10, &sqwc_hi, 2);
    std::memcpy(p + 12, &rbor, 4);
    std::memcpy(p + 16, &rbsr, 4);
}

void TestPutEnv() {
    Rig rig;
    CpuContext ctx{};
    const uint32_t env = 0x00300000u;
    // Valid env: sts=SIF0(1), std=GIF(2), mfd=VIF1(2), rcyc=3, pcr 0xABCD1234, sqwc 0x00050010.
    WriteEnv(rig.ram, env, 5, 2, 1, 3, 0xABCD, 0x1234, 0x0010, 0x0005, 0x00600000u, 0x0000FFF0u);
    SetArgs(ctx, env);
    rig.libdma.SceDmaPutEnv(ctx);
    CHECK(V0(ctx) == 0);
    CHECK(rig.dmac.globals().pcr == 0xABCD1234u);   // the one writable global
    // Everything else the body stores is dropped by the page: unchanged (cold = 0).
    CHECK(rig.dmac.globals().ctrl == 0 && rig.dmac.globals().sqwc == 0 && rig.dmac.globals().rbor == 0 &&
          rig.dmac.globals().rbsr == 0);
    // No channel register moves.
    for (int ch = 0; ch < 10; ++ch) CHECK(ChannelIs(rig.dmac.channel(ch), 0, 0, 0, 0, 0, 0, 0));
    // The four invalid envs: -1..-4 with NOTHING written (the section is
    // byte-identical, PCR keeps its value).
    struct Bad {
        uint8_t sts, std_ch, mfd, rcyc;
        int32_t want;
    };
    const Bad bads[] = {{10, 0, 0, 0, -1}, {0, 10, 0, 0, -2}, {0, 0, 10, 0, -3}, {0, 0, 0, 7, -4}};
    for (const Bad& b : bads) {
        WriteEnv(rig.ram, env + 0x40, b.sts, b.std_ch, b.mfd, b.rcyc, 0x1111, 0x2222, 0x3333, 0x4444, 0x55555555u,
                 0x66666666u);
        std::vector<uint8_t> before, after;
        rig.dmac.SerializeSection(before);
        SetArgs(ctx, env + 0x40);
        rig.libdma.SceDmaPutEnv(ctx);
        rig.dmac.SerializeSection(after);
        CHECK(V0(ctx) == b.want);
        CHECK(before == after);
        CHECK(rig.dmac.globals().pcr == 0xABCD1234u);
    }
    // The precedence is sts, std, mfd, rcyc: an env wrong everywhere reports -1.
    WriteEnv(rig.ram, env + 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0);
    SetArgs(ctx, env + 0x80);
    rig.libdma.SceDmaPutEnv(ctx);
    CHECK(V0(ctx) == -1);
    // rcyc 6 is the last valid value; 0 clears RELE. Both valid -> 0 and PCR lands.
    WriteEnv(rig.ram, env + 0xC0, 0, 0, 0, 6, 0x0000, 0x0002, 0, 0, 0, 0);
    SetArgs(ctx, env + 0xC0);
    rig.libdma.SceDmaPutEnv(ctx);
    CHECK(V0(ctx) == 0 && rig.dmac.globals().pcr == 0x2u);
    // An env in the scratchpad reads the same way (the SDK struct may live there).
    uint8_t* s = rig.spad.Pointer(0x100, 20);
    std::memset(s, 0, 20);
    s[3] = 1;
    s[6] = 0x77;   // pcr_lo low byte
    SetArgs(ctx, 0x70000100u);
    rig.libdma.SceDmaPutEnv(ctx);
    CHECK(V0(ctx) == 0 && rig.dmac.globals().pcr == 0x77u);
    // A KSEG0 mirror pointer reads the same RAM bytes.
    SetArgs(ctx, 0x80000000u | env);
    rig.libdma.SceDmaPutEnv(ctx);
    CHECK(V0(ctx) == 0 && rig.dmac.globals().pcr == 0xABCD1234u);
    CHECK(LibdmaBindings::PutEnvCalls() >= 9);
}

void TestMemclr() {
    Rig rig;
    CpuContext ctx{};
    uint8_t* p = rig.ram.Pointer(0x00500000u, 0x100);
    std::memset(p, 0xAB, 0x100);
    // 0x13 bytes: exactly n bytes zeroed, both neighbours intact.
    SetArgs(ctx, 0x00500010u, 0x13);
    rig.libdma.Memclr(ctx);
    CHECK(V0(ctx) == 0);
    CHECK(p[0x0F] == 0xAB && AllZero(p + 0x10, 0x13) && p[0x23] == 0xAB);
    CHECK(rig.libdma.DroppedStores() == 0);
    // n = 0: nothing.
    SetArgs(ctx, 0x00500030u, 0);
    rig.libdma.Memclr(ctx);
    CHECK(p[0x30] == 0xAB);
    // KSEG0 mirror addresses reach the same bytes.
    SetArgs(ctx, 0x80500040u, 0x10);
    rig.libdma.Memclr(ctx);
    CHECK(AllZero(p + 0x40, 0x10) && p[0x50] == 0xAB);
    // Scratchpad destinations land in the scratchpad (not RAM).
    std::memset(rig.spad.Data(), 0xCD, Scratchpad::kSize);
    std::vector<uint8_t> ram_before(rig.ram.Data(), rig.ram.Data() + MainMemory::kSize);
    SetArgs(ctx, 0x70000200u, 0x28);
    rig.libdma.Memclr(ctx);
    CHECK(V0(ctx) == 0);
    CHECK(rig.spad.Data()[0x1FF] == 0xCD && AllZero(rig.spad.Data() + 0x200, 0x28) && rig.spad.Data()[0x228] == 0xCD);
    CHECK(std::memcmp(ram_before.data(), rig.ram.Data(), MainMemory::kSize) == 0);
    CHECK(rig.libdma.DroppedStores() == 0);
    // The window's last 8 bytes land (offset 0x3FF8..0x3FFF), nothing dropped.
    SetArgs(ctx, 0x70003FF8u, 8);
    rig.libdma.Memclr(ctx);
    CHECK(AllZero(rig.spad.Data() + 0x3FF8, 8) && rig.libdma.DroppedStores() == 0);
    // A destination outside RAM and scratchpad: dropped and counted, RAM + SPAD untouched.
    std::vector<uint8_t> spad_before(rig.spad.Data(), rig.spad.Data() + Scratchpad::kSize);
    ram_before.assign(rig.ram.Data(), rig.ram.Data() + MainMemory::kSize);
    SetArgs(ctx, 0x12000000u, 0x30);   // a GS privileged-register address: neither RAM nor SPAD
    rig.libdma.Memclr(ctx);
    CHECK(V0(ctx) == 0 && rig.libdma.DroppedStores() == 0x30);
    CHECK(std::memcmp(ram_before.data(), rig.ram.Data(), MainMemory::kSize) == 0);
    CHECK(std::memcmp(spad_before.data(), rig.spad.Data(), Scratchpad::kSize) == 0);
    // A span running off the top of RAM: in-range bytes land, the rest are dropped and counted.
    const uint32_t top = MainMemory::kSize;
    uint8_t* t = rig.ram.Pointer(top - 0x20, 0x20);
    std::memset(t, 0xAB, 0x20);
    SetArgs(ctx, top - 0x18, 0x30);
    rig.libdma.Memclr(ctx);
    CHECK(V0(ctx) == 0 && t[0x07] == 0xAB && AllZero(t + 0x08, 0x18));
    CHECK(rig.libdma.DroppedStores() == 0x30 + 0x18);
}

void TestBind() {
    Rig rig;
    CpuContext ctx{};
    SdkBindingManifest& m = SdkBindingManifest::Instance();
    m.ResetForTest();
    CHECK(LibdmaBindings::Bind(m, &rig.libdma) == 6);
    size_t n = 0;
    const char* const* names = LibdmaBindings::RowNames(&n);
    CHECK(n == 6);
    const char* want[6] = {"memclr", "sceDmaGetChan", "sceDmaReset", "sceDmaPutEnv", "sceDmaSync", "sceGifSyncDma"};
    for (size_t i = 0; i < 6 && i < n; ++i) CHECK(std::string(names[i]) == want[i]);
    for (const char* w : want) {
        const SdkBindingManifest::EngineRow* row = m.Find("libdma.a", w);
        CHECK(row != nullptr);
        if (row) CHECK(row->kind == SdkBindingManifest::Kind::SdkBind && row->handler != nullptr);
    }
    CHECK(m.EngineRows().size() == 6);
    // The thunks dispatch to the bound instance.
    const SdkBindingManifest::EngineRow* get_chan = m.Find("libdma.a", "sceDmaGetChan");
    SetArgs(ctx, 2);
    if (get_chan) get_chan->handler(ctx);
    CHECK(V0(ctx) == static_cast<int64_t>(0x1000A000));
    const SdkBindingManifest::EngineRow* memclr = m.Find("libdma.a", "memclr");
    uint8_t* p = rig.ram.Pointer(0x00700000u, 0x20);
    std::memset(p, 0xEE, 0x20);
    SetArgs(ctx, 0x00700008u, 0x10);
    if (memclr) memclr->handler(ctx);
    CHECK(V0(ctx) == 0 && p[7] == 0xEE && AllZero(p + 8, 0x10) && p[0x18] == 0xEE);
    const SdkBindingManifest::EngineRow* reset = m.Find("libdma.a", "sceDmaReset");
    FillChannel(rig.dmac, 0, 0x5000u);
    SetArgs(ctx, 1);
    if (reset) reset->handler(ctx);
    CHECK(V0(ctx) == 0 && ChannelIs(rig.dmac.channel(0), 0, 0, 0x5020u, 0, 0, 0, 0));
    const SdkBindingManifest::EngineRow* put_env = m.Find("libdma.a", "sceDmaPutEnv");
    WriteEnv(rig.ram, 0x00700100u, 0, 0, 0, 0, 0x0001, 0x0003, 0, 0, 0, 0);
    SetArgs(ctx, 0x00700100u);
    if (put_env) put_env->handler(ctx);
    CHECK(V0(ctx) == 0 && rig.dmac.globals().pcr == 0x00010003u);
    // A dropped Memclr through the thunk counts on the instance.
    SetArgs(ctx, 0x12000000u, 4);
    if (memclr) memclr->handler(ctx);
    CHECK(rig.libdma.DroppedStores() == 4);
    m.ResetForTest();
}

void TestSyncSeams() {
    // A parked VIF1 chain (CNT UNPACK -> qw 1, END) flushes at sceDmaSync / sceGifSyncDma.
    for (int seam = 0; seam < 2; ++seam) {
        Rig rig;
        CpuContext ctx{};
        uint8_t* p = rig.ram.Pointer(0x4000, 0x40);
        const uint64_t cnt = 2ull | (1ull << 28);
        std::memcpy(p, &cnt, 8);
        std::memset(p + 8, 0, 8);
        const uint32_t unpack[8] = {(0x6Cu << 24) | (1u << 16) | 1u, 0x1100u, 0x1101u, 0x1102u, 0x1103u, 0, 0, 0};
        std::memcpy(p + 0x10, unpack, 32);
        const uint64_t end = 7ull << 28;
        std::memcpy(p + 0x30, &end, 8);
        std::memset(p + 0x38, 0, 8);
        rig.dmac.channel(1).tadr = 0x4000;
        rig.dmac.Write(DmacAddr::kVif1, 4, DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift));
        CHECK(rig.dmac.HasDeferredVif1());
        SetArgs(ctx, 0);
        if (seam == 0) rig.libdma.SceDmaSync(ctx);
        else rig.libdma.SceGifSyncDma(ctx);
        CHECK(V0(ctx) == 0 && !rig.dmac.HasDeferredVif1());
        uint32_t got[4];
        rig.vu1.ReadDataMem(16, reinterpret_cast<uint8_t*>(got), 16);
        CHECK(got[0] == 0x1100u && got[3] == 0x1103u);
        CHECK((rig.dmac.globals().stat & DmacStat::Cis(1)) != 0 && !rig.arbiter.hasPending());
        // Nothing parked: still 0, no state change.
        std::vector<uint8_t> before, after;
        rig.dmac.SerializeSection(before);
        if (seam == 0) rig.libdma.SceDmaSync(ctx);
        else rig.libdma.SceGifSyncDma(ctx);
        rig.dmac.SerializeSection(after);
        CHECK(V0(ctx) == 0 && before == after);
    }
    // The same seams through their manifest rows (the title glue hle_sceDmaSync /
    // hle_sceGifSyncDma and a title tsv row both reach the handler this way): a
    // parked kick is flushed, v0 = 0, and the per-seam call counters advance.
    {
        SdkBindingManifest& m = SdkBindingManifest::Instance();
        m.ResetForTest();
        const uint64_t sync_before = LibdmaBindings::SyncCalls(), gif_before = LibdmaBindings::GifSyncCalls();
        const char* rows[2] = {"sceDmaSync", "sceGifSyncDma"};
        for (int seam = 0; seam < 2; ++seam) {
            Rig rig;
            CpuContext ctx{};
            CHECK(LibdmaBindings::Bind(m, &rig.libdma) == 6);
            uint8_t* p = rig.ram.Pointer(0x4000, 0x40);
            const uint64_t cnt = 2ull | (1ull << 28);
            std::memcpy(p, &cnt, 8);
            std::memset(p + 8, 0, 8);
            const uint32_t unpack[8] = {(0x6Cu << 24) | (1u << 16) | 3u, 0x3100u, 0x3101u, 0x3102u, 0x3103u, 0, 0, 0};
            std::memcpy(p + 0x10, unpack, 32);
            const uint64_t end = 7ull << 28;
            std::memcpy(p + 0x30, &end, 8);
            std::memset(p + 0x38, 0, 8);
            rig.dmac.channel(1).tadr = 0x4000;
            rig.dmac.Write(DmacAddr::kVif1, 4, DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift));
            CHECK(rig.dmac.HasDeferredVif1());
            const SdkBindingManifest::EngineRow* row = m.Find("libdma.a", rows[seam]);
            CHECK(row != nullptr && row->handler != nullptr);
            SetArgs(ctx, 0);
            if (row && row->handler) row->handler(ctx);
            CHECK(V0(ctx) == 0 && !rig.dmac.HasDeferredVif1());
            CHECK((rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0);
            uint32_t got[4];
            rig.vu1.ReadDataMem(48, reinterpret_cast<uint8_t*>(got), 16);
            CHECK(got[0] == 0x3100u && got[3] == 0x3103u);
            m.ResetForTest();
        }
        CHECK(LibdmaBindings::SyncCalls() == sync_before + 1 && LibdmaBindings::GifSyncCalls() == gif_before + 1);
    }
    // A parked VIF1 kick is also observed by sceDmaReset's VIF1 stores (a
    // guest store to the VIF1 page flushes the parked kick before landing).
    Rig rig;
    CpuContext ctx{};
    uint8_t* p = rig.ram.Pointer(0x4000, 0x40);
    const uint64_t cnt = 2ull | (1ull << 28);
    std::memcpy(p, &cnt, 8);
    std::memset(p + 8, 0, 8);
    const uint32_t unpack[8] = {(0x6Cu << 24) | (1u << 16) | 1u, 0x2100u, 0x2101u, 0x2102u, 0x2103u, 0, 0, 0};
    std::memcpy(p + 0x10, unpack, 32);
    const uint64_t end = 7ull << 28;
    std::memcpy(p + 0x30, &end, 8);
    std::memset(p + 0x38, 0, 8);
    rig.dmac.channel(1).tadr = 0x4000;
    rig.dmac.Write(DmacAddr::kVif1, 4, DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift));
    CHECK(rig.dmac.HasDeferredVif1());
    SetArgs(ctx, 0);
    rig.libdma.SceDmaReset(ctx);
    CHECK(!rig.dmac.HasDeferredVif1());
    uint32_t got[4];
    rig.vu1.ReadDataMem(16, reinterpret_cast<uint8_t*>(got), 16);
    CHECK(got[0] == 0x2100u && got[3] == 0x2103u);
    CHECK(ChannelIs(rig.dmac.channel(1), 0, 0, 0, 0, 0, 0, 0));
}

}  // namespace

int main() {
    TestGetChan();
    TestReset();
    TestPutEnv();
    TestMemclr();
    TestBind();
    TestSyncSeams();
    if (g_fail) {
        std::printf("DmacLibdmaRows: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("DmacLibdmaRows: PASS\n");
    return 0;
}

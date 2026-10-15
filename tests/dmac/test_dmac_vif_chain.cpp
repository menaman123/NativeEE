// DmacVifChain: VIF1 / VIF0 DMAtag chains end to end -- TTE tag words fed
// before the payload (STCYCL in the tag changes the UNPACK write cycle),
// UNPACK V4-32 into VU1 data memory, MSCAL running a tiny micro through
// VuUnit, a REF from the scratchpad, and a DIRECT command whose PATH2 packet
// reaches RegisterOpStream through the arbiter at the flush point.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/dmac/dmac.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_regs.h"
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

struct TestDmacVifChain {
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
    TestDmacVifChain()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter) {}
};
using Rig = TestDmacVifChain;

uint8_t* At(Rig& rig, uint32_t addr, bool spr = false) {
    return spr ? rig.spad.Pointer(Scratchpad::Offset(addr), 16) : rig.ram.Pointer(addr, 16);
}
void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }
void PutQw(uint8_t* p, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
    Put32(p, w0);
    Put32(p + 4, w1);
    Put32(p + 8, w2);
    Put32(p + 12, w3);
}

uint64_t TagLo(uint32_t qwc, uint32_t id, bool irq, uint32_t addr, bool spr) {
    return static_cast<uint64_t>(qwc & 0xFFFFu) | (static_cast<uint64_t>(id & 7u) << 28) |
           (irq ? (1ull << 31) : 0ull) | (static_cast<uint64_t>(addr) << 32) | (spr ? (1ull << 63) : 0ull);
}
void PutTag(Rig& rig, uint32_t at, uint32_t qwc, uint32_t id, uint32_t addr = 0, bool spr = false,
            uint32_t w2 = 0, uint32_t w3 = 0, bool in_spad = false) {
    uint8_t* p = At(rig, at, in_spad);
    Put64(p, TagLo(qwc, id, false, addr, spr));
    Put64(p + 8, static_cast<uint64_t>(w2) | (static_cast<uint64_t>(w3) << 32));
}

// VIFcode helpers.
constexpr uint32_t VifCode(uint32_t cmd, uint32_t num, uint32_t imm) {
    return (cmd << 24) | ((num & 0xFFu) << 16) | (imm & 0xFFFFu);
}
constexpr uint32_t kStcyclCl2Wl1 = VifCode(0x01, 0, 0x0102);  // CL 2, WL 1: skipping writes
constexpr uint32_t kMscal0 = VifCode(0x14, 0, 0);
constexpr uint32_t kDirect2 = VifCode(0x50, 0, 2);
constexpr uint32_t UnpackV4_32(uint32_t num, uint32_t dst) { return VifCode(0x6C, num, dst & 0x3FFu); }

// UNPACK V4-32 num 2 to `dst`: 3 quadwords (code + 8 data words + 3 pad NOPs).
void PutUnpack2(Rig& rig, uint32_t at, uint32_t dst, uint32_t m, bool in_spad = false) {
    PutQw(At(rig, at, in_spad), UnpackV4_32(2, dst), m, m + 1, m + 2);
    PutQw(At(rig, at + 16, in_spad), m + 3, m + 0x10, m + 0x11, m + 0x12);
    PutQw(At(rig, at + 32, in_spad), m + 0x13, 0, 0, 0);
}
bool QwIs(const VuUnit& vu, uint32_t qw, uint32_t m) {
    uint32_t got[4];
    vu.ReadDataMem((qw * 16) & vu.mem_mask(), reinterpret_cast<uint8_t*>(got), 16);
    const bool ok = got[0] == m && got[1] == m + 1 && got[2] == m + 2 && got[3] == m + 3;
    if (!ok) std::printf("  qw %u = %08x %08x %08x %08x, want marker %08x\n", qw, got[0], got[1], got[2], got[3], m);
    return ok;
}
bool QwZero(const VuUnit& vu, uint32_t qw) {
    uint32_t got[4];
    vu.ReadDataMem((qw * 16) & vu.mem_mask(), reinterpret_cast<uint8_t*>(got), 16);
    return got[0] == 0 && got[1] == 0 && got[2] == 0 && got[3] == 0;
}

// Micro: `iaddiu vi1, vi0, imm` with the E bit at 0x00, nop at 0x08.
constexpr uint32_t U_NOP = 0x000002FFu;
constexpr uint32_t L_NOP = 0x8000033Cu;
constexpr uint32_t U_EBIT = 0x40000000u;
uint32_t EncIaddiu(uint32_t it, uint32_t is, uint32_t imm15) {
    return (0x08u << 25) | (((imm15 >> 11) & 0xFu) << 21) | (it << 16) | (is << 11) | (imm15 & 0x7FFu);
}
void PutBundle(VuUnit& vu, uint32_t pc, uint32_t lower, uint32_t upper) {
    uint8_t b[8];
    std::memcpy(b, &lower, 4);
    std::memcpy(b + 4, &upper, 4);
    vu.WriteMicroMem(pc, b, 8);
}
void LoadTinyMicro(VuUnit& vu, uint32_t imm) {
    PutBundle(vu, 0x00, EncIaddiu(1, 0, imm), U_NOP | U_EBIT);
    PutBundle(vu, 0x08, L_NOP, U_NOP);
}

constexpr uint32_t kChain = (DmacChcr::kModChain << DmacChcr::kModShift) | DmacChcr::kDir;
constexpr uint32_t kChainKick = DmacChcr::kStr | kChain;
constexpr uint32_t kChainKickTte = kChainKick | DmacChcr::kTte;

void TestTteUnpackMscal() {
    // CNT(3, TTE words = STCYCL CL2/WL1, NOP) UNPACK 2 qw -> 0x10 ; CNT(1) MSCAL 0 ; END(0).
    for (int tte = 0; tte < 2; ++tte) {
        Rig rig;
        LoadTinyMicro(rig.vu1, 5);
        PutTag(rig, 0x4000, 3, DmacTagId::kCnt, 0, false, kStcyclCl2Wl1, 0);
        PutUnpack2(rig, 0x4010, 0x10, 0x1000);
        PutTag(rig, 0x4040, 1, DmacTagId::kCnt);
        PutQw(At(rig, 0x4050), kMscal0, 0, 0, 0);
        PutTag(rig, 0x4060, 0, DmacTagId::kEnd);
        rig.dmac.channel(1).tadr = 0x4000;
        rig.dmac.Write(DmacAddr::kVif1, 4, tte ? kChainKickTte : kChainKick);
        // Parked: nothing landed, the micro has not run.
        CHECK(rig.dmac.HasDeferredVif1() && QwZero(rig.vu1, 0x10) && rig.vu1.regs().VI[1] == 0);
        // Observe through CHCR: the walk runs, STR reads clear.
        const uint32_t chcr = static_cast<uint32_t>(rig.dmac.Read(DmacAddr::kVif1, 4));
        CHECK((chcr & DmacChcr::kStr) == 0 && !rig.dmac.HasDeferredVif1());
        if (tte) {
            // The tag's STCYCL (CL 2, WL 1) took effect before the UNPACK: skipping writes.
            CHECK(rig.vif.regs(1).cycle == 0x0102u);
            CHECK(QwIs(rig.vu1, 0x10, 0x1000) && QwZero(rig.vu1, 0x11) && QwIs(rig.vu1, 0x12, 0x1010));
        } else {
            CHECK(rig.vif.regs(1).cycle == 0x0101u);
            CHECK(QwIs(rig.vu1, 0x10, 0x1000) && QwIs(rig.vu1, 0x11, 0x1010) && QwZero(rig.vu1, 0x12));
        }
        // MSCAL ran the micro: vi1 = 5, TPC past the E-bit delay slot.
        CHECK(rig.vu1.regs().VI[1] == 5 && rig.vu1.regs().TPC == 0x10u);
        CHECK(rig.vif.IsIdle(1));
        const DmacChannelRegs& r = rig.dmac.channel(1);
        CHECK(r.tadr == 0x4070u && r.qwc == 0 && (rig.dmac.globals().stat & DmacStat::Cis(1)) != 0);
        CHECK(rig.dmac.deferred().TailCount() == 1);
    }
}

void TestVif0InlineAndSprRef() {
    // VIF0: the chain runs at the kick (no park); REF with the SPR bit pulls
    // the UNPACK from the scratchpad; MSCAL runs VU0.
    Rig rig;
    LoadTinyMicro(rig.vu0, 9);
    PutUnpack2(rig, 0x70000100u, 0x20, 0x2000, true);
    PutTag(rig, 0x4000, 3, DmacTagId::kRef, 0x100, true);
    PutTag(rig, 0x4010, 1, DmacTagId::kCnt);
    PutQw(At(rig, 0x4020), kMscal0, 0, 0, 0);
    PutTag(rig, 0x4030, 0, DmacTagId::kEnd);
    rig.dmac.channel(0).tadr = 0x4000;
    rig.dmac.Write(DmacAddr::kVif0, 4, kChainKick);
    CHECK(!rig.dmac.HasDeferredVif1());
    CHECK(QwIs(rig.vu0, 0x20, 0x2000) && QwIs(rig.vu0, 0x21, 0x2010));
    CHECK(rig.vu0.regs().VI[1] == 9);
    CHECK((rig.dmac.channel(0).chcr & DmacChcr::kStr) == 0 && (rig.dmac.globals().stat & DmacStat::Cis(0)) != 0);
    CHECK(rig.dmac.channel(0).madr == 0x4040u);  // MADR mirrors the LAST tag (END qwc 0: tag + 16)
    CHECK(rig.dmac.deferred().TailCount() == 0);      // VIF0 records no tails
    // The same chain read from a scratchpad TADR: tags and CNT data from the scratchpad.
    Rig s;
    LoadTinyMicro(s.vu0, 3);
    PutTag(s, 0x70000200u, 3, DmacTagId::kCnt, 0, false, 0, 0, true);
    PutUnpack2(s, 0x70000210u, 0x30, 0x3000, true);
    PutTag(s, 0x70000240u, 1, DmacTagId::kCnt, 0, false, 0, 0, true);
    PutQw(At(s, 0x70000250u, true), kMscal0, 0, 0, 0);
    PutTag(s, 0x70000260u, 0, DmacTagId::kEnd, 0, false, 0, 0, true);
    s.dmac.channel(0).tadr = 0x70000200u;
    s.dmac.Write(DmacAddr::kVif0, 4, kChainKick);
    CHECK(QwIs(s.vu0, 0x30, 0x3000) && s.vu0.regs().VI[1] == 3);
    CHECK(s.dmac.channel(0).tadr == 0x270u);  // the walk keeps the masked TADR
}

void TestDirectPath2() {
    // VIF1 chain: CNT(3) = DIRECT 2 qw + GIFtag(A+D, NLOOP 1) + one A+D ; END(0).
    Rig rig;
    PutTag(rig, 0x4000, 3, DmacTagId::kCnt);
    PutQw(At(rig, 0x4010), kDirect2, 0, 0, 0);
    Put64(At(rig, 0x4020), 0x8001ull | (0ull << 58) | (1ull << 60));  // NLOOP 1, EOP, PACKED, NREG 1
    Put64(At(rig, 0x4020) + 8, 0xEull);                                 // A+D
    Put64(At(rig, 0x4030), 0x0000000000000DD1ull);
    Put64(At(rig, 0x4030) + 8, 0x47ull);                                // TEST_1
    PutTag(rig, 0x4040, 0, DmacTagId::kEnd);
    rig.dmac.channel(1).tadr = 0x4000;
    rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
    CHECK(rig.dmac.HasDeferredVif1() && !rig.arbiter.hasPending() && rig.ops.Count() == 0);
    // Flush point (MADR read): the walk submits PATH2 and the flush drains it.
    rig.dmac.Read(DmacAddr::kVif1 + DmacRegOffset::kMadr, 4);
    CHECK(!rig.dmac.HasDeferredVif1() && !rig.arbiter.hasPending());
    CHECK(rig.arbiter.DrainedPackets() == 1 && rig.ops.Count() == 3);
    if (rig.ops.Count() == 3) {
        const std::vector<RegisterOp>& o = rig.ops.Ops();
        CHECK(o[0].kind == static_cast<uint8_t>(RegisterOpKind::Tag) && o[0].path == 2);
        CHECK(o[1].kind == static_cast<uint8_t>(RegisterOpKind::Reg) && o[1].reg == 0x47 && o[1].lo == 0xDD1ull);
        CHECK(o[2].kind == static_cast<uint8_t>(RegisterOpKind::Eop) && o[2].path == 2);
    }
    CHECK(rig.vif.IsIdle(1));
    // No Finish op: PATH2 packets do not end a PATH3 pass.
    for (const RegisterOp& op : rig.ops.Ops()) CHECK(op.kind != static_cast<uint8_t>(RegisterOpKind::Finish));

    // Normal mode (MADR/QWC) on VIF1 is not deferred: the DIRECT lands at the kick and drains at once.
    Rig n;
    PutQw(At(n, 0x5000), kDirect2, 0, 0, 0);
    Put64(At(n, 0x5010), 0x8001ull | (1ull << 60));
    Put64(At(n, 0x5010) + 8, 0xEull);
    Put64(At(n, 0x5020), 0x0000000000000DD2ull);
    Put64(At(n, 0x5020) + 8, 0x48ull);
    n.dmac.channel(1).madr = 0x5000;
    n.dmac.channel(1).qwc = 3;
    n.dmac.Write(DmacAddr::kVif1, 4, DmacChcr::kStr | DmacChcr::kDir);
    CHECK(!n.dmac.HasDeferredVif1() && !n.arbiter.hasPending() && n.ops.Count() == 3);
    CHECK(n.ops.Ops()[1].reg == 0x48 && n.dmac.channel(1).madr == 0x5030u && n.dmac.channel(1).qwc == 0);
    CHECK((n.dmac.globals().stat & DmacStat::Cis(1)) != 0);
}

void TestDirectSplitAcrossTags() {
    // A DIRECT whose payload continues in the next CNT tag's data streams
    // through the VIF's DirectStream state between the two spans.
    Rig rig;
    PutTag(rig, 0x4000, 2, DmacTagId::kCnt);
    PutQw(At(rig, 0x4010), kDirect2, 0, 0, 0);
    Put64(At(rig, 0x4020), 0x8001ull | (1ull << 60));
    Put64(At(rig, 0x4020) + 8, 0xEull);
    PutTag(rig, 0x4030, 1, DmacTagId::kCnt);
    Put64(At(rig, 0x4040), 0x0000000000000DD3ull);
    Put64(At(rig, 0x4040) + 8, 0x49ull);
    PutTag(rig, 0x4050, 0, DmacTagId::kEnd);
    rig.dmac.channel(1).tadr = 0x4000;
    rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
    rig.dmac.FlushDeferredVif1();
    CHECK(rig.ops.Count() == 3 && rig.ops.Ops()[1].reg == 0x49 && rig.ops.Ops()[1].lo == 0xDD3ull);
    CHECK(rig.vif.IsIdle(1) && rig.arbiter.DrainedPackets() == 1);
}

}  // namespace

int main() {
    TestTteUnpackMscal();
    TestVif0InlineAndSprRef();
    TestDirectPath2();
    TestDirectSplitAcrossTags();
    if (g_fail) {
        std::printf("DmacVifChain: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("DmacVifChain: PASS\n");
    return 0;
}

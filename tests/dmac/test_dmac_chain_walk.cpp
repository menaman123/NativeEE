// DmacChainWalk: synthetic DMAtag chains in a 32 MB RAM + scratchpad fixture
// through the VIF0/VIF1 and GIF channel bodies -- every tag id, the ASR
// stack, the two terminator rules, TTE, IRQ+TIE, QWC 0, SPR-bit sources,
// kMaxTags, VIF normal mode + early returns, the GIF chain as ONE PATH3
// submit into RegisterOpStream, and GIF normal mode.
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

struct TestDmacChainWalk {
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
    TestDmacChainWalk()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter) {}
};
using Rig = TestDmacChainWalk;

uint8_t* At(Rig& rig, uint32_t addr, bool spr = false) {
    return spr ? rig.spad.Pointer(Scratchpad::Offset(addr), 16) : rig.ram.Pointer(addr, 16);
}
void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

uint64_t TagLo(uint32_t qwc, uint32_t id, bool irq, uint32_t addr, bool spr) {
    return static_cast<uint64_t>(qwc & 0xFFFFu) | (static_cast<uint64_t>(id & 7u) << 28) |
           (irq ? (1ull << 31) : 0ull) | (static_cast<uint64_t>(addr) << 32) | (spr ? (1ull << 63) : 0ull);
}
void PutTag(Rig& rig, uint32_t at, uint32_t qwc, uint32_t id, bool irq = false, uint32_t addr = 0,
            bool spr = false, uint32_t w2 = 0, uint32_t w3 = 0, bool in_spad = false) {
    uint8_t* p = At(rig, at, in_spad);
    Put64(p, TagLo(qwc, id, irq, addr, spr));
    Put64(p + 8, static_cast<uint64_t>(w2) | (static_cast<uint64_t>(w3) << 32));
}
// UNPACK V4-32 of one quadword to VU data qw `dst`; 2 quadwords of stream (code + 4 words, zero pad).
void PutUnpack(Rig& rig, uint32_t at, uint32_t dst, uint32_t m, bool in_spad = false) {
    uint8_t* p = At(rig, at, in_spad);
    Put32(p, (0x6Cu << 24) | (1u << 16) | (dst & 0x3FFu));
    Put32(p + 4, m);
    Put32(p + 8, m + 1);
    Put32(p + 12, m + 2);
    p = At(rig, at + 16, in_spad);
    Put32(p, m + 3);
    Put32(p + 4, 0);
    Put32(p + 8, 0);
    Put32(p + 12, 0);
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

constexpr uint32_t kChain = (DmacChcr::kModChain << DmacChcr::kModShift) | DmacChcr::kDir;
constexpr uint32_t kChainKick = DmacChcr::kStr | kChain;
constexpr uint32_t kVif0Chcr = DmacAddr::kVif0;
constexpr uint32_t kVif1Chcr = DmacAddr::kVif1;

// Kick VIF1 in chain mode and observe it through a CHCR read (the flush point).
void KickVif1(Rig& rig, uint32_t tadr, uint32_t chcr = kChainKick) {
    rig.dmac.channel(1).tadr = tadr;
    rig.dmac.Write(kVif1Chcr, 4, chcr);
    rig.dmac.Read(kVif1Chcr, 4);
}
void KickVif0(Rig& rig, uint32_t tadr, uint32_t chcr = kChainKick) {
    rig.dmac.channel(0).tadr = tadr;
    rig.dmac.Write(kVif0Chcr, 4, chcr);
}

void TestCntEnd() {
    Rig rig;
    // CNT(2) UNPACK->qw1, CNT(2) UNPACK->qw2, END(0) at 0x4060.
    PutTag(rig, 0x4000, 2, DmacTagId::kCnt);
    PutUnpack(rig, 0x4010, 1, 0x1100);
    PutTag(rig, 0x4030, 2, DmacTagId::kCnt);
    PutUnpack(rig, 0x4040, 2, 0x2200);
    PutTag(rig, 0x4060, 0, DmacTagId::kEnd, false, 0x77770, true);  // upper half mirrored into CHCR.TAG
    KickVif1(rig, 0x4000);
    CHECK(QwIs(rig.vu1, 1, 0x1100) && QwIs(rig.vu1, 2, 0x2200));
    const DmacChannelRegs& r = rig.dmac.channel(1);
    CHECK(r.tadr == 0x4070u);
    CHECK(r.madr == 0x4070u);  // END qwc 0: src = tag + 16, nothing added
    CHECK(r.qwc == 0);
    CHECK((r.chcr & DmacChcr::kStr) == 0);
    CHECK((r.chcr & DmacChcr::kTagMask) == (static_cast<uint32_t>(TagLo(0, 7, false, 0x77770, true)) & 0xFFFF0000u));
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(1)) != 0);
    // The END qwc 0 slot was recorded for the tail chase.
    CHECK(rig.dmac.deferred().TailCount() == 1 && rig.dmac.deferred().Tails()[0].end_tadr == 0x4060u);
    // Same chain on VIF0 runs inline (no park) and does not record tails.
    Rig rig0;
    PutTag(rig0, 0x4000, 2, DmacTagId::kCnt);
    PutUnpack(rig0, 0x4010, 3, 0x3300);
    PutTag(rig0, 0x4030, 0, DmacTagId::kEnd);
    KickVif0(rig0, 0x4000);
    CHECK(QwIs(rig0.vu0, 3, 0x3300) && (rig0.dmac.globals().stat & DmacStat::Cis(0)) != 0);
    CHECK(rig0.dmac.deferred().TailCount() == 0 && !rig0.dmac.HasDeferredVif1());
}

void TestRefRefsRefe() {
    Rig rig;
    PutUnpack(rig, 0x8000, 4, 0x4400);
    PutUnpack(rig, 0x8100, 5, 0x5500);
    PutUnpack(rig, 0x70000200u, 6, 0x6600, true);  // scratchpad data
    PutUnpack(rig, 0x8200, 7, 0x7700);
    PutTag(rig, 0x4000, 2, DmacTagId::kRef, false, 0x8000);
    PutTag(rig, 0x4010, 2, DmacTagId::kRefs, false, 0x8100);
    PutTag(rig, 0x4020, 2, DmacTagId::kRef, false, 0x200, true);  // SPR bit -> scratchpad
    PutTag(rig, 0x4030, 2, DmacTagId::kRefe, false, 0x8200);
    PutTag(rig, 0x4040, 2, DmacTagId::kCnt);  // never reached: REFE ended the chain
    PutUnpack(rig, 0x4050, 8, 0x8800);
    KickVif1(rig, 0x4000);
    CHECK(QwIs(rig.vu1, 4, 0x4400) && QwIs(rig.vu1, 5, 0x5500) && QwIs(rig.vu1, 6, 0x6600) && QwIs(rig.vu1, 7, 0x7700));
    CHECK(QwZero(rig.vu1, 8));
    const DmacChannelRegs& r = rig.dmac.channel(1);
    CHECK(r.tadr == 0x4040u && r.madr == 0x8200u + 32 && r.qwc == 0);
    CHECK(rig.dmac.deferred().TailCount() == 0);  // ended on REFE, not END
}

void TestNext() {
    Rig rig;
    // NEXT qwc 0 -> 0x5000; NEXT qwc 2 with data -> scratchpad tag; there: END qwc 2 with data.
    PutTag(rig, 0x4000, 0, DmacTagId::kNext, false, 0x5000);
    PutUnpack(rig, 0x4010, 1, 0x1100);  // not transferred: NEXT with qwc 0
    PutTag(rig, 0x5000, 2, DmacTagId::kNext, false, 0x300, true);
    PutUnpack(rig, 0x5010, 2, 0x2200);
    PutTag(rig, 0x70000300u, 2, DmacTagId::kEnd, false, 0, false, 0, 0, true);
    PutUnpack(rig, 0x70000310u, 3, 0x3300, true);
    KickVif1(rig, 0x4000);
    CHECK(QwZero(rig.vu1, 1) && QwIs(rig.vu1, 2, 0x2200) && QwIs(rig.vu1, 3, 0x3300));
    const DmacChannelRegs& r = rig.dmac.channel(1);
    CHECK(r.tadr == 0x330u);  // END: tag + 16 + data, in the masked scratchpad space
    CHECK(r.madr == 0x310u + 32);
    CHECK(rig.dmac.deferred().TailCount() == 0);  // END with payload is not a terminator slot
}

void TestCallRet() {
    Rig rig;
    // main: CALL(2)->sub1 [data qw1], then after return: CNT(2) qw6, END.
    PutTag(rig, 0x4000, 2, DmacTagId::kCall, false, 0x5000);
    PutUnpack(rig, 0x4010, 1, 0x1100);
    PutTag(rig, 0x4030, 2, DmacTagId::kCnt);
    PutUnpack(rig, 0x4040, 6, 0x6600);
    PutTag(rig, 0x4060, 0, DmacTagId::kEnd);
    // sub1: CALL(2)->sub2 [qw2], CALL(2)->sub3 at ASP 2 (no push, still jumps) [qw3]
    PutTag(rig, 0x5000, 2, DmacTagId::kCall, false, 0x6000);
    PutUnpack(rig, 0x5010, 2, 0x2200);
    PutTag(rig, 0x5030, 2, DmacTagId::kCall, false, 0x7000);
    PutUnpack(rig, 0x5040, 3, 0x3300);
    // sub2: RET(2) [qw4] -> back to sub1 + 0x30
    PutTag(rig, 0x6000, 2, DmacTagId::kRet);
    PutUnpack(rig, 0x6010, 4, 0x4400);
    // sub3: RET(2) [qw5] -> pops ASR1 (sub1's second CALL pushed it) -> 0x5060.
    PutTag(rig, 0x7000, 2, DmacTagId::kRet);
    PutUnpack(rig, 0x7010, 5, 0x5500);
    KickVif1(rig, 0x4000);
    // Walk: main CALL (ASR0 = 0x4030, ASP 1) -> sub1 CALL (ASR1 = 0x5030, ASP 2) -> sub2 RET (ASP 1, -> 0x5030)
    // -> sub1 CALL #2 (ASR1 = 0x5060, ASP 2) -> sub3 RET (ASP 1, -> 0x5060) -> 0x5060 is unwritten (zeros):
    // a REFE qwc 0 addr 0 -> ends. So qw6 is NOT reached; pin that grammar exactly.
    CHECK(QwIs(rig.vu1, 1, 0x1100) && QwIs(rig.vu1, 2, 0x2200) && QwIs(rig.vu1, 4, 0x4400));
    CHECK(QwIs(rig.vu1, 3, 0x3300) && QwIs(rig.vu1, 5, 0x5500));
    CHECK(QwZero(rig.vu1, 6));
    const DmacChannelRegs& r = rig.dmac.channel(1);
    CHECK(r.asr0 == 0x4030u && r.asr1 == 0x5060u);
    CHECK(DmacChcr::Asp(r.chcr) == 1);
    CHECK(r.tadr == 0x5070u);  // REFE at 0x5060: next = tag + 16

    // Depth-2 overflow: a third nested CALL leaves ASP 2 and both ASRs unchanged, and still jumps.
    Rig rig2;
    PutTag(rig2, 0x4000, 0, DmacTagId::kCall, false, 0x5000);
    PutTag(rig2, 0x5000, 0, DmacTagId::kCall, false, 0x6000);
    PutTag(rig2, 0x6000, 0, DmacTagId::kCall, false, 0x7000);
    PutTag(rig2, 0x7000, 2, DmacTagId::kEnd);
    PutUnpack(rig2, 0x7010, 9, 0x9900);
    KickVif1(rig2, 0x4000);
    CHECK(QwIs(rig2.vu1, 9, 0x9900));
    CHECK(rig2.dmac.channel(1).asr0 == 0x4010u && rig2.dmac.channel(1).asr1 == 0x5010u);
    CHECK(DmacChcr::Asp(rig2.dmac.channel(1).chcr) == 2);
    // RET at ASP 0 ends the chain after its data; the next tag is not read.
    Rig rig3;
    PutTag(rig3, 0x4000, 2, DmacTagId::kRet);
    PutUnpack(rig3, 0x4010, 1, 0x1100);
    PutTag(rig3, 0x4030, 2, DmacTagId::kCnt);
    PutUnpack(rig3, 0x4040, 2, 0x2200);
    KickVif1(rig3, 0x4000);
    CHECK(QwIs(rig3.vu1, 1, 0x1100) && QwZero(rig3.vu1, 2));
    CHECK(rig3.dmac.channel(1).tadr == 0x4000u);  // next stays at the RET tag
}

void TestIrqTie() {
    Rig rig;
    PutTag(rig, 0x4000, 2, DmacTagId::kCnt, true);  // IRQ without TIE: continues
    PutUnpack(rig, 0x4010, 1, 0x1100);
    PutTag(rig, 0x4030, 2, DmacTagId::kCnt);
    PutUnpack(rig, 0x4040, 2, 0x2200);
    PutTag(rig, 0x4060, 0, DmacTagId::kEnd);
    KickVif1(rig, 0x4000);
    CHECK(QwIs(rig.vu1, 1, 0x1100) && QwIs(rig.vu1, 2, 0x2200));
    Rig rig2;
    PutTag(rig2, 0x4000, 2, DmacTagId::kCnt, true);  // IRQ + CHCR.TIE: ends after this tag's data
    PutUnpack(rig2, 0x4010, 1, 0x1100);
    PutTag(rig2, 0x4030, 2, DmacTagId::kCnt);
    PutUnpack(rig2, 0x4040, 2, 0x2200);
    KickVif1(rig2, 0x4000, kChainKick | DmacChcr::kTie);
    CHECK(QwIs(rig2.vu1, 1, 0x1100) && QwZero(rig2.vu1, 2));
    CHECK(rig2.dmac.channel(1).tadr == 0x4030u);
    CHECK((rig2.dmac.globals().stat & DmacStat::Cis(1)) != 0);
}

void TestTerminators() {
    // VIF rule: both halves all-ones end the walk (STR clear + CIS).
    Rig rig;
    PutTag(rig, 0x4000, 2, DmacTagId::kCnt);
    PutUnpack(rig, 0x4010, 1, 0x1100);
    Put64(At(rig, 0x4030), ~0ull);
    Put64(At(rig, 0x4030) + 8, ~0ull);
    KickVif1(rig, 0x4000);
    CHECK(QwIs(rig.vu1, 1, 0x1100));
    CHECK((rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0 && (rig.dmac.globals().stat & DmacStat::Cis(1)) != 0);
    CHECK(rig.dmac.channel(1).tadr == 0x4030u);  // unchanged from the previous tag's next
    CHECK(rig.dmac.deferred().TailCount() == 0);
    // lo all-ones with hi 0 is DECODED by the VIF walk: END, qwc 0xFFFF, irq. Placed so the
    // 0xFFFF0-byte span overruns the end of RAM -> nothing fed, MADR left as set by the tag.
    Rig rig2;
    const uint32_t slot = MainMemory::kSize - 0x100000u + 0x100u;
    Put64(At(rig2, slot), ~0ull);
    Put64(At(rig2, slot) + 8, 0);
    KickVif1(rig2, slot);
    CHECK(rig2.dmac.channel(1).madr == slot + 16);
    CHECK(rig2.dmac.channel(1).qwc == 0);  // CompleteChannel zeroes it after the walk left 0xFFFF
    CHECK(rig2.dmac.channel(1).tadr == slot + 16 + 0xFFFFu * 16);
    CHECK((rig2.dmac.globals().stat & DmacStat::Cis(1)) != 0);
    CHECK((rig2.dmac.channel(1).chcr & DmacChcr::kTagMask) == 0xFFFF0000u);
    // GIF rule: lo all-ones OR qwc 0xFFFF ends WITHOUT decoding (no CHCR.TAG mirror, TADR untouched).
    Rig rig3;
    Put64(At(rig3, 0x4000), ~0ull);
    Put64(At(rig3, 0x4000) + 8, 0);
    rig3.dmac.channel(2).tadr = 0x4000;
    rig3.dmac.Write(DmacAddr::kGif, 4, kChainKick);
    CHECK(rig3.dmac.channel(2).tadr == 0x4000u && (rig3.dmac.channel(2).chcr & DmacChcr::kTagMask) == 0);
    CHECK((rig3.dmac.globals().stat & DmacStat::Cis(2)) != 0);
    PutTag(rig3, 0x4100, 0xFFFF, DmacTagId::kCnt, false, 0x1234);
    rig3.dmac.channel(2).tadr = 0x4100;
    rig3.dmac.Write(DmacAddr::kGif, 4, kChainKick);
    CHECK(rig3.dmac.channel(2).tadr == 0x4100u && (rig3.dmac.channel(2).chcr & DmacChcr::kTagMask) == 0);
    CHECK(!rig3.arbiter.hasPending());
}

void TestMaxTags() {
    Rig rig;
    // A NEXT that jumps to itself with qwc 0 walks exactly kMaxTags tags.
    PutTag(rig, 0x4000, 0, DmacTagId::kNext, false, 0x4000);
    DmacChannelRegs regs;
    regs.chcr = kChainKick;
    regs.tadr = 0x4000;
    const DmacVifWalkResult res = rig.dmac.walker().WalkVifChain(1, regs);
    CHECK(res.completed && res.tag_count == ChainWalker::kMaxTags);
    CHECK(res.last_id == DmacTagId::kNext && res.last_tag_tadr == 0x4000u);
    // Through the DMAC the overflow completes like any other chain.
    KickVif1(rig, 0x4000);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(1)) != 0 && (rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0);
}

void TestLongChainReachesEnd() {
    // A real display list is longer than any round-number guard: 12000 CNT tags
    // (qwc 0, each the next quadword) then END. The console walks to the END; so
    // must the walk -- the guard is the RAM + scratchpad quadword count, where a
    // longer walk has necessarily re-read a slot (a loop).
    Rig rig;
    constexpr int kTags = 12000;
    const uint32_t base = 0x00400000u;
    for (int i = 0; i < kTags; ++i) PutTag(rig, base + 16u * static_cast<uint32_t>(i), 0, DmacTagId::kCnt);
    PutTag(rig, base + 16u * kTags, 0, DmacTagId::kEnd);
    DmacChannelRegs regs;
    regs.chcr = kChainKick;
    regs.tadr = base;
    const DmacVifWalkResult res = rig.dmac.walker().WalkVifChain(1, regs);
    CHECK(res.completed && res.tag_count == kTags + 1 && res.last_id == DmacTagId::kEnd);
    CHECK(res.last_tag_tadr == base + 16u * kTags);
    CHECK(ChainWalker::kMaxTags == (MainMemory::kSize + Scratchpad::kSize) / 16);
}

void TestTte() {
    // TTE: the tag's upper two words are fed as one quadword before the data.
    // MARK 0x1234 in word 2 (NOP in word 3) changes VIF1_MARK only with TTE.
    Rig rig;
    PutTag(rig, 0x4000, 2, DmacTagId::kCnt, false, 0, false, (0x07u << 24) | 0x1234u, 0);
    PutUnpack(rig, 0x4010, 1, 0x1100);
    PutTag(rig, 0x4030, 0, DmacTagId::kEnd, false, 0, false, (0x07u << 24) | 0x5678u, 0);
    KickVif1(rig, 0x4000, kChainKick | DmacChcr::kTte);
    CHECK(rig.vif.regs(1).mark == 0x5678u && QwIs(rig.vu1, 1, 0x1100));
    Rig rig2;
    PutTag(rig2, 0x4000, 2, DmacTagId::kCnt, false, 0, false, (0x07u << 24) | 0x1234u, 0);
    PutUnpack(rig2, 0x4010, 1, 0x1100);
    PutTag(rig2, 0x4030, 0, DmacTagId::kEnd, false, 0, false, (0x07u << 24) | 0x5678u, 0);
    KickVif1(rig2, 0x4000);
    CHECK(rig2.vif.regs(1).mark == 0 && QwIs(rig2.vu1, 1, 0x1100));
    // TTE bracket leaves the unit idle (the pad skip consumed the zero half).
    CHECK(rig.vif.IsIdle(1));
}

void TestVifNormalAndEarlyReturns() {
    Rig rig;
    // VIF0 normal: qwc * 16 bytes from MADR (25-bit window), MADR advanced, QWC 0, CIS.
    PutUnpack(rig, 0x9000, 2, 0x2200);
    rig.dmac.channel(0).madr = 0x9000;
    rig.dmac.channel(0).qwc = 2;
    rig.dmac.Write(kVif0Chcr, 4, DmacChcr::kStr | DmacChcr::kDir);
    CHECK(QwIs(rig.vu0, 2, 0x2200));
    CHECK(rig.dmac.channel(0).madr == 0x9020u && rig.dmac.channel(0).qwc == 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(0)) != 0);
    // A scratchpad-prefixed MADR in normal mode reads MAIN RAM at the masked address.
    PutUnpack(rig, 0xA000, 3, 0x3300);
    rig.dmac.channel(1).madr = 0x7000A000u;
    rig.dmac.channel(1).qwc = 2;
    rig.dmac.Write(kVif1Chcr, 4, DmacChcr::kStr | DmacChcr::kDir);  // mode 0 is not deferred
    CHECK(QwIs(rig.vu1, 3, 0x3300) && !rig.dmac.HasDeferredVif1());
    CHECK(rig.dmac.channel(1).madr == 0x7000A020u);
    // Normal with QWC 0: nothing fed, still completed.
    rig.dmac.globals().stat = 0;
    rig.dmac.channel(0).qwc = 0;
    rig.dmac.Write(kVif0Chcr, 4, DmacChcr::kStr);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(0)) != 0);
    // Interleave mode: completed, no transfer.
    rig.dmac.globals().stat = 0;
    rig.dmac.channel(0).madr = 0x9000;
    rig.dmac.channel(0).qwc = 2;
    rig.dmac.Write(kVif0Chcr, 4, DmacChcr::kStr | (2u << DmacChcr::kModShift));
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(0)) != 0 && rig.dmac.channel(0).qwc == 0);
    CHECK(rig.dmac.channel(0).madr == 0x9000u);
    // VIF1 chain with TADR 0: STR cleared, NO interrupt status.
    rig.dmac.globals().stat = 0;
    KickVif1(rig, 0);
    CHECK((rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(1)) == 0);
    // VIF1 FDR set: no feed, STR clear, no CIS (walked through the flush).
    Rig rig2;
    PutTag(rig2, 0x4000, 2, DmacTagId::kCnt);
    PutUnpack(rig2, 0x4010, 1, 0x1100);
    PutTag(rig2, 0x4030, 0, DmacTagId::kEnd);
    rig2.vif.regs(1).stat |= VifStat::kFdr;
    KickVif1(rig2, 0x4000);
    CHECK(QwZero(rig2.vu1, 1) && (rig2.dmac.globals().stat & DmacStat::Cis(1)) == 0);
    CHECK((rig2.dmac.channel(1).chcr & DmacChcr::kStr) == 0);
}

// A PACKED A+D packet of `n` register writes: GIFtag then n quadwords {value, reg}.
void PutAdTag(uint8_t* p, uint32_t nloop) {
    Put64(p, static_cast<uint64_t>(nloop & 0x7FFFu) | (1ull << 15) | (1ull << 60));  // EOP, NREG 1
    Put64(p + 8, 0xEull);                                                            // REGS[0] = A+D
}
void PutAd(uint8_t* p, uint64_t value, uint32_t reg) {
    Put64(p, value);
    Put64(p + 8, reg);
}

void TestGifChain() {
    Rig rig;
    // CNT(2): GIFtag NLOOP 3 + first A+D qw; REF(2) -> two more A+D qws at 0x8000; END(0).
    PutTag(rig, 0x4000, 2, DmacTagId::kCnt);
    PutAdTag(At(rig, 0x4010), 3);
    PutAd(At(rig, 0x4020), 0x1111ull, 0x47);  // TEST_1
    PutTag(rig, 0x4030, 2, DmacTagId::kRefs, false, 0x8000, true);  // REFS: SPR bit ignored, address routes
    PutAd(At(rig, 0x8000), 0x2222ull, 0x48);
    PutAd(At(rig, 0x8010), 0x3333ull, 0x49);
    PutTag(rig, 0x4040, 0, DmacTagId::kEnd);
    rig.dmac.channel(2).tadr = 0x4000;
    rig.dmac.Write(DmacAddr::kGif, 4, kChainKick);
    // ONE PATH3 submit, not drained by the DMAC.
    CHECK(rig.arbiter.hasPending() && rig.ops.Count() == 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(2)) != 0);
    CHECK((rig.dmac.channel(2).chcr & DmacChcr::kStr) == 0);
    CHECK(rig.dmac.channel(2).tadr == 0x4050u && rig.dmac.channel(2).qwc == 0);
    CHECK(rig.dmac.channel(2).madr == 0x4050u);  // END qwc 0: data address = tag + 16
    rig.arbiter.drain();
    CHECK(rig.arbiter.DrainedPackets() == 1);
    // Reference: the same 64 bytes fed as one PATH3 packet.
    Rig ref;
    std::vector<uint8_t> bytes(64);
    std::memcpy(bytes.data(), At(rig, 0x4010), 32);
    std::memcpy(bytes.data() + 32, At(rig, 0x8000), 32);
    ref.parser.ReceiveData(GifPath::Path3, bytes.data(), bytes.size());
    ref.parser.FinishPath3();
    CHECK(rig.ops.Count() == ref.ops.Count() && rig.ops.Hash() == ref.ops.Hash() && rig.ops.Count() > 0);

    // REF with the SPR bit reads the scratchpad; NEXT routes the next tag by its address prefix.
    Rig rig2;
    PutTag(rig2, 0x4000, 2, DmacTagId::kCnt);
    PutAdTag(At(rig2, 0x4010), 2);
    PutAd(At(rig2, 0x4020), 0x4444ull, 0x47);
    PutTag(rig2, 0x4030, 1, DmacTagId::kRef, false, 0x400, true);
    PutAd(At(rig2, 0x70000400u, true), 0x5555ull, 0x48);
    PutTag(rig2, 0x4040, 0, DmacTagId::kNext, false, 0x70000500u);
    PutTag(rig2, 0x70000500u, 2, DmacTagId::kEnd, false, 0, false, 0, 0, true);
    PutAdTag(At(rig2, 0x70000510u, true), 1);
    PutAd(At(rig2, 0x70000520u, true), 0x6666ull, 0x49);
    rig2.dmac.channel(2).tadr = 0x4000;
    rig2.dmac.Write(DmacAddr::kGif, 4, kChainKick);
    CHECK(rig2.dmac.channel(2).tadr == 0x70000530u);
    rig2.arbiter.drain();
    Rig ref2;
    std::vector<uint8_t> b2(80);
    std::memcpy(b2.data(), At(rig2, 0x4010), 32);
    std::memcpy(b2.data() + 32, At(rig2, 0x70000400u, true), 16);
    std::memcpy(b2.data() + 48, At(rig2, 0x70000510u, true), 32);
    ref2.parser.ReceiveData(GifPath::Path3, b2.data(), b2.size());
    ref2.parser.FinishPath3();
    CHECK(rig2.ops.Count() == ref2.ops.Count() && rig2.ops.Hash() == ref2.ops.Hash() && rig2.ops.Count() > 0);

    // CALL / RET on the GIF walk: ASR push/pop, RET at ASP 0 ends, CALL at ASP 2 still jumps.
    Rig rig3;
    PutTag(rig3, 0x4000, 0, DmacTagId::kCall, false, 0x5000);
    PutTag(rig3, 0x5000, 0, DmacTagId::kCall, false, 0x6000);
    PutTag(rig3, 0x6000, 0, DmacTagId::kCall, false, 0x7000);  // ASP 2: no push
    PutTag(rig3, 0x7000, 0, DmacTagId::kRet);                  // -> ASR1 = 0x5010
    PutTag(rig3, 0x5010, 0, DmacTagId::kRet);                  // -> ASR0 = 0x4010
    PutTag(rig3, 0x4010, 0, DmacTagId::kRet);                  // ASP 0: ends
    PutTag(rig3, 0x4020, 0, DmacTagId::kEnd);
    rig3.dmac.channel(2).tadr = 0x4000;
    rig3.dmac.Write(DmacAddr::kGif, 4, kChainKick);
    CHECK(rig3.dmac.channel(2).asr0 == 0x4010u && rig3.dmac.channel(2).asr1 == 0x5010u);
    CHECK(rig3.dmac.channel(2).tadr == 0x4010u && DmacChcr::Asp(rig3.dmac.channel(2).chcr) == 0);
    CHECK(!rig3.arbiter.hasPending());  // no data: nothing submitted
    CHECK((rig3.dmac.globals().stat & DmacStat::Cis(2)) != 0);
    // A parked VIF1 chain flushes before a GIF kick; the GIF kick itself does not drain.
    Rig rig4;
    PutTag(rig4, 0x3000, 2, DmacTagId::kCnt);
    PutUnpack(rig4, 0x3010, 1, 0x1100);
    PutTag(rig4, 0x3030, 0, DmacTagId::kEnd);
    rig4.dmac.channel(1).tadr = 0x3000;
    rig4.dmac.Write(kVif1Chcr, 4, kChainKick);
    CHECK(rig4.dmac.HasDeferredVif1());
    PutTag(rig4, 0x4000, 2, DmacTagId::kEnd);
    PutAdTag(At(rig4, 0x4010), 1);
    PutAd(At(rig4, 0x4020), 0x7777ull, 0x47);
    rig4.dmac.channel(2).tadr = 0x4000;
    rig4.dmac.Write(DmacAddr::kGif, 4, kChainKick);
    CHECK(!rig4.dmac.HasDeferredVif1() && QwIs(rig4.vu1, 1, 0x1100));
    CHECK(rig4.arbiter.hasPending());
}

void TestGifNormal() {
    Rig rig;
    PutAdTag(At(rig, 0x4000), 1);
    PutAd(At(rig, 0x4010), 0x8888ull, 0x47);
    rig.dmac.channel(2).madr = 0x4000;
    rig.dmac.channel(2).qwc = 2;
    rig.dmac.Write(DmacAddr::kGif, 4, DmacChcr::kStr);
    CHECK(rig.dmac.channel(2).madr == 0x4020u && rig.dmac.channel(2).qwc == 0);
    CHECK((rig.dmac.channel(2).chcr & DmacChcr::kStr) == 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(2)) == 0);  // normal mode never sets CIS
    CHECK(rig.arbiter.hasPending());
    rig.arbiter.drain();
    CHECK(rig.arbiter.DrainedPackets() == 1 && rig.ops.Count() > 0);
    // QWC 0: nothing submitted, STR still cleared by the write path.
    rig.dmac.Write(DmacAddr::kGif, 4, DmacChcr::kStr);
    CHECK(!rig.arbiter.hasPending() && (rig.dmac.channel(2).chcr & DmacChcr::kStr) == 0);
    // Scratchpad-prefixed MADR routes to the scratchpad.
    PutAdTag(At(rig, 0x70000600u, true), 1);
    PutAd(At(rig, 0x70000610u, true), 0x9999ull, 0x48);
    rig.dmac.channel(2).madr = 0x70000600u;
    rig.dmac.channel(2).qwc = 2;
    rig.dmac.Write(DmacAddr::kGif, 4, DmacChcr::kStr);
    rig.arbiter.drain();
    CHECK(rig.arbiter.DrainedPackets() == 2);
}

}  // namespace

int main() {
    TestCntEnd();
    TestRefRefsRefe();
    TestNext();
    TestCallRet();
    TestIrqTie();
    TestTerminators();
    TestMaxTags();
    TestLongChainReachesEnd();
    TestTte();
    TestVifNormalAndEarlyReturns();
    TestGifChain();
    TestGifNormal();
    if (g_fail) {
        std::printf("DmacChainWalk: %d FAIL\n", g_fail);
        return 1;
    }
    std::printf("DmacChainWalk: PASS\n");
    return 0;
}

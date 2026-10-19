// DmacDeferredVif1: the "DMA completes when observed" model of a VIF1 chain
// kick -- park, the game appending to the live chain before the flush, the
// observation-point matrix (and the non-points), kick order under a re-kick,
// the kick-time snapshot, the END-slot tail chase (resume / reject table,
// batch consumption, dedup, termination on a self-loop), the frame-sync
// flush inside ChaseVif1Tail, and the blocking kernel wait that ends the
// append race (no tail recorded, earlier records dropped).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "engine/dmac/dmac.h"
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

struct TestDmacDeferredVif1 {
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
    TestDmacDeferredVif1()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter) {}
};
using Rig = TestDmacDeferredVif1;

uint8_t* At(Rig& rig, uint32_t addr, bool spr = false) {
    return spr ? rig.spad.Pointer(Scratchpad::Offset(addr), 16) : rig.ram.Pointer(addr, 16);
}
void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

uint64_t TagLo(uint32_t qwc, uint32_t id, bool irq, uint32_t addr, bool spr) {
    return static_cast<uint64_t>(qwc & 0xFFFFu) | (static_cast<uint64_t>(id & 7u) << 28) |
           (irq ? (1ull << 31) : 0ull) | (static_cast<uint64_t>(addr) << 32) | (spr ? (1ull << 63) : 0ull);
}
void PutTag(Rig& rig, uint32_t at, uint32_t qwc, uint32_t id, uint32_t addr = 0, bool spr = false,
            bool in_spad = false) {
    uint8_t* p = At(rig, at, in_spad);
    Put64(p, TagLo(qwc, id, false, addr, spr));
    Put64(p + 8, 0);
}
// UNPACK V4-32 of one quadword to VU data qw `dst` (2 quadwords of stream).
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
    return got[0] == m && got[1] == m + 1 && got[2] == m + 2 && got[3] == m + 3;
}
bool QwZero(const VuUnit& vu, uint32_t qw) {
    uint32_t got[4];
    vu.ReadDataMem((qw * 16) & vu.mem_mask(), reinterpret_cast<uint8_t*>(got), 16);
    return got[0] == 0 && got[1] == 0 && got[2] == 0 && got[3] == 0;
}

constexpr uint32_t kChain = (DmacChcr::kModChain << DmacChcr::kModShift) | DmacChcr::kDir;
constexpr uint32_t kChainKick = DmacChcr::kStr | kChain;

// CNT(2) UNPACK -> qw `dst`, END(0) at `at + 0x30`; returns the END slot address.
uint32_t PutSimpleChain(Rig& rig, uint32_t at, uint32_t dst, uint32_t m) {
    PutTag(rig, at, 2, DmacTagId::kCnt);
    PutUnpack(rig, at + 0x10, dst, m);
    PutTag(rig, at + 0x30, 0, DmacTagId::kEnd);
    return at + 0x30;
}
void KickVif1(Rig& rig, uint32_t tadr) {
    rig.dmac.channel(1).tadr = tadr;
    rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
}

void TestParkAndAppend() {
    Rig rig;
    const uint32_t slot = PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    // Parked: STR reads set through the member, nothing landed, no interrupt status, no tail yet.
    CHECK(rig.dmac.HasDeferredVif1() && (rig.dmac.channel(1).chcr & DmacChcr::kStr) != 0);
    CHECK(QwZero(rig.vu1, 1) && (rig.dmac.globals().stat & DmacStat::Cis(1)) == 0);
    CHECK(rig.dmac.deferred().TailCount() == 0 && rig.dmac.deferred().Snapshot().tadr == 0x4000u);
    // The game appends to the live chain: the END slot becomes CNT(2) UNPACK -> qw 2, new END after it.
    PutTag(rig, slot, 2, DmacTagId::kCnt);
    PutUnpack(rig, slot + 0x10, 2, 0x2200);
    PutTag(rig, slot + 0x30, 0, DmacTagId::kEnd);
    // CHCR read = the observation point: the walk runs over the appended chain, STR reads clear.
    const uint32_t chcr = static_cast<uint32_t>(rig.dmac.Read(DmacAddr::kVif1, 4));
    CHECK((chcr & DmacChcr::kStr) == 0 && !rig.dmac.HasDeferredVif1());
    CHECK(QwIs(rig.vu1, 1, 0x1100) && QwIs(rig.vu1, 2, 0x2200));
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(1)) != 0 && rig.dmac.channel(1).qwc == 0);
    CHECK(rig.dmac.channel(1).tadr == slot + 0x40);
    // Only the final END slot is recorded.
    CHECK(rig.dmac.deferred().TailCount() == 1 && rig.dmac.deferred().Tails()[0].end_tadr == slot + 0x30);
    CHECK(rig.dmac.deferred().Tails()[0].end_tag_lo == TagLo(0, DmacTagId::kEnd, false, 0, false));
    // Pre-STR-clear CHCR with the END tag's upper half mirrored into TAG (bits 16-31).
    CHECK(rig.dmac.deferred().Tails()[0].chcr ==
          (kChainKick | (static_cast<uint32_t>(TagLo(0, DmacTagId::kEnd, false, 0, false)) & DmacChcr::kTagMask)));
    // A second read is not a flush (nothing parked) and reads the same value.
    CHECK(static_cast<uint32_t>(rig.dmac.Read(DmacAddr::kVif1, 4)) == chcr);
}

struct ObservationPoint {
    const char* name;
    bool flushes;
    std::function<void(Rig&)> act;
};

void TestObservationPoints() {
    const std::vector<ObservationPoint> points = {
        {"chcr read", true, [](Rig& r) { r.dmac.Read(DmacAddr::kVif1, 4); }},
        {"madr read", true, [](Rig& r) { r.dmac.Read(DmacAddr::kVif1 + DmacRegOffset::kMadr, 4); }},
        {"tadr byte read", true, [](Rig& r) { r.dmac.Read(DmacAddr::kVif1 + DmacRegOffset::kTadr + 1, 1); }},
        {"asr1 read", true, [](Rig& r) { r.dmac.Read(DmacAddr::kVif1 + DmacRegOffset::kAsr1, 4); }},
        {"vif1 page read (not a register)", true, [](Rig& r) { r.dmac.Read(DmacAddr::kVif1 + 0x60, 4); }},
        {"qwc write", true, [](Rig& r) { r.dmac.Write(DmacAddr::kVif1 + DmacRegOffset::kQwc, 4, 7); }},
        {"sadr write", true, [](Rig& r) { r.dmac.Write(DmacAddr::kVif1 + DmacRegOffset::kSadr, 4, 0x40); }},
        {"pcr with CPC(VIF1)", true, [](Rig& r) { r.dmac.Write(DmacAddr::kPcr, 4, 2); }},
        {"gif kick", true,
         [](Rig& r) {
             PutTag(r, 0x6000, 0, DmacTagId::kEnd);
             r.dmac.channel(2).tadr = 0x6000;
             r.dmac.Write(DmacAddr::kGif, 4, kChainKick);
         }},
        {"FlushDeferredVif1()", true, [](Rig& r) { r.dmac.FlushDeferredVif1(); }},
        {"ChaseVif1Tail()", true, [](Rig& r) { r.dmac.ChaseVif1Tail(); }},
        {"ObserveAtKernelWait()", true, [](Rig& r) { r.dmac.ObserveAtKernelWait(); }},
        {"d_stat read", false, [](Rig& r) { r.dmac.Read(DmacAddr::kStat, 4); }},
        {"d_ctrl read", false, [](Rig& r) { r.dmac.Read(DmacAddr::kCtrl, 4); }},
        {"pcr without CPC(VIF1)", false, [](Rig& r) { r.dmac.Write(DmacAddr::kPcr, 4, 0xFFFDu); }},
        {"vif0 chcr read", false, [](Rig& r) { r.dmac.Read(DmacAddr::kVif0, 4); }},
        {"gif chcr read", false, [](Rig& r) { r.dmac.Read(DmacAddr::kGif, 4); }},
        {"spr kick", false, [](Rig& r) { r.dmac.Write(DmacAddr::kSprFrom, 4, DmacChcr::kStr); }},
        {"sif0 kick", false, [](Rig& r) { r.dmac.Write(DmacAddr::kSif0, 4, DmacChcr::kStr); }},
        {"vif0 chain kick", false,
         [](Rig& r) {
             PutSimpleChain(r, 0x7000, 9, 0x9900);
             r.dmac.channel(0).tadr = 0x7000;
             r.dmac.Write(DmacAddr::kVif0, 4, kChainKick);
         }},
        {"d_stat acknowledge", false, [](Rig& r) { r.dmac.Write(DmacAddr::kStat, 4, 0xFFFFu); }},
        {"enablew write (dropped)", false, [](Rig& r) { r.dmac.Write(DmacAddr::kEnablew, 4, 1); }},
    };
    for (const ObservationPoint& p : points) {
        Rig rig;
        PutSimpleChain(rig, 0x4000, 1, 0x1100);
        KickVif1(rig, 0x4000);
        CHECK(rig.dmac.HasDeferredVif1());
        p.act(rig);
        const bool flushed = !rig.dmac.HasDeferredVif1();
        if (flushed != p.flushes) std::printf("  observation point '%s': flushed=%d want %d\n", p.name, flushed, p.flushes);
        CHECK(flushed == p.flushes);
        CHECK(QwIs(rig.vu1, 1, 0x1100) == p.flushes);
        CHECK(((rig.dmac.globals().stat & DmacStat::Cis(1)) != 0) == p.flushes);
        CHECK(((rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0) == p.flushes);
        // The parked kick, if still parked, flushes later in order at the frame sync.
        if (!p.flushes) {
            rig.dmac.ChaseVif1Tail();
            CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 1, 0x1100));
        }
    }
    // A VIF1 register write flushes FIRST, then stores: the written value survives the snapshot restore.
    Rig w;
    PutSimpleChain(w, 0x4000, 1, 0x1100);
    KickVif1(w, 0x4000);
    w.dmac.Write(DmacAddr::kVif1 + DmacRegOffset::kQwc, 4, 7);
    CHECK(!w.dmac.HasDeferredVif1() && w.dmac.channel(1).qwc == 7 && QwIs(w.vu1, 1, 0x1100));
    w.dmac.channel(1).qwc = 0;
    // Writing CHCR without STR while parked flushes (walk done) and stores the value.
    Rig c;
    PutSimpleChain(c, 0x4000, 1, 0x1100);
    KickVif1(c, 0x4000);
    c.dmac.Write(DmacAddr::kVif1, 4, kChain);
    CHECK(!c.dmac.HasDeferredVif1() && QwIs(c.vu1, 1, 0x1100) && c.dmac.channel(1).chcr == kChain);
}

// A blocking kernel wait (WaitSema for the FINISH handler's semaphore) is an
// observation point: the parked VIF1 chain is walked and the PATH3 the game
// kicked AFTER it is delivered behind it, in one call; nothing pending = no-op.
void TestKernelWaitObservation() {
    Rig rig;
    PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    // A one-tag PATH3 chain (END, 1 qword GIFtag with NLOOP 0, EOP) kicked while VIF1 is parked.
    PutTag(rig, 0x6000, 1, DmacTagId::kEnd);
    Put64(At(rig, 0x6010), 0x8000ull);   // GIFtag: NLOOP 0, EOP
    Put64(At(rig, 0x6018), 0);
    rig.dmac.channel(2).tadr = 0x6000;
    // The GIF kick itself is an observation point (PATH2 before PATH3); park a
    // fresh VIF1 kick behind it so the kernel wait has both to deliver.
    rig.dmac.Write(DmacAddr::kGif, 4, kChainKick);
    CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 1, 0x1100));
    const uint64_t drained_before = rig.arbiter.DrainedPackets();
    PutSimpleChain(rig, 0x5000, 2, 0x2200);
    KickVif1(rig, 0x5000);
    CHECK(rig.dmac.HasDeferredVif1() && !QwIs(rig.vu1, 2, 0x2200));
    rig.dmac.ObserveAtKernelWait();
    CHECK(!rig.dmac.HasDeferredVif1());
    CHECK(QwIs(rig.vu1, 2, 0x2200));
    CHECK((rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(1)) != 0);
    CHECK(!rig.arbiter.hasPending());
    CHECK(rig.arbiter.DrainedPackets() >= drained_before);
    // Nothing parked, nothing queued: a no-op (the hot path of every WaitSema).
    const uint64_t drained = rig.arbiter.DrainedPackets();
    rig.dmac.ObserveAtKernelWait();
    CHECK(rig.arbiter.DrainedPackets() == drained && !rig.dmac.HasDeferredVif1());
}

void TestReKickOrder() {
    // Kick A (-> qw 1) parks. The game programs kick B the way a game does --
    // TADR through the register file, then CHCR: the TADR write is itself an
    // observation point (A flushes, then 0x5000 is stored), the CHCR write
    // parks B. A CHCR read then flushes B. Strict kick order, one walk each.
    Rig rig;
    PutSimpleChain(rig, 0x4000, 1, 0x1100);
    PutSimpleChain(rig, 0x5000, 2, 0x2200);
    KickVif1(rig, 0x4000);
    CHECK(QwZero(rig.vu1, 1) && rig.dmac.HasDeferredVif1());
    rig.dmac.Write(DmacAddr::kVif1 + DmacRegOffset::kTadr, 4, 0x5000);
    CHECK(QwIs(rig.vu1, 1, 0x1100) && !rig.dmac.HasDeferredVif1() && rig.dmac.channel(1).tadr == 0x5000u);
    CHECK(rig.dmac.deferred().TailCount() == 1 && rig.dmac.deferred().Tails()[0].end_tadr == 0x4030u);
    rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
    CHECK(QwZero(rig.vu1, 2) && rig.dmac.HasDeferredVif1() && rig.dmac.deferred().Snapshot().tadr == 0x5000u);
    rig.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(QwIs(rig.vu1, 2, 0x2200) && !rig.dmac.HasDeferredVif1());
    CHECK(rig.dmac.deferred().TailCount() == 2);
    CHECK(rig.dmac.deferred().Tails()[0].end_tadr == 0x4030u && rig.dmac.deferred().Tails()[1].end_tadr == 0x5030u);
    // A CHCR kick while parked with the TADR left alone re-walks the SAME chain
    // first (flush restores the kick-time TADR), then parks the new kick over
    // the post-walk registers.
    Rig same;
    PutSimpleChain(same, 0x4000, 1, 0x1100);
    KickVif1(same, 0x4000);
    same.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
    CHECK(QwIs(same.vu1, 1, 0x1100) && same.dmac.HasDeferredVif1());
    CHECK(same.dmac.deferred().Snapshot().tadr == 0x4040u);  // the parked kick starts where the last walk ended
}

void TestSnapshot() {
    // The flush restores the kick-time registers: a TADR changed through the
    // member after the kick (no MMIO, so no flush) is ignored by the walk.
    Rig rig;
    PutSimpleChain(rig, 0x4000, 1, 0x1100);
    PutSimpleChain(rig, 0x5000, 2, 0x2200);
    KickVif1(rig, 0x4000);
    rig.dmac.channel(1).tadr = 0x5000;
    rig.dmac.channel(1).chcr = kChainKick | DmacChcr::kTte;
    rig.dmac.FlushDeferredVif1();
    CHECK(QwIs(rig.vu1, 1, 0x1100) && QwZero(rig.vu1, 2));
    CHECK(rig.dmac.channel(1).tadr == 0x4040u && (rig.dmac.channel(1).chcr & DmacChcr::kTte) == 0);
    // ASR0/ASR1 come back from the snapshot too (a CALL chain pushes over them).
    Rig a;
    PutTag(a, 0x4000, 0, DmacTagId::kCall, 0x4800);
    PutSimpleChain(a, 0x4800, 3, 0x3300);
    a.dmac.channel(1).asr0 = 0xDEAD0;
    KickVif1(a, 0x4000);
    a.dmac.channel(1).asr0 = 0x11110;
    a.dmac.FlushDeferredVif1();
    CHECK(QwIs(a.vu1, 3, 0x3300) && a.dmac.channel(1).asr0 == 0x4010u);
}

void TestTailChase() {
    // Chain ends on END(0) at slot S; flushed; then the game rewrites S.
    Rig rig;
    const uint32_t slot = PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    rig.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(rig.dmac.deferred().TailCount() == 1);
    // Unchanged slot: nothing happens, the record is consumed.
    rig.dmac.ChaseVif1Tail();
    CHECK(rig.dmac.deferred().TailCount() == 0 && QwZero(rig.vu1, 2));
    // Re-run the chain to re-record (dedup: the same slot walked twice is one record).
    KickVif1(rig, 0x4000);
    rig.dmac.Read(DmacAddr::kVif1, 4);
    KickVif1(rig, 0x4000);
    rig.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(rig.dmac.deferred().TailCount() == 1);
    // Sane rewrite: S = CNT(2) UNPACK -> qw 2, new END(0) at S + 0x30 -> the chase resumes from S.
    PutTag(rig, slot, 2, DmacTagId::kCnt);
    PutUnpack(rig, slot + 0x10, 2, 0x2200);
    PutTag(rig, slot + 0x30, 0, DmacTagId::kEnd);
    const uint32_t stat_before = rig.dmac.globals().stat;
    rig.dmac.ChaseVif1Tail();
    CHECK(QwIs(rig.vu1, 2, 0x2200));
    CHECK(rig.dmac.channel(1).tadr == slot + 0x40 && (rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0);
    CHECK(rig.dmac.globals().stat == stat_before);  // CIS(1) was already set; the resume sets it again
    // The record the resumed walk appended (the new END) is dropped with the batch.
    CHECK(rig.dmac.deferred().TailCount() == 0);
    rig.dmac.ChaseVif1Tail();
    CHECK(QwIs(rig.vu1, 2, 0x2200) && rig.dmac.deferred().TailCount() == 0);

    // Reject table: each rewrite of a recorded slot that must NOT resume.
    struct Reject {
        const char* name;
        uint64_t lo;
    };
    const std::vector<Reject> rejects = {
        {"zero", 0ull},
        {"all ones", ~0ull},
        {"qwc 0x4001 CNT", TagLo(0x4001, DmacTagId::kCnt, false, 0, false)},
        {"REF beyond RAM", TagLo(1, DmacTagId::kRef, false, 0x02000000u, false)},
        {"REFE beyond RAM", TagLo(1, DmacTagId::kRefe, false, 0x0FFFFFF0u, false)},
        {"NEXT beyond RAM", TagLo(0, DmacTagId::kNext, false, 0x03000000u, false)},
        {"CALL beyond RAM", TagLo(0, DmacTagId::kCall, false, 0x02000010u, false)},
    };
    for (const Reject& rj : rejects) {
        Rig r;
        const uint32_t s = PutSimpleChain(r, 0x4000, 1, 0x1100);
        KickVif1(r, 0x4000);
        r.dmac.Read(DmacAddr::kVif1, 4);
        PutUnpack(r, s + 0x10, 5, 0x5500);
        Put64(At(r, s), rj.lo);
        const uint32_t tadr = r.dmac.channel(1).tadr;
        r.dmac.ChaseVif1Tail();
        const bool resumed = !QwZero(r.vu1, 5) || r.dmac.channel(1).tadr != tadr;
        if (resumed) std::printf("  reject '%s' resumed\n", rj.name);
        CHECK(!resumed && r.dmac.deferred().TailCount() == 0);
    }
    // Accept table: sane rewrites that resume (RET / END / SPR-prefixed REF / RAM REF).
    struct Accept {
        const char* name;
        uint64_t lo;
        uint32_t marker;
    };
    // `marker` 0 = no data expected (the span is fed but carries only NOPs).
    const std::vector<Accept> accepts = {
        {"qwc 0x4000 CNT", TagLo(0x4000, DmacTagId::kCnt, false, 0, false), 0},
        {"REF into RAM", TagLo(2, DmacTagId::kRef, false, 0x8000, false), 0x6600},
        {"REF spr bit -> scratchpad", TagLo(2, DmacTagId::kRef, false, 0x100, true), 0x6B00},
        // The gate accepts a scratchpad-prefixed address without the SPR bit; the
        // VIF walk then reads RAM at the 25-bit masked address (the walk's rule).
        {"REF 0x7000-prefixed without spr", TagLo(2, DmacTagId::kRef, false, 0x70000100u, false), 0x6A00},
        {"END with payload", TagLo(2, DmacTagId::kEnd, false, 0, false), 0x6C00},
    };
    for (const Accept& ac : accepts) {
        Rig r;
        const uint32_t s = PutSimpleChain(r, 0x4000, 1, 0x1100);
        KickVif1(r, 0x4000);
        r.dmac.Read(DmacAddr::kVif1, 4);
        PutUnpack(r, 0x8000, 6, 0x6600);
        PutUnpack(r, 0x100, 6, 0x6A00);
        PutUnpack(r, 0x70000100u, 6, 0x6B00, true);
        PutUnpack(r, s + 0x10, 6, 0x6C00);
        Put64(At(r, s), ac.lo);
        r.dmac.ChaseVif1Tail();
        if (ac.marker) {
            if (!QwIs(r.vu1, 6, ac.marker)) std::printf("  accept '%s' did not land\n", ac.name);
            CHECK(QwIs(r.vu1, 6, ac.marker));
        }
        CHECK((r.dmac.channel(1).chcr & DmacChcr::kStr) == 0 && r.dmac.deferred().TailCount() == 0);
        CHECK(r.dmac.channel(1).qwc == 0);
    }

    // An END with payload is not recorded; a chain ending on REFE is not recorded.
    Rig e;
    PutTag(e, 0x4000, 2, DmacTagId::kEnd);
    PutUnpack(e, 0x4010, 7, 0x7700);
    KickVif1(e, 0x4000);
    e.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(QwIs(e.vu1, 7, 0x7700) && e.dmac.deferred().TailCount() == 0);
    PutUnpack(e, 0x8000, 8, 0x8800);
    PutTag(e, 0x5000, 2, DmacTagId::kRefe, 0x8000);
    KickVif1(e, 0x5000);
    e.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(QwIs(e.vu1, 8, 0x8800) && e.dmac.deferred().TailCount() == 0);

    // A slot in the scratchpad is re-read from the scratchpad and resumes with the SPR prefix.
    // (Scratchpad offset 0 would read as TADR 0 through the 28-bit mask and take the VIF1 early return.)
    Rig sp;
    PutTag(sp, 0x70000100u, 2, DmacTagId::kCnt, 0, false, true);
    PutUnpack(sp, 0x70000110u, 1, 0x1100, true);
    PutTag(sp, 0x70000130u, 0, DmacTagId::kEnd, 0, false, true);
    KickVif1(sp, 0x70000100u);
    sp.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(QwIs(sp.vu1, 1, 0x1100));
    CHECK(sp.dmac.deferred().TailCount() == 1 && sp.dmac.deferred().Tails()[0].from_spr &&
          sp.dmac.deferred().Tails()[0].end_tadr == 0x130u);
    PutTag(sp, 0x70000130u, 2, DmacTagId::kCnt, 0, false, true);
    PutUnpack(sp, 0x70000140u, 2, 0x2200, true);
    PutTag(sp, 0x70000160u, 0, DmacTagId::kEnd, 0, false, true);
    sp.dmac.ChaseVif1Tail();
    CHECK(QwIs(sp.vu1, 2, 0x2200) && sp.dmac.channel(1).tadr == 0x170u);
    // The scratchpad-offset-0 kick: VIF1 TADR reads 0 -> STR clear, no CIS, nothing fed, no record.
    Rig z;
    PutTag(z, 0x70000000u, 2, DmacTagId::kCnt, 0, false, true);
    PutUnpack(z, 0x70000010u, 1, 0x1100, true);
    PutTag(z, 0x70000030u, 0, DmacTagId::kEnd, 0, false, true);
    KickVif1(z, 0x70000000u);
    z.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(QwZero(z.vu1, 1) && (z.dmac.globals().stat & DmacStat::Cis(1)) == 0 && z.dmac.deferred().TailCount() == 0);
    CHECK((z.dmac.channel(1).chcr & DmacChcr::kStr) == 0 && !z.dmac.HasDeferredVif1());
}

void TestChaseTerminates() {
    // The slot rewritten into a NEXT that points at itself: the resumed walk
    // stops at kMaxTags and completes; the chase returns.
    Rig rig;
    const uint32_t slot = PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    rig.dmac.Read(DmacAddr::kVif1, 4);
    PutTag(rig, slot, 0, DmacTagId::kNext, slot);
    rig.dmac.ChaseVif1Tail();
    CHECK((rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0 && rig.dmac.channel(1).tadr == slot);
    CHECK(rig.dmac.deferred().TailCount() == 0 && !rig.dmac.HasDeferredVif1());
    // A resumed walk that itself parks nothing: the chase never leaves a kick parked.
    CHECK(!rig.dmac.HasDeferredVif1());
}

void TestFrameSyncFlush() {
    // ChaseVif1Tail with a kick parked: flush first (the frame-sync observation
    // point), then the record that flush produced is re-read and consumed.
    Rig rig;
    PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    CHECK(rig.dmac.HasDeferredVif1() && rig.dmac.deferred().TailCount() == 0);
    rig.dmac.ChaseVif1Tail();
    CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 1, 0x1100));
    CHECK(rig.dmac.deferred().TailCount() == 0 && (rig.dmac.globals().stat & DmacStat::Cis(1)) != 0);
    // Nothing parked, no records: a no-op.
    const uint32_t tadr = rig.dmac.channel(1).tadr;
    rig.dmac.ChaseVif1Tail();
    CHECK(rig.dmac.channel(1).tadr == tadr);
    // Flush with nothing parked is a no-op as well.
    rig.dmac.FlushDeferredVif1();
    CHECK(rig.dmac.channel(1).tadr == tadr && !rig.arbiter.hasPending());
    // The flush drains queued PATH3 too: a GIF normal kick leaves PATH3 pending; the
    // next VIF1 flush delivers it.
    Rig g;
    Put64(At(g, 0x6000), 0x8001ull | (1ull << 60));
    Put64(At(g, 0x6000) + 8, 0xEull);
    Put64(At(g, 0x6010), 0x1ull);
    Put64(At(g, 0x6010) + 8, 0x47ull);
    g.dmac.channel(2).madr = 0x6000;
    g.dmac.channel(2).qwc = 2;
    g.dmac.Write(DmacAddr::kGif, 4, DmacChcr::kStr);
    CHECK(g.arbiter.hasPending() && g.ops.Count() == 0);
    PutSimpleChain(g, 0x4000, 1, 0x1100);
    KickVif1(g, 0x4000);
    CHECK(g.arbiter.hasPending());
    g.dmac.FlushDeferredVif1();
    CHECK(!g.arbiter.hasPending() && g.ops.Count() == 4 && g.ops.Ops()[0].path == 3);
}

}  // namespace

// The blocking kernel wait completes the chain for good: the EE slept while the
// DMA ran to END, so the END slot the game rewrites afterwards is a NEW list
// (a second title's rebuilds its double-buffered display list in place once its FINISH
// arrived; the old chase resumed into that half-built list and misparsed the
// next kick). A KernelWait flush records no tail; records of earlier walks go.
void TestKernelWaitEndsAppendRace() {
    // Kicked, flushed at the kernel wait: no record, and a sane rewrite + chase does not resume.
    Rig rig;
    const uint32_t slot = PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    rig.dmac.ObserveAtKernelWait();
    CHECK(QwIs(rig.vu1, 1, 0x1100) && !rig.dmac.HasDeferredVif1());
    CHECK(rig.dmac.deferred().TailCount() == 0);
    PutTag(rig, slot, 2, DmacTagId::kCnt);
    PutUnpack(rig, slot + 0x10, 2, 0x2200);
    PutTag(rig, slot + 0x30, 0, DmacTagId::kEnd);
    const uint32_t tadr = rig.dmac.channel(1).tadr;
    rig.dmac.ChaseVif1Tail();
    CHECK(QwZero(rig.vu1, 2) && rig.dmac.channel(1).tadr == tadr);
    // The game's next kick of the rebuilt list walks it from ITS start, cleanly.
    KickVif1(rig, 0x4000);
    rig.dmac.ObserveAtKernelWait();
    CHECK(QwIs(rig.vu1, 2, 0x2200) && rig.dmac.channel(1).tadr == slot + 0x40);

    // A record made by a register-read flush is dropped by a later kernel wait
    // (nothing parked): the rewrite after the wait is not an append either.
    Rig b;
    const uint32_t slot_b = PutSimpleChain(b, 0x4000, 1, 0x1100);
    KickVif1(b, 0x4000);
    b.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(b.dmac.deferred().TailCount() == 1);
    b.dmac.ObserveAtKernelWait();
    CHECK(b.dmac.deferred().TailCount() == 0);
    PutTag(b, slot_b, 2, DmacTagId::kCnt);
    PutUnpack(b, slot_b + 0x10, 2, 0x2200);
    PutTag(b, slot_b + 0x30, 0, DmacTagId::kEnd);
    b.dmac.ChaseVif1Tail();
    CHECK(QwZero(b.vu1, 2));

    // The other observation points keep the chase (the pinned Crash model): a
    // CHCR-read flush followed by a frame-sync chase of the rewritten slot resumes.
    Rig c;
    const uint32_t slot_c = PutSimpleChain(c, 0x4000, 1, 0x1100);
    KickVif1(c, 0x4000);
    c.dmac.Read(DmacAddr::kVif1, 4);
    PutTag(c, slot_c, 2, DmacTagId::kCnt);
    PutUnpack(c, slot_c + 0x10, 2, 0x2200);
    PutTag(c, slot_c + 0x30, 0, DmacTagId::kEnd);
    c.dmac.ChaseVif1Tail();
    CHECK(QwIs(c.vu1, 2, 0x2200));
}

int main() {
    TestParkAndAppend();
    TestObservationPoints();
    TestKernelWaitObservation();
    TestReKickOrder();
    TestSnapshot();
    TestTailChase();
    TestChaseTerminates();
    TestFrameSyncFlush();
    TestKernelWaitEndsAppendRace();
    if (g_fail) {
        std::printf("DmacDeferredVif1: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("DmacDeferredVif1: PASS\n");
    return 0;
}

// VuStallModel: the FMAC / IALU writeback lanes and the read stalls --
// read-after-write stalls to +4, lane-disjoint reads do not stall, VF0 never
// enters the model, ILW -> branch stalls, a second FDIV op waits and commits Q,
// a second EFU op releases P one cycle early, a later write to a lane owns it,
// flushes advance the clock and forget the writebacks, Reset forgets them
// without touching the clock, and the classed / spec entry points agree with
// the dynamic body.
#include <cstdio>
#include <cstring>

#include "engine/vu/flag_pipeline.h"
#include "engine/vu/span_classifier.h"
#include "engine/vu/stall_model.h"
#include "engine/vu/vu_decoder.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static constexpr uint32_t kUNop = 0x000002FFu;
static constexpr uint32_t kLNop = 0x8000033Cu;

static uint32_t EncUpper(uint32_t func, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t dest) {
    return func | (fd << 6) | (fs << 11) | (ft << 16) | (dest << 21);
}
// ADD.dest fd, fs, ft
static uint32_t EncAdd(uint32_t fd, uint32_t fs, uint32_t ft, uint32_t dest) { return EncUpper(0x28, fd, fs, ft, dest); }
// ILW.dest it, imm(is)
static uint32_t EncIlw(uint32_t it, uint32_t is, uint32_t dest, uint32_t imm11) {
    return (0x04u << 25) | (dest << 21) | (it << 16) | (is << 11) | (imm11 & 0x7FFu);
}
// IBNE it, is, imm11
static uint32_t EncIbne(uint32_t it, uint32_t is, uint32_t imm11) {
    return (0x29u << 25) | (it << 16) | (is << 11) | (imm11 & 0x7FFu);
}
static uint32_t EncDiv(uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    return (0x40u << 25) | (ftf << 23) | (fsf << 21) | (ft << 16) | (fs << 11) | (0x0Eu << 6) | 0x3C;
}
static uint32_t EncWaitQ() { return (0x40u << 25) | (0x0Eu << 6) | 0x3F; }
// ESADD P, fs
static uint32_t EncEsadd(uint32_t fs) { return (0x40u << 25) | (0xFu << 21) | (fs << 11) | (0x1Cu << 6) | 0x3C; }
static uint32_t EncWaitP() { return (0x40u << 25) | (0x1Eu << 6) | 0x3F; }

static VUregs FreshRegs() {
    VUregs r;
    std::memset(&r, 0, sizeof(r));
    r.Reset();
    return r;
}

static void TestFmacReadStall() {
    StallModel sm;
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    fp.SetCycle(10);
    // Bundle A: ADD.xyzw vf1, vf2, vf3  -> vf1 retires at 14
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(1, 2, 3, 0xF)), r, fp);
    for (int k = 0; k < 4; ++k) CHECK(sm.FmacRetire(1, k) == 14);
    CHECK(sm.FmacRetire(2, 0) == 0 && sm.FmacRetire(3, 0) == 0);
    CHECK(sm.fmac_newest_retire() == 14);
    CHECK(fp.cycle() == 10);
    // Bundle B at 11: ADD.xyzw vf4, vf1, vf0 reads vf1 -> stall to 14
    fp.SetCycle(11);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(4, 1, 0, 0xF)), r, fp);
    CHECK(fp.cycle() == 14);
    CHECK(sm.FmacRetire(4, 0) == 18 && sm.FmacRetire(4, 3) == 18 && sm.fmac_newest_retire() == 18);
    // Bundle C at 15: reads vf4.w only while vf4's write is xyzw -> stall to 18
    fp.SetCycle(15);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(5, 4, 0, vu_dest::W)), r, fp);
    CHECK(fp.cycle() == 18);
    // vf1's writeback (14) is behind the clock -- inert; vf4 (18) and vf5 (22) ahead.
    CHECK(sm.FmacRetire(1, 0) == 14 && sm.FmacRetire(4, 0) == 18 && sm.FmacRetire(5, 3) == 22 && sm.FmacRetire(5, 0) == 0);
    // At 22 everything has retired: a reader of vf5 at 22 does not stall.
    fp.SetCycle(22);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(6, 5, 0, 0xF)), r, fp);
    CHECK(fp.cycle() == 22);
    // A later write to the same lanes owns them: vf6.x rewritten at 30 retires at 34; a reader of
    // vf6.x at 31 waits to 34, a reader of vf6.w (written at 22, retired 26) does not.
    fp.SetCycle(30);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(6, 0, 0, vu_dest::X)), r, fp);
    CHECK(sm.FmacRetire(6, 0) == 34 && sm.FmacRetire(6, 3) == 26);
    fp.SetCycle(31);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(7, 6, 0, vu_dest::W)), r, fp);
    CHECK(fp.cycle() == 31);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(8, 6, 0, vu_dest::X)), r, fp);
    CHECK(fp.cycle() == 34);
}

static void TestLaneDisjointAndVf0() {
    StallModel sm;
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    fp.SetCycle(100);
    // ADD.xy vf1, vf2, vf3 ; then ADD.zw vf4, vf1(zw), vf0 -> lanes disjoint: no stall
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(1, 2, 3, vu_dest::X | vu_dest::Y)), r, fp);
    fp.SetCycle(101);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(4, 1, 0, vu_dest::Z | vu_dest::W)), r, fp);
    CHECK(fp.cycle() == 101);
    // A write to vf0 never enters the model; a read of vf0 never stalls.
    fp.SetCycle(200);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(0, 2, 3, 0xF)), r, fp);
    CHECK(sm.FmacRetire(0, 0) == 0 && sm.fmac_newest_retire() == 105);   // vf0 was never queued
    fp.SetCycle(201);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(6, 0, 0, 0xF)), r, fp);
    CHECK(fp.cycle() == 201);
}

static void TestIaluBranchStall() {
    StallModel sm;
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    fp.SetCycle(50);
    // ILW.x vi3, 0(vi1) -> vi3 retires at 54
    sm.Apply(VuDecoder::DecodeVuSlot(EncIlw(3, 1, vu_dest::X, 0), kUNop), r, fp);
    CHECK(sm.IaluRetire(3) == 54 && sm.ialu_newest_retire() == 54);
    // IADD-style consumer is not a branch: no stall (IADDIU vi4, vi3, 1)
    fp.SetCycle(51);
    sm.Apply(VuDecoder::DecodeVuSlot((0x08u << 25) | (4u << 16) | (3u << 11) | 1u, kUNop), r, fp);
    CHECK(fp.cycle() == 51);
    // IBNE vi3, vi0 -> waits for the load
    fp.SetCycle(52);
    sm.Apply(VuDecoder::DecodeVuSlot(EncIbne(3, 0, 4), kUNop), r, fp);
    CHECK(fp.cycle() == 54);
    // A branch on another VI does not stall.
    fp.SetCycle(60);
    sm.Apply(VuDecoder::DecodeVuSlot(EncIlw(5, 1, vu_dest::X, 0), kUNop), r, fp);
    fp.SetCycle(61);
    sm.Apply(VuDecoder::DecodeVuSlot(EncIbne(2, 0, 4), kUNop), r, fp);
    CHECK(fp.cycle() == 61);
    // An I-bit bundle never looks at the IALU side (its lower is an immediate): no stall, no write.
    fp.SetCycle(62);
    sm.Apply(VuDecoder::DecodeVuSlot(0x12345678u, kUNop | 0x80000000u), r, fp);
    CHECK(fp.cycle() == 62 && sm.IaluRetire(5) == 64);
    // A branch on vi5 at 63 waits for the load (64); at 64 it does not.
    fp.SetCycle(63);
    sm.Apply(VuDecoder::DecodeVuSlot(EncIbne(5, 0, 4), kUNop), r, fp);
    CHECK(fp.cycle() == 64);
    sm.Apply(VuDecoder::DecodeVuSlot(EncIbne(5, 0, 4), kUNop), r, fp);
    CHECK(fp.cycle() == 64);
}

static void TestPipeWaits() {
    StallModel sm;
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    // DIV then a second DIV two cycles later: wait to the first's retire, Q committed with status.
    fp.SetCycle(100);
    fp.PushQ(r, 2.0f, FlagPipeline::kDivDelay, vu_status::D | vu_status::DS);
    fp.SetCycle(102);
    sm.Apply(VuDecoder::DecodeVuSlot(EncDiv(1, 0, 2, 0), kUNop), r, fp);
    CHECK(fp.cycle() == 107);
    CHECK(r.Q == 2.0f);
    CHECK(!fp.q_pending());
    CHECK(r.status_flag == (vu_status::D | vu_status::DS));
    // WAITQ with a pending Q behaves the same way.
    fp.SetCycle(200);
    fp.PushQ(r, 3.0f, FlagPipeline::kSqrtDelay, 0);
    fp.SetCycle(201);
    sm.Apply(VuDecoder::DecodeVuSlot(EncWaitQ(), kUNop), r, fp);
    CHECK(fp.cycle() == 207 && r.Q == 3.0f);
    CHECK(r.status_flag == vu_status::DS);           // D replaced by 0, DS sticky
    // WAITQ with nothing pending: no movement.
    fp.SetCycle(300);
    sm.Apply(VuDecoder::DecodeVuSlot(EncWaitQ(), kUNop), r, fp);
    CHECK(fp.cycle() == 300);
    // ESADD then WAITP: released one cycle early.
    fp.SetCycle(400);
    fp.PushP(r, 9.0f, FlagPipeline::kEfuEsadd);      // retire 411
    fp.SetCycle(402);
    sm.Apply(VuDecoder::DecodeVuSlot(EncWaitP(), kUNop), r, fp);
    CHECK(fp.cycle() == 410 && r.P == 9.0f && !fp.p_pending());
    // A second EFU op sees the same early release.
    fp.SetCycle(500);
    fp.PushP(r, 1.0f, FlagPipeline::kEfuEleng);      // retire 518
    fp.SetCycle(503);
    sm.Apply(VuDecoder::DecodeVuSlot(EncEsadd(2), kUNop), r, fp);
    CHECK(fp.cycle() == 517 && r.P == 1.0f);
}

static void TestManyWritesAndFlush() {
    StallModel sm;
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    // Nine writes over nine bundles, one per cycle: every one is held (a reader of each lane
    // stalls to its own retire; the OLD ring of 8 never saw more than 8 live entries because
    // a bundle takes at least one cycle -- here the ninth bundle's reader of vf1 finds vf1 retired).
    for (uint32_t i = 1; i <= 9; ++i) {
        fp.SetCycle(1000 + (i - 1));
        sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(i, 0, 0, 0xF)), r, fp);
        CHECK(sm.FmacRetire(static_cast<int>(i), 0) == 1000 + (i - 1) + 4);
    }
    CHECK(sm.fmac_newest_retire() == 1012);
    fp.SetCycle(1009);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(10, 1, 0, 0xF)), r, fp);   // vf1 retired at 1004
    CHECK(fp.cycle() == 1009);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(11, 9, 0, 0xF)), r, fp);   // vf9 retires at 1012
    CHECK(fp.cycle() == 1012);
    // Flush: the clock passes the newest writeback (vf11 @ 1016) and the lanes are forgotten.
    sm.FlushFmacWb(fp);
    CHECK(fp.cycle() == 1016);
    CHECK(sm.fmac_newest_retire() == 0 && sm.FmacRetire(11, 0) == 0 && sm.FmacRetire(1, 0) == 0);
    // IALU: five loads, one per cycle; the branch on the last waits; the flush passes it.
    for (uint32_t i = 1; i <= 5; ++i) {
        fp.SetCycle(2000 + (i - 1));
        sm.Apply(VuDecoder::DecodeVuSlot(EncIlw(i, 0, vu_dest::X, 0), kUNop), r, fp);
    }
    CHECK(sm.IaluRetire(1) == 2004 && sm.IaluRetire(5) == 2008 && sm.ialu_newest_retire() == 2008);
    fp.SetCycle(2005);
    sm.Apply(VuDecoder::DecodeVuSlot(EncIbne(1, 0, 4), kUNop), r, fp);
    CHECK(fp.cycle() == 2005);                                  // vi1 retired
    sm.Apply(VuDecoder::DecodeVuSlot(EncIbne(5, 0, 4), kUNop), r, fp);
    CHECK(fp.cycle() == 2008);
    fp.SetCycle(2005);
    sm.FlushIaluWb(fp);
    CHECK(fp.cycle() == 2008 && sm.ialu_newest_retire() == 0 && sm.IaluRetire(5) == 0);
    // Reset forgets the writebacks without touching the clock.
    fp.SetCycle(3000);
    sm.Apply(VuDecoder::DecodeVuSlot(EncIlw(1, 0, vu_dest::X, 0), EncAdd(1, 0, 0, 0xF)), r, fp);
    CHECK(sm.FmacRetire(1, 0) == 3004 && sm.IaluRetire(1) == 3004);
    sm.Reset();
    CHECK(sm.fmac_newest_retire() == 0 && sm.ialu_newest_retire() == 0 && sm.FmacRetire(1, 0) == 0 && fp.cycle() == 3000);
    // MaterializeFmacIssued: the block tier's close rebuilds the lanes from recorded issue cycles.
    const VuDecodedSlot w1 = VuDecoder::DecodeVuSlot(kLNop, EncAdd(2, 0, 0, vu_dest::X | vu_dest::Y));
    const VuDecodedSlot w2 = VuDecoder::DecodeVuSlot(kLNop, EncAdd(2, 0, 0, vu_dest::Y));
    const VuDecodedSlot* bundles[2] = {&w1, &w2};
    const uint64_t issue[2] = {4000, 4002};
    sm.MaterializeFmacIssued(bundles, issue, 2);
    CHECK(sm.FmacRetire(2, 0) == 4004 && sm.FmacRetire(2, 1) == 4006 && sm.FmacRetire(2, 2) == 0 && sm.fmac_newest_retire() == 4006);
}

// The classed / spec entry points reproduce the dynamic body on a classified
// straight-line program (the classes only prune steps their bundles cannot need).
static void TestClassedEqualsDynamic() {
    const uint32_t prog[][2] = {
        {kLNop, EncAdd(1, 2, 3, 0xF)},                    // upper write, lower nop
        {EncIlw(3, 1, vu_dest::X, 0), EncAdd(4, 1, 0, 0xF)},   // read stall + ialu write
        {kLNop, EncAdd(5, 4, 0, vu_dest::W)},
        {(0x08u << 25) | (4u << 16) | (3u << 11) | 1u, kUNop},  // IADDIU
        {(0x01u << 25) | (0xFu << 21) | (5u << 16) | (1u << 11), EncAdd(6, 5, 5, 0xF)},  // SQ vf5 + upper
        {kLNop, kUNop},
        {kLNop, EncAdd(7, 6, 6, 0xF)},
        {kLNop, kUNop | 0x40000000u},
        {kLNop, kUNop},
    };
    const size_t n = sizeof(prog) / sizeof(prog[0]);
    VuDecodedSlot slots[16];
    for (size_t i = 0; i < n; ++i) slots[i] = VuDecoder::DecodeVuSlot(prog[i][0], prog[i][1]);
    SpanClassifier::ComputeStallClasses(slots, n);
    int classed = 0;
    for (size_t i = 0; i < n; ++i) classed += slots[i].stall_class != 0;
    CHECK(classed >= 5);

    StallModel dyn, cls;
    FlagPipeline fd(false), fc(false);
    VUregs rd = FreshRegs(), rc = FreshRegs();
    for (size_t i = 0; i < n; ++i) {
        fd.cycle() += 1;
        fc.cycle() += 1;
        dyn.Apply(slots[i], rd, fd);
        cls.ApplyClassed(slots[i], rc, fc);
        CHECK(fd.cycle() == fc.cycle());
        CHECK(dyn.fmac_newest_retire() == cls.fmac_newest_retire() && dyn.ialu_newest_retire() == cls.ialu_newest_retire());
        for (int reg = 0; reg < 32; ++reg)
            for (int k = 0; k < 4; ++k) CHECK(dyn.FmacRetire(reg, k) == cls.FmacRetire(reg, k));
        for (int vi = 0; vi < 16; ++vi) CHECK(dyn.IaluRetire(vi) == cls.IaluRetire(vi));
    }
    // Spec<M> on a hand-picked slot: class 9 (WR1 only) equals the dynamic body.
    StallModel a, b;
    FlagPipeline fa(false), fb(false);
    fa.SetCycle(7);
    fb.SetCycle(7);
    const VuDecodedSlot s = VuDecoder::DecodeVuSlot(kLNop, EncAdd(1, 0, 0, 0xF));
    a.Apply(s, rd, fa);
    b.ApplySpec<StallModel::kWr1>(s, rc, fb);
    CHECK(a.FmacRetire(1, 0) == 11 && b.FmacRetire(1, 0) == 11 && a.fmac_newest_retire() == b.fmac_newest_retire());
}

int main() {
    TestFmacReadStall();
    TestLaneDisjointAndVf0();
    TestIaluBranchStall();
    TestPipeWaits();
    TestManyWritesAndFlush();
    TestClassedEqualsDynamic();
    if (g_fail) {
        std::printf("VuStallModel: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("VuStallModel: PASS\n");
    return 0;
}

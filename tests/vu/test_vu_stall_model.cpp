// VuStallModel: the FMAC / IALU writeback rings and the read stalls --
// read-after-write stalls to +4, lane-disjoint reads do not stall, VF0 never
// enters a ring, ILW -> branch stalls, a second FDIV op waits and commits Q,
// a second EFU op releases P one cycle early, ring overflow drops the
// soonest-retiring entry, flushes advance the clock, and the classed / spec
// entry points agree with the dynamic body.
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
    CHECK(sm.fmac_depth() == 1);
    CHECK(sm.fmac(0).reg == 1 && sm.fmac(0).mask == 0xF && sm.fmac(0).retire == 14);
    CHECK(sm.fmac_regs() == (1u << 1));
    CHECK(sm.fmac_min_retire() == 14);
    CHECK(fp.cycle() == 10);
    // Bundle B at 11: ADD.xyzw vf4, vf1, vf0 reads vf1 -> stall to 14
    fp.SetCycle(11);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(4, 1, 0, 0xF)), r, fp);
    CHECK(fp.cycle() == 14);
    CHECK(sm.fmac_depth() == 2);
    CHECK(sm.fmac(1).reg == 4 && sm.fmac(1).retire == 18);
    // Bundle C at 15: reads vf4.w only while vf4's write is xyzw -> stall to 18
    fp.SetCycle(15);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(5, 4, 0, vu_dest::W)), r, fp);
    CHECK(fp.cycle() == 18);
    // The prune ran at the bundle's issue cycle (15): vf1 (14) went, vf4 (18) stayed; vf5 retires at 22.
    CHECK(sm.fmac_depth() == 2);
    CHECK(sm.fmac(0).reg == 4 && sm.fmac(0).retire == 18);
    CHECK(sm.fmac(1).reg == 5 && sm.fmac(1).retire == 22);
    // At 22 everything is retired; the next bundle prunes the ring empty.
    fp.SetCycle(22);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, kUNop), r, fp);
    CHECK(sm.fmac_depth() == 0);
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
    // A write to vf0 never enters the ring; a read of vf0 never stalls.
    fp.SetCycle(200);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(0, 2, 3, 0xF)), r, fp);
    CHECK(sm.fmac_depth() == 0);                     // vf1/vf4 retired and pruned; vf0 was never queued
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
    CHECK(sm.ialu_depth() == 1);
    CHECK(sm.ialu(0).vi == 3 && sm.ialu(0).retire == 54);
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
    // An I-bit bundle never prunes / looks at the IALU ring (its lower is an immediate).
    fp.SetCycle(70);
    sm.Apply(VuDecoder::DecodeVuSlot(0x12345678u, kUNop | 0x80000000u), r, fp);
    CHECK(sm.ialu_depth() == 1);                     // the stale vi5 entry is still there
    fp.SetCycle(71);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, kUNop), r, fp);
    CHECK(sm.ialu_depth() == 0);                     // a lower-bearing bundle prunes it
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

static void TestOverflowAndFlush() {
    StallModel sm;
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    // Nine writes inside one 4-cycle window (nothing retires); the ring keeps
    // 8: the soonest retiring (vf1 @ 1004) is dropped and the last entry (vf8)
    // moves into its slot; the stale min_retire stays a lower bound.
    for (uint32_t i = 1; i <= 8; ++i) {
        fp.SetCycle(1000 + (i - 1) / 2);
        sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(i, 0, 0, 0xF)), r, fp);
    }
    CHECK(sm.fmac_depth() == 8);
    CHECK(sm.fmac(0).reg == 1 && sm.fmac(0).retire == 1004);
    CHECK(sm.fmac(7).reg == 8 && sm.fmac(7).retire == 1007);
    fp.SetCycle(1003);
    sm.Apply(VuDecoder::DecodeVuSlot(kLNop, EncAdd(9, 0, 0, 0xF)), r, fp);
    CHECK(sm.fmac_depth() == 8);
    CHECK(sm.fmac(0).reg == 8 && sm.fmac(0).retire == 1007);   // vf8 moved into slot 0
    CHECK(sm.fmac(7).reg == 9 && sm.fmac(7).retire == 1007);
    CHECK(sm.fmac_min_retire() == 1004);
    CHECK((sm.fmac_regs() & (1u << 1)) != 0);                  // the reg mask is a superset too
    // Flush: the clock passes the latest writeback, the ring empties.
    sm.FlushFmacWb(fp);
    CHECK(fp.cycle() == 1007);
    CHECK(sm.fmac_depth() == 0);
    // IALU ring of 4 (same window rule).
    for (uint32_t i = 1; i <= 5; ++i) {
        fp.SetCycle(2000 + (i - 1) / 2);
        sm.Apply(VuDecoder::DecodeVuSlot(EncIlw(i, 0, vu_dest::X, 0), kUNop), r, fp);
    }
    CHECK(sm.ialu_depth() == 4);
    CHECK(sm.ialu(0).vi == 4 && sm.ialu(0).retire == 2005);    // vi1 (soonest) dropped, vi4 moved into slot 0
    CHECK(sm.ialu(3).vi == 5 && sm.ialu(3).retire == 2006);
    sm.FlushIaluWb(fp);
    CHECK(fp.cycle() == 2006);
    CHECK(sm.ialu_depth() == 0);
    // Reset drops the rings without touching the clock.
    fp.SetCycle(3000);
    sm.Apply(VuDecoder::DecodeVuSlot(EncIlw(1, 0, vu_dest::X, 0), EncAdd(1, 0, 0, 0xF)), r, fp);
    sm.Reset();
    CHECK(sm.fmac_depth() == 0 && sm.ialu_depth() == 0 && fp.cycle() == 3000);
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
        CHECK(dyn.fmac_depth() == cls.fmac_depth());
        CHECK(dyn.ialu_depth() == cls.ialu_depth());
        for (int k = 0; k < dyn.fmac_depth(); ++k) {
            CHECK(dyn.fmac(k).reg == cls.fmac(k).reg && dyn.fmac(k).mask == cls.fmac(k).mask &&
                  dyn.fmac(k).retire == cls.fmac(k).retire);
        }
    }
    // Spec<M> on a hand-picked slot: class 9 (WR1 only) equals the dynamic body.
    StallModel a, b;
    FlagPipeline fa(false), fb(false);
    fa.SetCycle(7);
    fb.SetCycle(7);
    const VuDecodedSlot s = VuDecoder::DecodeVuSlot(kLNop, EncAdd(1, 0, 0, 0xF));
    a.Apply(s, rd, fa);
    b.ApplySpec<StallModel::kWr1>(s, rc, fb);
    CHECK(a.fmac_depth() == 1 && b.fmac_depth() == 1 && a.fmac(0).retire == b.fmac(0).retire);
}

int main() {
    TestFmacReadStall();
    TestLaneDisjointAndVf0();
    TestIaluBranchStall();
    TestPipeWaits();
    TestOverflowAndFlush();
    TestClassedEqualsDynamic();
    if (g_fail) {
        std::printf("VuStallModel: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("VuStallModel: PASS\n");
    return 0;
}

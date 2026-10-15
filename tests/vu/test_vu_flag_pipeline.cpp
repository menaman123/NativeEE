// VuFlagPipeline: the CLIP ring, the MAC/STATUS ring (both representations),
// the Q latch and the P latch -- push/visible timing at delays 4 / 7 / 13,
// ring overflow, sticky STATUS bits, FCSET dropping the ring, WAITQ advancing
// the clock, the EFU early release, the case-record latch accessors.
#include <cstdio>
#include <cstring>

#include "engine/vu/flag_pipeline.h"
#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static VUregs FreshRegs() {
    VUregs r;
    std::memset(&r, 0, sizeof(r));
    r.Reset();
    return r;
}

static VR_reg Lanes(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    VR_reg v;
    v.I[0] = x;
    v.I[1] = y;
    v.I[2] = z;
    v.I[3] = w;
    return v;
}

static void TestClipTiming() {
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    fp.SetCycle(100);
    fp.PushClip(r, 0x00ABCDEF);
    CHECK(fp.clip_latest() == 0x00ABCDEFu);       // issued value visible to the next CLIP immediately
    CHECK(fp.clip_depth() == 1);
    fp.SetCycle(103);
    CHECK(fp.VisibleClip(r) == 0);                 // 3 cycles: not yet
    fp.SetCycle(104);
    CHECK(fp.VisibleClip(r) == 0x00ABCDEFu);       // exactly kClipDelay
    CHECK(fp.clip_depth() == 0);

    // The 24-bit mask on push.
    fp.PushClip(r, 0xFF123456u);
    CHECK(fp.clip_latest() == 0x123456u);
    fp.SetCycle(200);
    CHECK(fp.VisibleClip(r) == 0x123456u);

    // Two entries due at once: the later issue wins.
    fp.SetCycle(300);
    fp.PushClip(r, 1);
    fp.SetCycle(301);
    fp.PushClip(r, 2);
    fp.SetCycle(305);
    CHECK(fp.VisibleClip(r) == 2u);

    // Overflow: the ninth push retires the oldest now.
    fp.SetCycle(400);
    for (uint32_t i = 1; i <= 8; ++i) fp.PushClip(r, 0x100 + i);
    CHECK(fp.clip_depth() == 8);
    CHECK(r.clip_flag == 2u);
    fp.PushClip(r, 0x109);
    CHECK(fp.clip_depth() == 8);
    CHECK(r.clip_flag == 0x101u);
    CHECK(fp.VisibleClip(r) == 0x101u);            // nothing else due at cycle 400

    // Flush: the newest queued judgement becomes visible; clip_latest follows.
    fp.FlushClipPipe(r);
    CHECK(r.clip_flag == 0x109u);
    CHECK(fp.clip_latest() == 0x109u);
    CHECK(fp.clip_depth() == 0);

    // FCSET: replace and drop the ring.
    fp.PushClip(r, 0x777);
    fp.SetClipImmediate(r, 0x01000042u);
    CHECK(r.clip_flag == 0x42u);
    CHECK(fp.clip_latest() == 0x42u);
    CHECK(fp.clip_depth() == 0);
    fp.SetCycle(1000);
    CHECK(fp.VisibleClip(r) == 0x42u);
}

static void TestMacTiming(bool lazy) {
    FlagPipeline fp(lazy);
    VUregs r = FreshRegs();
    r.status_flag = vu_status::D | vu_status::I | vu_status::IS;  // FDIV bits must survive every MAC commit
    fp.SetCycle(10);
    // x = -0 (Z+S), y = denormal (Z+U), z = +Inf (O), w = 1.0 (nothing)
    fp.UpdateMACFlags(r, Lanes(0x80000000u, 0x00000001u, 0x7F800000u, 0x3F800000u), vu_dest::XYZW);
    CHECK(fp.mac_depth() == 1);
    CHECK(fp.mac_retire(0) == 14);
    const uint16_t expect_mac = vu_mac::ZX | vu_mac::SX | vu_mac::ZY | vu_mac::UY | vu_mac::OZ;
    CHECK(fp.PackPendingMac(0) == expect_mac);
    fp.SetCycle(13);
    fp.RetireMacStatus(r);
    CHECK(r.mac_flag == 0);                        // not yet visible
    CHECK(fp.mac_depth() == 1);
    fp.SetCycle(14);
    fp.RetireMacStatus(r);
    CHECK(r.mac_flag == expect_mac);
    CHECK(fp.mac_depth() == 0);
    const uint16_t zsuo = vu_status::Z | vu_status::S | vu_status::U | vu_status::O;
    const uint16_t sticky = vu_status::ZS | vu_status::SS | vu_status::US | vu_status::OS;
    CHECK(r.status_flag == (zsuo | sticky | vu_status::D | vu_status::I | vu_status::IS));

    // A clean result clears the current Z/S/U/O but the sticky bits stay.
    fp.SetCycle(20);
    fp.UpdateMACFlags(r, Lanes(0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u), vu_dest::XYZW);
    fp.SetCycle(24);
    fp.RetireMacStatus(r);
    CHECK(r.mac_flag == 0);
    CHECK(r.status_flag == (sticky | vu_status::D | vu_status::I | vu_status::IS));

    // Only the dest lanes are judged.
    fp.SetCycle(30);
    fp.UpdateMACFlags(r, Lanes(0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u), vu_dest::W);
    fp.SetCycle(34);
    fp.RetireMacStatus(r);
    CHECK(r.mac_flag == vu_mac::ZW);
    CHECK((r.status_flag & 0xF) == vu_status::Z);

    // Overflow: the ninth push retires the oldest; a reader at that cycle
    // sees the same registers in both representations (all nine pushed at one cycle).
    fp.SetCycle(40);
    for (int i = 0; i < 8; ++i) fp.UpdateMACFlags(r, Lanes(0, 0, 0, 0x3F800000u), vu_dest::XYZW);
    CHECK(fp.mac_depth() == 8);
    fp.UpdateMACFlags(r, Lanes(0x80000000u, 0, 0, 0), vu_dest::X);
    CHECK(fp.mac_depth() == 8);
    fp.RetireMacStatus(r);                           // nothing due at 40: the evicted entry is what both representations show
    CHECK(r.mac_flag == (vu_mac::ZX | vu_mac::ZY | vu_mac::ZZ));
    fp.SetCycle(44);
    fp.RetireMacStatus(r);
    CHECK(fp.mac_depth() == 0);
    CHECK(r.mac_flag == (vu_mac::ZX | vu_mac::SX));  // the newest entry is the visible MAC
    CHECK((r.status_flag & 0xF) == (vu_status::Z | vu_status::S));

    // Flush drains everything regardless of timing.
    fp.SetCycle(50);
    fp.UpdateMACFlags(r, Lanes(0x7F800000u, 0, 0, 0), vu_dest::X);
    fp.FlushMacPipe(r);
    CHECK(fp.mac_depth() == 0);
    CHECK(r.mac_flag == vu_mac::OX);
    CHECK(fp.cycle() == 50);                         // the MAC flush never moves the clock

    // FSSET-style rewrite of the sticky bits leaves the ring alone.
    r.status_flag = static_cast<uint16_t>((r.status_flag & 0x3Fu) | 0x000u);
    fp.SetCycle(60);
    fp.UpdateMACFlags(r, Lanes(0, 0, 0, 0), vu_dest::X);
    fp.SetCycle(64);
    fp.RetireMacStatus(r);
    CHECK((r.status_flag & 0xFC0) == vu_status::ZS);
}

static void TestQLatch() {
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    CHECK(r.Q == 1.0f);
    fp.SetCycle(100);
    fp.PushQ(r, 0.5f, FlagPipeline::kDivDelay, 0);
    CHECK(fp.q_pending());
    CHECK(fp.q_pending_retire() == 107);
    CHECK(fp.VisibleQ(r) == 1.0f);                   // old Q while pending
    fp.SetCycle(106);
    CHECK(fp.VisibleQ(r) == 1.0f);
    fp.SetCycle(107);
    CHECK(fp.VisibleQ(r) == 0.5f);                   // exactly kDivDelay
    CHECK(!fp.q_pending());

    // RSQRT: 13 cycles; the status bits commit with the value (D/I replace, DS/IS OR).
    r.status_flag = vu_status::Z | vu_status::I | vu_status::IS;
    fp.SetCycle(200);
    fp.PushQ(r, 2.0f, FlagPipeline::kRsqrtDelay, vu_status::D | vu_status::DS);
    fp.SetCycle(212);
    CHECK(fp.VisibleQ(r) == 0.5f);
    fp.SetCycle(213);
    CHECK(fp.VisibleQ(r) == 2.0f);
    CHECK(r.status_flag == (vu_status::Z | vu_status::D | vu_status::DS | vu_status::IS));

    // WAITQ: the clock jumps to the retire cycle.
    fp.SetCycle(300);
    fp.PushQ(r, 3.0f, FlagPipeline::kSqrtDelay, 0);
    fp.FlushQPipe(r);
    CHECK(fp.cycle() == 307);
    CHECK(r.Q == 3.0f);
    CHECK(!fp.q_pending());

    // Back-to-back FDIV: the second push waits for and commits the first.
    fp.SetCycle(400);
    fp.PushQ(r, 4.0f, FlagPipeline::kDivDelay, 0);
    fp.SetCycle(402);
    fp.PushQ(r, 5.0f, FlagPipeline::kDivDelay, 0);
    CHECK(fp.cycle() == 407);
    CHECK(r.Q == 4.0f);
    CHECK(fp.q_pending_retire() == 414);
    CHECK(fp.q_pending_value() == 5.0f);

    // The pushed value is clamped; the visible read clamps too.
    fp.SetCycle(500);
    fp.PushQ(r, VuFloat::Bits2F(0x7F800000u), FlagPipeline::kDivDelay, 0);
    CHECK(VuFloat::F2Bits(fp.q_pending_value()) == 0x7F7FFFFFu);
    r.Q = VuFloat::Bits2F(0x00000001u);
    CHECK(VuFloat::F2Bits(fp.VisibleQ(r)) == 0);     // denormal Q reads as +0
    CHECK(VuFloat::F2Bits(r.Q) == 0x00000001u);      // the register itself keeps the raw bits
    fp.SetCycle(507);
    CHECK(VuFloat::F2Bits(fp.VisibleQ(r)) == 0x7F7FFFFFu);

    // Case-record accessors.
    fp.SetQPending(true, 999, 7.0f, vu_status::I);
    CHECK(fp.q_pending() && fp.q_pending_retire() == 999 && fp.q_pending_value() == 7.0f &&
          fp.q_pending_status() == vu_status::I);
}

static void TestPLatch() {
    FlagPipeline fp(false);
    VUregs r = FreshRegs();
    fp.SetCycle(100);
    fp.PushP(r, 11.0f, FlagPipeline::kEfuEsadd);
    CHECK(fp.p_pending_retire() == 111);
    fp.SetCycle(110);
    CHECK(fp.VisibleP(r) == 0.0f);                   // MFP never stalls: old P
    fp.SetCycle(111);
    CHECK(fp.VisibleP(r) == 11.0f);
    CHECK(!fp.p_pending());

    // A second EFU op releases the pending P one cycle early.
    fp.SetCycle(200);
    fp.PushP(r, 1.0f, FlagPipeline::kEfuEatan);      // retire 254
    fp.SetCycle(210);
    fp.ReleasePEarly(r);
    CHECK(fp.cycle() == 253);
    CHECK(r.P == 1.0f);
    CHECK(!fp.p_pending());

    // PushP over a pending P: the same early release, then the new latency.
    fp.SetCycle(300);
    fp.PushP(r, 2.0f, FlagPipeline::kEfuEsin);       // retire 329
    fp.PushP(r, 3.0f, FlagPipeline::kEfuEexp);
    CHECK(fp.cycle() == 328);
    CHECK(r.P == 2.0f);
    CHECK(fp.p_pending_retire() == 328 + 44);

    // Program end waits the FULL latency.
    fp.FlushPPipe(r);
    CHECK(fp.cycle() == 372);
    CHECK(r.P == 3.0f);

    // Already-due P: no clock movement on either release.
    fp.SetCycle(400);
    fp.PushP(r, 4.0f, FlagPipeline::kEfuEsum);
    fp.SetCycle(500);
    fp.ReleasePEarly(r);
    CHECK(fp.cycle() == 500 && r.P == 4.0f);

    // The P value is NOT clamped by the latch (the EFU bodies decide).
    fp.PushP(r, VuFloat::Bits2F(0x7F800000u), FlagPipeline::kEfuErcpr);
    CHECK(VuFloat::F2Bits(fp.p_pending_value()) == 0x7F800000u);
    fp.SetPPending(false, 0, 0.0f);
    CHECK(!fp.p_pending());
}

static void TestLatencyTable() {
    CHECK(FlagPipeline::kClipDelay == 4 && FlagPipeline::kMacDelay == 4);
    CHECK(FlagPipeline::kDivDelay == 7 && FlagPipeline::kSqrtDelay == 7 && FlagPipeline::kRsqrtDelay == 13);
    CHECK(FlagPipeline::kEfuEsadd == 11 && FlagPipeline::kEfuErsadd == 18 && FlagPipeline::kEfuEleng == 18);
    CHECK(FlagPipeline::kEfuErleng == 24 && FlagPipeline::kEfuEatanxy == 54 && FlagPipeline::kEfuEatanxz == 54);
    CHECK(FlagPipeline::kEfuEsum == 12 && FlagPipeline::kEfuErcpr == 12 && FlagPipeline::kEfuEsqrt == 12);
    CHECK(FlagPipeline::kEfuErsqrt == 18 && FlagPipeline::kEfuEsin == 29 && FlagPipeline::kEfuEatan == 54);
    CHECK(FlagPipeline::kEfuEexp == 44);
#if defined(PS2R_SIMD) && PS2R_SIMD
    CHECK(FlagPipeline::kDefaultLazy);
#else
    CHECK(!FlagPipeline::kDefaultLazy);
#endif
    CHECK(FlagPipeline().lazy() == FlagPipeline::kDefaultLazy);
}

int main() {
    TestLatencyTable();
    TestClipTiming();
    TestMacTiming(false);
    TestMacTiming(true);
    TestQLatch();
    TestPLatch();
    if (g_fail) {
        std::printf("VuFlagPipeline: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("VuFlagPipeline: PASS (eager + lazy rings, Q/P latches)\n");
    return 0;
}

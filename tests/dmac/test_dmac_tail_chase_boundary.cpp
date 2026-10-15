// DmacTailChaseAtBoundary: the DMAC's frame-sync observation point at the EE
// frame boundary (INTEGRATION_PLAN item 5.5). GsSync::SyncV / WaitForVblank run
// the bound `Dmac::ChaseVif1Tail` BEFORE BoundarySequencer::RunBoundary -- so a
// VIF1 chain kick parked by the game lands before slot 0 (PresentRendezvous)
// and the field tick (TickAdvance) run, and an END slot the game rewrote behind
// a flushed walk is resumed at the same point. Without the DMAC hook bound the
// kick stays parked across the boundary (the proof the step is load-bearing).
// GsSync::SyncPath flushes the parked kick before it reports the path idle.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/dmac/dmac.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/timing/boundary_sequencer.h"
#include "engine/ee/timing/derived_clocks.h"
#include "engine/ee/timing/emu_domain.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/gs/hle/sce/gs_sync.h"
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

using Step = BoundarySequencer::Step;

// The DMAC over its VIF/GIF/VU rig plus a bare BoundarySequencer whose slots 0
// and 1 are probes recording whether the VIF1 kick was still parked when they ran.
struct TestDmacTailChaseBoundary {
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
    EmuDomain domain;
    DerivedClocks clocks{domain};
    BoundarySequencer seq;
    CpuContext ctx{};
    int present_saw_parked = -1;   // -1 = the slot did not run
    int tick_saw_parked = -1;
    uint64_t chases = 0, chases_parked = 0, flushes = 0, flushes_parked = 0;

    TestDmacTailChaseBoundary()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter) {
        seq.SetStep(Step::PresentRendezvous, [this](CpuContext&) {
            present_saw_parked = dmac.HasDeferredVif1() ? 1 : 0;
            return true;
        });
        seq.SetStep(Step::TickAdvance, [this](CpuContext&) {
            tick_saw_parked = dmac.HasDeferredVif1() ? 1 : 0;
            return true;
        });
        GsSync::ResetForTest();
        GsSync::Bind(&seq, &clocks);
    }
    ~TestDmacTailChaseBoundary() { GsSync::ResetForTest(); }

    // The composition root's hooks (ee_bootstrap): count, note a parked kick, run the Dmac method.
    void BindDmacHooks() {
        GsSync::BindDmac(
            [this] {
                if (dmac.HasDeferredVif1()) ++chases_parked;
                dmac.ChaseVif1Tail();
                ++chases;
            },
            [this] {
                if (dmac.HasDeferredVif1()) ++flushes_parked;
                dmac.FlushDeferredVif1();
                ++flushes;
            });
    }
    void ResetProbes() { present_saw_parked = tick_saw_parked = -1; }
};
using Rig = TestDmacTailChaseBoundary;

void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }
void PutTag(Rig& rig, uint32_t at, uint32_t qwc, uint32_t id) {
    uint8_t* p = rig.ram.Pointer(at, 16);
    Put64(p, static_cast<uint64_t>(qwc & 0xFFFFu) | (static_cast<uint64_t>(id & 7u) << 28));
    Put64(p + 8, 0);
}
// UNPACK V4-32 of one quadword to VU1 data qw `dst` (two quadwords of stream).
void PutUnpack(Rig& rig, uint32_t at, uint32_t dst, uint32_t m) {
    uint8_t* p = rig.ram.Pointer(at, 32);
    Put32(p, (0x6Cu << 24) | (1u << 16) | (dst & 0x3FFu));
    Put32(p + 4, m);
    Put32(p + 8, m + 1);
    Put32(p + 12, m + 2);
    Put32(p + 16, m + 3);
    Put32(p + 20, 0);
    Put32(p + 24, 0);
    Put32(p + 28, 0);
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
// CNT(2) UNPACK -> qw `dst`, END(0) at `at + 0x30`; returns the END slot address.
uint32_t PutSimpleChain(Rig& rig, uint32_t at, uint32_t dst, uint32_t m) {
    PutTag(rig, at, 2, DmacTagId::kCnt);
    PutUnpack(rig, at + 0x10, dst, m);
    PutTag(rig, at + 0x30, 0, DmacTagId::kEnd);
    return at + 0x30;
}
void KickVif1(Rig& rig, uint32_t tadr) {
    rig.dmac.channel(1).tadr = tadr;
    rig.dmac.Write(DmacAddr::kVif1, 4,
                   DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift) | DmacChcr::kDir);
}

// With the hook bound: the parked kick lands before slot 0 and slot 1 run, at
// both boundary sites.
void TestChaseBeforeTheSequence() {
    Rig rig;
    rig.BindDmacHooks();
    PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    CHECK(rig.dmac.HasDeferredVif1() && QwZero(rig.vu1, 1));
    GsSync::SyncV(rig.ctx);
    CHECK(rig.present_saw_parked == 0 && rig.tick_saw_parked == 0);
    CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 1, 0x1100));
    CHECK((rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0 && (rig.dmac.globals().stat & DmacStat::Cis(1)) != 0);
    CHECK(rig.chases == 1 && rig.chases_parked == 1 && GsSync::TailChases() == 1);
    CHECK(rig.seq.Completed() == 1 && GsSync::SyncVCount() == 1);
    CHECK(rig.ctx.cpuRegs.GPR.r[2].UD[0] == rig.clocks.SyncVFieldParity());
    // The WaitForVblank mirror is the same body.
    rig.ResetProbes();
    PutSimpleChain(rig, 0x5000, 2, 0x2200);
    KickVif1(rig, 0x5000);
    CHECK(rig.dmac.HasDeferredVif1());
    GsSync::WaitForVblank(rig.ctx);
    CHECK(rig.present_saw_parked == 0 && rig.tick_saw_parked == 0);
    CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 2, 0x2200));
    CHECK(rig.chases == 2 && rig.chases_parked == 2 && GsSync::TailChases() == 2 && rig.seq.Completed() == 2);
    // A boundary with nothing parked: the chase runs and changes nothing.
    rig.ResetProbes();
    std::vector<uint8_t> before, after;
    rig.dmac.SerializeSection(before);
    GsSync::SyncV(rig.ctx);
    rig.dmac.SerializeSection(after);
    CHECK(before == after && rig.chases == 3 && rig.chases_parked == 2 && rig.present_saw_parked == 0);
}

// Without the hook (the Phase-3 boundary) the kick stays parked across the
// whole sequence: the step is load-bearing.
void TestUnboundStaysParked() {
    Rig rig;
    PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    CHECK(rig.dmac.HasDeferredVif1());
    GsSync::SyncV(rig.ctx);
    CHECK(rig.present_saw_parked == 1 && rig.tick_saw_parked == 1);
    CHECK(rig.dmac.HasDeferredVif1() && QwZero(rig.vu1, 1));
    CHECK((rig.dmac.channel(1).chcr & DmacChcr::kStr) != 0);
    CHECK(GsSync::TailChases() == 0 && rig.seq.Completed() == 1);
    // SyncPath unbound: reports idle, flushes nothing.
    GsSync::SyncPath(rig.ctx);
    CHECK(rig.ctx.cpuRegs.GPR.r[2].UD[0] == 0 && rig.dmac.HasDeferredVif1() && GsSync::SyncPathFlushes() == 0);
    rig.dmac.FlushDeferredVif1();
    CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 1, 0x1100));
}

// The tail chase proper: a chain flushed at a register observation recorded
// its END slot; the game rewrites that slot behind the walk; the boundary
// resumes the chain from the slot so the appended packet lands in this frame.
void TestRewrittenEndSlotResumesAtBoundary() {
    Rig rig;
    rig.BindDmacHooks();
    const uint32_t slot = PutSimpleChain(rig, 0x4000, 1, 0x1100);
    KickVif1(rig, 0x4000);
    rig.dmac.Read(DmacAddr::kVif1, 4);   // the game polls CHCR: the parked walk runs, END recorded
    CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 1, 0x1100) && rig.dmac.deferred().TailCount() == 1);
    // The game appends: slot = CNT(2) UNPACK -> qw 2, new END(0) behind it.
    PutTag(rig, slot, 2, DmacTagId::kCnt);
    PutUnpack(rig, slot + 0x10, 2, 0x2200);
    PutTag(rig, slot + 0x30, 0, DmacTagId::kEnd);
    CHECK(QwZero(rig.vu1, 2));
    GsSync::SyncV(rig.ctx);
    CHECK(QwIs(rig.vu1, 2, 0x2200) && rig.dmac.deferred().TailCount() == 0);
    CHECK(rig.present_saw_parked == 0 && rig.tick_saw_parked == 0);
    CHECK(rig.dmac.channel(1).tadr == slot + 0x40 && (rig.dmac.channel(1).chcr & DmacChcr::kStr) == 0);
    CHECK(rig.chases == 1 && rig.chases_parked == 0);   // nothing was parked; the resume did the work
}

// sceGsSyncPath: the parked kick is consumed before "idle" is reported.
void TestSyncPathFlushes() {
    Rig rig;
    rig.BindDmacHooks();
    PutSimpleChain(rig, 0x4000, 3, 0x3300);
    KickVif1(rig, 0x4000);
    CHECK(rig.dmac.HasDeferredVif1());
    rig.ctx.cpuRegs.GPR.r[2].UD[0] = 0xDEADBEEFull;
    GsSync::SyncPath(rig.ctx);
    CHECK(rig.ctx.cpuRegs.GPR.r[2].UD[0] == 0);
    CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 3, 0x3300));
    CHECK(rig.flushes == 1 && rig.flushes_parked == 1 && GsSync::SyncPathFlushes() == 1);
    CHECK(rig.chases == 0 && rig.seq.Completed() == 0);   // no boundary ran
    GsSync::SyncPath(rig.ctx);
    CHECK(rig.ctx.cpuRegs.GPR.r[2].UD[0] == 0 && rig.flushes == 2 && rig.flushes_parked == 1);
}

}  // namespace

int main() {
    TestChaseBeforeTheSequence();
    TestUnboundStaysParked();
    TestRewrittenEndSlotResumesAtBoundary();
    TestSyncPathFlushes();
    if (g_fail) {
        std::printf("DmacTailChaseAtBoundary: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("DmacTailChaseAtBoundary: PASS\n");
    return 0;
}

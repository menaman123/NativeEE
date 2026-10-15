// DmacMtvuFeed: the DMAC's VIF1 chain walk routed to the MTVU worker -- the
// TTE UNPACK + MSCAL chain of DmacVifChain walked by ChainWalker -> Vif inline
// and by ChainWalker -> DmacMtvuFeed -> MtvuWorker -> Vif (one staged Batch
// segment per walk, the walk-head latch reset as a ResetLatch segment) lands
// identical VU1 data memory and registers on the worker's unit; Uninstall /
// destruction put the inline feed back.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "engine/dmac/dmac.h"
#include "engine/dmac/dmac_mtvu_feed.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_regs.h"
#include "engine/vu/tiers/mtvu_backpressure.h"
#include "engine/vu/tiers/mtvu_clock.h"
#include "engine/vu/tiers/mtvu_sync.h"
#include "engine/vu/tiers/mtvu_worker.h"
#include "engine/vu/tiers/runahead_guard.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fail = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

struct TestDmacMtvuFeed {
    MainMemory ram;
    Scratchpad spad;
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0, vu1, vu1w;
    Vif vif;
    Dmac dmac;
    RunaheadGuard guard;
    MtvuBackpressure backpressure;
    MtvuWorker worker;
    MtvuSync sync;
    TestDmacMtvuFeed()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vu1w(1, &arbiter),
          vif(vu0, vu1, arbiter, gif_regs), dmac(ram, spad, vif, arbiter), guard(2),
          backpressure(guard, MtvuClock::Steady()), worker(vif, vu1w, arbiter, backpressure, MtvuSettings{true, 2}),
          sync(worker, arbiter) {}

    static constexpr uint32_t VifCode(uint32_t cmd, uint32_t num, uint32_t imm) {
        return (cmd << 24) | ((num & 0xFFu) << 16) | (imm & 0xFFFFu);
    }
    static uint64_t TagLo(uint32_t qwc, uint32_t id) {
        return static_cast<uint64_t>(qwc) | (static_cast<uint64_t>(id & 7u) << 28);
    }
    void PutQw(uint32_t at, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
        const uint32_t w[4] = {w0, w1, w2, w3};
        std::memcpy(ram.Pointer(at, 16), w, 16);
    }
    void PutTag(uint32_t at, uint32_t qwc, uint32_t id, uint32_t w2 = 0) {
        const uint64_t lo = TagLo(qwc, id);
        std::memcpy(ram.Pointer(at, 16), &lo, 8);
        const uint64_t hi = w2;
        std::memcpy(ram.Pointer(at, 16) + 8, &hi, 8);
    }
    // Micro at 0: iaddiu vi1, vi0, 5 with the E bit; nop.
    static void LoadTinyMicro(VuUnit& vu) {
        const uint32_t iaddiu = (0x08u << 25) | (1u << 16) | 5u;
        const uint32_t b[4] = {iaddiu, 0x000002FFu | 0x40000000u, 0x8000033Cu, 0x000002FFu};
        vu.WriteMicroMem(0, reinterpret_cast<const uint8_t*>(b), 16);
    }
    // CNT(3, TTE = STCYCL CL2/WL1) UNPACK V4-32 num 2 -> qw 0x10 ; CNT(1) MSCAL 0 ; END(0).
    void PutChain() {
        PutTag(0x4000, 3, DmacTagId::kCnt, VifCode(0x01, 0, 0x0102));
        PutQw(0x4010, VifCode(0x6C, 2, 0x10), 0x1000, 0x1001, 0x1002);
        PutQw(0x4020, 0x1003, 0x1010, 0x1011, 0x1012);
        PutQw(0x4030, 0x1013, 0, 0, 0);
        PutTag(0x4040, 1, DmacTagId::kCnt);
        PutQw(0x4050, VifCode(0x14, 0, 0), 0, 0, 0);
        PutTag(0x4060, 0, DmacTagId::kEnd);
        dmac.channel(1).tadr = 0x4000;
    }
    static constexpr uint32_t kChainKickTte = DmacChcr::kStr | DmacChcr::kTte | DmacChcr::kDir |
                                              (DmacChcr::kModChain << DmacChcr::kModShift);

    static bool SameUnit(const VuUnit& a, const VuUnit& b) {
        return std::memcmp(a.state().data, b.state().data, Vu1State::kMemSize) == 0 &&
               std::memcmp(&a.regs(), &b.regs(), sizeof(VUregs)) == 0;
    }
};

void TestWalkThroughWorker() {
    // Inline reference: the walker feeds Vif -> the inline unit.
    TestDmacMtvuFeed ref;
    TestDmacMtvuFeed::LoadTinyMicro(ref.vu1);
    ref.vu1.regs().Reset();
    ref.PutChain();
    ref.dmac.Write(DmacAddr::kVif1, 4, TestDmacMtvuFeed::kChainKickTte);
    ref.dmac.FlushDeferredVif1();
    CHECK(ref.vu1.regs().VI[1] == 5 && ref.vif.regs(1).cycle == 0x0102u);

    // Through the worker: the same walk staged into one Batch on the worker's unit.
    TestDmacMtvuFeed r;
    CHECK(!r.dmac.walker().HasVif1StagedFeed());
    r.worker.Start();
    TestDmacMtvuFeed::LoadTinyMicro(r.vu1w);
    {
        DmacMtvuFeed feed(r.dmac.walker(), r.worker);
        CHECK(feed.Installed() && r.dmac.walker().HasVif1StagedFeed());
        r.PutChain();
        r.dmac.Write(DmacAddr::kVif1, 4, TestDmacMtvuFeed::kChainKickTte);
        r.dmac.FlushDeferredVif1();
        r.sync.Sync(SyncSite::SceDmaSync);
        CHECK(r.worker.BatchSegmentsProcessed() == 1);
        CHECK(r.worker.StagedRecords() == 0 && r.worker.PendingBytes() == 0);
        CHECK(TestDmacMtvuFeed::SameUnit(ref.vu1, r.vu1w));
        CHECK(r.vu1w.regs().VI[1] == 5 && r.vif.regs(1).cycle == 0x0102u);
        CHECK(r.vu1.regs().VI[1] == 0);  // the inline unit never ran
        feed.Uninstall();
        CHECK(!feed.Installed() && !r.dmac.walker().HasVif1StagedFeed());
        feed.Install();
        CHECK(r.dmac.walker().HasVif1StagedFeed());
    }
    CHECK(!r.dmac.walker().HasVif1StagedFeed());  // destruction restores the inline feed
    r.worker.Stop();
}

void TestLibraryWorkBetweenSpans(bool threaded) {
    TestDmacMtvuFeed r;
    std::unique_ptr<DmacMtvuFeed> feed;
    if (threaded) {
        r.worker.Start();
        feed = std::make_unique<DmacMtvuFeed>(r.dmac.walker(), r.worker);
    }
    // Two existing DIRECT producers, with one library job between them.
    for (uint32_t index = 0; index < 2; ++index) {
        const uint32_t at = 0x4000 + index * 0x40;
        r.PutTag(at, 3, DmacTagId::kCnt);
        r.PutQw(at + 16, 0, 0, 0, TestDmacMtvuFeed::VifCode(0x50, 0, 2));
        r.PutQw(at + 32, 0x8001, 0x10000000, 0xe, 0);
        r.PutQw(at + 48, index + 1, 0, 0x3d, 0);
    }
    r.PutTag(0x4080, 0, DmacTagId::kEnd);
    std::vector<uint32_t> visited;
    bool delivered = false;
    bool ended = false;
    r.dmac.walker().SetVif1TagObserver([&](uint32_t address, bool scratchpad, bool first) {
        CHECK(!scratchpad);
        CHECK(first == visited.empty());
        visited.push_back(address);
        if (address != 0x4000) return;
        r.dmac.walker().FlushVif1StagedFeed();
        r.arbiter.submitOrdered([&] {
            CHECK(r.ops.Count() == 3);
            r.ops.WriteRegister(0x3d, 99);
            delivered = true;
        });
    }, [&] { ended = true; CHECK(visited.size() == 3); });
    r.dmac.channel(1).tadr = 0x4000;
    r.dmac.Write(DmacAddr::kVif1, 4, TestDmacMtvuFeed::kChainKickTte & ~DmacChcr::kTte);
    r.dmac.FlushDeferredVif1();
    if (threaded) r.sync.Sync(SyncSite::SceDmaSync);
    r.arbiter.drain();
    CHECK(visited == std::vector<uint32_t>({0x4000, 0x4040, 0x4080}));
    CHECK(ended && delivered && r.ops.Count() == 7);
    if (r.ops.Count() == 7) {
        CHECK(r.ops.Ops()[1].lo == 1);
        CHECK(r.ops.Ops()[3].lo == 99);
        CHECK(r.ops.Ops()[5].lo == 2);
    }
    r.dmac.walker().SetVif1TagObserver({});
    feed.reset();
    if (threaded) r.worker.Stop();
}

}  // namespace

int main() {
    TestWalkThroughWorker();
    TestLibraryWorkBetweenSpans(false);
    TestLibraryWorkBetweenSpans(true);
    if (g_fail) {
        std::printf("DmacMtvuFeed: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("DmacMtvuFeed: PASS\n");
    return 0;
}

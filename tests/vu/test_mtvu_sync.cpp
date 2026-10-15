// VuMtvuSync: the EE side of the MTVU pipeline. Sync waits for the worker to
// fall idle while eager-draining the arbiter on the EE thread (a gated fake
// feed holds the worker mid-span so the loop path runs deterministically; the
// drain inside Sync releases the gate); Sync + the sealed-prefix drain gives
// the inline RegisterOpStream; InstallInto puts the EE-kick redirect on the
// INLINE unit's dispatch so VuUnit::Execute syncs first and runs on the
// worker's register file (identical to inline, the inline unit untouched);
// TryVif1StatBusyView = VPS=3|VEW|FQC=16 only while a segment is in flight;
// the site counters; DeferOn. No wall-clock assert: fake clock, predicate waits.
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vu/tiers/mtvu_backpressure.h"
#include "engine/vu/tiers/mtvu_clock.h"
#include "engine/vu/tiers/mtvu_sync.h"
#include "engine/vu/tiers/mtvu_vif_feed.h"
#include "engine/vu/tiers/mtvu_worker.h"
#include "engine/vu/tiers/runahead_guard.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

struct TestMtvuSync {
    struct FakeClock : MtvuClock {
        uint64_t now = 7'000'000'000;
        uint64_t NowNs() const override { return now; }
    };

    // A VIF1 feed that parks the worker inside ProcessVif1 until released.
    struct GatedFeed : MtvuVifFeed {
        std::mutex mtx;
        std::condition_variable cv;
        bool entered = false;
        bool released = false;
        size_t bytes = 0;
        VuUnit* bound = nullptr;
        void ProcessVif1(const uint8_t*, size_t size) override {
            std::unique_lock<std::mutex> lk(mtx);
            bytes += size;
            entered = true;
            cv.notify_all();
            cv.wait(lk, [&] { return released; });
        }
        void SetVif1TteFeed(bool) override {}
        void ResetVif1Latch() override {}
        void BindVu1(VuUnit* unit) override { bound = unit; }
        void WaitEntered() {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait(lk, [&] { return entered; });
        }
        void Release() {
            std::lock_guard<std::mutex> lk(mtx);
            released = true;
            cv.notify_all();
        }
    };

    // The real chain: MPG (vi1 = 2; XGKICK vi1; E), UNPACK a packet to qw2, MSCAL 0.
    static uint32_t Vc(uint8_t cmd, uint8_t num, uint16_t imm) {
        return (static_cast<uint32_t>(cmd) << 24) | (static_cast<uint32_t>(num) << 16) | imm;
    }
    static std::vector<uint8_t> Chain(bool with_kick) {
        std::vector<uint32_t> w;
        w.push_back(Vc(0x01, 0, 0x0101));
        w.push_back(Vc(0x4A, 4, 0));
        const uint32_t U_NOP = 0x000002FF, U_NOPE = 0x400002FF, L_NOP = 0x8000033C;
        const uint32_t L_IADDIU = (0x08u << 25) | (1u << 16) | 2u;
        const uint32_t L_XGKICK = 0x80000000u | (1u << 11) | (0x1Bu << 6) | 0x3Cu;
        const uint32_t b[8] = {L_IADDIU, U_NOP, L_XGKICK, U_NOP, L_NOP, U_NOPE, L_NOP, U_NOP};
        for (uint32_t x : b) w.push_back(x);
        w.push_back(Vc(0x6C, 2, 2));
        w.push_back(0x00008001u);  // NLOOP 1, EOP, PACKED, NREGS 1
        w.push_back(0x10000000u);
        w.push_back(0x00000001u);  // RGBAQ
        w.push_back(0);
        for (uint32_t i = 0; i < 4; i++) w.push_back(0x1234567u * (i + 1));
        if (with_kick) w.push_back(Vc(0x14, 0, 0));
        while (w.size() % 4) w.push_back(0);
        std::vector<uint8_t> out(w.size() * 4);
        std::memcpy(out.data(), w.data(), out.size());
        return out;
    }
    // A 2-QW A+D PATH3 packet (TEXFLUSH): one delivered register op.
    static std::vector<uint8_t> AdPacket() {
        const uint32_t w[8] = {0x00008001u, 0x10000000u, 0x0000000Eu, 0, 0, 0, 0x3Fu, 0};
        std::vector<uint8_t> out(32);
        std::memcpy(out.data(), w, 32);
        return out;
    }

    struct Rig {
        FakeClock clock;
        RegisterOpStream ops;
        GifRegs gif_regs;
        GifParser parser;
        GifArbiter arbiter;
        VuUnit vu0, vu1, vu1w;
        Vif vif;
        RunaheadGuard guard;
        MtvuBackpressure backpressure;
        MtvuWorker worker;
        MtvuSync sync;
        Rig()
            : parser(ops, gif_regs),
              arbiter(parser),
              vu0(0),
              vu1(1, &arbiter),
              vu1w(1, &arbiter),
              vif(vu0, vu1, arbiter, gif_regs),
              guard(2),
              backpressure(guard, clock),
              worker(vif, vu1w, arbiter, backpressure, MtvuSettings{true, 2}),
              sync(worker, arbiter) {}
        // 16-byte spans (the DMAC hands whole quadwords), to the worker or inline.
        void Feed(const std::vector<uint8_t>& c, bool to_worker) {
            for (size_t o = 0; o < c.size(); o += 16)
                to_worker ? worker.SubmitVif1(c.data() + o, 16) : vif.ProcessData(1, c.data() + o, 16);
        }
    };

    static bool AllZero(const uint8_t* p, size_t n) {
        while (n--) if (*p++) return false;
        return true;
    }

    // Sync while the worker is held mid-span: the loop path drains the arbiter
    // on this thread (that drain releases the gate), then the worker is idle.
    static void SyncWaitsAndDrains() {
        struct GateRig {
            GatedFeed feed;
            RegisterOpStream ops;
            GifRegs gif_regs;
            GifParser parser;
            GifArbiter arbiter;
            VuUnit vu1w;
            RunaheadGuard guard;
            FakeClock clock;
            MtvuBackpressure backpressure;
            MtvuWorker worker;
            MtvuSync sync;
            int delivered = 0;
            GateRig()
                : parser(ops, gif_regs), arbiter(parser), vu1w(1, &arbiter), guard(2), backpressure(guard, clock),
                  worker(feed, vu1w, arbiter, backpressure, MtvuSettings{true, 2}), sync(worker, arbiter) {}
            static void OnDrain(void* user, const GifArbiterPacket&) {
                auto* r = static_cast<GateRig*>(user);
                r->delivered++;
                r->feed.Release();
            }
        } r;
        uint32_t stat = 0;
        CHECK(!r.sync.TryVif1StatBusyView(&stat) && stat == 0 && !r.sync.DeferOn(), "worker down: no view, no defer");
        r.sync.Sync(SyncSite::Vif1RegRead);
        r.sync.NoteSiteSkipped(SyncSite::SceGsSyncPath);
        CHECK(r.sync.SyncCount(SyncSite::Vif1RegRead) == 0 && r.sync.SkippedCount(SyncSite::SceGsSyncPath) == 0,
              "nothing counted while the worker is down");

        r.arbiter.SetDrainObserver(&GateRig::OnDrain, &r);
        r.worker.Start();
        CHECK(r.feed.bound == &r.vu1w, "Start binds the worker unit");
        CHECK(r.sync.DeferOn(), "DeferOn while the worker runs");
        const std::vector<uint8_t> pkt = AdPacket();
        r.arbiter.submit(GifPath::Path3, pkt.data(), pkt.size());  // ahead of the span's marker
        uint8_t span[16] = {0};
        r.worker.SubmitVif1(span, 16);
        r.feed.WaitEntered();
        CHECK(r.worker.IsBusy(), "a segment is in flight");
        CHECK(r.sync.TryVif1StatBusyView(&stat), "busy view while in flight");
        CHECK(stat == (0x03u | (1u << 2) | (16u << 24)), "VPS=3 | VEW | FQC=16");
        CHECK(stat == MtvuSync::kVif1StatBusyView, "the named constant is that view");
        CHECK(r.delivered == 0, "nothing drained before Sync");
        r.sync.Sync(SyncSite::Vif1RegRead);
        CHECK(r.delivered == 1, "Sync eager-drained the arbiter on this thread");
        CHECK(r.feed.bytes == 16, "the span was replayed");
        CHECK(r.worker.WorkerIdle() && !r.sync.TryVif1StatBusyView(&stat), "idle after Sync: no busy view");
        CHECK(r.sync.SyncCount(SyncSite::Vif1RegRead) == 1, "one Vif1RegRead sync counted");
        CHECK(r.ops.Count() >= 1, "the drained A+D packet reached the parser");
        r.sync.Sync(SyncSite::Vu1Mem);  // idle fast path
        CHECK(r.sync.SyncCount(SyncSite::Vu1Mem) == 1 && r.delivered == 1, "fast path counts, drains nothing");
        for (SyncSite s : {SyncSite::SceGsSyncPath, SyncSite::SceGsSyncPath, SyncSite::SceDmaSync}) r.sync.NoteSiteSkipped(s);
        CHECK(r.sync.SkippedCount(SyncSite::SceGsSyncPath) == 2 && r.sync.SkippedCount(SyncSite::SceDmaSync) == 1 &&
                  r.sync.SkippedCount(SyncSite::SceGifSyncDma) == 0,
              "skip counters per site");
        r.sync.TryEagerDrain();
        CHECK(r.delivered == 1, "nothing sealed awaits: no drain");
        r.worker.Stop();
        CHECK(r.feed.bound == nullptr, "Stop rebinds the inline unit");
    }

    // Sync + the sealed-prefix drain: RegisterOpStream identical to inline.
    static void SyncOpsIdentical() {
        const std::vector<uint8_t> chain = Chain(true);
        uint64_t inline_hash = 0;
        size_t inline_count = 0;
        {
            Rig r;
            r.vif.Reset();
            r.vif.ResetLatch(1);
            r.Feed(chain, false);
            r.arbiter.drain();
            inline_hash = r.ops.Hash();
            inline_count = r.ops.Count();
            CHECK(inline_count > 0, "the inline chain kicked a packet");
        }
        Rig r;
        r.vif.Reset();
        r.worker.Start();
        r.worker.SubmitResetLatch();
        r.Feed(chain, true);
        r.sync.Sync(SyncSite::SceGsSyncV);
        CHECK(r.worker.WorkerIdle(), "idle after Sync");
        // Sync's loop drains may have collected the seals already; the try-drain takes the rest.
        r.sync.TryEagerDrain();
        CHECK(!r.arbiter.hasSealedReady() && r.arbiter.PendingSlots() == 0, "every sealed marker collected");
        CHECK(r.ops.Hash() == inline_hash && r.ops.Count() == inline_count, "worker ops == inline ops");
        r.sync.TryParkDrain();
        CHECK(r.ops.Count() == inline_count, "a second try delivers nothing");
        r.worker.Stop();
    }

    // The EE-kick redirect: Execute on the INLINE unit while the worker owns
    // VU1 -> Sync(Vu1EeKick) -> the program runs on the worker's unit.
    static void RedirectRunsOnWorker() {
        const std::vector<uint8_t> load = Chain(false);  // micro + data, no MSCAL
        std::vector<uint8_t> micro_ref, data_ref;
        VUregs regs_ref;
        uint64_t inline_hash = 0;
        {
            Rig r;
            r.vif.Reset();
            r.vu1.regs().Reset();
            r.Feed(load, false);
            r.vu1.Execute(0);
            r.arbiter.drain();
            micro_ref.assign(r.vu1.state().micro, r.vu1.state().micro + Vu1State::kMemSize);
            data_ref.assign(r.vu1.state().data, r.vu1.state().data + Vu1State::kMemSize);
            regs_ref = r.vu1.regs();
            inline_hash = r.ops.Hash();
            CHECK(r.ops.Count() > 0, "the EE kick produced ops inline");
        }
        Rig r;
        r.vif.Reset();
        r.worker.Start();
        r.sync.InstallInto(r.vu1.dispatch());
        r.worker.SubmitResetLatch();
        r.Feed(load, true);
        r.vu1.Execute(0);  // the inline unit: redirected
        CHECK(r.sync.SyncCount(SyncSite::Vu1EeKick) == 1, "the kick synced first");
        CHECK(r.worker.WorkerIdle(), "the worker was idle when the program ran");
        r.arbiter.drain();
        CHECK(r.ops.Hash() == inline_hash, "redirected kick ops == inline ops");
        CHECK(std::memcmp(&r.vu1w.regs(), &regs_ref, sizeof(VUregs)) == 0, "worker unit registers == inline run");
        CHECK(std::memcmp(r.vu1w.state().micro, micro_ref.data(), Vu1State::kMemSize) == 0, "worker micro == inline");
        CHECK(std::memcmp(r.vu1w.state().data, data_ref.data(), Vu1State::kMemSize) == 0, "worker data == inline");
        CHECK(AllZero(r.vu1.state().micro, Vu1State::kMemSize), "the inline unit never ran (micro untouched)");
        CHECK(r.vu1.regs().VI[1] == 0, "the inline unit's VI1 untouched");
        CHECK(!MtvuSync::Redirect(&r.sync, r.vu1w, 0), "a kick on the worker unit itself falls through");
        MtvuSync::UninstallFrom(r.vu1.dispatch());
        r.worker.Stop();
        CHECK(!MtvuSync::Redirect(&r.sync, r.vu1, 0), "no redirect once the worker is down");
        CHECK(r.sync.SyncCount(SyncSite::Vu1EeKick) == 1, "no sync counted for the fall-through");
    }

    static void Run() {
        SyncWaitsAndDrains();
        SyncOpsIdentical();
        RedirectRunsOnWorker();
    }
};

}  // namespace

int main() {
    TestMtvuSync::Run();
    if (g_fail) {
        std::fprintf(stderr, "test_mtvu_sync: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("test_mtvu_sync: ALL PASS\n");
    return 0;
}

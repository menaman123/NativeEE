// GifDrainThread: the drain thread owns the arbiter's drain while it runs --
// (a) packets from a producer thread (plain inbox slots and worker-style
// markers interleaved) are delivered in submission order, every delivery on
// the drain thread; (b) a boundary slot is delivered exactly once, in stream
// position, on the drain thread; (c) DrainFence returns only after a pass
// that collected every seal published before it; (d) Stop with an unsealed
// marker blocking the inbox joins without delivering what sits behind it;
// plus the idle-generation predicate and the fence-after-stop no-op. No
// wall-clock assertion: every check is on order, counts and thread identity.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "counting_gs_sink.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_boundary_hook.h"
#include "engine/gif/gif_drain_thread.h"
#include "engine/gif/gif_marker_owner.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"

namespace {

int g_checks = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_fail;                                                      \
            std::printf("[GIFDRAIN] FAIL: %s (line %d)\n", msg, __LINE__); \
        }                                                                  \
    } while (0)

struct Rec {
    bool boundary;
    uint32_t id;
    bool on_drain_thread;
};

// A 16-byte PACKED tag (NLOOP=0, EOP) whose high quadword carries the id:
// legal for the parser, consumed as an empty tag.
GifArbiterPacket MakePkt(uint32_t id) {
    GifArbiterPacket p;
    p.path = GifPath::Path1;
    p.data.assign(16, 0);
    const uint64_t lo = (1ull << 15) | (1ull << 60);  // EOP, NREG=1
    std::memcpy(p.data.data(), &lo, 8);
    std::memcpy(p.data.data() + 8, &id, 4);
    return p;
}

uint32_t IdOf(const GifArbiterPacket& p) {
    uint32_t id = 0;
    if (p.data.size() >= 12) std::memcpy(&id, p.data.data() + 8, 4);
    return id;
}

struct TestGifDrainThread : GifBoundaryHook {
    // The worker stand-in: the thread that is currently filling a marker answers
    // the arbiter's three routing questions, so fills go through the real
    // worker-side submit (ledger included) and EE-side submits take the inbox.
    struct FakeWorker : GifMarkerOwner {
        std::atomic<bool> running{true};
        mutable std::mutex mtx;
        std::thread::id worker_tid;
        std::shared_ptr<GifMtvuMarker> active;

        bool Running() const override { return running.load(std::memory_order_acquire); }
        bool OnWorkerThread() const override {
            std::lock_guard<std::mutex> lock(mtx);
            return std::this_thread::get_id() == worker_tid;
        }
        std::shared_ptr<GifMtvuMarker> ActiveMarker() const override {
            std::lock_guard<std::mutex> lock(mtx);
            return active;
        }
        void Begin(const std::shared_ptr<GifMtvuMarker>& m) {
            std::lock_guard<std::mutex> lock(mtx);
            worker_tid = std::this_thread::get_id();
            active = m;
        }
        void End() {
            std::lock_guard<std::mutex> lock(mtx);
            active.reset();
            worker_tid = std::thread::id();
        }
    };

        GifRegs regs;
    CountingGsSink sink;
    GifParser parser;
    GifArbiter arbiter;
    GifDrainThread thread;
    FakeWorker worker;
    std::mutex mtx;
    std::vector<Rec> delivered;
    std::atomic<uint64_t> hook_fires{0};

    TestGifDrainThread() : parser(sink, regs), arbiter(parser, this), thread(arbiter) {
        arbiter.SetDrainObserver(&TestGifDrainThread::OnDrain, this);
        arbiter.SetMarkerOwner(&worker);
    }
    static void OnDrain(void* user, const GifArbiterPacket& pkt) {
        auto* r = static_cast<TestGifDrainThread*>(user);
        std::lock_guard<std::mutex> lock(r->mtx);
        if (pkt.boundary_seq != 0) {
            r->delivered.push_back(Rec{true, pkt.boundary_seq, r->thread.OnDrainThread()});
        } else {
            r->delivered.push_back(Rec{false, IdOf(pkt), r->thread.OnDrainThread()});
        }
    }
    void OnBoundary(uint32_t, uint64_t) override { hook_fires.fetch_add(1, std::memory_order_relaxed); }
    size_t Count() {
        std::lock_guard<std::mutex> lock(mtx);
        return delivered.size();
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mtx);
        delivered.clear();
    }
    void Submit(uint32_t id) {
        GifArbiterPacket p = MakePkt(id);
        arbiter.submit(p.path, p.data.data(), p.data.size());
    }
    // A worker span: open the marker, fill it through the real worker-side
    // submit path on the calling thread, seal it.
    void WorkerSpan(const std::shared_ptr<GifMtvuMarker>& m, uint32_t first, uint32_t count) {
        worker.Begin(m);
        for (uint32_t i = 0; i < count; ++i) Submit(first + i);
        worker.End();
        arbiter.sealMarker(m);
    }
};

}  // namespace

int main() {
    TestGifDrainThread rig;
    rig.thread.Start();
    CHECK(rig.thread.Running(), "drain thread reports running");
    CHECK(rig.arbiter.DrainThreadRunning(), "arbiter sees the drain thread");
    CHECK(!rig.thread.OnDrainThread(), "main thread is not the drain thread");

    // (a) submission order == delivery order, producer thread, mixed slots.
    constexpr uint32_t kTags = 600;
    {
        std::thread producer([&] {
            std::mt19937 rng(0xB1u);
            uint32_t tag = 1;
            while (tag <= kTags) {
                if ((rng() & 3u) == 0) {
                    auto m = rig.arbiter.openMarker();
                    uint32_t n = 1 + (rng() % 4u);
                    if (tag + n - 1 > kTags) n = kTags - tag + 1;
                    rig.WorkerSpan(m, tag, n);
                    tag += n;
                } else {
                    rig.Submit(tag++);
                }
                if ((rng() & 15u) == 0) std::this_thread::yield();
            }
        });
        producer.join();
        rig.arbiter.DrainFence();
        std::lock_guard<std::mutex> lock(rig.mtx);
        CHECK(rig.delivered.size() == kTags, "(a) every packet delivered after the fence");
        bool in_order = true, all_on_dt = true;
        for (size_t i = 0; i < rig.delivered.size(); ++i) {
            if (rig.delivered[i].boundary || rig.delivered[i].id != static_cast<uint32_t>(i) + 1) in_order = false;
            if (!rig.delivered[i].on_drain_thread) all_on_dt = false;
        }
        CHECK(in_order, "(a) delivery order == submission order");
        CHECK(all_on_dt, "(a) every delivery ran on the drain thread");
        CHECK(rig.sink.begin_tag == kTags, "(a) the parser saw every tag");
    }

    // (b) boundary delivered exactly once, in stream position.
    {
        rig.Clear();
        rig.Submit(9001);
        rig.arbiter.noteFeedForBoundary();
        rig.arbiter.openBoundary(42, 123456);
        rig.Submit(9002);
        rig.arbiter.DrainFence();
        std::lock_guard<std::mutex> lock(rig.mtx);
        CHECK(rig.delivered.size() == 3, "(b) packet + boundary + packet delivered");
        if (rig.delivered.size() == 3) {
            CHECK(!rig.delivered[0].boundary && rig.delivered[0].id == 9001, "(b) pre-boundary packet first");
            CHECK(rig.delivered[1].boundary && rig.delivered[1].id == 42, "(b) boundary in stream position");
            CHECK(rig.delivered[1].on_drain_thread, "(b) boundary dispatched on the drain thread");
            CHECK(!rig.delivered[2].boundary && rig.delivered[2].id == 9002, "(b) post-boundary packet last");
        }
        CHECK(rig.hook_fires.load() == 1, "(b) the boundary hook fired exactly once");
        const GifBoundaryStats st = rig.arbiter.boundary_stats();
        CHECK(st.enqueued == 1 && st.delivered == 1 && st.order_violations == 0, "(b) boundary census");
    }

    // (c) drained-to-mark: a fence after seal K returns only after packets <= K.
    {
        rig.Clear();
        auto m = rig.arbiter.openMarker();
        rig.WorkerSpan(m, 1, 25);
        rig.arbiter.DrainFence();
        std::lock_guard<std::mutex> lock(rig.mtx);
        CHECK(rig.delivered.size() == 25, "(c) fence returns only after the sealed span is delivered");
        bool ordered = true;
        for (size_t i = 0; i < rig.delivered.size(); ++i)
            if (rig.delivered[i].id != static_cast<uint32_t>(i) + 1) ordered = false;
        CHECK(ordered, "(c) drained-to-mark preserves order");
    }

    // Idle generation: nonzero and stable while nothing is held.
    {
        const uint64_t g1 = rig.thread.DrainIdleGeneration();
        const uint64_t g2 = rig.thread.DrainIdleGeneration();
        CHECK(g1 != 0 && g1 == g2, "idle generation nonzero and stable on an empty pipeline");
        CHECK(g1 == rig.thread.PassGeneration() + 1, "idle generation == passes + 1");
        auto m = rig.arbiter.openMarker();  // a held slot -> 0
        CHECK(rig.thread.DrainIdleGeneration() == 0, "idle generation 0 while a slot is held");
        rig.arbiter.sealMarker(m);
        rig.arbiter.DrainFence();
        const uint64_t g3 = rig.thread.DrainIdleGeneration();
        CHECK(g3 != 0 && g3 > g1, "idle generation advances after passes");
        CHECK(rig.arbiter.PendingBytes() == 0 && rig.arbiter.PendingSlots() == 0, "ledger empty when idle");
    }

    // A drain() from the EE thread is a kick, not a pass.
    {
        rig.Clear();
        rig.Submit(31337);
        const uint64_t gen = rig.thread.PassGeneration();
        rig.arbiter.drain();
        rig.arbiter.DrainFence();
        CHECK(rig.thread.PassGeneration() >= gen + 2, "foreign drain() kicked a pass");
        std::lock_guard<std::mutex> lock(rig.mtx);
        CHECK(rig.delivered.size() == 1 && rig.delivered[0].on_drain_thread, "kicked pass delivered on the drain thread");
    }

    // (d) stop with a non-empty queue: bounded join, nothing behind the marker.
    {
        rig.Clear();
        auto stuck = rig.arbiter.openMarker();  // never sealed
        rig.Submit(7777);
        CHECK(rig.arbiter.hasPending(), "(d) queue is non-empty before stop");
        CHECK(rig.thread.DrainIdleGeneration() == 0, "(d) not idle while slots are held");
        rig.thread.Stop();
        CHECK(!rig.thread.Running(), "(d) thread reports stopped");
        CHECK(!rig.arbiter.DrainThreadRunning(), "(d) arbiter detached");
        CHECK(rig.Count() == 0, "(d) nothing behind the unsealed marker was delivered");
        CHECK(rig.arbiter.hasPending(), "(d) the packet still waits behind the marker");
        CHECK(rig.thread.DrainIdleGeneration() == 1, "(d) idle generation reads 1 when stopped");
        (void)stuck;
    }
    rig.thread.DrainFence();  // no-op when stopped
    rig.arbiter.DrainFence();

    // Restart after stop: the thread is reusable and the stuck slot drains inline.
    {
        rig.arbiter.clear();
        CHECK(!rig.arbiter.hasPending() && rig.arbiter.PendingBytes() == 0, "clear() settles the ledger");
        rig.thread.Start();
        CHECK(rig.thread.Running(), "restart");
        rig.Clear();
        rig.Submit(1);
        rig.arbiter.DrainFence();
        CHECK(rig.Count() == 1, "delivery after restart");
        rig.thread.Stop();
    }

    std::printf("[GIFDRAIN] RESULT: %s (%d/%d checks passed)\n", g_fail == 0 ? "PASS" : "FAIL",
                g_checks - g_fail, g_checks);
    return g_fail == 0 ? 0 : 1;
}

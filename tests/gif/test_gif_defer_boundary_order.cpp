// GifDeferBoundaryOrder: frame boundaries as ordered command slots, inline
// drains (no drain thread). For a directed interleaving matrix and a 10k
// randomized soak with a real scripted worker thread, the delivered order is
//   [frame-N packets..., BOUNDARY(N), frame-N+1 packets..., BOUNDARY(N+1), ...]
// for every interleaving of seals and drains; every boundary fires exactly
// once on the drain owner thread through the injected hook; the feed-note
// tripwire counts exactly the boundaries opened without their frame's note;
// and the inline path with markers produces the identical RegisterOpStream
// + hook order as the same packets submitted through a deferred drain.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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
#include "engine/gs/hle/ir/register_op_stream.h"

namespace {

int g_checks = 0;
int g_fails = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        ++g_checks;                                             \
        if (!(cond)) {                                          \
            ++g_fails;                                          \
            std::printf("FAIL: %s (line %d)\n", msg, __LINE__); \
        }                                                       \
    } while (0)

struct Rec {
    bool boundary;
    uint32_t id;  // packet tag or boundary seq
};

bool SameRecs(const std::vector<Rec>& a, const std::vector<Rec>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].boundary != b[i].boundary || a[i].id != b[i].id) return false;
    return true;
}

void PrintRecs(const char* tag, const std::vector<Rec>& r) {
    std::printf("  %s:", tag);
    for (const Rec& x : r) std::printf(" %s%u", x.boundary ? "B" : "p", x.id);
    std::printf("\n");
}

// A PACKED A+D packet writing TEXFLUSH with the id as its value: one
// recorded register op per packet, so the op stream carries the order.
std::vector<uint8_t> MakePkt(uint32_t id) {
    std::vector<uint8_t> v(32, 0);
    const uint64_t lo = 1ull | (1ull << 15) | (1ull << 60);  // NLOOP=1, EOP, NREG=1
    const uint64_t desc = static_cast<uint64_t>(GsReg::Ad);
    const uint64_t val = id;
    const uint64_t reg = static_cast<uint64_t>(GsReg::Texflush);
    std::memcpy(v.data(), &lo, 8);
    std::memcpy(v.data() + 8, &desc, 8);
    std::memcpy(v.data() + 16, &val, 8);
    std::memcpy(v.data() + 24, &reg, 8);
    return v;
}

std::vector<RegisterOp> WithoutFinish(const std::vector<RegisterOp>& ops) {
    std::vector<RegisterOp> out;
    for (const RegisterOp& op : ops)
        if (op.kind != static_cast<uint8_t>(RegisterOpKind::Finish)) out.push_back(op);
    return out;
}

bool SameOps(const std::vector<RegisterOp>& a, const std::vector<RegisterOp>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(RegisterOp)) != 0) return false;
    return true;
}

// One arbiter over a recording parser; the hook and the drain tap record the
// delivery order and the thread every boundary fired on.
struct TestGifDeferBoundaryOrder : GifBoundaryHook {
    // The worker stand-in (see GifDrainThread): the filling thread answers the
    // arbiter's routing questions so fills use the real worker-side submit.
    struct FakeWorker : GifMarkerOwner {
        mutable std::mutex mtx;
        std::thread::id worker_tid;
        std::shared_ptr<GifMtvuMarker> active;

        bool Running() const override { return true; }
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
    RegisterOpStream ops;
    GifParser parser;
    GifArbiter arb;
    FakeWorker worker;
    std::vector<Rec> delivered;   // drain owner thread only
    std::vector<uint32_t> hooks;  // boundary seqs in hook order
    std::thread::id owner_tid;
    uint64_t hook_offthread = 0;

    TestGifDeferBoundaryOrder() : parser(ops, regs), arb(parser, this) {
        owner_tid = std::this_thread::get_id();
        arb.SetMarkerOwner(&worker);
        arb.SetDrainObserver(&TestGifDeferBoundaryOrder::OnDrain, this);
    }
    static void OnDrain(void* user, const GifArbiterPacket& pkt) {
        auto* r = static_cast<TestGifDeferBoundaryOrder*>(user);
        if (pkt.boundary_seq != 0) {
            r->delivered.push_back(Rec{true, pkt.boundary_seq});
        } else {
            uint32_t id = 0;
            std::memcpy(&id, pkt.data.data() + 16, 4);
            r->delivered.push_back(Rec{false, id});
        }
    }
    void OnBoundary(uint32_t seq, uint64_t) override {
        hooks.push_back(seq);
        if (std::this_thread::get_id() != owner_tid) ++hook_offthread;
    }
    void Submit(uint32_t id, GifPath path = GifPath::Path1) {
        std::vector<uint8_t> p = MakePkt(id);
        arb.submit(path, p.data(), p.size());
    }
    // Scripted worker: fill a marker through the worker-side submit, then seal.
    void WorkerFillSeal(const std::shared_ptr<GifMtvuMarker>& m, const std::vector<uint32_t>& tags) {
        worker.Begin(m);
        for (uint32_t t : tags) Submit(t);
        worker.End();
        arb.sealMarker(m);
    }
    void Boundary(uint32_t seq) {
        arb.noteFeedForBoundary();
        arb.openBoundary(seq, 0);
    }
};

// The op stream + hook sequence one scripted frame set produces when driven
// inline (markers, EE drains) vs deferred (a drain thread owning the drain).
struct Script {
    std::vector<uint32_t> span_a{1, 2, 3};
    std::vector<uint32_t> span_b{4};
    uint32_t ee1 = 5, ee2 = 6, ee3 = 7;
};

void RunScript(TestGifDeferBoundaryOrder& r, const Script& s, GifDrainThread* dt) {
    auto m1 = r.arb.openMarker();
    r.Submit(s.ee1, GifPath::Path3);
    r.Boundary(11);
    auto m2 = r.arb.openMarker();
    r.Submit(s.ee2, GifPath::Path3);
    r.arb.drain();  // nothing ready: m1 is unsealed at the head
    std::thread worker([&] {
        r.WorkerFillSeal(m1, s.span_a);
        r.WorkerFillSeal(m2, s.span_b);
    });
    worker.join();
    r.Boundary(12);
    r.Submit(s.ee3, GifPath::Path3);
    if (dt) {
        r.arb.drain();
        dt->DrainFence();
    } else {
        r.arb.drain();
    }
}

}  // namespace

int main() {
    // (a) all packets sealed BEFORE the boundary enqueue.
    {
        TestGifDeferBoundaryOrder r;
        auto m1 = r.arb.openMarker();
        r.WorkerFillSeal(m1, {1, 2});
        r.Boundary(100);
        r.arb.drain();
        std::vector<Rec> exp = {{false, 1}, {false, 2}, {true, 100}};
        CHECK(SameRecs(r.delivered, exp), "case a: sealed-before-enqueue order");
        if (g_fails) PrintRecs("got", r.delivered);
        CHECK(!r.arb.hasPending(), "case a: fully drained");
        CHECK(r.arb.PendingBytes() == 0 && r.arb.PendingSlots() == 0, "case a: ledger settled");
        CHECK(r.hooks.size() == 1 && r.hooks[0] == 100, "case a: hook fired once with the seq");
    }

    // (b) seal AFTER the boundary enqueue.
    {
        TestGifDeferBoundaryOrder r;
        auto m1 = r.arb.openMarker();
        r.Boundary(200);
        r.arb.drain();
        CHECK(r.delivered.empty(), "case b: boundary parked behind the unsealed marker");
        CHECK(r.arb.hasPending(), "case b: pending while parked");
        r.WorkerFillSeal(m1, {3});
        CHECK(r.arb.hasSealedReady(), "case b: sealed marker awaits collection");
        r.arb.drain();
        std::vector<Rec> exp = {{false, 3}, {true, 200}};
        CHECK(SameRecs(r.delivered, exp), "case b: seal-after-enqueue order");
        CHECK(!r.arb.hasSealedReady(), "case b: collected");
    }

    // (c) drain stops at the first unsealed marker.
    {
        TestGifDeferBoundaryOrder r;
        auto m1 = r.arb.openMarker();
        auto m2 = r.arb.openMarker();
        r.Boundary(300);
        r.WorkerFillSeal(m1, {4, 5});
        r.arb.drain();
        std::vector<Rec> exp1 = {{false, 4}, {false, 5}};
        CHECK(SameRecs(r.delivered, exp1), "case c: sealed prefix only");
        r.WorkerFillSeal(m2, {6});
        r.arb.drain();
        std::vector<Rec> exp2 = {{false, 4}, {false, 5}, {false, 6}, {true, 300}};
        CHECK(SameRecs(r.delivered, exp2), "case c: tail + boundary after the late seal");
    }

    // (d) two boundaries in flight.
    {
        TestGifDeferBoundaryOrder r;
        auto m1 = r.arb.openMarker();
        r.Boundary(400);
        auto m2 = r.arb.openMarker();
        r.Boundary(401);
        r.WorkerFillSeal(m1, {7});
        r.WorkerFillSeal(m2, {8, 9});
        r.arb.drain();
        std::vector<Rec> exp = {{false, 7}, {true, 400}, {false, 8}, {false, 9}, {true, 401}};
        CHECK(SameRecs(r.delivered, exp), "case d: two boundaries in flight");
    }

    // (e) an EE-side packet submitted AFTER a boundary stays after it.
    {
        TestGifDeferBoundaryOrder r;
        auto m1 = r.arb.openMarker();
        r.WorkerFillSeal(m1, {10});
        r.Boundary(500);
        r.Submit(11, GifPath::Path3);
        r.arb.drain();
        std::vector<Rec> exp = {{false, 10}, {true, 500}, {false, 11}};
        CHECK(SameRecs(r.delivered, exp), "case e: EE packet after the boundary");
        // PATH3 delivered -> one FinishPath3 recorded, after the PATH3 packet.
        size_t finishes = 0;
        for (const RegisterOp& op : r.ops.Ops()) finishes += op.kind == static_cast<uint8_t>(RegisterOpKind::Finish);
        CHECK(finishes == 1, "case e: FinishPath3 once for the PATH3 pass");
    }

    // (f) the feed-note tripwire.
    {
        TestGifDeferBoundaryOrder r;
        const uint64_t v0 = r.arb.boundary_stats().order_violations;
        r.arb.noteFeedForBoundary();
        r.arb.openBoundary(600, 0);
        CHECK(r.arb.boundary_stats().order_violations == v0, "case f: noted feed -> zero violations");
        r.arb.openBoundary(601, 0);  // no note: 600 consumed it
        r.arb.noteFeedForBoundary();  // late note
        CHECK(r.arb.boundary_stats().order_violations == v0 + 1, "case f: swapped order -> exactly one violation");
        r.arb.openBoundary(602, 0);  // consumes the late note
        CHECK(r.arb.boundary_stats().order_violations == v0 + 1, "case f: one note per boundary");
        r.arb.openBoundary(603, 0);
        CHECK(r.arb.boundary_stats().order_violations == v0 + 2, "case f: the flag is one-shot");
        r.arb.drain();
        CHECK(r.delivered.size() == 4 && r.delivered[0].boundary && r.delivered[1].boundary &&
                  r.delivered[2].boundary && r.delivered[3].boundary,
              "case f: violations do not affect delivery");
        CHECK(r.arb.boundary_stats().enqueued == 4 && r.arb.boundary_stats().delivered == 4,
              "case f: enqueued == delivered");
        r.arb.noteFeedForBoundary();
        r.arb.clear();  // a pipeline reset drops the pending note
        r.arb.openBoundary(604, 0);
        CHECK(r.arb.boundary_stats().order_violations == v0 + 3, "case f: clear() resets the note");
    }

    // (g) seq 0 maps to 1; hook order == delivery order; nothing off-thread.
    {
        TestGifDeferBoundaryOrder r;
        r.Boundary(0);
        r.arb.drain();
        CHECK(r.hooks.size() == 1 && r.hooks[0] == 1, "case g: boundary seq 0 is delivered as 1");
        CHECK(r.hook_offthread == 0, "case g: hook on the drain owner thread");
    }

    // (h) clear(): unsealed markers detach and settle their bytes at the seal.
    {
        TestGifDeferBoundaryOrder r;
        auto m1 = r.arb.openMarker();
        auto m2 = r.arb.openMarker();
        r.WorkerFillSeal(m1, {20, 21});
        r.Submit(22, GifPath::Path3);
        r.Boundary(700);
        CHECK(r.arb.PendingSlots() == 4, "case h: four slots held");
        r.arb.clear();
        CHECK(r.arb.PendingSlots() == 0 && !r.arb.hasPending() && !r.arb.hasSealedReady(), "case h: cleared");
        CHECK(r.arb.PendingBytes() == 0, "case h: sealed + plain bytes settled at clear");
        r.WorkerFillSeal(m2, {23});  // fills after detach: bytes settle at this seal
        CHECK(r.arb.PendingBytes() == 0 && !r.arb.hasSealedReady(), "case h: detached seal settles, not collected");
        r.arb.drain();
        CHECK(r.delivered.empty(), "case h: nothing delivered from a cleared inbox");
    }

    // (i) inline vs deferred: identical RegisterOpStream and hook order.
    {
        Script s;
        TestGifDeferBoundaryOrder inline_rig;
        RunScript(inline_rig, s, nullptr);
        TestGifDeferBoundaryOrder deferred_rig;
        GifDrainThread dt(deferred_rig.arb);
        // The drain owner is now the drain thread: record its id from on_start.
        dt.Start(
            [](void* user) {
                auto* r = static_cast<TestGifDeferBoundaryOrder*>(user);
                r->owner_tid = std::this_thread::get_id();
            },
            &deferred_rig);
        RunScript(deferred_rig, s, &dt);
        dt.Stop();
        std::vector<Rec> exp = {{false, 1}, {false, 2}, {false, 3}, {false, 5}, {true, 11},
                                {false, 4}, {false, 6}, {true, 12}, {false, 7}};
        CHECK(SameRecs(inline_rig.delivered, exp), "case i: inline delivery order");
        if (g_fails) PrintRecs("inline", inline_rig.delivered);
        CHECK(SameRecs(deferred_rig.delivered, exp), "case i: deferred delivery order");
        if (g_fails) PrintRecs("deferred", deferred_rig.delivered);
        // FinishPath3 is per drain pass (the thread runs more passes than the
        // one inline drain), so the comparison excludes Finish records.
        std::vector<RegisterOp> a = WithoutFinish(inline_rig.ops.Ops());
        std::vector<RegisterOp> b = WithoutFinish(deferred_rig.ops.Ops());
        CHECK(a.size() == b.size() && a.size() + 1 == inline_rig.ops.Ops().size() &&
                  b.size() < deferred_rig.ops.Ops().size() && SameOps(a, b),
              "case i: identical RegisterOpStream (minus per-pass Finish) inline vs deferred");
        CHECK(inline_rig.hooks == deferred_rig.hooks && inline_rig.hooks.size() == 2, "case i: identical hook order");
        CHECK(inline_rig.hook_offthread == 0 && deferred_rig.hook_offthread == 0,
              "case i: hooks on the drain owner thread in both modes");
        std::printf("case i: %zu register ops identical inline vs deferred (Finish: %zu inline, %zu deferred passes)\n",
                    a.size(), inline_rig.ops.Ops().size() - a.size(), deferred_rig.ops.Ops().size() - b.size());
    }

    // (j) 10k randomized soak with a real worker thread.
    {
        std::mt19937 rng(0x51680u);
        int soak_fail = 0;
        uint64_t total_enq = 0, total_del = 0, total_fires = 0, offthread = 0;
        for (int iter = 0; iter < 10000 && !soak_fail; ++iter) {
            TestGifDeferBoundaryOrder r;
            std::vector<Rec> expected;
            uint32_t next_tag = 1;
            uint32_t next_seq = 1;
            struct Job {
                std::shared_ptr<GifMtvuMarker> m;
                std::vector<uint32_t> tags;
            };
            std::vector<Job> jobs;
            std::mutex jm;
            std::atomic<bool> done{false};
            size_t jtaken = 0;
            std::thread worker([&] {
                for (;;) {
                    Job j;
                    {
                        std::lock_guard<std::mutex> lock(jm);
                        if (jtaken < jobs.size()) {
                            j = jobs[jtaken++];
                        } else if (done.load(std::memory_order_acquire)) {
                            return;
                        } else {
                            continue;  // spin: the soak wants tight racing
                        }
                    }
                    r.WorkerFillSeal(j.m, j.tags);
                }
            });
            const int frames = 1 + static_cast<int>(rng() % 3);
            for (int f = 0; f < frames; ++f) {
                const int nmarkers = static_cast<int>(rng() % 4);
                for (int k = 0; k < nmarkers; ++k) {
                    Job j;
                    j.m = r.arb.openMarker();
                    const int npk = static_cast<int>(rng() % 4);
                    for (int p = 0; p < npk; ++p) {
                        j.tags.push_back(next_tag);
                        expected.push_back(Rec{false, next_tag});
                        ++next_tag;
                    }
                    {
                        std::lock_guard<std::mutex> lock(jm);
                        jobs.push_back(std::move(j));
                    }
                    if ((rng() & 3) == 0) r.arb.drain();
                }
                const int nee = static_cast<int>(rng() % 3);
                for (int p = 0; p < nee; ++p) {
                    r.Submit(next_tag, GifPath::Path3);
                    expected.push_back(Rec{false, next_tag});
                    ++next_tag;
                }
                r.Boundary(next_seq);
                expected.push_back(Rec{true, next_seq});
                ++next_seq;
                if ((rng() & 1) == 0) r.arb.drain();
            }
            done.store(true, std::memory_order_release);
            worker.join();
            r.arb.drain();
            r.arb.drain();
            if (!SameRecs(r.delivered, expected)) {
                std::printf("FAIL: soak iter %d order mismatch\n", iter);
                PrintRecs("expected", expected);
                PrintRecs("got     ", r.delivered);
                soak_fail = 1;
            }
            if (r.arb.hasPending() || r.arb.PendingBytes() != 0 || r.arb.PendingSlots() != 0) {
                std::printf("FAIL: soak iter %d left pending slots/bytes\n", iter);
                soak_fail = 1;
            }
            const GifBoundaryStats st = r.arb.boundary_stats();
            total_enq += st.enqueued;
            total_del += st.delivered;
            total_fires += r.hooks.size();
            offthread += r.hook_offthread;
            if (st.order_violations != 0) {
                std::printf("FAIL: soak iter %d counted violations\n", iter);
                soak_fail = 1;
            }
        }
        ++g_checks;
        if (soak_fail) {
            ++g_fails;
        } else {
            std::printf("soak: 10000 randomized iterations, delivered order == submission order with boundaries in position\n");
        }
        CHECK(offthread == 0, "soak: boundaries on the drain owner thread only");
        CHECK(total_enq == total_del && total_del == total_fires, "soak: enqueued == delivered == hook fires");
    }

    std::printf("DeferBoundaryOrder: %d/%d checks passed; RESULT: %s\n", g_checks - g_fails, g_checks,
                g_fails ? "FAIL" : "PASS");
    return g_fails ? 1 : 0;
}

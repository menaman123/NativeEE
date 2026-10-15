// VuMtvuWorker: the MTVU worker against the real VIF + VU1 + GIF arbiter --
// a VIF1 chain (MPG, UNPACK, MSCAL with XGKICK, DIRECT) fed inline and through
// the worker (per span and staged) gives byte-identical VU1 data + micro
// memory, identical registers and an identical arbiter delivery (the same
// RegisterOpStream hash after the drain); the reset-latch segment restores
// the word feed; the Batch record layout and the 32-record / 32 KiB flush
// thresholds; the DMAC thunks; Stop drains queued work and joins; the busy
// view / idle predicates.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vu/tiers/mtvu_backpressure.h"
#include "engine/vu/tiers/mtvu_clock.h"
#include "engine/vu/tiers/mtvu_sync.h"
#include "engine/vu/tiers/mtvu_worker.h"
#include "engine/vu/tiers/runahead_guard.h"
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

struct TestMtvuWorker {
    // ---- stream builders ----
    static uint32_t Vc(uint8_t cmd, uint8_t num, uint16_t imm) {
        return (static_cast<uint32_t>(cmd) << 24) | (static_cast<uint32_t>(num) << 16) | imm;
    }
    // Micro at 0: vi1 = 2; XGKICK vi1; E; NOP.
    static void Micro(std::vector<uint32_t>& w) {
        const uint32_t U_NOP = 0x000002FF, U_NOPE = 0x400002FF, L_NOP = 0x8000033C;
        const uint32_t L_IADDIU = (0x08u << 25) | (1u << 16) | (0u << 11) | 2u;
        const uint32_t L_XGKICK = 0x80000000u | (1u << 11) | (0x1Bu << 6) | 0x3Cu;
        const uint32_t b[8] = {L_IADDIU, U_NOP, L_XGKICK, U_NOP, L_NOP, U_NOPE, L_NOP, U_NOP};
        for (uint32_t x : b) w.push_back(x);
    }
    // A 2-QW PACKED packet: NLOOP 1, EOP, NREGS 1 (RGBAQ) + one seeded register quadword.
    static void Packet(std::vector<uint32_t>& w, uint32_t seed) {
        w.push_back(0x00008001u);
        w.push_back(0x10000000u);
        w.push_back(0x00000001u);
        w.push_back(0);
        for (int i = 0; i < 4; i++) w.push_back(seed * 2654435761u + static_cast<uint32_t>(i) * 0x9E3779B9u);
    }
    // The chain: STCYCL, MPG 4 bundles, UNPACK V4-32 to qw2 (packet), MSCAL 0, DIRECT qwc 2 (packet).
    static std::vector<uint8_t> Chain(uint32_t seed) {
        std::vector<uint32_t> w;
        w.push_back(Vc(0x01, 0, 0x0101));
        w.push_back(Vc(0x4A, 4, 0));
        Micro(w);
        w.push_back(Vc(0x6C, 2, 2));
        Packet(w, seed);
        w.push_back(Vc(0x14, 0, 0));
        w.push_back(Vc(0x50, 0, 2));
        Packet(w, seed ^ 0x55AA);
        w.push_back(Vc(0x11, 0, 0));
        while (w.size() % 4) w.push_back(0);
        std::vector<uint8_t> b(w.size() * 4);
        std::memcpy(b.data(), w.data(), b.size());
        return b;
    }

    struct Rig {
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
              backpressure(guard, MtvuClock::Steady()),
              worker(vif, vu1w, arbiter, backpressure, MtvuSettings{true, 2}),
              sync(worker, arbiter) {}
    };

    struct Snapshot {
        std::vector<uint8_t> data, micro;
        VUregs regs;
        uint64_t ops_hash;
        size_t ops_count;
    };
    static Snapshot Snap(Rig& r, VuUnit& unit) {
        Snapshot s;
        s.data.assign(unit.state().data, unit.state().data + Vu1State::kMemSize);
        s.micro.assign(unit.state().micro, unit.state().micro + Vu1State::kMemSize);
        s.regs = unit.regs();
        s.ops_hash = r.ops.Hash();
        s.ops_count = r.ops.Count();
        return s;
    }
    static bool Same(const Snapshot& a, const Snapshot& b) {
        return a.data == b.data && a.micro == b.micro && std::memcmp(&a.regs, &b.regs, sizeof(VUregs)) == 0 &&
               a.ops_hash == b.ops_hash && a.ops_count == b.ops_count;
    }

    // Feed one chain in 16-byte spans (the DMAC hands whole quadwords).
    template <typename F>
    static void Spans(const std::vector<uint8_t>& chain, F&& feed) {
        for (size_t o = 0; o < chain.size(); o += 16) feed(chain.data() + o, 16);
    }

    static void InlineVsWorker() {
        const std::vector<uint8_t> chain = Chain(7);
        // Inline: the inline unit, drained on this thread.
        Snapshot inline_end;
        {
            Rig r;
            r.vif.Reset();
            r.vu1.regs().Reset();
            r.vif.ResetLatch(1);
            Spans(chain, [&](const uint8_t* d, size_t n) { r.vif.ProcessData(1, d, n); });
            r.arbiter.drain();
            inline_end = Snap(r, r.vu1);
            CHECK(inline_end.ops_count > 0, "the inline chain produced register ops");
        }
        // Worker, per span.
        {
            Rig r;
            r.vif.Reset();
            r.worker.Start();
            CHECK(r.worker.IsRunning(), "worker running after Start");
            CHECK(&r.vif.CtxFor(1) == &r.vu1w, "Start binds VIF1 to the worker unit");
            r.worker.SubmitResetLatch();
            Spans(chain, [&](const uint8_t* d, size_t n) { r.worker.SubmitVif1(d, n); });
            r.sync.Sync();
            CHECK(!r.worker.IsBusy(), "idle after Sync");
            r.arbiter.drain();
            CHECK(Same(inline_end, Snap(r, r.vu1w)), "per-span worker == inline (memory, regs, op hash)");
            r.worker.Stop();
            CHECK(!r.worker.IsRunning(), "stopped");
            CHECK(&r.vif.CtxFor(1) == &r.vu1, "Stop rebinds the inline unit");
        }
        // Worker, staged (one Batch).
        {
            Rig r;
            r.vif.Reset();
            r.worker.Start();
            r.worker.SubmitResetLatch();
            Spans(chain, [&](const uint8_t* d, size_t n) { r.worker.SubmitVif1Staged(d, n); });
            CHECK(r.worker.BatchSegmentsProcessed() == 0, "nothing flushed before the walk exit");
            r.worker.FlushVif1Staged();
            r.sync.Sync();
            CHECK(r.worker.BatchSegmentsProcessed() == 1, "one Batch segment per walk");
            r.arbiter.drain();
            CHECK(Same(inline_end, Snap(r, r.vu1w)), "staged worker == inline");
        }
        // Worker, via the DMAC thunks (same shape the ChainWalker seam takes).
        {
            Rig r;
            r.vif.Reset();
            r.worker.Start();
            MtvuWorker::StagedResetThunk(&r.worker);
            Spans(chain, [&](const uint8_t* d, size_t n) { MtvuWorker::StagedFeedThunk(&r.worker, d, n, false); });
            MtvuWorker::StagedFlushThunk(&r.worker);
            r.sync.Sync();
            r.arbiter.drain();
            CHECK(Same(inline_end, Snap(r, r.vu1w)), "thunk-fed worker == inline");
        }
    }

    // A TTE quadword's pad words are not payload: the worker's tte bracket
    // matches the inline SetTteFeed bracket (an UNPACK started by a TTE word
    // continues into the next span).
    static void TteBracket() {
        std::vector<uint32_t> tte = {Vc(0x6C, 1, 5), 0x11111111u, 0, 0};  // UNPACK V4-32 qw5 num 1 + 1 payload word
        std::vector<uint32_t> rest = {0x22222222u, 0x33333333u, 0x44444444u, 0};
        std::vector<uint8_t> tq(16), rq(16);
        std::memcpy(tq.data(), tte.data(), 16);
        std::memcpy(rq.data(), rest.data(), 16);
        Snapshot a, b;
        {
            Rig r;
            r.vif.Reset();
            r.vif.SetTteFeed(1, true);
            r.vif.ProcessData(1, tq.data(), 16);
            r.vif.SetTteFeed(1, false);
            r.vif.ProcessData(1, rq.data(), 16);
            a = Snap(r, r.vu1);
        }
        {
            Rig r;
            r.vif.Reset();
            r.worker.Start();
            r.worker.SubmitVif1(tq.data(), 16, true);
            r.worker.SubmitVif1(rq.data(), 16, false);
            r.sync.Sync();
            b = Snap(r, r.vu1w);
        }
        CHECK(a.data == b.data, "worker TTE bracket == inline");
    }

    // The reset-latch segment: a partial quadword left in the word latch is
    // dropped at the next walk head, inline and through the worker alike.
    static void ResetLatch() {
        std::vector<uint32_t> partial = {Vc(0x6C, 1, 9), 0xAAAAAAAAu};  // 8 bytes: an incomplete quadword
        std::vector<uint32_t> full = {Vc(0x6C, 1, 3), 1, 2, 3};
        std::vector<uint32_t> tail = {4, 0, 0, 0};
        std::vector<uint8_t> p(8), f(16), t(16);
        std::memcpy(p.data(), partial.data(), 8);
        std::memcpy(f.data(), full.data(), 16);
        std::memcpy(t.data(), tail.data(), 16);
        Snapshot a, b;
        {
            Rig r;
            r.vif.Reset();
            r.vif.ProcessData(1, p.data(), 8);
            r.vif.ResetLatch(1);
            r.vif.ProcessData(1, f.data(), 16);
            r.vif.ProcessData(1, t.data(), 16);
            a = Snap(r, r.vu1);
        }
        {
            Rig r;
            r.vif.Reset();
            r.worker.Start();
            r.worker.SubmitVif1(p.data(), 8);
            r.worker.SubmitResetLatch();
            r.worker.SubmitVif1(f.data(), 16);
            r.worker.SubmitVif1(t.data(), 16);
            r.sync.Sync();
            b = Snap(r, r.vu1w);
        }
        CHECK(a.data == b.data, "reset-latch segment == inline ResetLatch");
        uint32_t got[4];
        std::memcpy(got, b.data.data() + 3 * 16, 16);
        CHECK(got[0] == 1 && got[1] == 2 && got[2] == 3 && got[3] == 4, "the full quadword landed at qw3");
    }

    // Record layout + flush thresholds. The staged buffer is producer-local
    // and observable before the flush.
    static void BatchLayout() {
        Rig r;
        r.vif.Reset();
        r.worker.Start();
        uint8_t qw[16] = {0};
        r.worker.SubmitVif1Staged(qw, 16, true);
        CHECK(r.worker.StagedRecords() == 1, "one staged record");
        CHECK(r.worker.StagedBytes() == MtvuWorker::kBatchRecordHeader + 16, "[u32 len][u8 tte][pad3][bytes]");
        for (int i = 1; i < 31; i++) r.worker.SubmitVif1Staged(qw, 16, false);
        CHECK(r.worker.StagedRecords() == 31, "31 records stay staged");
        CHECK(r.worker.BatchSegmentsProcessed() == 0, "no flush below 32 records");
        r.worker.SubmitVif1Staged(qw, 16, false);
        CHECK(r.worker.StagedRecords() == 0, "the 32nd record flushes");
        r.sync.Sync();
        CHECK(r.worker.BatchSegmentsProcessed() == 1, "one Batch processed at 32 records");
        // Bytes threshold: one span of 32 KiB - 8 fills exactly to the limit.
        std::vector<uint8_t> big(MtvuWorker::kBatchFlushBytes - MtvuWorker::kBatchRecordHeader - 16, 0);
        r.worker.SubmitVif1Staged(big.data(), big.size(), false);
        CHECK(r.worker.StagedRecords() == 1, "below the byte limit stays staged");
        r.worker.SubmitVif1Staged(qw, 16, false);
        CHECK(r.worker.StagedRecords() == 0, "reaching 32 KiB flushes");
        r.sync.Sync();
        CHECK(r.worker.BatchSegmentsProcessed() == 2, "second Batch processed");
        // The staging buffer is the one buffer that keeps its high-water
        // capacity (>= the 32 KiB batch); a small batch after it still stages
        // in place (no reallocation: the capacity is unchanged, StagedBytes the
        // small record) and its flushed segment carries only its bytes
        // (PendingBytes is the payload ledger; the segment is right-sized).
        const size_t capacity = r.worker.StagedCapacity();
        CHECK(capacity >= MtvuWorker::kBatchFlushBytes, "the staging capacity reached the byte limit");
        r.worker.SubmitVif1Staged(qw, 16, false);
        CHECK(r.worker.StagedBytes() == MtvuWorker::kBatchRecordHeader + 16 && r.worker.StagedCapacity() == capacity,
              "a small record stages in the retained buffer");
        r.worker.FlushVif1Staged();
        r.sync.Sync();
        CHECK(r.worker.BatchSegmentsProcessed() == 3 && r.worker.StagedCapacity() == capacity &&
              r.worker.StagedBytes() == 0, "the flush hands off a copy and keeps the staging capacity");
        r.worker.FlushVif1Staged();
        r.sync.Sync();
        CHECK(r.worker.BatchSegmentsProcessed() == 3, "an empty flush submits nothing");
        // A flush after Stop drops the stage.
        r.worker.SubmitVif1Staged(qw, 16, false);
        r.worker.Stop();
        r.worker.FlushVif1Staged();
        CHECK(r.worker.StagedRecords() == 0 && r.worker.StagedBytes() == 0, "flush on a stopped worker drops");
        r.worker.SubmitVif1(qw, 16);
        CHECK(!r.worker.IsBusy(), "a submit on a stopped worker is dropped");
    }

    // Stop drains what is queued (every marker sealed, ledger 0) and joins.
    static void StopDrains() {
        Rig r;
        r.vif.Reset();
        r.worker.Start();
        const std::vector<uint8_t> chain = Chain(3);
        for (int i = 0; i < 50; i++) {
            r.worker.SubmitResetLatch();
            Spans(chain, [&](const uint8_t* d, size_t n) { r.worker.SubmitVif1(d, n); });
        }
        r.worker.Stop();
        CHECK(!r.worker.IsRunning(), "joined");
        CHECK(r.worker.PendingBytes() == 0, "segment ledger settled");
        CHECK(r.worker.WorkerIdle(), "nothing in flight");
        r.arbiter.drain();
        CHECK(r.arbiter.PendingBytes() == 0 && r.arbiter.PendingSlots() == 0, "every marker was sealed and drained");
        CHECK(r.ops.Count() > 0, "the kicks were delivered");
        r.worker.Start();
        CHECK(r.worker.IsRunning(), "restart");
        r.worker.Stop();
    }

    // KICK DROP (the clock contract): a frame the backpressure marks dropped is
    // replayed with the worker unit's kick drop set -- the VIF stream runs (the
    // MPG lands in micro memory, the UNPACK in data memory, the DIRECT packet
    // reaches the arbiter) but no program executes (no XGKICK packet, the
    // kick counted) -- and the frame marks measure the worker's lag.
    static void KickDrop() {
        const std::vector<uint8_t> chain = Chain(11);
        Snapshot full;
        {
            Rig r;
            r.vif.Reset();
            r.worker.Start();
            r.worker.SubmitResetLatch();
            Spans(chain, [&](const uint8_t* d, size_t n) { r.worker.SubmitVif1Staged(d, n); });
            r.worker.FlushVif1Staged();
            r.sync.Sync();
            r.arbiter.drain();
            full = Snap(r, r.vu1w);
            CHECK(full.ops_count > 0 && r.vu1w.DroppedKicks() == 0, "admitted frame: the program ran, no kick dropped");
        }
        {
            Rig r;
            r.vif.Reset();
            r.worker.Start();
            r.backpressure.SetDropMode(true);
            // The frame sync with the worker idle: lag 0 -> admitted; a scripted
            // lag past the bound marks the next frame dropped.
            r.worker.SubmitFrameMark();
            r.sync.Sync();
            CHECK(r.worker.FrameMarksPushed() == 1 && r.worker.FrameMarksPassed() == 1 && r.worker.FrameLag() == 0,
                  "a frame mark pushed and passed; lag 0");
            CHECK(r.backpressure.NoteFrameSync(MtvuBackpressure::kDropLagFrames + 1), "lag over the bound: the next frame is dropped");
            r.worker.SubmitResetLatch();
            Spans(chain, [&](const uint8_t* d, size_t n) { r.worker.SubmitVif1Staged(d, n); });
            r.worker.FlushVif1Staged();
            r.sync.Sync();
            r.arbiter.drain();
            const Snapshot dropped = Snap(r, r.vu1w);
            CHECK(dropped.micro == full.micro, "dropped frame: the MPG landed (micro memory identical)");
            CHECK(dropped.data == full.data, "dropped frame: the UNPACK landed (data memory identical)");
            CHECK(r.vu1w.DroppedKicks() == 1, "dropped frame: the MSCAL was counted, not run");
            CHECK(dropped.ops_count > 0 && dropped.ops_count < full.ops_count,
                  "dropped frame: the DIRECT packet was delivered, the XGKICK packet was not");
            CHECK(r.backpressure.DropSegments() > 0 && r.backpressure.DropFrames() == 1, "the drop census counts the frame");
            CHECK(!r.vu1w.KickDrop(), "the kick drop is cleared after the segment");
            // The next frame, admitted (lag 0 again), runs its program.
            r.worker.SubmitFrameMark();
            r.sync.Sync();
            CHECK(!r.backpressure.NoteFrameSync(r.worker.FrameLag()), "lag 0: admitted");
            Spans(chain, [&](const uint8_t* d, size_t n) { r.worker.SubmitVif1Staged(d, n); });
            r.worker.FlushVif1Staged();
            r.sync.Sync();
            r.arbiter.drain();
            CHECK(r.vu1w.DroppedKicks() == 1 && r.ops.Count() > dropped.ops_count, "admitted frame after a drop: the program ran");
            r.worker.Stop();
        }
    }

    static void Run() {
        InlineVsWorker();
        TteBracket();
        ResetLatch();
        BatchLayout();
        StopDrains();
        KickDrop();
    }
};

}  // namespace

int main() {
    TestMtvuWorker::Run();
    if (g_fail) {
        std::fprintf(stderr, "test_mtvu_worker: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("test_mtvu_worker: ALL PASS\n");
    return 0;
}

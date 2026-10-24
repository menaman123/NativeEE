// MtvuStackEarlyFinish (E425/E426): the stack's side of the early FINISH over a
// real worker + drain thread.
//  * A staged VIF1 span whose DIRECT carries an A+D FINISH pre-raises ONCE at the
//    staging call (the raiser runs on the staging thread, the arbiter's count is
//    1) before the worker has replayed anything; the worker's replay reaches the
//    parser on the drain thread, the parser's consumer takes the count, and
//    Quiesce succeeds with nothing outstanding ("savestate quiesce drains
//    pre_raised to 0").
//  * A pre-raised FINISH that is never consumed HOLDS the quiesce (false with a
//    reason) -- the INTC / GIFS sections are not written over a FINISH in
//    flight; ResyncEarlyFinish (a restore) drops it and the quiesce passes.
//  * With the early FINISH off nothing is pre-raised; the parser raises as before.
//  * E426: a deferred FrameSync declares the frame; the boundary slot's delivery
//    by the drain owner presents it (the runahead guard's counters move as a pair).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "app/host/mtvu_stack.h"
#include "counting_gs_sink.h"
#include "engine/dmac/chain_walker.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/vif/vif.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                            \
        }                                                                        \
    } while (0)

using Bytes = std::vector<uint8_t>;
void put_u64(Bytes& v, uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
void put_u32(Bytes& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }

// The parser's side of GsPrivRegs::Finish: count the FINISH writes and, when
// asked, consume the arbiter's pre-raised count as the title's consumer does.
class FinishSink final : public CountingGsSink {
public:
    void WriteRegister(uint8_t reg, uint64_t value) override {
        CountingGsSink::WriteRegister(reg, value);
        if (reg != 0x61) return;
        ++finishes;
        if (consume && arbiter) {
            if (arbiter->ConsumePreRaisedFinish()) ++consumed;
            else ++parser_raised;
        } else {
            ++parser_raised;
        }
    }
    GifArbiter* arbiter = nullptr;
    bool consume = true;
    std::atomic<uint64_t> finishes{0}, consumed{0}, parser_raised{0};
};

// A VIF1 span: [DIRECT 3 qw][pad x3] + a PACKED A+D packet {TEXFLUSH, FINISH}.
Bytes direct_finish_span() {
    Bytes v;
    put_u32(v, (0x50u << 24) | 3u);
    put_u32(v, 0); put_u32(v, 0); put_u32(v, 0);
    put_u64(v, 2ull | (1ull << 15) | (1ull << 60));   // nloop 2, eop, nregs 1
    put_u64(v, 0x0E);                                   // A+D
    put_u64(v, 0); put_u64(v, 0x3F);                    // TEXFLUSH
    put_u64(v, 0); put_u64(v, 0x61);                    // FINISH
    return v;
}
// A span with a DIRECT that writes no FINISH.
Bytes direct_plain_span() {
    Bytes v;
    put_u32(v, (0x50u << 24) | 2u);
    put_u32(v, 0); put_u32(v, 0); put_u32(v, 0);
    put_u64(v, 1ull | (1ull << 15) | (1ull << 60));
    put_u64(v, 0x0E);
    put_u64(v, 0); put_u64(v, 0x3F);
    return v;
}

struct Rig {
    MainMemory ram;
    Scratchpad spad;
    GifRegs gif_regs;
    FinishSink sink;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0, vu1;
    Vif vif;
    ChainWalker walker;
    std::atomic<bool> running{true};
    MtvuStack stack;
    int raises = 0;
    std::thread::id raise_tid;
    Rig()
        : parser(sink, gif_regs),
          arbiter(parser),
          vu0(0),
          vu1(1, &arbiter),
          vif(vu0, vu1, arbiter, gif_regs),
          walker(ram, spad, vif, arbiter),
          stack(vif, walker, arbiter, vu1.dispatch(), vu1.dispatch().settings(), false, MtvuSettings{true, 2}, &running) {
        sink.arbiter = &arbiter;
    }
    static void Raise(void* user) {
        auto* r = static_cast<Rig*>(user);
        ++r->raises;
        r->raise_tid = std::this_thread::get_id();
    }
    void WaitFinishes(uint64_t n) {
        for (int i = 0; i < 4000 && sink.finishes.load() < n; ++i) std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
};

void pre_raise_then_quiesce() {
    Rig r;
    r.stack.EnableEarlyFinish(&Rig::Raise, &r);
    CHECK(r.stack.EarlyFinishOn());
    r.stack.Start(/*with_drain_thread=*/true);
    CHECK(r.stack.drain().Running());
    const Bytes span = direct_finish_span();
    r.stack.worker().SubmitVif1Staged(span.data(), span.size(), false);
    CHECK(r.raises == 1);                                      // at the staging call ...
    CHECK(r.raise_tid == std::this_thread::get_id());          // ... on the staging thread
    CHECK(r.arbiter.PreRaisedFinish() == 1);
    CHECK(r.sink.finishes.load() == 0);                        // nothing replayed yet (still staged)
    r.stack.worker().FlushVif1Staged();
    r.WaitFinishes(1);
    CHECK(r.sink.finishes.load() == 1);                        // the parser reached the write on the drain thread
    CHECK(r.sink.consumed.load() == 1 && r.sink.parser_raised.load() == 0);
    CHECK(r.raises == 1);                                      // one edge per FINISH write
    std::string why;
    CHECK(r.stack.Quiesce(&why));
    CHECK(r.arbiter.PreRaisedFinish() == 0);
    CHECK(r.arbiter.PreRaisedFinishTotal() == 1 && r.arbiter.ConsumedFinishTotal() == 1);
    // A span without FINISH: no raise.
    const Bytes plain = direct_plain_span();
    r.stack.worker().SubmitVif1Staged(plain.data(), plain.size(), false);
    r.stack.worker().FlushVif1Staged();
    CHECK(r.raises == 1);
    CHECK(r.stack.Quiesce(&why));
    // The non-staged path (SubmitVif1) is scanned too.
    r.stack.worker().SubmitVif1(span.data(), span.size(), false);
    CHECK(r.raises == 2 && r.arbiter.PreRaisedFinish() == 1);
    r.WaitFinishes(3);
    CHECK(r.stack.Quiesce(&why));
    CHECK(r.sink.consumed.load() == 2 && r.arbiter.PreRaisedFinish() == 0);
    r.stack.Stop();
}

void unconsumed_holds_quiesce() {
    Rig r;
    r.sink.consume = false;   // the parser's Finish raises instead of consuming: the count is left standing
    r.stack.EnableEarlyFinish(&Rig::Raise, &r);
    r.stack.Start(true);
    const Bytes span = direct_finish_span();
    r.stack.worker().SubmitVif1Staged(span.data(), span.size(), false);
    r.stack.worker().FlushVif1Staged();
    r.WaitFinishes(1);
    CHECK(r.arbiter.PreRaisedFinish() == 1);
    std::string why;
    CHECK(!r.stack.Quiesce(&why));                             // a FINISH in flight: not admissible
    CHECK(!why.empty());
    r.stack.ResyncEarlyFinish(1, 1);                            // a restore: nothing in flight
    CHECK(r.arbiter.PreRaisedFinish() == 0);
    CHECK(r.stack.Quiesce(&why));
    r.stack.Stop();
}

void off_is_inert() {
    Rig r;
    r.stack.Start(true);
    CHECK(!r.stack.EarlyFinishOn());
    const Bytes span = direct_finish_span();
    r.stack.worker().SubmitVif1Staged(span.data(), span.size(), false);
    r.stack.worker().FlushVif1Staged();
    r.WaitFinishes(1);
    CHECK(r.raises == 0 && r.arbiter.PreRaisedFinish() == 0);
    CHECK(r.sink.finishes.load() == 1 && r.sink.parser_raised.load() == 1);
    std::string why;
    CHECK(r.stack.Quiesce(&why));
    r.stack.Stop();
}

void deferred_boundary_presents() {
    Rig r;
    r.stack.EnableEarlyFinish(&Rig::Raise, &r);
    r.stack.SetDeferBoundaries(true);
    r.stack.Start(true);
    const Bytes span = direct_finish_span();
    r.stack.worker().SubmitVif1Staged(span.data(), span.size(), false);
    r.stack.worker().FlushVif1Staged();
    r.arbiter.noteFeedForBoundary();
    r.stack.FrameSync(1, 0);
    CHECK(r.stack.Declared() == 1);
    CHECK(r.stack.DeferredBoundaries() == 1 && r.stack.TerminalSyncs() == 0);
    for (int i = 0; i < 4000 && r.stack.Presented() < 1; ++i) std::this_thread::sleep_for(std::chrono::microseconds(500));
    CHECK(r.stack.Presented() == 1);                           // the boundary slot delivered behind the frame's packets
    CHECK(r.arbiter.boundary_stats().delivered == 1 && r.arbiter.boundary_stats().order_violations == 0);
    std::string why;
    CHECK(r.stack.Quiesce(&why));
    CHECK(r.sink.finishes.load() == 1 && r.sink.consumed.load() == 1);
    r.stack.Stop();
}

}  // namespace

int main() {
    pre_raise_then_quiesce();
    unconsumed_holds_quiesce();
    off_is_inert();
    deferred_boundary_presents();
    std::printf("test_mtvu_stack_early_finish: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

// GifArbiterOrder: the synchronous arbiter delivers packets to the parser in
// submission order whatever their path, tells the parser a PATH3 pass finished
// exactly once per drain that delivered PATH3, drops sub-tag payloads, keeps
// the pending-bytes ledger, and recycles staging-class buffers into a pool of
// at most kPoolMax.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "counting_gs_sink.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/gif_tag.h"
#include "engine/gs/hle/ir/register_op_stream.h"

namespace {

int g_fail = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                            \
        }                                                                        \
    } while (0)

using Bytes = std::vector<uint8_t>;

void put_u64(Bytes& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

// A PACKED tag with one A+D descriptor and `nloop` writes of TEXFLUSH (0x3F)
// carrying `mark + i`, EOP set: nloop + 2 recorded ops per packet.
Bytes ad_packet(uint16_t nloop, uint64_t mark) {
    Bytes v;
    const uint64_t lo = static_cast<uint64_t>(nloop) | (1ull << 15) | (1ull << 60);
    put_u64(v, lo);
    put_u64(v, static_cast<uint64_t>(GsReg::Ad));
    for (uint16_t i = 0; i < nloop; ++i) {
        put_u64(v, mark + i);
        put_u64(v, static_cast<uint64_t>(GsReg::Texflush));
    }
    return v;
}

// An IMAGE tag with `nloop` quadwords, EOP set.
Bytes image_packet(uint16_t nloop, uint64_t mark) {
    Bytes v;
    put_u64(v, static_cast<uint64_t>(nloop) | (1ull << 15) | (2ull << 58));
    put_u64(v, 0);
    for (uint16_t i = 0; i < nloop; ++i) {
        put_u64(v, mark + i);
        put_u64(v, ~(mark + i));
    }
    return v;
}

// The rig: recording sink + counting sink are exercised in separate rigs.
struct TestGifArbiter {
    GifRegs regs;
    RegisterOpStream ops;
    GifParser parser;
    GifArbiter arbiter;
    TestGifArbiter() : parser(ops, regs), arbiter(parser) {}
};

bool is_kind(const RegisterOp& op, RegisterOpKind k) { return op.kind == static_cast<uint8_t>(k); }

void submission_order() {
    TestGifArbiter t;
    const Bytes p2 = ad_packet(2, 0x200);
    const Bytes p3 = ad_packet(3, 0x300);
    const Bytes p1 = ad_packet(1, 0x100);
    t.arbiter.submit(GifPath::Path2, p2.data(), p2.size(), true);
    t.arbiter.submit(GifPath::Path3, p3.data(), p3.size());
    Bytes owned = p1;
    t.arbiter.submitOwned(GifPath::Path1, std::move(owned));
    CHECK(t.arbiter.hasPending());
    CHECK(t.arbiter.PendingBytes() == p2.size() + p3.size() + p1.size());
    CHECK(t.ops.Count() == 0);  // nothing reaches the parser before drain
    t.arbiter.drain();
    CHECK(!t.arbiter.hasPending() && t.arbiter.PendingBytes() == 0 && t.arbiter.DrainedPackets() == 3);
    const std::vector<RegisterOp>& ops = t.ops.Ops();
    // PATH2: Tag, 2 Reg, Eop; PATH3: Tag, 3 Reg, Eop; PATH1: Tag, Reg, Eop; then Finish.
    CHECK(ops.size() == 4 + 5 + 3 + 1);
    CHECK(is_kind(ops[0], RegisterOpKind::Tag) && ops[0].path == 2);
    CHECK(is_kind(ops[1], RegisterOpKind::Reg) && ops[1].lo == 0x200 && ops[2].lo == 0x201);
    CHECK(is_kind(ops[4], RegisterOpKind::Tag) && ops[4].path == 3);
    CHECK(ops[5].lo == 0x300 && ops[6].lo == 0x301 && ops[7].lo == 0x302);
    CHECK(is_kind(ops[9], RegisterOpKind::Tag) && ops[9].path == 1 && ops[10].lo == 0x100);
    CHECK(is_kind(ops[12], RegisterOpKind::Finish));
    std::fprintf(stderr, "order: PATH2, PATH3, PATH1 drained in submission order, finish once: ok\n");

    // A second drain with nothing queued is a no-op.
    t.arbiter.drain();
    CHECK(t.ops.Count() == 13 && t.arbiter.DrainedPackets() == 3);

    // A pass without PATH3 does not finish.
    t.arbiter.submit(GifPath::Path1, p1.data(), p1.size());
    t.arbiter.submit(GifPath::Path2, p2.data(), p2.size());
    t.arbiter.drain();
    CHECK(t.ops.Count() == 13 + 3 + 4);
    CHECK(is_kind(t.ops.Ops()[13], RegisterOpKind::Tag) && t.ops.Ops()[13].path == 1);
    CHECK(!is_kind(t.ops.Ops().back(), RegisterOpKind::Finish));

    // Two PATH3 packets in one pass: still one finish.
    t.arbiter.submit(GifPath::Path3, p3.data(), p3.size());
    t.arbiter.submit(GifPath::Path3, p3.data(), p3.size());
    t.arbiter.drain();
    size_t finishes = 0;
    for (const RegisterOp& op : t.ops.Ops()) finishes += is_kind(op, RegisterOpKind::Finish) ? 1 : 0;
    CHECK(finishes == 2 && t.ops.Count() == 20 + 10 + 1);
}

void finish_counting() {
    GifRegs regs;
    CountingGsSink sink;
    GifParser parser(sink, regs);
    GifArbiter arbiter(parser);
    const Bytes img = image_packet(4, 0x900);
    const Bytes p3 = ad_packet(1, 0x300);
    // PATH3 IMAGE split across two packets: the first pass leaves the transfer
    // mid-IMAGE, so FinishPath3 does not reach the sink; the second completes it.
    Bytes head(img.begin(), img.begin() + 16 + 2 * 16);
    Bytes tail(img.begin() + 16 + 2 * 16, img.end());
    arbiter.submit(GifPath::Path3, head.data(), head.size());
    arbiter.drain();
    CHECK(sink.image_data == 2 && sink.finish_path3 == 0 && parser.Transfer().in_transfer);
    // (a PATH3 packet would reset the stale transfer; PATH1 continues it)
    arbiter.submit(GifPath::Path1, tail.data(), tail.size());
    arbiter.drain();
    CHECK(sink.image_data == 4 && sink.end_of_packet == 1 && sink.finish_path3 == 0);
    arbiter.submit(GifPath::Path3, p3.data(), p3.size());
    arbiter.drain();
    CHECK(sink.finish_path3 == 1 && sink.write_register == 1 && sink.last_value == 0x300);
    // Sub-tag payloads are dropped at submit.
    arbiter.submit(GifPath::Path3, p3.data(), 15);
    Bytes small(15, 0);
    arbiter.submitOwned(GifPath::Path2, std::move(small));
    arbiter.submit(GifPath::Path3, nullptr, 64);
    CHECK(!arbiter.hasPending() && arbiter.PendingBytes() == 0);
    arbiter.drain();
    CHECK(sink.finish_path3 == 1 && arbiter.DrainedPackets() == 3);
    // clear() discards without delivering.
    arbiter.submit(GifPath::Path3, p3.data(), p3.size());
    CHECK(arbiter.hasPending() && arbiter.PendingBytes() == p3.size());
    arbiter.clear();
    CHECK(!arbiter.hasPending() && arbiter.PendingBytes() == 0);
    arbiter.drain();
    CHECK(sink.write_register == 1 && sink.finish_path3 == 1);
    std::fprintf(stderr, "finish: once per PATH3 pass, never mid-IMAGE; sub-tag payloads dropped; clear ok\n");
}

void pool_recycle() {
    TestGifArbiter t;
    CHECK(t.arbiter.acquireBuffer().capacity() == 0 && t.arbiter.PoolSize() == 0);
    // A drained owned buffer of staging capacity returns to the pool, cleared.
    Bytes staged;
    staged.reserve(0x4000);
    const Bytes p1 = ad_packet(1, 0x100);
    staged.insert(staged.end(), p1.begin(), p1.end());
    t.arbiter.submitOwned(GifPath::Path1, std::move(staged));
    t.arbiter.drain();
    CHECK(t.arbiter.PoolSize() == 1);
    Bytes got = t.arbiter.acquireBuffer();
    CHECK(got.empty() && got.capacity() >= 0x4000 && t.arbiter.PoolSize() == 0);
    // A copied submit (capacity == size < 4 KiB) is freed, not pooled.
    t.arbiter.submit(GifPath::Path2, p1.data(), p1.size());
    t.arbiter.drain();
    CHECK(t.arbiter.PoolSize() == 0);
    // Capacity bounds: below 4 KiB and above 64 KiB are refused; the pool holds kPoolMax.
    Bytes tiny;
    tiny.reserve(0x800);
    t.arbiter.recycleBuffer(std::move(tiny));
    Bytes huge;
    huge.reserve(0x20000);
    t.arbiter.recycleBuffer(std::move(huge));
    CHECK(t.arbiter.PoolSize() == 0);
    for (size_t i = 0; i < GifArbiter::kPoolMax + 2; ++i) {
        Bytes b;
        b.reserve(0x1000);
        b.push_back(1);
        t.arbiter.recycleBuffer(std::move(b));
    }
    CHECK(t.arbiter.PoolSize() == GifArbiter::kPoolMax);
    Bytes b0 = t.arbiter.acquireBuffer();
    CHECK(b0.empty() && b0.capacity() >= 0x1000 && t.arbiter.PoolSize() == GifArbiter::kPoolMax - 1);
    // 64 KiB exactly is still staging class.
    Bytes edge;
    edge.reserve(0x10000);
    t.arbiter.recycleBuffer(std::move(edge));
    CHECK(t.arbiter.PoolSize() == GifArbiter::kPoolMax);
    std::fprintf(stderr, "pool: recycle bounds 4 KiB..64 KiB, kPoolMax=%zu, acquire clears: ok\n", GifArbiter::kPoolMax);
}

}  // namespace

int main() {
    submission_order();
    finish_counting();
    pool_recycle();
    if (g_fail != 0) {
        std::fprintf(stderr, "GIF ARBITER ORDER FAIL: %d\n", g_fail);
        return 1;
    }
    std::fprintf(stderr, "GIF ARBITER ORDER PASS\n");
    return 0;
}

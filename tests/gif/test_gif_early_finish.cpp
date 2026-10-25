// GifEarlyFinish (E425): the EE-side FINISH scans and the arbiter's pre-raised
// count.
//  * GifFinishScan counts exactly the A+D FINISH writes GifParser performs on
//    the same bytes: PACKED A+D 0x61 found, REGLIST / IMAGE / FLG 3 / NLOOP 0 /
//    other registers not, state kept across Feed boundaries (a tag and its
//    payload split anywhere), MidPacket / MidImage, Reset.
//  * VifFinishScan agrees with the real Vif + GifArbiter + GifParser on the
//    FINISH count of randomized VIF1 streams (STCYCL / UNPACK of every format
//    and CL/WL mode, MPG, STMASK / STROW / STCOL, the no-payload codes, DIRECT
//    and DIRECTHL payloads with PACKED / REGLIST / IMAGE packets) cut into
//    random whole-quadword spans with TTE quadwords and latch resets -- the
//    oracle is the parser's own WriteRegister(0x61) count.
//  * GifArbiter: a PATH3 submit from the EE while the worker runs is pre-raised
//    once (the raiser runs on the submitting thread, the count is 1) and
//    delivered in stream order BEHIND the worker's unsealed marker (the
//    GifArbiterOrder case with a pre-raised FINISH in flight); the parser's
//    arrival consumes the count once; inert without a running worker, without
//    a raiser, from the worker's own thread; clear() drops the count.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "counting_gs_sink.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_finish_scan.h"
#include "engine/gif/gif_marker_owner.h"
#include "engine/gif/gif_mtvu_marker.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/gif_tag.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_finish_scan.h"
#include "engine/vif/vif_regs.h"
#include "engine/vif/vif_unpack.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fail = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                            \
        }                                                                        \
    } while (0)

using Bytes = std::vector<uint8_t>;

void put_u64(Bytes& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void put_u32(Bytes& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

// A PACKED tag: nloop, eop, nregs descriptors (nibbles, low first).
uint64_t packed_tag_lo(uint16_t nloop, bool eop, uint32_t nregs) {
    return static_cast<uint64_t>(nloop) | (eop ? (1ull << 15) : 0) | (static_cast<uint64_t>(nregs & 0xF) << 60);
}
uint64_t regs_hi(const std::vector<uint8_t>& regs) {
    uint64_t hi = 0;
    for (size_t i = 0; i < regs.size() && i < 16; ++i) hi |= static_cast<uint64_t>(regs[i] & 0xF) << (4 * i);
    return hi;
}
// One A+D quadword writing register `reg`.
void put_ad(Bytes& v, uint8_t reg, uint64_t value) {
    put_u64(v, value);
    put_u64(v, reg);
}

constexpr uint8_t kAd = 0x0E, kNop = 0x0F, kRgbaq = 0x01;
constexpr uint8_t kFinish = 0x61, kTexflush = 0x3F, kLabel = 0x62;

// A FINISH-counting GsSink over the counting one.
class FinishSink final : public CountingGsSink {
public:
    void WriteRegister(uint8_t reg, uint64_t value) override {
        CountingGsSink::WriteRegister(reg, value);
        if (reg == kFinish) ++finishes;
    }
    uint64_t finishes = 0;
};

// ---- GifFinishScan ----------------------------------------------------------------

void scan_units() {
    // PACKED: A+D FINISH found, A+D TEXFLUSH / NOP descriptors / RGBAQ not.
    {
        GifFinishScan s;
        Bytes p;
        put_u64(p, packed_tag_lo(3, true, 3));
        put_u64(p, regs_hi({kAd, kNop, kRgbaq}));
        for (int i = 0; i < 3; ++i) {
            put_ad(p, i == 1 ? kTexflush : kFinish, 0);   // A+D: FINISH, TEXFLUSH, FINISH
            put_u64(p, 0x61); put_u64(p, 0x61);            // NOP descriptor: data that looks like FINISH
            put_u64(p, 0x61); put_u64(p, 0x61);            // RGBAQ descriptor: same bytes
        }
        CHECK(s.Feed(p.data(), p.size()) == 2);
        CHECK(!s.MidPacket());
    }
    // Split anywhere: the tag alone, then the payload in 8-byte pieces -> the same count.
    {
        Bytes p;
        put_u64(p, packed_tag_lo(2, true, 1));
        put_u64(p, regs_hi({kAd}));
        put_ad(p, kFinish, 0);
        put_ad(p, kFinish, 0);
        for (size_t cut = 8; cut <= p.size(); cut += 8) {
            GifFinishScan s;
            uint32_t n = 0;
            size_t i = 0;
            while (i < p.size()) {
                const size_t take = std::min(cut, p.size() - i);
                n += s.Feed(p.data() + i, take);
                i += take;
            }
            CHECK(n == 2);
            CHECK(!s.MidPacket());
        }
        GifFinishScan s;
        CHECK(s.Feed(p.data(), 16) == 0);
        CHECK(s.MidPacket());        // tag accepted, two loops owed
        CHECK(s.Feed(p.data() + 16, 16) == 1);
        CHECK(s.MidPacket());
        s.Reset();
        CHECK(!s.MidPacket());
    }
    // REGLIST: no FINISH (4-bit descriptors); odd NLOOP x NREG pads the last quadword.
    {
        GifFinishScan s;
        Bytes p;
        put_u64(p, 3 | (1ull << 15) | (1ull << 58) | (1ull << 60));   // nloop 3, REGLIST, nregs 1
        put_u64(p, regs_hi({kAd}));
        put_u64(p, 0x61); put_u64(p, 0x61);
        put_u64(p, 0x61); put_u64(p, 0x61);
        CHECK(s.Feed(p.data(), p.size()) == 0);
        CHECK(!s.MidPacket());       // 3 registers = 2 quadwords
    }
    // IMAGE: data quadwords carrying the FINISH pattern are not register writes.
    {
        GifFinishScan s;
        Bytes p;
        put_u64(p, 2 | (1ull << 15) | (2ull << 58));
        put_u64(p, 0);
        put_ad(p, kFinish, 0);
        CHECK(s.Feed(p.data(), p.size()) == 0);
        CHECK(s.MidPacket() && s.MidImage());
        put_ad(p, kFinish, 0);
        CHECK(s.Feed(p.data() + 32, 16) == 0);
        CHECK(!s.MidPacket());
    }
    // NLOOP 0 completes at once; the next unit is a tag.
    {
        GifFinishScan s;
        Bytes p;
        put_u64(p, packed_tag_lo(0, true, 1));
        put_u64(p, regs_hi({kAd}));
        put_u64(p, packed_tag_lo(1, true, 1));
        put_u64(p, regs_hi({kAd}));
        put_ad(p, kFinish, 0);
        CHECK(s.Feed(p.data(), p.size()) == 1);
        CHECK(!s.MidPacket());
    }
    // FLG 3: quadwords consumed, nothing advances (as the parser).
    {
        GifFinishScan s;
        Bytes p;
        put_u64(p, 1 | (3ull << 58) | (1ull << 60));
        put_u64(p, regs_hi({kAd}));
        put_ad(p, kFinish, 0);
        put_ad(p, kFinish, 0);
        CHECK(s.Feed(p.data(), p.size()) == 0);
        CHECK(s.MidPacket());
    }
}

// ---- VifFinishScan vs the real Vif ------------------------------------------------

struct VifRig {
    GifRegs gif_regs;
    FinishSink sink;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0, vu1;
    Vif vif;
    VifRig() : parser(sink, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs) {}
};

struct Span {
    Bytes bytes;
    bool tte;
    bool reset_latch_before;
};

// A randomized VIF1 stream as the DMAC walker feeds it: whole-quadword spans,
// an occasional TTE quadword (two codes + two zero words) before a data span,
// an occasional latch reset (a walk head). Returns the spans; `expected` is
// not computed here -- the real Vif is the oracle.
std::vector<Span> random_stream(std::mt19937& rng, int commands) {
    Bytes words;   // the whole stream, then cut
    std::vector<size_t> tte_qw;   // quadword indices that are TTE quadwords
    auto put_code = [&](uint32_t cmd, uint32_t num, uint32_t imm, bool irq = false) {
        put_u32(words, (irq ? 0x80000000u : 0u) | (cmd << 24) | (num << 16) | imm);
    };
    auto pad_to_qw = [&] { while (words.size() % 16) put_u32(words, 0); };
    uint32_t cl = 1, wl = 1;
    std::uniform_int_distribution<int> kind(0, 11);
    // One command of kind `k`. Under `tte_second` (the second code of a TTE
    // quadword) the payload, if any, must be one the feed places on the next
    // quadword (UNPACK / MPG payloads longer than the two pad words, DIRECT), so
    // the generator's layout and the feed's agree; the pad words are zero.
    auto emit = [&](int k, bool tte_second) {
        switch (k) {
            case 0: put_code(0x00, 0, 0); break;                     // NOP
            case 1: {                                                // STCYCL
                const uint32_t imm = static_cast<uint32_t>(1 + rng() % 4) | (static_cast<uint32_t>(1 + rng() % 4) << 8);
                put_code(0x01, 0, imm);
                cl = imm & 0xFF; wl = (imm >> 8) & 0xFF;
                break;
            }
            case 2: put_code(0x02 + rng() % 4, 0, rng() & 0x3FF); break;   // OFFSET BASE ITOP STMOD
            case 3: put_code(0x10 + (rng() % 2 ? 1 : 0), 0, 0); break;     // FLUSHE FLUSH
            case 4:                                                  // STMASK (payload 1 word)
                if (tte_second) { put_code(0x00, 0, 0); break; }   // its payload would be the pad word
                put_code(0x20, 0, 0); put_u32(words, rng());
                break;
            case 5: {                                                // STROW STCOL (4 words)
                if (tte_second) { put_code(0x11, 0, 0); break; }   // not aligned by the feed: the pad would be payload
                put_code(0x30 + rng() % 2, 0, 0);
                for (int i = 0; i < 4; ++i) put_u32(words, rng());
                break;
            }
            case 6: {                                                // MPG (num * 2 words)
                const uint32_t num = 2 + rng() % 5;
                put_code(0x4A, num, rng() & 0x7FF);
                if (tte_second) pad_to_qw();
                for (uint32_t i = 0; i < num * 2; ++i) put_u32(words, rng());
                break;
            }
            case 7: case 8: {                                        // UNPACK
                static const uint32_t kFormats[] = {0x0, 0x1, 0x2, 0x4, 0x5, 0x6, 0x8, 0x9, 0xA, 0xC, 0xD, 0xE, 0xF};
                uint32_t fmt = kFormats[rng() % 13];
                uint32_t cmd = 0x60 | fmt | (rng() % 2 ? 0x10 : 0);
                uint32_t num = 1 + rng() % 9;
                uint32_t bytes = VifUnpack::GetUnpackSize(cmd, num, cl, wl);
                if (tte_second) {
                    cmd = 0x6C; num = 2 + rng() % 4;                  // V4-32: >= 32 bytes, never fits the pad
                    bytes = VifUnpack::GetUnpackSize(cmd, num, cl, wl);
                }
                put_code(cmd, num, rng() & 0x3FF);
                if (tte_second) pad_to_qw();
                for (uint32_t i = 0; i < bytes / 4; ++i) put_u32(words, rng());
                break;
            }
            default: {                                               // DIRECT / DIRECTHL
                Bytes gif;
                const int packets = 1 + rng() % 3;
                for (int p = 0; p < packets; ++p) {
                    const int flg = rng() % 4;
                    if (flg == 0 || flg == 3) {   // PACKED
                        const uint32_t nregs = 1 + rng() % 3;
                        const uint16_t nloop = static_cast<uint16_t>(rng() % 4);   // 0 happens
                        std::vector<uint8_t> regs;
                        for (uint32_t i = 0; i < nregs; ++i) regs.push_back(rng() % 2 ? kAd : (rng() % 2 ? kNop : kRgbaq));
                        put_u64(gif, packed_tag_lo(nloop, p + 1 == packets, nregs));
                        put_u64(gif, regs_hi(regs));
                        for (uint16_t l = 0; l < nloop; ++l)
                            for (uint32_t r = 0; r < nregs; ++r) {
                                const uint8_t reg = rng() % 3 == 0 ? kFinish : (rng() % 2 ? kTexflush : kLabel);
                                put_ad(gif, reg, rng());
                            }
                    } else if (flg == 1) {        // REGLIST
                        const uint32_t nregs = 1 + rng() % 3;
                        const uint16_t nloop = static_cast<uint16_t>(1 + rng() % 3);
                        std::vector<uint8_t> regs;
                        for (uint32_t i = 0; i < nregs; ++i) regs.push_back(rng() % 2 ? kAd : kRgbaq);
                        put_u64(gif, static_cast<uint64_t>(nloop) | ((p + 1 == packets) ? (1ull << 15) : 0) | (1ull << 58) |
                                         (static_cast<uint64_t>(nregs) << 60));
                        put_u64(gif, regs_hi(regs));
                        const uint32_t regs_total = nloop * nregs;
                        for (uint32_t i = 0; i < (regs_total + 1) / 2; ++i) { put_u64(gif, kFinish); put_u64(gif, kFinish); }
                    } else {                      // IMAGE
                        const uint16_t nloop = static_cast<uint16_t>(1 + rng() % 3);
                        put_u64(gif, static_cast<uint64_t>(nloop) | ((p + 1 == packets) ? (1ull << 15) : 0) | (2ull << 58));
                        put_u64(gif, 0);
                        for (uint16_t i = 0; i < nloop; ++i) put_ad(gif, kFinish, rng());
                    }
                }
                const uint32_t qwc = static_cast<uint32_t>(gif.size() / 16);
                put_code(rng() % 2 ? 0x50 : 0x51, 0, qwc, rng() % 4 == 0);
                pad_to_qw();
                words.insert(words.end(), gif.begin(), gif.end());
                break;
            }
        }
    };
    for (int c = 0; c < commands; ++c) {
        const int k = kind(rng);
        const bool in_tte = (rng() % 4) == 0 && words.size() % 16 == 0;
        if (in_tte) {
            // A TTE quadword [STCYCL, code, 0, 0]: the second code's payload (if any) on the next quadword.
            tte_qw.push_back(words.size() / 16);
            const uint32_t imm = static_cast<uint32_t>(1 + rng() % 4) | (static_cast<uint32_t>(1 + rng() % 4) << 8);
            put_code(0x01, 0, imm);
            cl = imm & 0xFF; wl = (imm >> 8) & 0xFF;
            emit(k, true);
            pad_to_qw();
            continue;
        }
        emit(k, false);
    }
    pad_to_qw();
    // Cut into spans of random whole-quadword lengths; TTE quadwords are their own span.
    std::vector<Span> spans;
    const size_t nqw = words.size() / 16;
    size_t q = 0;
    size_t tte_i = 0;
    while (q < nqw) {
        const bool is_tte = tte_i < tte_qw.size() && tte_qw[tte_i] == q;
        size_t take = is_tte ? 1 : 1 + rng() % 5;
        if (!is_tte && tte_i < tte_qw.size() && q + take > tte_qw[tte_i]) take = tte_qw[tte_i] - q;
        if (take == 0) take = 1;
        if (q + take > nqw) take = nqw - q;
        Span s;
        s.bytes.assign(words.begin() + static_cast<std::ptrdiff_t>(q * 16), words.begin() + static_cast<std::ptrdiff_t>((q + take) * 16));
        s.tte = is_tte;
        s.reset_latch_before = (rng() % 7) == 0;
        spans.push_back(std::move(s));
        if (is_tte) ++tte_i;
        q += take;
    }
    return spans;
}

void vif_scan_equivalence() {
    uint64_t finishes_total = 0;
    for (uint32_t seed = 1; seed <= 120; ++seed) {
        std::mt19937 rng(seed);
        const std::vector<Span> spans = random_stream(rng, 40);
        VifRig rig;
        VifFinishScan scan;
        uint32_t found = 0;
        for (const Span& s : spans) {
            if (s.reset_latch_before) {
                rig.vif.ResetLatch(1);
                scan.ResetLatch();
            }
            if (s.tte) rig.vif.SetTteFeed(1, true);
            rig.vif.ProcessData(1, s.bytes.data(), s.bytes.size());
            if (s.tte) rig.vif.SetTteFeed(1, false);
            rig.arbiter.drain();
            found += scan.Feed(s.bytes.data(), s.bytes.size(), s.tte);
        }
        if (found != rig.sink.finishes)
            std::fprintf(stderr, "seed %u: scan %u vs parser %llu FINISH writes\n", seed, found,
                         static_cast<unsigned long long>(rig.sink.finishes));
        CHECK(found == rig.sink.finishes);
        finishes_total += rig.sink.finishes;
    }
    CHECK(finishes_total > 200);   // the generator produced a meaningful number of FINISH writes
    std::printf("vif_scan_equivalence: %llu FINISH writes over 120 streams agreed\n",
                static_cast<unsigned long long>(finishes_total));
    // The TTE alignment rule, directed: a TTE quadword [STCYCL, UNPACK V4-32 num 1 (16 bytes), 0, 0]
    // followed by the data span: the payload does not fit the two pad words, so it starts on
    // the data span; a FINISH DIRECT after it is found at the right place.
    {
        VifRig rig;
        VifFinishScan scan;
        Bytes tte;
        put_u32(tte, (0x01u << 24) | 0x0101u);   // STCYCL CL1 WL1
        put_u32(tte, (0x6Cu << 24) | (1u << 16));   // UNPACK V4-32 num 1
        put_u32(tte, 0); put_u32(tte, 0);
        Bytes data;
        for (int i = 0; i < 4; ++i) put_u32(data, 0x50000001u);   // the UNPACK payload: DIRECT-looking words
        put_u32(data, (0x50u << 24) | 1u);   // DIRECT 1 qw
        put_u32(data, 0); put_u32(data, 0); put_u32(data, 0);
        put_u64(data, packed_tag_lo(1, true, 1)); put_u64(data, regs_hi({kAd}));   // wait: 2 qw needed
        put_ad(data, kFinish, 0);
        // DIRECT imm was 1 but the packet is 2 quadwords: fix the code word to 2.
        data[16 + 0] = 2;
        rig.vif.SetTteFeed(1, true);
        rig.vif.ProcessData(1, tte.data(), tte.size());
        rig.vif.SetTteFeed(1, false);
        rig.vif.ProcessData(1, data.data(), data.size());
        rig.arbiter.drain();
        uint32_t n = scan.Feed(tte.data(), tte.size(), true);
        n += scan.Feed(data.data(), data.size(), false);
        CHECK(rig.sink.finishes == 1);
        CHECK(n == 1);
    }
}

// ---- the arbiter's pre-raised count --------------------------------------------------

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

struct ArbRig {
    GifRegs regs;
    FinishSink sink;
    GifParser parser;
    GifArbiter arbiter;
    FakeWorker worker;
    int raises = 0;
    std::thread::id raise_tid;
    ArbRig() : parser(sink, regs), arbiter(parser) {
        arbiter.SetMarkerOwner(&worker);
        arbiter.SetEarlyFinish(&ArbRig::Raise, this);
    }
    static void Raise(void* user) {
        auto* r = static_cast<ArbRig*>(user);
        ++r->raises;
        r->raise_tid = std::this_thread::get_id();
    }
};

Bytes finish_packet(uint64_t mark) {
    Bytes p;
    put_u64(p, packed_tag_lo(2, true, 1));
    put_u64(p, regs_hi({kAd}));
    put_ad(p, kTexflush, mark);
    put_ad(p, kFinish, 0);
    return p;
}
Bytes texflush_packet(uint64_t mark) {
    Bytes p;
    put_u64(p, packed_tag_lo(1, true, 1));
    put_u64(p, regs_hi({kAd}));
    put_ad(p, kTexflush, mark);
    return p;
}

void arbiter_pre_raise_in_flight() {
    ArbRig t;
    // The worker owns an unsealed marker ahead of the EE's PATH3 submit.
    std::shared_ptr<GifMtvuMarker> m = t.arbiter.openMarker();
    const Bytes p3 = finish_packet(0x300);
    t.arbiter.submit(GifPath::Path3, p3.data(), p3.size());
    CHECK(t.raises == 1);                                    // raised once, at the submit
    CHECK(t.raise_tid == std::this_thread::get_id());        // on the submitting (EE) thread
    CHECK(t.arbiter.PreRaisedFinish() == 1);
    CHECK(t.arbiter.PreRaisedPath3Total() == 1);
    t.arbiter.drain();
    CHECK(t.sink.finishes == 0);                             // behind the unsealed marker: not delivered
    CHECK(t.arbiter.PreRaisedFinish() == 1);
    // The worker fills and seals; the drain delivers the marker's packet, then the PATH3.
    t.worker.Begin(m);
    const Bytes p1 = texflush_packet(0x100);
    t.arbiter.submit(GifPath::Path1, p1.data(), p1.size());
    t.worker.End();
    CHECK(t.raises == 1);                                    // a worker-thread submit never pre-raises
    t.arbiter.sealMarker(m);
    t.arbiter.drain();
    CHECK(t.sink.finishes == 1);
    CHECK(t.sink.write_register == 3);                       // TEXFLUSH(p1), TEXFLUSH(p3), FINISH(p3): stream order
    CHECK(t.sink.last_reg == kFinish);
    // The parser's Finish consumes the one count.
    CHECK(t.arbiter.ConsumePreRaisedFinish());
    CHECK(t.arbiter.PreRaisedFinish() == 0);
    CHECK(!t.arbiter.ConsumePreRaisedFinish());
    CHECK(t.arbiter.ConsumedFinishTotal() == 1 && t.arbiter.PreRaisedFinishTotal() == 1);
    // A packet without FINISH pre-raises nothing.
    const Bytes q = texflush_packet(0x301);
    t.arbiter.submit(GifPath::Path3, q.data(), q.size());
    CHECK(t.raises == 1);
    // Two FINISH writes in one chain: two raises, two counts.
    Bytes two = finish_packet(0x302);
    const Bytes more = finish_packet(0x303);
    two.insert(two.end(), more.begin(), more.end());
    t.arbiter.submitOwned(GifPath::Path3, std::move(two));
    CHECK(t.raises == 3 && t.arbiter.PreRaisedFinish() == 2);
    // clear() drops the outstanding count with the packets.
    t.arbiter.clear();
    CHECK(t.arbiter.PreRaisedFinish() == 0);
    CHECK(t.raises == 3);
}

void arbiter_pre_raise_inert() {
    // No running worker: the inline drain reaches the write in the same call -- no pre-raise.
    {
        ArbRig t;
        t.worker.running.store(false);
        const Bytes p3 = finish_packet(0x300);
        t.arbiter.submit(GifPath::Path3, p3.data(), p3.size());
        t.arbiter.drain();
        CHECK(t.raises == 0 && t.arbiter.PreRaisedFinish() == 0);
        CHECK(t.sink.finishes == 1);
        CHECK(!t.arbiter.ConsumePreRaisedFinish());
    }
    // No raiser: off.
    {
        ArbRig t;
        t.arbiter.SetEarlyFinish(nullptr, nullptr);
        CHECK(!t.arbiter.EarlyFinishOn());
        const Bytes p3 = finish_packet(0x300);
        t.arbiter.submit(GifPath::Path3, p3.data(), p3.size());
        CHECK(t.raises == 0 && t.arbiter.PreRaisedFinish() == 0);
        t.arbiter.PreRaiseFinish(1);
        CHECK(t.raises == 0 && t.arbiter.PreRaisedFinish() == 0);
    }
    // PATH1 / PATH2 submits from the EE are not scanned here (the stack's VIF scan reports them).
    {
        ArbRig t;
        const Bytes p = finish_packet(0x200);
        t.arbiter.submit(GifPath::Path2, p.data(), p.size(), true);
        t.arbiter.submit(GifPath::Path1, p.data(), p.size());
        CHECK(t.raises == 0);
        t.arbiter.PreRaiseFinish(2);   // the stack's report
        CHECK(t.raises == 2 && t.arbiter.PreRaisedFinish() == 2);
        t.arbiter.ResetEarlyFinish();
        CHECK(t.arbiter.PreRaisedFinish() == 0);
    }
    // A PATH3 chain ending inside a non-IMAGE tag: the parser's FinishPath3 drops the open tag
    // at the pass end, so the scan resets too and the next chain's bytes are read as a tag.
    {
        ArbRig t;
        Bytes p = finish_packet(0x300);
        const Bytes head(p.begin(), p.begin() + 16);
        const Bytes tail(p.begin() + 16, p.end());
        t.arbiter.submit(GifPath::Path3, head.data(), head.size());
        CHECK(t.raises == 0 && !t.arbiter.Path3Scan().MidPacket());
        t.arbiter.submit(GifPath::Path3, tail.data(), tail.size());
        CHECK(t.raises == 0);   // read as a tag (TEXFLUSH data) + one data quadword: no FINISH write
        // An IMAGE left open persists (the parser keeps an incomplete IMAGE across passes).
        Bytes img;
        put_u64(img, 2 | (1ull << 15) | (2ull << 58)); put_u64(img, 0);
        put_ad(img, kFinish, 0);
        t.arbiter.submit(GifPath::Path3, img.data(), img.size());
        CHECK(t.arbiter.Path3Scan().MidImage());
        Bytes rest; put_ad(rest, kFinish, 0);
        t.arbiter.submit(GifPath::Path3, rest.data(), rest.size());
        CHECK(t.raises == 0 && !t.arbiter.Path3Scan().MidPacket());
    }
}

}  // namespace

int main() {
    scan_units();
    vif_scan_equivalence();
    arbiter_pre_raise_in_flight();
    arbiter_pre_raise_inert();
    std::printf("test_gif_early_finish: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

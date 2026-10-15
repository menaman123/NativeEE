// tests/core/test_ring_log.cpp -- the always-on ring: wrap + EXACT drop count,
// threaded producers (SPSC window integrity), decoder round-trip, channel mask
// + verbose tier, signal-safe text tail, and the frozen extern-C crash-ring
// symbols. argv[1] = scratch dir for the .rlog/.txt artifacts; the fixture
// pair <dir>/fixture.rlog + <dir>/fixture.txt is what ring_decode_oracle.py
// compares against the frozen original's decoder.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/log/ring_decoder.h"
#include "engine/core/log/ring_log.h"

static int g_fail = 0;
#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
            g_fail++;                                                         \
        }                                                                     \
    } while (0)

using namespace ps2r::ringlog;

static std::string g_dir = "/tmp";
static std::string P(const char* name) { return g_dir + "/" + name; }
static const uint32_t kCap = 1024;   // configured capacity (log.ring_events)

static void TestWrapAndDrops() {
    Ring* r = RingLog::RegisterThread("main");
    CHECK(r != nullptr, "main ring registration");
    CHECK(r->capacity == kCap, "capacity from Configure");

    const uint64_t total = (uint64_t)kCap * 3 + 17;   // force 2+ wraps
    for (uint64_t i = 0; i < total; i++) RingLog::Emit(CH_EE, TAG_TEST_EVENT, i, ~i);
    CHECK(r->wpos.load() == total, "total_emitted == emit count");

    CHECK(RingLog::DumpAllToPath(P("wrap.rlog").c_str(), DUMP_TEST), "wrap dump write");
    DecodedFile df;
    std::string err;
    CHECK(RingDecoder::ReadRlog(P("wrap.rlog").c_str(), df, &err), "wrap dump parse");
    CHECK(df.hdr.reason == DUMP_TEST, "dump reason");
    const DecodedRing* mr = nullptr;
    for (const auto& ring : df.rings)
        if (ring.name == "main") mr = &ring;
    CHECK(mr != nullptr, "main ring in dump");
    if (mr) {
        CHECK(mr->total_emitted == total, "emitted in header");
        CHECK(mr->events.size() == kCap, "retained == capacity");
        CHECK(mr->total_emitted - mr->events.size() == total - kCap, "EXACT drop count");
        bool order_ok = true;
        for (size_t i = 0; i < mr->events.size(); i++) {
            const Event& e = mr->events[i];
            const uint64_t want = total - kCap + i;
            if (e.a0 != want || e.a1 != ~want || e.tag != TAG_TEST_EVENT || e.channel != CH_EE ||
                e.frame != 7) {
                order_ok = false;
                break;
            }
        }
        CHECK(order_ok, "retained window is the LAST cap events, in order");
    }
}

static void TestThreadedProducers() {
    constexpr int kThreads = 4;
    constexpr uint64_t kPer = 100000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; t++) {
        ts.emplace_back([t] {
            char nm[16];
            std::snprintf(nm, sizeof(nm), "prod%d", t);
            Ring* r = RingLog::RegisterThread(nm);
            CHECK(r != nullptr, "producer registration");
            for (uint64_t i = 0; i < kPer; i++)
                RingLog::Emit(CH_GFX, TAG_TEST_EVENT, ((uint64_t)t << 32) | i, i * 3);
        });
    }
    for (auto& t : ts) t.join();

    CHECK(RingLog::DumpAllToPath(P("threads.rlog").c_str(), DUMP_TEST), "threaded dump write");
    DecodedFile df;
    std::string err;
    CHECK(RingDecoder::ReadRlog(P("threads.rlog").c_str(), df, &err), "threaded dump parse");
    int found = 0;
    for (const auto& ring : df.rings) {
        if (ring.name.rfind("prod", 0) != 0) continue;
        found++;
        const int t = ring.name[4] - '0';
        CHECK(ring.total_emitted == kPer, "producer emitted count");
        CHECK(ring.events.size() == kCap, "producer retained");
        bool ok = true;
        const uint64_t start = kPer - kCap;
        for (size_t i = 0; i < ring.events.size(); i++) {
            const Event& e = ring.events[i];
            const uint64_t want_i = start + i;
            if (e.a0 != (((uint64_t)t << 32) | want_i) || e.a1 != want_i * 3) {
                ok = false;
                break;
            }
        }
        CHECK(ok, "producer SPSC window order-exact");
    }
    CHECK(found == kThreads, "all producer rings present in dump");
}

static void TestDecoderRoundtrip() {
    RingLog::SetFrame(1234);
    RingLog::Emit(CH_EE, TAG_BOOT_SM_EDGE, ((uint64_t)8 << 32) | 10, 0xdeadbeefcafe0000ull);
    RingLog::Emit(CH_GFX, TAG_TSIG, 0x70000, 42);
    RingLog::Emit(CH_INPUT, TAG_INLAT_T1, 999888, ((uint64_t)0x0000 << 16) | 0x4000);
    RingLog::Emit(CH_MEM, TAG_MEM_GAUGE, GL_CLUT_TEX_CACHE, 4096);

    CHECK(RingLog::DumpAllToPath(P("round.rlog").c_str(), DUMP_HOTKEY), "round dump");
    CHECK(RingDecoder::DecodeFileToText(P("round.rlog").c_str(), P("round.txt").c_str()),
          "decode to text");

    std::ifstream txt(P("round.txt"));
    std::string all((std::istreambuf_iterator<char>(txt)), std::istreambuf_iterator<char>());
    CHECK(all.find("RLHDR ") != std::string::npos, "RLHDR line");
    CHECK(all.find("reason=hotkey") != std::string::npos, "reason decoded");
    CHECK(all.find("RLRING ring=main") != std::string::npos, "RLRING line");
    CHECK(all.find("BOOT_SM_EDGE") != std::string::npos, "tag name decoded");
    CHECK(all.find("prev=8 cur=10") != std::string::npos, "pretty args");
    CHECK(all.find("fbp=0x70000 batches=42") != std::string::npos, "TSIG pretty");
    CHECK(all.find("INLAT_T1") != std::string::npos, "INLAT_T1 present");
    CHECK(all.find("MEM_GAUGE") != std::string::npos, "MEM_GAUGE present");
    CHECK(all.find("f=1234") != std::string::npos, "frame stamp decoded");

    DecodedFile df;
    std::string err;
    CHECK(RingDecoder::ReadRlog(P("round.rlog").c_str(), df, &err), "round parse");
    const DecodedRing* mr = nullptr;
    for (const auto& ring : df.rings)
        if (ring.name == "main") mr = &ring;
    CHECK(mr && mr->events.size() >= 4, "main ring has the events");
    if (mr && mr->events.size() >= 4) {
        const Event& e0 = mr->events[mr->events.size() - 4];
        CHECK(e0.tag == TAG_BOOT_SM_EDGE && e0.frame == 1234 && e0.a0 == (((uint64_t)8 << 32) | 10),
              "binary fields exact");
    }
}

static void TestMasks() {
    Ring* r = RingLog::RegisterThread("main");
    const uint32_t all = (1u << CH_COUNT) - 1;
    RingLog::Configure(all & ~(1u << CH_PERF), 0, kCap, g_dir.c_str());
    const uint64_t w0 = r->wpos.load();
    RingLog::Emit(CH_PERF, TAG_TEST_EVENT, 1, 2);
    CHECK(r->wpos.load() == w0, "masked channel drops");
    RingLog::Emit(CH_EE, TAG_TEST_EVENT, 1, 2);
    CHECK(r->wpos.load() == w0 + 1, "open channel emits");
    RingLog::EmitV(CH_EE, TAG_TEST_EVENT, 3, 4);
    CHECK(r->wpos.load() == w0 + 1, "verbose dropped when verbose mask empty");
    // The generated code's plain-bool gate (owned by the EE seam) is bound here
    // and follows the verbose EE bit -- latched at Bind and on every Configure,
    // in either order.
    bool gate = true;
    RingLog::BindGeneratedGate(&gate);
    CHECK(!gate, "bound gate latched at Bind (verbose EE off)");
    RingLog::Configure(all, 1u << CH_EE, kCap, g_dir.c_str());
    RingLog::EmitV(CH_EE, TAG_TEST_EVENT, 3, 4);
    CHECK(r->wpos.load() == w0 + 2, "verbose emits when channel in verbose mask");
    CHECK(gate, "bound gate follows the verbose EE bit (on)");
    RingLog::BindGeneratedGate(nullptr);
    RingLog::Configure(all, 0, kCap, g_dir.c_str());
    CHECK(gate, "an unbound gate is left alone");
    bool late = false;
    RingLog::Configure(all, 1u << CH_EE, kCap, g_dir.c_str());
    RingLog::BindGeneratedGate(&late);
    CHECK(late, "Bind after Configure latches the current bit");
    RingLog::BindGeneratedGate(nullptr);
    RingLog::Configure(all, 0, kCap, g_dir.c_str());
    CHECK(RingLog::ParseChannelList("ee,gfx", 0) == ((1u << CH_EE) | (1u << CH_GFX)), "channel list");
    CHECK(RingLog::ParseChannelList("all", 0) == all, "channel list all");
    CHECK(RingLog::ParseChannelList("none", all) == 0, "channel list none");
    CHECK(RingLog::ParseChannelList("", 7) == 7, "channel list empty = default");
}

static void TestTextTail() {
    std::FILE* f = std::fopen(P("tail.txt").c_str(), "w");
    CHECK(f != nullptr, "tail file open");
    if (!f) return;
    RingLog::DumpTextTailFd(fileno(f), 8);
    std::fclose(f);
    std::ifstream txt(P("tail.txt"));
    std::string all((std::istreambuf_iterator<char>(txt)), std::istreambuf_iterator<char>());
    CHECK(all.find("[RINGLOG] ring=main") != std::string::npos, "tail names the ring");
    CHECK(all.find("RL f=") != std::string::npos, "tail has RL lines");
}

// Fixture for the cross-decoder oracle: every tag once, both tiers, a wrap.
static void WriteOracleFixture() {
    RingLog::Configure((1u << CH_COUNT) - 1, (1u << CH_COUNT) - 1, kCap, g_dir.c_str());
    RingLog::SetFrame(77);
    size_t n = 0;
    const TagInfo* t = TagTable(&n);
    for (size_t i = 0; i < n; i++) {
        RingLog::Emit((Channel)t[i].channel, t[i].id, 0x0102030405060708ull * (i + 1), 0x4142434445464748ull ^ i);
        RingLog::EmitV((Channel)t[i].channel, t[i].id, 0x8000000100000002ull + i, (uint64_t)i << 40);
    }
    RingLog::SetFrame(78);
    RingLog::Emit(CH_PERF, 0x7fff, 1, 2);   // unknown tag
    CHECK(RingLog::DumpAllToPath(P("fixture.rlog").c_str(), DUMP_EXIT), "fixture dump");
    CHECK(RingDecoder::DecodeFileToText(P("fixture.rlog").c_str(), P("fixture.txt").c_str()),
          "fixture decode");
}

int main(int argc, char** argv) {
    if (argc > 1) g_dir = argv[1];
    RingLog::Configure((1u << CH_COUNT) - 1, 0, kCap, g_dir.c_str());
    RingLog::SetFrame(7);
    TestWrapAndDrops();
    TestThreadedProducers();
    TestDecoderRoundtrip();
    TestMasks();
    TestTextTail();
    WriteOracleFixture();
    if (g_fail == 0) {
        std::printf("RING LOG PASS: wrap/drops, 4x100k threaded producers, decoder roundtrip, "
                    "masks + generated gate, text tail, fixture\n");
        return 0;
    }
    std::printf("RING LOG FAIL: %d checks failed\n", g_fail);
    return 1;
}

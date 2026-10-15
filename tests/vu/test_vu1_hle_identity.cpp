// Vu1HleIdentityObserver -- Vu1HleRuntime's identity observer: on a synthetic
// library (one wildcard entry) a run that produces two kicks reports two
// KickIdentity records BEFORE their submits (handler name, the analysed image
// hash, the entry pc, ordinals 0 and 1) then null; a run with no kicks reports
// nothing; a MISS reports nothing; the kicks themselves still reach the arbiter
// one slot each (observation only).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/vu/hle/vu1_hle_runtime.h"
#include "engine/vu/hle/vu1_image_analyzer.h"
#include "engine/vu/vu_unit.h"
#include "tools/vu1_replay/xgk_capture_sink.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                                     \
        }                                                                 \
    } while (0)

namespace {

std::vector<uint8_t> EopTag(uint8_t marker) {
    std::vector<uint8_t> t(16, 0);
    t[1] = 0x80;  // EOP, NLOOP 0
    t[15] = marker;
    return t;
}

bool TwoKicks(const Vu1HleInput&, Vu1HleOutput& out) {
    out.kicks.push_back(EopTag(1));
    out.kicks.push_back(EopTag(2));
    return true;
}
bool NoKicks(const Vu1HleInput&, Vu1HleOutput&) { return true; }

struct Seen {
    std::string name;
    uint64_t image;
    uint16_t pc;
    uint16_t ord;
    bool clear;
    uint64_t submitted_at;  // the arbiter's submit count when the report arrived
};

struct TestVu1HleIdentity {
    XgkCaptureSink sink;
    GifRegs regs;
    GifParser parser;
    GifArbiter arbiter;
    std::vector<Seen> seen;
    TestVu1HleIdentity() : parser(sink, regs), arbiter(parser) { sink.Attach(arbiter); }
    static void Observe(void* user, const Vu1HleRuntime::KickIdentity* id) {
        auto* rig = static_cast<TestVu1HleIdentity*>(user);
        Seen s{};
        s.submitted_at = rig->sink.kicks().size();
        if (!id) {
            s.clear = true;
        } else {
            s.name = id->handler_name;
            s.image = id->image_hash;
            s.pc = id->kick_pc;
            s.ord = id->kick_ord;
        }
        rig->seen.push_back(s);
    }
};

void Observer() {
    static const Vu1LibraryEntry kTwo[] = {{0, 0, "synthetic_two_kicks", kVu1AnyEntry, TwoKicks, nullptr, 0}};
    static const Vu1LibraryEntry kNone[] = {{0, 0, "synthetic_no_kick", kVu1AnyEntry, NoKicks, nullptr, 0}};
    const Vu1FamilySignature fam = {0x10, 1, {2, 3}, 0xA0};

    {
        TestVu1HleIdentity rig;
        VuUnit vu(1, &rig.arbiter);
        Vu1HleRuntime rt({fam, kTwo, 1}, rig.arbiter);
        rt.SetIdentityObserver(&TestVu1HleIdentity::Observe, &rig);
        CHECK(rt.TryExecute(vu, 0x3F8, 1));
        const Vu1ImageInfo info = Vu1ImageAnalyzer(fam).Analyze(vu.state().micro, vu.state().data);
        CHECK(rig.seen.size() == 3);
        if (rig.seen.size() == 3) {
            CHECK(rig.seen[0].name == "synthetic_two_kicks" && rig.seen[0].image == info.image_hash && rig.seen[0].pc == 0x3F8 &&
                  rig.seen[0].ord == 0 && !rig.seen[0].clear);
            CHECK(rig.seen[0].submitted_at == 0);  // reported BEFORE the first submit
            CHECK(rig.seen[1].name == "synthetic_two_kicks" && rig.seen[1].ord == 1 && !rig.seen[1].clear);
            CHECK(rig.seen[1].submitted_at == 1);  // after kick 0's submit, before kick 1's
            CHECK(rig.seen[2].clear && rig.seen[2].submitted_at == 2);
        }
        CHECK(rig.sink.kicks().size() == 2 && rig.sink.kicks()[0][15] == 1 && rig.sink.kicks()[1][15] == 2);
        // A second run: ordinals restart at 0.
        rig.seen.clear();
        CHECK(rt.TryExecute(vu, 0x3F8, 1));
        CHECK(rig.seen.size() == 3 && rig.seen[0].ord == 0 && rig.seen[1].ord == 1 && rig.seen[2].clear);
        CHECK(rt.census().runs == 2);
    }
    {
        TestVu1HleIdentity rig;
        VuUnit vu(1, &rig.arbiter);
        Vu1HleRuntime rt({fam, kNone, 1}, rig.arbiter);
        rt.SetIdentityObserver(&TestVu1HleIdentity::Observe, &rig);
        CHECK(rt.TryExecute(vu, 0, 1));
        CHECK(rig.seen.empty());  // no kick, no report (nothing to clear)
    }
    {
        TestVu1HleIdentity rig;
        VuUnit vu(1, &rig.arbiter);
        static const Vu1LibraryEntry kPinned[] = {{0, 0, "pinned", 0x100, TwoKicks, nullptr, 0}};
        Vu1HleRuntime rt({fam, kPinned, 1}, rig.arbiter);
        rt.SetIdentityObserver(&TestVu1HleIdentity::Observe, &rig);
        CHECK(!rt.TryExecute(vu, 0x200, 1));  // MISS (entry pinned elsewhere)
        CHECK(rig.seen.empty() && rt.census().misses == 1);
    }
    {
        // No observer installed: the run is unchanged.
        TestVu1HleIdentity rig;
        VuUnit vu(1, &rig.arbiter);
        Vu1HleRuntime rt({fam, kTwo, 1}, rig.arbiter);
        CHECK(rt.TryExecute(vu, 0, 1));
        CHECK(rig.sink.kicks().size() == 2 && rig.seen.empty());
    }
}

}  // namespace

int main() {
    Observer();
    if (g_fail == 0) std::printf("Vu1HleIdentityObserver PASS\n");
    return g_fail ? 1 : 0;
}

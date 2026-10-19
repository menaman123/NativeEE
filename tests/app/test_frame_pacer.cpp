// tests/app/test_frame_pacer.cpp -- ctest FramePacerCadence: video.frame_cap's pure pacer. Hz60
// admits a pending picture at every 16.67 ms edge and nowhere between; Hz30 at every OTHER
// edge (a 33.33 ms train) and a picture posted mid-period waits for the edge while two
// pictures inside one period supersede; a game slower than the cap holds edges (the train
// stays anchored, never re-anchors to a late picture); a pump a whole period late re-anchors
// once (no catch-up burst); Off admits at the pump; ToNextEdge bounds the wait; the census
// row renders; the cap names / indices round-trip with the Config order.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "app/host/frame_pacer.h"

using namespace ps2r::app;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

namespace {

constexpr uint64_t kField = FramePacer::kFieldNs;
constexpr uint64_t kMs = 1000000;

// Pumps every `pump_ns` from 0 to `end_ns`; a picture is posted at each instant of `posts`
// (sorted). Returns the admission instants.
std::vector<uint64_t> Simulate(FramePacer& pacer, const std::vector<uint64_t>& posts, uint64_t pump_ns, uint64_t end_ns) {
    std::vector<uint64_t> shown;
    size_t next_post = 0;
    bool pending = false;
    for (uint64_t now = 0; now <= end_ns; now += pump_ns) {
        while (next_post < posts.size() && posts[next_post] <= now) {
            if (pending) pacer.Supersede();
            pending = true;
            ++next_post;
        }
        if (pacer.Admit(now, pending)) {
            shown.push_back(now);
            pending = false;
        }
    }
    return shown;
}

std::vector<uint64_t> Gaps(const std::vector<uint64_t>& t) {
    std::vector<uint64_t> g;
    for (size_t i = 1; i < t.size(); ++i) g.push_back(t[i] - t[i - 1]);
    return g;
}

bool AllNear(const std::vector<uint64_t>& g, uint64_t target, uint64_t tol) {
    for (uint64_t v : g)
        if (v + tol < target || v > target + tol) return false;
    return true;
}

}  // namespace

int main() {
    // Names and indices: the Config order off | 30 | 60.
    FramePacer::Cap cap{};
    CHECK(FramePacer::CapOf(0, cap) && cap == FramePacer::Cap::Off && std::string(FramePacer::CapName(cap)) == "off", "off");
    CHECK(FramePacer::CapOf(1, cap) && cap == FramePacer::Cap::Hz30 && std::string(FramePacer::CapName(cap)) == "30", "30");
    CHECK(FramePacer::CapOf(2, cap) && cap == FramePacer::Cap::Hz60 && std::string(FramePacer::CapName(cap)) == "60", "60");
    CHECK(!FramePacer::CapOf(3, cap) && !FramePacer::CapOf(-1, cap), "3 / -1 are not caps");

    // Hz60: a 60 Hz game (a post every field, 1 ms after the edge) -> one admission per field,
    // every gap one field; nothing admitted between edges.
    {
        FramePacer p;
        CHECK(p.cap() == FramePacer::Cap::Hz60 && p.PeriodNs() == kField, "default 60");
        std::vector<uint64_t> posts;
        for (int i = 0; i < 120; ++i) posts.push_back(i * kField + kMs);
        const auto shown = Simulate(p, posts, 250000, 120 * kField);
        CHECK(shown.size() >= 118 && shown.size() <= 120, "60 Hz game under Hz60: %zu shown", shown.size());
        CHECK(AllNear(Gaps(shown), kField, 300000), "every gap one field");
        CHECK(p.Count().superseded == 0, "nothing superseded at 60");
    }
    // Hz60 with a 28 fps game (a post every 35.7 ms): the field mixture of 2 and 3 -- the
    // judder this cap keeps (the record the 30 cap is judged against).
    {
        FramePacer p;
        std::vector<uint64_t> posts;
        for (int i = 0; i < 60; ++i) posts.push_back(i * 35700000ull + 500000);
        const auto g = Gaps(Simulate(p, posts, 250000, 60 * 35700000ull + 2 * kField));
        int two = 0, three = 0, other = 0;
        for (uint64_t v : g) {
            if (v + 300000 >= 2 * kField && v <= 2 * kField + 300000) ++two;
            else if (v + 300000 >= 3 * kField && v <= 3 * kField + 300000) ++three;
            else ++other;
        }
        CHECK(two > 0 && three > 0 && other == 0, "28 fps under 60: 2-field %d, 3-field %d, other %d", two, three, other);
    }
    // Hz30: a 60 Hz game -> one admission per 2 fields, every gap 33.33 ms, half the pictures
    // superseded (the game's ticks all ran; the cap picks the pictures).
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz30);
        CHECK(p.PeriodNs() == 2 * kField, "30 = two fields");
        std::vector<uint64_t> posts;
        for (int i = 0; i < 120; ++i) posts.push_back(i * kField + kMs);
        const auto shown = Simulate(p, posts, 250000, 120 * kField);
        CHECK(shown.size() >= 59 && shown.size() <= 60, "60 Hz game under Hz30: %zu shown", shown.size());
        CHECK(AllNear(Gaps(shown), 2 * kField, 300000), "every gap two fields");
        CHECK(p.Count().superseded >= 58, "superseded %llu", static_cast<unsigned long long>(p.Count().superseded));
        // The anchoring pump (before the first post) and the tail after the last post are the only held edges.
        CHECK(p.Count().held <= 2, "held edges at 60 Hz: %llu (anchor / tail only)", static_cast<unsigned long long>(p.Count().held));
    }
    // Hz30 with a 40 fps game (irregular: 20 / 30 ms alternating): every gap STILL 33.33 ms --
    // the steadiness the cap buys (under Hz60 the same train gives a 1/2-field mixture).
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz30);
        std::vector<uint64_t> posts;
        uint64_t t = 500000;
        for (int i = 0; i < 80; ++i) { posts.push_back(t); t += (i & 1) ? 30 * kMs : 20 * kMs; }
        const auto shown = Simulate(p, posts, 250000, t + 2 * kField);
        CHECK(AllNear(Gaps(shown), 2 * kField, 300000), "40 fps irregular under 30: every gap two fields (%zu shown)", shown.size());
        CHECK(p.Count().held <= 3, "held edges when the game is faster than the cap: %llu (anchor / tail only)",
              static_cast<unsigned long long>(p.Count().held));
        FramePacer q;   // the same train under 60: a mixture
        const auto g60 = Gaps(Simulate(q, posts, 250000, t + 2 * kField));
        CHECK(!AllNear(g60, kField, 300000) && !AllNear(g60, 2 * kField, 300000), "under 60 the same game is a 1/2-field mixture");
    }
    // Hz30 with a 25 fps game (a post every 40 ms): the train stays anchored -- gaps are 2 or 4
    // fields (a held edge), never re-anchored to the late picture.
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz30);
        std::vector<uint64_t> posts;
        for (int i = 0; i < 50; ++i) posts.push_back(i * 40 * kMs + 500000);
        const auto shown = Simulate(p, posts, 250000, 50 * 40 * kMs + 4 * kField);
        int two = 0, four = 0, other = 0;
        for (uint64_t v : Gaps(shown)) {
            if (v + 300000 >= 2 * kField && v <= 2 * kField + 300000) ++two;
            else if (v + 300000 >= 4 * kField && v <= 4 * kField + 300000) ++four;
            else ++other;
        }
        CHECK(two > 0 && four > 0 && other == 0, "25 fps under 30: 2-field %d, 4-field %d, other %d", two, four, other);
        CHECK(p.Count().held >= static_cast<uint64_t>(four) && p.Count().held <= static_cast<uint64_t>(four) + 3,
              "held edges %llu ~ 4-field gaps %d (+ anchor / tail)", static_cast<unsigned long long>(p.Count().held), four);
        CHECK(p.Count().late == 0, "no re-anchor when the pump keeps up");
    }
    // A pump a whole period late (a display swap on main) re-anchors ONCE: the next edge is a
    // full period after the late pump, not a burst of catch-up admissions.
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz60);
        CHECK(p.Admit(0, true), "first pump anchors and admits");
        CHECK(!p.Admit(5 * kMs, true), "between edges: waits");
        CHECK(p.ToNextEdge(5 * kMs) == kField - 5 * kMs, "to the edge");
        CHECK(p.Admit(100 * kMs, true), "a late pump admits");
        CHECK(p.Count().late == 1, "re-anchored once");
        CHECK(!p.Admit(100 * kMs + 10 * kMs, true), "no catch-up edge inside the next period");
        CHECK(p.Admit(100 * kMs + kField, true), "the re-anchored edge");
    }
    // Off: admitted at the pump; ToNextEdge 0; nothing held.
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Off);
        CHECK(p.PeriodNs() == 0, "off has no period");
        CHECK(!p.Admit(0, false) && p.Admit(1, true) && p.Admit(2, true), "off admits whenever pending");
        CHECK(p.ToNextEdge(3) == 0 && p.Count().admitted == 2 && p.Count().held == 0, "off census");
    }
    // The census row.
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz30);
        p.Admit(0, true);
        p.Admit(2 * kField, false);
        p.Supersede();
        CHECK(p.CensusRow() == "cap=30:admitted=1:held=1:superseded=1:late=0", "row: %s", p.CensusRow().c_str());
    }
    std::printf("test_frame_pacer: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}

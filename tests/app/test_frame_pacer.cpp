// tests/app/test_frame_pacer.cpp -- ctest FramePacerCadence: video.frame_cap's pure pacer. Hz60
// admits the oldest waiting picture at every 16.67 ms edge: posted before the edge it waits
// (jitter absorbed, the train fixed); posted after an OPEN edge it is shown at once and the
// train slips to it (a late picture costs its lateness, never a held field: the round-3 rule).
// Two pictures inside one period are shown at consecutive edges (a two-deep FIFO, nothing
// superseded); a third drops the oldest. Hz30 at every OTHER edge; a 60 Hz game under it
// shows every other picture at a steady 33.33 ms; a 25 fps game is shown at its own 40 ms
// (no 2/4-field mixture). A pump a whole period late with a picture waiting re-anchors once
// (no catch-up burst). Off admits at the pump; ToNextEdge bounds the wait; the census row
// renders; the cap names / indices round-trip with the Config order.
#include <cstdint>
#include <cstdio>
#include <deque>
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

// The session's presenter loop in miniature: a pump every `pump_ns` from 0 to `end_ns`, at
// every post instant (PostFrame wakes the pump) and at the pacer's edge while a picture waits
// (the session sleeps ToNextEdge); the posted pictures wait in a two-deep FIFO (a third arrival
// drops the oldest: Supersede); Admit sees the oldest's post instant. Returns the admission
// instants.
std::vector<uint64_t> Simulate(FramePacer& pacer, const std::vector<uint64_t>& posts, uint64_t pump_ns, uint64_t end_ns) {
    std::vector<uint64_t> shown;
    std::deque<uint64_t> waiting;
    size_t next_post = 0;
    uint64_t grid = 0, last = 0;
    for (;;) {
        uint64_t now = grid;
        if (next_post < posts.size() && posts[next_post] < now) now = posts[next_post];
        if (!waiting.empty() && pacer.NextEdgeNs() > last && pacer.NextEdgeNs() < now) now = pacer.NextEdgeNs();
        if (now > end_ns) break;
        last = now;
        while (next_post < posts.size() && posts[next_post] <= now) {
            if (waiting.size() == 2) { pacer.Supersede(); waiting.pop_front(); }
            waiting.push_back(posts[next_post]);
            ++next_post;
        }
        if (pacer.Admit(now, waiting.empty() ? 0 : waiting.front())) {
            shown.push_back(now);
            waiting.pop_front();
        }
        if (now == grid) grid += pump_ns;
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

uint64_t MaxOf(const std::vector<uint64_t>& g) {
    uint64_t m = 0;
    for (uint64_t v : g) if (v > m) m = v;
    return m;
}

}  // namespace

int main() {
    // Names and indices: the Config order off | 30 | 60.
    FramePacer::Cap cap{};
    CHECK(FramePacer::CapOf(0, cap) && cap == FramePacer::Cap::Off && std::string(FramePacer::CapName(cap)) == "off", "off");
    CHECK(FramePacer::CapOf(1, cap) && cap == FramePacer::Cap::Hz30 && std::string(FramePacer::CapName(cap)) == "30", "30");
    CHECK(FramePacer::CapOf(2, cap) && cap == FramePacer::Cap::Hz60 && std::string(FramePacer::CapName(cap)) == "60", "60");
    CHECK(!FramePacer::CapOf(3, cap) && !FramePacer::CapOf(-1, cap), "3 / -1 are not caps");

    // Hz60: a 60 Hz game posting exactly on cadence -> one admission per post, every gap one field,
    // nothing slipped, held or superseded.
    {
        FramePacer p;
        CHECK(p.cap() == FramePacer::Cap::Hz60 && p.PeriodNs() == kField, "default 60");
        std::vector<uint64_t> posts;
        for (int i = 0; i < 120; ++i) posts.push_back(i * kField + kMs);
        const auto shown = Simulate(p, posts, 250000, 120 * kField + 2 * kMs);
        CHECK(shown.size() == 120, "60 Hz game under Hz60: %zu shown", shown.size());
        CHECK(AllNear(Gaps(shown), kField, 1), "every gap one field");
        CHECK(p.Count().superseded == 0 && p.Count().held == 0 && p.Count().slipped == 0, "census clean: %s", p.CensusRow().c_str());
        CHECK(p.Count().doubled == 0, "no doubled frame on cadence: %s", p.CensusRow().c_str());
    }
    // Hz60 with a JITTERY 60 Hz producer (0..4 ms after its cadence, pseudo-random): the train slips
    // to the latest arrival a few times, then every picture is early and waits -- no gap reaches two
    // fields, every picture shown, nothing superseded. (The round-2 rule turned every jitter crossing
    // of the edge into a 33 ms held frame: 16 % of the Paris frames.)
    {
        FramePacer p;
        std::vector<uint64_t> posts;
        uint32_t seed = 12345;
        for (int i = 0; i < 300; ++i) {
            seed = seed * 1103515245u + 12345u;
            posts.push_back(i * kField + 2 * kMs + (seed >> 16) % (4 * kMs));
        }
        const auto shown = Simulate(p, posts, 250000, 300 * kField + 8 * kMs);
        const auto g = Gaps(shown);
        CHECK(shown.size() == 300, "jittery 60 Hz: %zu shown", shown.size());
        CHECK(MaxOf(g) < kField + 4 * kMs + 250000, "no gap near two fields: max %llu", static_cast<unsigned long long>(MaxOf(g)));
        CHECK(p.Count().superseded == 0 && p.Count().held == 0, "nothing lost: %s", p.CensusRow().c_str());
        CHECK(p.Count().slipped >= 1 && p.Count().slipped <= 8, "a few slips while the phase settles: %llu",
              static_cast<unsigned long long>(p.Count().slipped));
        // Once settled (the second half), the train is fixed: every gap exactly one field.
        std::vector<uint64_t> tail(g.begin() + g.size() / 2, g.end());
        CHECK(AllNear(tail, kField, 1), "settled tail: every gap one field");
    }
    // ONE late picture (3 ms after its edge) on a cadence otherwise on time: shown at once (the
    // gap one field + 3 ms), the train slips 3 ms, the following pictures wait for the new edges:
    // one hitch of 3 ms, no 33 ms frame, no held edge, nothing superseded.
    {
        FramePacer p;
        std::vector<uint64_t> posts;
        for (int i = 0; i < 40; ++i) posts.push_back(i * kField + kMs + (i == 20 ? 3 * kMs : 0));
        const auto shown = Simulate(p, posts, 250000, 41 * kField);
        const auto g = Gaps(shown);
        CHECK(shown.size() == 40 && g.size() == 39, "all shown: %zu", shown.size());
        CHECK(g[19] == kField + 3 * kMs, "the late one: gap %llu", static_cast<unsigned long long>(g[19]));
        bool rest_steady = true;
        for (size_t i = 0; i < g.size(); ++i) if (i != 19 && g[i] != kField) rest_steady = false;
        CHECK(rest_steady, "every other gap exactly one field");
        CHECK(p.Count().slipped == 1 && p.Count().slip_ns == 3 * kMs && p.Count().held == 0 && p.Count().superseded == 0,
              "census: %s", p.CensusRow().c_str());
        CHECK(p.Count().doubled == 0, "a 3 ms slip (19.7 ms gap) is under the 1.2-field doubled line: %s", p.CensusRow().c_str());
    }
    // A consumer catch-up BURST: one picture 4 ms late, the next two 12 ms apart, then the cadence
    // again -- the FIFO shows them at consecutive edges: no gap over one field + 4 ms, nothing
    // superseded (the round-2 rule superseded the middle one and doubled a frame).
    {
        FramePacer p;
        std::vector<uint64_t> posts = {kMs, kField + kMs, 2 * kField + kMs, 3 * kField + 5 * kMs, 3 * kField + 17 * kMs,
                                       3 * kField + 29 * kMs};
        for (int i = 6; i < 20; ++i) posts.push_back(i * kField + 5 * kMs);
        const auto shown = Simulate(p, posts, 250000, 21 * kField);
        const auto g = Gaps(shown);
        CHECK(shown.size() == posts.size(), "burst: all %zu shown (%zu)", posts.size(), shown.size());
        CHECK(MaxOf(g) <= kField + 4 * kMs, "burst: max gap %llu", static_cast<unsigned long long>(MaxOf(g)));
        CHECK(p.Count().superseded == 0, "burst: nothing superseded (%s)", p.CensusRow().c_str());
    }
    // A TRUE production miss (no picture for a whole field): the next picture is shown at once, the
    // gap two fields + its lateness, the empty period counted held -- the floor the judge reports.
    {
        FramePacer p;
        std::vector<uint64_t> posts;
        for (int i = 0; i < 30; ++i) if (i != 15) posts.push_back(i * kField + kMs);
        const auto shown = Simulate(p, posts, 250000, 31 * kField);
        const auto g = Gaps(shown);
        CHECK(shown.size() == 29 && g[14] == 2 * kField, "the miss: gap %llu", static_cast<unsigned long long>(g[14]));
        CHECK(p.Count().held == 1 && p.Count().slipped == 1 && p.Count().slip_ns == 0, "census: %s", p.CensusRow().c_str());
        CHECK(p.Count().doubled == 1, "the miss is the one doubled frame (the F1 counter): %s", p.CensusRow().c_str());
    }
    // Three pictures inside one period (the harness drops the oldest): superseded counted once.
    {
        FramePacer p;
        std::vector<uint64_t> posts = {kMs, kField + kMs, kField + 3 * kMs, kField + 5 * kMs, 2 * kField + kMs, 3 * kField + kMs};
        const auto shown = Simulate(p, posts, 250000, 5 * kField);
        CHECK(shown.size() == 5 && p.Count().superseded == 1, "over-production: %zu shown, %s", shown.size(), p.CensusRow().c_str());
    }
    // Hz30: a 60 Hz game -> every other picture at a steady 33.33 ms; the rest superseded (the
    // game's ticks all ran; the cap picks the pictures).
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz30);
        CHECK(p.PeriodNs() == 2 * kField, "30 = two fields");
        std::vector<uint64_t> posts;
        for (int i = 0; i < 120; ++i) posts.push_back(i * kField + kMs);
        const auto shown = Simulate(p, posts, 250000, 120 * kField);
        CHECK(shown.size() >= 59 && shown.size() <= 60, "60 Hz game under Hz30: %zu shown", shown.size());
        CHECK(AllNear(Gaps(shown), 2 * kField, 1), "every gap two fields");
        CHECK(p.Count().superseded >= 58 && p.Count().held == 0, "superseded %llu held %llu",
              static_cast<unsigned long long>(p.Count().superseded), static_cast<unsigned long long>(p.Count().held));
        CHECK(p.Count().doubled == 0, "Hz30's own 33 ms cadence is not a doubled frame: %s", p.CensusRow().c_str());
    }
    // Hz30 with a 40 fps game (irregular: 20 / 30 ms alternating): every gap STILL 33.33 ms --
    // the steadiness the cap buys; one of every four pictures superseded.
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz30);
        std::vector<uint64_t> posts;
        uint64_t t = 500000;
        for (int i = 0; i < 80; ++i) { posts.push_back(t); t += (i & 1) ? 30 * kMs : 20 * kMs; }
        const auto shown = Simulate(p, posts, 250000, t + 2 * kField);
        CHECK(AllNear(Gaps(shown), 2 * kField, 1), "40 fps irregular under 30: every gap two fields (%zu shown)", shown.size());
        CHECK(p.Count().held == 0 && p.Count().slipped == 0, "no held edge, no slip: %s", p.CensusRow().c_str());
    }
    // Hz30 with a 25 fps game (a post every 40 ms): every edge stands open when the picture
    // comes -> the train slips to each: shown at the game's own steady 40 ms, no 2/4-field mixture.
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz30);
        std::vector<uint64_t> posts;
        for (int i = 0; i < 50; ++i) posts.push_back(i * 40 * kMs + 500000);
        const auto shown = Simulate(p, posts, 250000, 50 * 40 * kMs + 4 * kField);
        CHECK(shown.size() == 50 && AllNear(Gaps(shown), 40 * kMs, 1), "25 fps under 30: steady 40 ms (%zu shown)", shown.size());
        CHECK(p.Count().slipped == 49 && p.Count().held == 0 && p.Count().late == 0, "census: %s", p.CensusRow().c_str());
    }
    // A pump a whole period late with a picture WAITING since before the edge (a display swap on
    // main) re-anchors ONCE: the next edge is a full period after the late pump, no catch-up burst.
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Hz60);
        CHECK(p.Admit(0, 0) == false && p.NextEdgeNs() == 0, "nothing waiting: nothing anchored");
        FramePacer q;
        CHECK(q.Admit(1, 1), "first picture anchors and admits");
        CHECK(!q.Admit(5 * kMs, 2 * kMs), "between edges: waits");
        CHECK(q.ToNextEdge(5 * kMs) == kField + 1 - 5 * kMs, "to the edge");
        CHECK(q.Admit(100 * kMs, 2 * kMs), "a late pump admits");
        CHECK(q.Count().late == 1 && q.Count().slipped == 0, "re-anchored once as late: %s", q.CensusRow().c_str());
        CHECK(!q.Admit(110 * kMs, 105 * kMs), "no catch-up edge inside the next period");
        CHECK(q.Admit(100 * kMs + kField, 105 * kMs), "the re-anchored edge");
        // An open edge with nothing waiting does not advance the train (ToNextEdge 0 once passed).
        CHECK(!q.Admit(100 * kMs + 3 * kField, 0) && q.ToNextEdge(100 * kMs + 3 * kField) == 0, "open edge stays open");
    }
    // Off: admitted at the pump; ToNextEdge 0; nothing held.
    {
        FramePacer p;
        p.SetCap(FramePacer::Cap::Off);
        CHECK(p.PeriodNs() == 0, "off has no period");
        CHECK(!p.Admit(0, 0) && p.Admit(1, 1) && p.Admit(2, 2), "off admits whenever a picture waits");
        CHECK(p.ToNextEdge(3) == 0 && p.Count().admitted == 2 && p.Count().held == 0, "off census");
    }
    // The census row.
    {
        FramePacer q;
        q.SetCap(FramePacer::Cap::Hz30);
        q.Admit(kMs, kMs);                                 // anchor + admit; next edge 2 fields + 1 ms
        q.Admit(4 * kField + 2 * kMs, 4 * kField + kMs);   // 2 fields + 1 ms behind: one empty period held, slipped 1 ms
        q.Supersede();
        CHECK(q.CensusRow() == "cap=30:admitted=2:held=1:superseded=1:late=0:slipped=1:slip_ms=1:doubled=1", "row: %s", q.CensusRow().c_str());
    }
    std::printf("test_frame_pacer: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}

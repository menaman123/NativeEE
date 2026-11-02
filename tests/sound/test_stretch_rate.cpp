// StretchRate: the device-ring rate controller. A model of the pull ring (the device drains 48000 frames
// per wall second; each push adds 800 x ratio frames in 480-frame hops like TimeStretch) under the boundary
// cadences a play host sees. Pins (1) a wall-locked 60 Hz cadence with 0..4 ms of boundary jitter: the ratio
// is exactly 1 (pass-through) once the window holds and the fill stays within the lead's quantisation band;
// (2) isolated two-field boundaries with catch-up: never a pad (fill > 0), the ratio within 5% of 1 and
// within 0.5% of 1 again inside 20 pushes; (3) a sustained 54 Hz cadence: the ratio converges to 60/54 within two seconds, the fill never
// reaches zero (a stretch, not a repeat); (4) a burst of six two-field fields in twenty pushes: at most one
// pad, a stretch of at most 1.5, and the rate term does not stretch the seconds after it; (5) a 300 ms stall: the window resets, the guard lengthens the
// pushes after it, the ratio is within 0.5% of 1 inside 1.5 s and the fill never exceeds the ring's trim bound in
// steady state; (6) the legacy failure mode is not reproduced: no push changes the ratio by more than 0.1
// outside the guard.
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/sound/stretch_rate.h"

using ps2r::sound::StretchRate;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

namespace {

constexpr int kLead = 3 * StretchRate::kQuantumFrames;   // 2400
constexpr int kTrim = kLead + 2 * StretchRate::kQuantumFrames;

struct Model {
    StretchRate rate;
    double fill = kLead;      // frames in the ring (primed with the lead)
    double carry = 0.0;       // TimeStretch's hop quantisation
    int pads = 0, trims = 0;
    double fill_min = 1e9, fill_max = 0.0, ratio_min = 9.0, ratio_max = 0.0, step_max = 0.0;
    double prev_ratio = 1.0;
    uint64_t guard_before = 0;
    std::vector<double> ratios;
    // One boundary after `dt_s` of wall time: the device drained, then the push.
    double Push(double dt_s) {
        if (dt_s >= 0.0) {
            fill -= 48000.0 * dt_s;
            if (fill < 0.0) { fill = 0.0; ++pads; }
        }
        const double r = rate.Next(dt_s, static_cast<int>(fill), kLead);
        carry += StretchRate::kQuantumFrames * r;
        const double out = std::floor(carry / 480.0) * 480.0;
        carry -= out;
        fill += out;
        if (fill > kTrim) { fill = kLead; ++trims; }
        fill_min = std::min(fill_min, fill);
        fill_max = std::max(fill_max, fill);
        ratio_min = std::min(ratio_min, r);
        ratio_max = std::max(ratio_max, r);
        const bool guarded = rate.GuardPushes() != guard_before;
        guard_before = rate.GuardPushes();
        if (!guarded) step_max = std::max(step_max, std::fabs(r - prev_ratio));
        prev_ratio = r;
        ratios.push_back(r);
        return r;
    }
    void Reset() { fill_min = 1e9; fill_max = 0.0; ratio_min = 9.0; ratio_max = 0.0; step_max = 0.0; pads = trims = 0; ratios.clear(); }
};

constexpr double kField = 1.0 / 60.0;

// A wall-locked pacer: boundary i arrives 0..4 ms after its tick (the intervals telescope, so the window's
// mean is the span between two jittered boundaries, not a random walk).
struct Jitter {
    uint32_t lcg = 7;
    double prev = 0.0;
    double Next() {
        lcg = lcg * 1664525u + 1013904223u;
        const double j = static_cast<double>(lcg >> 8) / 16777216.0 * 0.004;
        const double dt = kField + j - prev;
        prev = j;
        return dt;
    }
};

void TestSteady() {
    Model m;
    Jitter jit;
    m.Push(-1.0);
    for (int i = 0; i < 600; ++i) m.Push(jit.Next());
    m.Reset();
    for (int i = 0; i < 1200; ++i) m.Push(jit.Next());
    CHECK(m.ratio_min == 1.0 && m.ratio_max == 1.0);
    CHECK(m.pads == 0 && m.trims == 0);
    CHECK(m.fill_min > kLead - 1200 && m.fill_max < kLead + 1200);
    std::printf("steady: ratio %.4f..%.4f fill %.0f..%.0f pads %d trims %d\n", m.ratio_min, m.ratio_max, m.fill_min, m.fill_max, m.pads, m.trims);
}

void TestTwoFieldPictures() {
    Model m;
    m.Push(-1.0);
    for (int i = 0; i < 300; ++i) m.Push(kField);
    m.Reset();
    int back_to_one_max = 0;
    for (int rep = 0; rep < 8; ++rep) {
        m.Push(2 * kField);                 // the two-field picture
        m.Push(0.003);                      // the catch-up boundary (its own work only)
        int n = 0;
        while (n < 60 && std::fabs(m.Push(kField) - 1.0) >= 0.005) ++n;
        back_to_one_max = std::max(back_to_one_max, n);
        for (int i = 0; i < 40; ++i) m.Push(kField);
    }
    CHECK(m.pads == 0 && m.trims == 0);
    CHECK(m.ratio_min >= 0.95 && m.ratio_max <= 1.05);
    CHECK(back_to_one_max <= 20);   // within 0.5 % of 1 (the drift term's tail is below that)
    CHECK(m.rate.GuardPushes() <= 8);   // at most the push right after each two-field field
    std::printf("two-field: ratio %.4f..%.4f fill %.0f..%.0f back-to-1 within %d pushes guard %llu\n", m.ratio_min, m.ratio_max, m.fill_min, m.fill_max,
                back_to_one_max, static_cast<unsigned long long>(m.rate.GuardPushes()));
}

void TestSustainedSlow() {
    Model m;
    m.Push(-1.0);
    for (int i = 0; i < 300; ++i) m.Push(kField);
    m.Reset();
    const double slow = 1.0 / 54.0;
    for (int i = 0; i < 400; ++i) m.Push(slow);   // ~7.4 s
    CHECK(m.pads == 0 && m.trims == 0);
    CHECK(m.fill_min > 0.0);
    // After two seconds (108 pushes) the ratio sits at 60/54 within 2%.
    double lo = 9.0, hi = 0.0;
    for (size_t i = 120; i < m.ratios.size(); ++i) { lo = std::min(lo, m.ratios[i]); hi = std::max(hi, m.ratios[i]); }
    CHECK(lo > 60.0 / 54.0 - 0.02 && hi < 60.0 / 54.0 + 0.02);
    CHECK(m.step_max <= 0.1);
    std::printf("54 Hz: ratio %.4f..%.4f (after 2 s %.4f..%.4f) fill %.0f..%.0f pads %d trims %d step_max %.4f\n", m.ratio_min, m.ratio_max, lo, hi, m.fill_min,
                m.fill_max, m.pads, m.trims, m.step_max);
}

// A movie's end / a level load: six two-field fields inside twenty pushes (100 ms of game time lost in
// 330 ms of wall), none caught up -- the pattern of the boot FMV lanes at frames 264-284. The lead cannot
// absorb it; the controller must bridge it as a stretch of at most 1.5 with at most one pad, and the rate
// term must NOT stretch the next two seconds for it (the trimmed mean drops the burst).
void TestBurst() {
    Model m;
    m.Push(-1.0);
    for (int i = 0; i < 300; ++i) m.Push(kField);
    m.Reset();
    const double seq[] = {2, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2, 1, 0.2, 2, 1};
    for (double f : seq) m.Push(f * kField);
    CHECK(m.pads <= 1);
    CHECK(m.ratio_max <= 1.5);
    const int burst_pads = m.pads;
    m.Reset();
    for (int i = 0; i < 30; ++i) m.Push(kField);   // the fill terms rebuild the lead
    m.Reset();
    for (int i = 0; i < 90; ++i) m.Push(kField);   // the window still holds the burst: the rate term must say 1
    CHECK(m.rate.RateRatio() == 1.0);
    CHECK(m.ratio_max <= 1.02 && m.trims == 0);
    std::printf("burst: pads %d, ratio max after the burst %.4f, rate term %.4f, trims %d\n", burst_pads, m.ratio_max, m.rate.RateRatio(), m.trims);
}

void TestStall() {
    Model m;
    m.Push(-1.0);
    for (int i = 0; i < 300; ++i) m.Push(kField);
    m.Reset();
    const uint64_t resets = m.rate.Resets();
    m.Push(0.3);                                  // the stall: the ring ran dry (padded)
    CHECK(m.rate.Resets() == resets + 1);
    CHECK(m.rate.GuardPushes() >= 1);
    for (int i = 0; i < 6; ++i) m.Push(0.002);    // the pacer's catch-up burst
    int n = 0;
    while (n < 120 && std::fabs(m.Push(kField) - 1.0) >= 0.005) ++n;
    CHECK(n < 90);   // 1.5 s: the deficit term rebuilds the lead in ~30 pushes, the drift term's tail is below 0.5 % by then
    for (int i = 0; i < 600; ++i) m.Push(kField);   // the drift term's tail settles
    m.Reset();
    for (int i = 0; i < 300; ++i) m.Push(kField);
    CHECK(m.ratio_min == 1.0 && m.ratio_max == 1.0);
    CHECK(m.trims == 0 && m.fill_max < kTrim);
    std::printf("stall: ratio 1 again after %d pushes; steady after: fill %.0f..%.0f trims %d\n", n, m.fill_min, m.fill_max, m.trims);
}

}  // namespace

int main() {
    TestSteady();
    TestTwoFieldPictures();
    TestSustainedSlow();
    TestBurst();
    TestStall();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

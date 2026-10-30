// TimeStretch: the play host's pitch-preserving tempo change. Pins (1) ratio 1 is the identity after
// the first hop; (2) a stretched or compressed tone keeps its pitch and level, has no discontinuity,
// and comes out at the requested length; (3) a ratio that changes on every call yields the sum of the
// per-call lengths; (4) Reset starts a new stream.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/sound/time_stretch.h"

using ps2r::sound::TimeStretch;

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

static constexpr double kPi = 3.14159265358979323846;
static constexpr int kQuantum = 800;

static std::vector<float> Tone(double hz, int frames, float amp) {
    std::vector<float> v(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float s = amp * static_cast<float>(std::sin(2.0 * kPi * hz * i / 48000.0));
        v[static_cast<size_t>(i) * 2] = s;
        v[static_cast<size_t>(i) * 2 + 1] = -s;
    }
    return v;
}

// A broadband test signal: three tones plus deterministic noise.
static std::vector<float> Mix(int frames) {
    std::vector<float> v(static_cast<size_t>(frames) * 2);
    uint32_t lcg = 12345;
    for (int i = 0; i < frames; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        const double noise = (static_cast<double>(lcg >> 8) / 16777216.0 - 0.5) * 0.1;
        const double t = i / 48000.0;
        v[static_cast<size_t>(i) * 2] = static_cast<float>(0.3 * std::sin(2 * kPi * 220 * t) + 0.2 * std::sin(2 * kPi * 1330 * t) + noise);
        v[static_cast<size_t>(i) * 2 + 1] = static_cast<float>(0.3 * std::sin(2 * kPi * 330 * t) + 0.1 * std::sin(2 * kPi * 4100 * t) - noise);
    }
    return v;
}

static std::vector<float> Run(TimeStretch& ts, const std::vector<float>& in, double ratio) {
    std::vector<float> out;
    const int frames = static_cast<int>(in.size() / 2);
    for (int f = 0; f < frames; f += kQuantum) ts.Process(in.data() + static_cast<size_t>(f) * 2, kQuantum, ratio, out);
    return out;
}

static void TestIdentity() {
    TimeStretch ts;
    const std::vector<float> in = Mix(48000);
    const std::vector<float> out = Run(ts, in, 1.0);
    const size_t frames = out.size() / 2;
    CHECK(frames + TimeStretch::kWindow + TimeStretch::kSeek >= 48000);
    float worst = 0.0f;
    for (size_t i = TimeStretch::kHop * 2; i < out.size(); ++i) worst = std::max(worst, std::fabs(out[i] - in[i]));
    CHECK(worst < 1e-5f);
    CHECK(ts.FramesIn() == 48000);
    CHECK(ts.FramesOut() == frames);
}

// Rising zero crossings per second of the left channel over [from, to).
static double Pitch(const std::vector<float>& v, size_t from, size_t to) {
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (v[(i - 1) * 2] < 0.0f && v[i * 2] >= 0.0f) ++crossings;
    return crossings * 48000.0 / static_cast<double>(to - from);
}

static void CheckTone(double ratio) {
    TimeStretch ts;
    const std::vector<float> in = Tone(440.0, 96000, 0.5f);
    const std::vector<float> out = Run(ts, in, ratio);
    const double frames = static_cast<double>(out.size() / 2);
    const double want = 96000.0 * ratio;
    CHECK(std::fabs(frames - want) < (TimeStretch::kWindow + TimeStretch::kSeek) * ratio + TimeStretch::kHop);
    const size_t from = TimeStretch::kWindow, to = out.size() / 2 - TimeStretch::kWindow;
    const double hz = Pitch(out, from, to);
    CHECK(std::fabs(hz - 440.0) < 440.0 * 0.01);
    float peak = 0.0f, step = 0.0f;
    for (size_t i = from; i < to; ++i) {
        peak = std::max(peak, std::fabs(out[i * 2]));
        step = std::max(step, std::fabs(out[i * 2] - out[(i - 1) * 2]));
    }
    // The tone's own largest sample step is 0.5 * 2 pi 440 / 48000 = 0.0288.
    CHECK(peak > 0.45f && peak < 0.55f);
    CHECK(step < 0.0288f * 1.5f);
    std::printf("ratio %.2f: %zu -> %zu frames, %.1f Hz, peak %.3f, max step %.4f\n", ratio, in.size() / 2, out.size() / 2, hz, peak, step);
}

static void TestVaryingRatio() {
    TimeStretch ts;
    const std::vector<float> in = Mix(96000);
    std::vector<float> out;
    double want = 0.0;
    for (int q = 0; q < 120; ++q) {
        const double ratio = 1.0 + 0.6 * std::sin(q * 0.3);
        ts.Process(in.data() + static_cast<size_t>(q) * kQuantum * 2, kQuantum, ratio, out);
        want += kQuantum * ratio;
    }
    CHECK(std::fabs(static_cast<double>(out.size() / 2) - want) < (TimeStretch::kWindow + TimeStretch::kSeek) * 2.0);
}

static void TestReset() {
    TimeStretch ts;
    const std::vector<float> in = Mix(4800);
    std::vector<float> out;
    ts.Process(in.data(), 4800, 1.3, out);
    ts.Reset();
    out.clear();
    ts.Process(in.data(), 4800, 1.0, out);
    float worst = 0.0f;
    for (size_t i = TimeStretch::kHop * 2; i < out.size(); ++i) worst = std::max(worst, std::fabs(out[i] - in[i]));
    CHECK(!out.empty());
    CHECK(worst < 1e-5f);
}

int main() {
    TestIdentity();
    CheckTone(1.25);
    CheckTone(1.6);
    CheckTone(2.0);
    CheckTone(0.8);
    TestVaryingRatio();
    TestReset();
    std::printf("test_time_stretch: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures) {
        std::printf("test_time_stretch: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("test_time_stretch: PASS (identity, pitch, level, continuity, length, varying ratio, reset)\n");
    return 0;
}

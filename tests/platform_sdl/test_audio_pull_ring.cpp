// tests/platform_sdl/test_audio_pull_ring.cpp -- the AudioPullRing contract
// (PHASE7_PLAN section 4.4): quantum arithmetic, byte-exact order across the
// 512/800 chunk mismatch, underrun zero-fill, overrun drop-oldest-quantum,
// no drift over 10^5 quanta, and one producer / one consumer on two threads.
#include "engine/platform/sdl/audio_pull_ring.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using ps2r::platform::sdl::AudioPullRing;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

constexpr int kQ  = AudioPullRing::kQuantumFrames;
constexpr int kCh = AudioPullRing::kChannels;

// Frame `index` of the counting pattern: L = index + 1 (never 0, so a zero-filled
// frame is distinguishable), R = -(index + 1).
void FillPattern(std::vector<float>& buf, uint64_t first_index, int frames) {
    buf.resize(static_cast<size_t>(frames) * kCh);
    for (int i = 0; i < frames; ++i) {
        const float v = static_cast<float>(first_index + static_cast<uint64_t>(i) + 1);
        buf[static_cast<size_t>(i) * kCh]     = v;
        buf[static_cast<size_t>(i) * kCh + 1] = -v;
    }
}

void PushQuantum(AudioPullRing& ring, uint64_t first_index) {
    std::vector<float> q;
    FillPattern(q, first_index, kQ);
    ring.Push(q.data(), kQ);
}

void TestArithmetic() {
    static_assert(AudioPullRing::kQuantumFrames == 800, "one vsync at 48 kHz");
    static_assert(AudioPullRing::kCapacityQuanta == 8, "eight quanta");
    static_assert(AudioPullRing::kCapacityFrames == 6400, "6400 frames");
    AudioPullRing ring;
    CHECK(ring.Available() == 0);
    for (int k = 1; k <= AudioPullRing::kCapacityQuanta; ++k) {
        PushQuantum(ring, static_cast<uint64_t>(k - 1) * kQ);
        CHECK(ring.Available() == kQ * k);
        CHECK(ring.overruns() == 0);
    }
    CHECK(ring.pushed() == 6400);
    CHECK(ring.pulled() == 0);
    CHECK(ring.dropped() == 0);
    CHECK(ring.underruns() == 0);
}

// 25 quanta pushed, consumed in 512-frame chunks while pushing (never above
// capacity, never below zero): the concatenated output is byte-identical to the
// concatenated input. 20000 is not a multiple of 512 and 6400 is not either, so
// both the wrap and the chunk mismatch are exercised.
void TestOrder() {
    AudioPullRing ring;
    constexpr int kQuanta = 25;
    std::vector<float> expected;
    FillPattern(expected, 0, kQuanta * kQ);

    std::vector<float> out;
    std::vector<float> chunk(512 * kCh);
    for (int q = 0; q < kQuanta; ++q) {
        PushQuantum(ring, static_cast<uint64_t>(q) * kQ);
        while (ring.Available() >= 512) {
            ring.Pull(chunk.data(), 512);
            out.insert(out.end(), chunk.begin(), chunk.end());
        }
    }
    const int rest = ring.Available();
    CHECK(rest == (kQuanta * kQ) % 512);
    chunk.assign(static_cast<size_t>(rest) * kCh, 0.0f);
    ring.Pull(chunk.data(), rest);
    out.insert(out.end(), chunk.begin(), chunk.end());

    CHECK(out.size() == expected.size());
    CHECK(std::memcmp(out.data(), expected.data(), expected.size() * sizeof(float)) == 0);
    CHECK(ring.overruns() == 0);
    CHECK(ring.underruns() == 0);
    CHECK(ring.dropped() == 0);
    CHECK(ring.pulled() == ring.pushed());
    CHECK(ring.Available() == 0);
}

void TestUnderrun() {
    {
        AudioPullRing ring;
        std::vector<float> out(512 * kCh, 1.0f);
        ring.Pull(out.data(), 512);
        bool all_zero = true;
        for (float v : out) all_zero = all_zero && (v == 0.0f);
        CHECK(all_zero);
        CHECK(ring.underruns() == 1);
        CHECK(ring.pulled() == 512);
        CHECK(ring.pushed() == 0);
        CHECK(ring.Available() == 0);
    }
    {
        AudioPullRing ring;
        std::vector<float> in;
        FillPattern(in, 0, 300);
        ring.Push(in.data(), 300);
        std::vector<float> out(512 * kCh, 1.0f);
        ring.Pull(out.data(), 512);
        CHECK(std::memcmp(out.data(), in.data(), in.size() * sizeof(float)) == 0);
        bool tail_zero = true;
        for (size_t i = in.size(); i < out.size(); ++i) tail_zero = tail_zero && (out[i] == 0.0f);
        CHECK(tail_zero);
        CHECK(ring.underruns() == 1);
        CHECK(ring.pulled() == 512);
        CHECK(ring.Available() == 0);
    }
}

// Nine quanta into an eight-quantum ring: the FIRST quantum is gone, the next
// pull starts on quantum 2's first frame.
void TestOverrun() {
    AudioPullRing ring;
    for (int q = 0; q < 9; ++q) PushQuantum(ring, static_cast<uint64_t>(q) * kQ);
    CHECK(ring.overruns() == 1);
    CHECK(ring.dropped() == static_cast<uint64_t>(kQ));
    CHECK(ring.pushed() == 9u * kQ);
    CHECK(ring.Available() == AudioPullRing::kCapacityFrames);

    std::vector<float> expected;
    FillPattern(expected, kQ, 512);
    std::vector<float> out(512 * kCh, 0.0f);
    ring.Pull(out.data(), 512);
    CHECK(std::memcmp(out.data(), expected.data(), expected.size() * sizeof(float)) == 0);
    CHECK(ring.underruns() == 0);

    // A tenth push after that partial pull frees room by dropping the REST of
    // quantum 2 plus nothing more (the read position lands on a quantum edge).
    PushQuantum(ring, 9u * kQ);
    CHECK(ring.overruns() == 2);
    CHECK(ring.dropped() == static_cast<uint64_t>(kQ) + (kQ - 512));
    FillPattern(expected, 2u * kQ, 512);
    ring.Pull(out.data(), 512);
    CHECK(std::memcmp(out.data(), expected.data(), expected.size() * sizeof(float)) == 0);
}

// 10^5 pushes of 800 with one 512-frame pull each: the ring saturates and
// drops periodically; at every 1000th step pulled == pushed - dropped - Available()
// and no pull ever underran.
void TestNoDrift() {
    AudioPullRing ring;
    std::vector<float> q(kQ * kCh, 0.5f);
    std::vector<float> out(512 * kCh);
    constexpr int kIterations = 100000;
    bool identity_held = true;
    for (int i = 1; i <= kIterations; ++i) {
        ring.Push(q.data(), kQ);
        ring.Pull(out.data(), 512);
        if (i % 1000 == 0) {
            const uint64_t lhs = ring.pulled();
            const uint64_t rhs = ring.pushed() - ring.dropped() - static_cast<uint64_t>(ring.Available());
            identity_held = identity_held && (lhs == rhs);
        }
    }
    CHECK(identity_held);
    CHECK(ring.pushed() == static_cast<uint64_t>(kIterations) * kQ);
    CHECK(ring.pulled() == static_cast<uint64_t>(kIterations) * 512);
    CHECK(ring.underruns() == 0);
    CHECK(ring.overruns() > 0);
    CHECK(ring.dropped() > 0);
    CHECK(ring.Available() <= AudioPullRing::kCapacityFrames);
}

// One producer thread pushes 10^4 quanta of the counting pattern; one consumer
// thread pulls 512-frame chunks until the producer is done and the ring is
// empty. Every real (non-zero) frame read must be strictly later than the one
// before it, every gap must resume on a quantum boundary, and the count of real
// frames read must equal pushed - dropped.
void TestSpsc() {
    AudioPullRing ring;
    constexpr int kQuanta = 10000;
    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        std::vector<float> q;
        for (int i = 0; i < kQuanta; ++i) {
            FillPattern(q, static_cast<uint64_t>(i) * kQ, kQ);
            ring.Push(q.data(), kQ);
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::vector<float> out(512 * kCh);
    uint64_t real_frames = 0;
    uint64_t last_index = 0;        // 1-based pattern value of the last real frame
    bool     have_last = false;
    bool     monotonic = true;
    bool     gaps_on_quantum_edges = true;
    bool     pairs_consistent = true;
    for (;;) {
        const bool done = producer_done.load(std::memory_order_acquire);
        if (done && ring.Available() == 0) break;
        ring.Pull(out.data(), 512);
        for (int f = 0; f < 512; ++f) {
            const float l = out[static_cast<size_t>(f) * kCh];
            const float r = out[static_cast<size_t>(f) * kCh + 1];
            if (l == 0.0f) continue;                       // zero-filled underrun frame
            const uint64_t idx = static_cast<uint64_t>(l); // = frame index + 1
            pairs_consistent = pairs_consistent && (r == -l);
            if (have_last) {
                if (idx <= last_index) monotonic = false;
                if (idx != last_index + 1 && ((idx - 1) % kQ) != 0) gaps_on_quantum_edges = false;
            } else if (((idx - 1) % kQ) != 0) {
                gaps_on_quantum_edges = false;             // first frame read must open a quantum
            }
            last_index = idx;
            have_last = true;
            ++real_frames;
        }
    }
    producer.join();

    CHECK(monotonic);
    CHECK(gaps_on_quantum_edges);
    CHECK(pairs_consistent);
    CHECK(ring.pushed() == static_cast<uint64_t>(kQuanta) * kQ);
    CHECK(real_frames == ring.pushed() - ring.dropped());
    CHECK(ring.Available() == 0);
    std::printf("spsc: pushed=%llu pulled=%llu dropped=%llu overruns=%llu underruns=%llu real=%llu\n",
                static_cast<unsigned long long>(ring.pushed()),
                static_cast<unsigned long long>(ring.pulled()),
                static_cast<unsigned long long>(ring.dropped()),
                static_cast<unsigned long long>(ring.overruns()),
                static_cast<unsigned long long>(ring.underruns()),
                static_cast<unsigned long long>(real_frames));
}

// The clock contract, rule 4 (pad with the previous tail): a 1 kHz tone pushed one
// 800-frame quantum per game tick, pulled 512 frames at a time at the device's
// steady rate; the game thread is LATE for stretches (no push for six ticks,
// then the six quanta at once). With pad mode on and the standing lead:
// dropped = 0 and underruns = 0; the gaps were filled from the previous quantum's
// tail (padded > 0), the lead re-latched (relatches > 0) and the late burst trimmed
// (trimmed > 0; the ring never exceeds kTrimFrames after a push, so latency never
// grows); the tone's frequency over the whole device stream -- its zero-crossing
// count -- is the same as a never-late run's within 0.5 % (no pitch change: the
// pad repeats, the repayment skips; nothing is resampled).
void TestPadWithTail() {
    constexpr int kTicks = 600;                      // 10 s of game ticks
    constexpr double kHz = 1000.0, kRate = 48000.0;
    auto run = [&](bool late, bool pad, uint64_t* zero_crossings, AudioPullRing** ring_out) {
        static AudioPullRing rings[4];
        static int next = 0;
        AudioPullRing& ring = rings[next++];
        ring.SetPadWithTail(pad);
        if (pad) ring.Prime();
        std::vector<float> q(static_cast<size_t>(kQ) * kCh);
        std::vector<float> chunk(512 * kCh);
        uint64_t phase_frame = 0;                       // the tone's phase, in frames
        uint64_t pulled_frames = 0, due_frames = 0;
        float prev = 0.0f;
        uint64_t zc = 0;
        int withheld = 0;
        for (int t = 0; t < kTicks; ++t) {
            // The producer's tick: late stretches withhold three ticks then push them all.
            const bool stall = late && (t % 50) >= 20 && (t % 50) < 26;   // a 100 ms stall every 50 ticks
            if (stall) {
                ++withheld;
            } else {
                for (int k = 0; k <= withheld; ++k) {
                    for (int i = 0; i < kQ; ++i) {
                        const float v = static_cast<float>(std::sin(2.0 * M_PI * kHz * static_cast<double>(phase_frame + static_cast<uint64_t>(i)) / kRate));
                        q[static_cast<size_t>(i) * kCh] = v;
                        q[static_cast<size_t>(i) * kCh + 1] = v;
                    }
                    phase_frame += static_cast<uint64_t>(kQ);
                    ring.Push(q.data(), kQ);
                }
                withheld = 0;
            }
            // The device's pulls for this wall tick: 800 frames per tick in 512-frame chunks.
            due_frames += static_cast<uint64_t>(kQ);
            while (pulled_frames + 512 <= due_frames) {
                ring.Pull(chunk.data(), 512);
                for (int i = 0; i < 512; ++i) {
                    const float v = chunk[static_cast<size_t>(i) * kCh];
                    if ((prev < 0.0f && v >= 0.0f) || (prev >= 0.0f && v < 0.0f)) ++zc;
                    prev = v;
                }
                pulled_frames += 512;
            }
        }
        *zero_crossings = zc;
        *ring_out = &ring;
    };
    uint64_t zc_ref = 0, zc_late = 0, zc_zero = 0;
    AudioPullRing *ref = nullptr, *late = nullptr, *zero = nullptr;
    run(false, true, &zc_ref, &ref);
    run(true, true, &zc_late, &late);
    run(true, false, &zc_zero, &zero);
    CHECK(ref->dropped() == 0 && ref->underruns() == 0 && ref->padded() == 0);
    CHECK(late->dropped() == 0);
    CHECK(late->underruns() == 0);
    CHECK(late->padded() > 0 && late->relatches() > 0 && late->trimmed() > 0);
    CHECK(late->Available() <= AudioPullRing::kTrimFrames);
    const double ratio = static_cast<double>(zc_late) / static_cast<double>(zc_ref);
    CHECK(ratio > 0.995 && ratio < 1.005);
    CHECK(zero->underruns() > 0);   // the old contract on the same late producer: gaps
    std::printf("pad-with-tail: ref zc=%llu late zc=%llu (ratio %.4f) padded=%llu relatches=%llu trimmed=%llu dropped=%llu underruns=%llu; zero-fill underruns=%llu\n",
                (unsigned long long)zc_ref, (unsigned long long)zc_late, ratio, (unsigned long long)late->padded(),
                (unsigned long long)late->relatches(), (unsigned long long)late->trimmed(), (unsigned long long)late->dropped(),
                (unsigned long long)late->underruns(), (unsigned long long)zero->underruns());
}

}  // namespace

int main() {
    TestArithmetic();
    TestOrder();
    TestUnderrun();
    TestOverrun();
    TestNoDrift();
    TestSpsc();
    TestPadWithTail();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_audio_pull_ring: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_audio_pull_ring: PASS (arithmetic, order, underrun, overrun, no-drift, spsc, pad-with-tail)\n");
    return 0;
}

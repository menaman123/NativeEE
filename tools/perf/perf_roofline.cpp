// tools/perf/perf_roofline.cpp -- the HOST ROOFLINE microbenchmark (CPU side).
//
// Measures, on THIS machine, the peaks every engine unit's achieved rate is judged against
// (PERF1 roofline method): per-core scalar and SIMD FLOP/s (FMA chains with enough
// independent accumulators to fill the pipeline), L1 / L2 / DRAM read bandwidth (a strided
// sum over a working set of the given size, single thread and all threads), memcpy
// bandwidth, the clock read cost, the scheduler yield cost, a mutex lock/unlock pair, a
// condition-variable round trip between two threads (the handoff shape of the MTVU worker
// and the GS consumer) and an atomic ping-pong (the core-to-core latency floor).
//
// Portable: C++17 + <thread>/<atomic>/<chrono>; the SIMD arm is NEON under PS2R_SIMD (the
// engine's convention; scalar elsewhere -- the row is then absent, never faked). No
// environment is read; every knob is a flag. Output is one `key=value` row per line so the
// notes can quote it verbatim:
//
//   perf_roofline [--threads N] [--reps N] [--quick] [--pin]
//
// Each row is the best of --reps repetitions (a roofline is the PEAK, not the mean); the
// run quotes its wall-clock so a shared-host reading can be discarded by its load.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if PS2R_SIMD
#include <arm_neon.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double SecondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// Best-of-N: the caller's body returns the work done (ops / bytes); the row is max(work / s).
template <class F>
double BestRate(int reps, F body) {
    double best = 0.0;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = Clock::now();
        const double work = body();
        const double s = SecondsSince(t0);
        if (s > 0) best = std::max(best, work / s);
    }
    return best;
}

// Best-of-N over a body that already returns a rate (the parallel rows).
template <class F>
double BestOf(int reps, F body) {
    double best = 0.0;
    for (int r = 0; r < reps; ++r) best = std::max(best, body());
    return best;
}

volatile float g_sink_f = 0.0f;
volatile uint64_t g_sink_u = 0;

// ---- FLOP/s ------------------------------------------------------------------------------
// Scalar: 16 independent fma chains (4 FP pipes x 4-cycle latency on an M-series P core need 16 in flight).
double ScalarFlops(uint64_t iters) {
    float a[16];
    for (int i = 0; i < 16; ++i) a[i] = 1.0f + 0.1f * float(i);
    const float m = 0.999999f, c = 0.000001f;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int k = 0; k < 16; ++k) a[k] = a[k] * m + c;
    }
    float s = 0; for (int k = 0; k < 16; ++k) s += a[k];
    g_sink_f = s;
    return double(iters) * 16.0 * 2.0;   // 16 chains x (mul + add)
}

#if PS2R_SIMD
// NEON: 16 independent float32x4 FMA chains = 128 FLOP per iteration.
double NeonFlops(uint64_t iters) {
    float32x4_t a[16];
    for (int k = 0; k < 16; ++k) a[k] = vdupq_n_f32(1.0f + 0.1f * float(k));
    const float32x4_t m = vdupq_n_f32(0.999999f), c = vdupq_n_f32(0.000001f);
    for (uint64_t i = 0; i < iters; ++i) {
        for (int k = 0; k < 16; ++k) a[k] = vfmaq_f32(c, a[k], m);
    }
    float32x4_t s = a[0]; for (int k = 1; k < 16; ++k) s = vaddq_f32(s, a[k]);
    g_sink_f = vgetq_lane_f32(s, 0) + vgetq_lane_f32(s, 1) + vgetq_lane_f32(s, 2) + vgetq_lane_f32(s, 3);
    return double(iters) * 16.0 * 4.0 * 2.0;
}
#endif

// Integer: 8 independent 64-bit mul-add chains (the recompiled EE bodies are integer-heavy).
double ScalarIops(uint64_t iters) {
    uint64_t a0 = 1, a1 = 2, a2 = 3, a3 = 4, a4 = 5, a5 = 6, a6 = 7, a7 = 8;
    for (uint64_t i = 0; i < iters; ++i) {
        a0 = a0 * 3 + i; a1 = a1 * 3 + i; a2 = a2 * 3 + i; a3 = a3 * 3 + i;
        a4 = a4 * 3 + i; a5 = a5 * 3 + i; a6 = a6 * 3 + i; a7 = a7 * 3 + i;
    }
    g_sink_u = a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7;
    return double(iters) * 8.0 * 2.0;
}

// ---- bandwidth -----------------------------------------------------------------------------
// Read: sum of a working set of `bytes` as 64-bit words, repeated until >= `min_bytes` moved.
// (4 accumulators; the compiler vectorises; the loop is load-bound once the set exceeds L1.)
double ReadBandwidth(const uint64_t* buf, size_t words, uint64_t min_bytes) {
    uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    uint64_t moved = 0;
    const uint64_t pass = uint64_t(words) * 8;
    while (moved < min_bytes) {
        std::atomic_signal_fence(std::memory_order_seq_cst);   // a compiler barrier: every pass re-reads the set
        for (size_t i = 0; i + 4 <= words; i += 4) {
            s0 += buf[i]; s1 += buf[i + 1]; s2 += buf[i + 2]; s3 += buf[i + 3];
        }
        g_sink_u = s0;
        moved += pass;
    }
    g_sink_u = s0 ^ s1 ^ s2 ^ s3;
    return double(moved);
}

double WriteBandwidth(uint64_t* buf, size_t words, uint64_t min_bytes) {
    uint64_t moved = 0;
    const uint64_t pass = uint64_t(words) * 8;
    uint64_t v = 1;
    while (moved < min_bytes) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        for (size_t i = 0; i < words; ++i) buf[i] = v;
        ++v;
        moved += pass;
    }
    g_sink_u = buf[words / 2];
    return double(moved);
}

double CopyBandwidth(uint64_t* dst, const uint64_t* src, size_t words, uint64_t min_bytes) {
    uint64_t moved = 0;
    const uint64_t pass = uint64_t(words) * 8;
    while (moved < min_bytes) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        std::memcpy(dst, src, words * 8);
        g_sink_u = dst[moved % words];
        moved += pass;
    }
    g_sink_u = dst[words / 2];
    return double(moved) * 2.0;   // read + write
}

// Dependent pointer chase over a random cycle covering the set: the LOAD-TO-USE latency per level.
double ChaseLatencyNs(const uint32_t* next, size_t n, uint64_t steps) {
    uint32_t p = 0;
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < steps; ++i) p = next[p];
    const double s = SecondsSince(t0);
    g_sink_u = p;
    (void)n;
    return s * 1e9 / double(steps);
}

std::vector<uint32_t> RandomCycle(size_t n, uint32_t seed) {
    std::vector<uint32_t> perm(n);
    for (size_t i = 0; i < n; ++i) perm[i] = uint32_t(i);
    uint64_t x = seed | 1;
    for (size_t i = n - 1; i > 0; --i) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        std::swap(perm[i], perm[x % (i + 1)]);
    }
    std::vector<uint32_t> next(n);
    for (size_t i = 0; i < n; ++i) next[perm[i]] = perm[(i + 1) % n];
    return next;
}

// ---- latency: clock, yield, mutex, condvar round trip, atomic ping-pong -------------------
double ClockNs(uint64_t n) {
    const auto t0 = Clock::now();
    uint64_t acc = 0;
    for (uint64_t i = 0; i < n; ++i) acc += uint64_t(Clock::now().time_since_epoch().count());
    const double s = SecondsSince(t0);
    g_sink_u = acc;
    return s * 1e9 / double(n);
}

double YieldNs(uint64_t n) {
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < n; ++i) std::this_thread::yield();
    return SecondsSince(t0) * 1e9 / double(n);
}

double MutexPairNs(uint64_t n) {
    std::mutex m;
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < n; ++i) { m.lock(); m.unlock(); }
    return SecondsSince(t0) * 1e9 / double(n);
}

// Two threads hand a token back and forth through a condition variable: one round trip =
// two wakes (the worker handoff shape: post work, wait for completion).
double CondvarRoundTripUs(uint64_t rounds) {
    std::mutex m;
    std::condition_variable cv;
    int token = 0;   // 0 = main's turn, 1 = worker's turn
    bool stop = false;
    std::thread worker([&] {
        std::unique_lock<std::mutex> lk(m);
        for (;;) {
            cv.wait(lk, [&] { return token == 1 || stop; });
            if (stop) return;
            token = 0;
            cv.notify_one();
        }
    });
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < rounds; ++i) {
        std::unique_lock<std::mutex> lk(m);
        token = 1;
        cv.notify_one();
        cv.wait(lk, [&] { return token == 0; });
    }
    const double s = SecondsSince(t0);
    { std::lock_guard<std::mutex> lk(m); stop = true; }
    cv.notify_one();
    worker.join();
    return s * 1e6 / double(rounds);
}

// Spinning ping-pong on one atomic: the cache-line transfer latency between two cores (x2 per round).
double AtomicPingPongNs(uint64_t rounds) {
    alignas(128) std::atomic<uint32_t> flag{0};
    std::atomic<bool> go{false};
    std::thread other([&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (uint64_t i = 0; i < rounds; ++i) {
            while (flag.load(std::memory_order_acquire) != 1) {}
            flag.store(2, std::memory_order_release);
        }
    });
    go.store(true, std::memory_order_release);
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < rounds; ++i) {
        flag.store(1, std::memory_order_release);
        while (flag.load(std::memory_order_acquire) != 2) {}
    }
    const double s = SecondsSince(t0);
    other.join();
    return s * 1e9 / double(rounds);
}

// A thread spawn + join: the cost of the pattern the engine must never use per frame.
double ThreadSpawnJoinUs(uint64_t n) {
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < n; ++i) { std::thread t([] {}); t.join(); }
    return SecondsSince(t0) * 1e6 / double(n);
}

// Run `body` on `threads` threads at once; returns the sum of their work / the longest wall.
template <class F>
double ParallelRate(int threads, F body) {
    std::vector<std::thread> ts;
    std::vector<double> work(size_t(threads), 0.0);
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    for (int t = 0; t < threads; ++t) {
        ts.emplace_back([&, t] {
            ready.fetch_add(1);
            while (!go.load(std::memory_order_acquire)) {}
            work[size_t(t)] = body(t);
        });
    }
    while (ready.load() < threads) {}
    const auto t0 = Clock::now();
    go.store(true, std::memory_order_release);
    for (auto& t : ts) t.join();
    const double s = SecondsSince(t0);
    double total = 0.0;
    for (double w : work) total += w;
    return total / s;
}

struct Args {
    int threads = int(std::thread::hardware_concurrency());
    int reps = 5;
    bool quick = false;
};

}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--threads" && i + 1 < argc) a.threads = std::atoi(argv[++i]);
        else if (s == "--reps" && i + 1 < argc) a.reps = std::atoi(argv[++i]);
        else if (s == "--quick") a.quick = true;
        else { std::fprintf(stderr, "usage: perf_roofline [--threads N] [--reps N] [--quick]\n"); return 2; }
    }
    if (a.threads < 1) a.threads = 1;
    const double k = a.quick ? 0.25 : 1.0;
    const uint64_t flop_iters = uint64_t(200e6 * k);
    const uint64_t bw_bytes = uint64_t(2e9 * k);

    std::printf("perf_roofline hw_threads=%u threads=%d reps=%d\n", std::thread::hardware_concurrency(), a.threads, a.reps);

    // --- compute ---
    const double sf = BestRate(a.reps, [&] { return ScalarFlops(flop_iters); });
    std::printf("cpu_scalar_fma_gflops_1core=%.1f\n", sf / 1e9);
    const double si = BestRate(a.reps, [&] { return ScalarIops(flop_iters); });
    std::printf("cpu_scalar_int64_giops_1core=%.1f\n", si / 1e9);
#if PS2R_SIMD
    const double nf = BestRate(a.reps, [&] { return NeonFlops(flop_iters); });
    std::printf("cpu_neon_fma_gflops_1core=%.1f\n", nf / 1e9);
    const double nf_all = BestOf(std::max(1, a.reps / 2), [&] {
        return ParallelRate(a.threads, [&](int) { return NeonFlops(flop_iters / 2); });
    });
    std::printf("cpu_neon_fma_gflops_%dthreads=%.1f\n", a.threads, nf_all / 1e9);
#else
    std::printf("cpu_neon_fma_gflops_1core=absent(PS2R_SIMD off)\n");
#endif
    const double sf_all = BestOf(std::max(1, a.reps / 2), [&] {
        return ParallelRate(a.threads, [&](int) { return ScalarFlops(flop_iters / 2); });
    });
    std::printf("cpu_scalar_fma_gflops_%dthreads=%.1f\n", a.threads, sf_all / 1e9);

    // --- bandwidth per level: 32 KB (L1), 2 MB (L2-resident on a P core), 64 MB (L2 miss / SLC), 512 MB (DRAM) ---
    const size_t sets[] = {32u << 10, 2u << 20, 64u << 20, 512u << 20};
    const char* names[] = {"l1_32k", "l2_2m", "slc_64m", "dram_512m"};
    std::vector<uint64_t> buf((512u << 20) / 8, 1), buf2((512u << 20) / 8, 2);
    for (int i = 0; i < 4; ++i) {
        const size_t words = sets[i] / 8;
        const uint64_t want = std::max<uint64_t>(bw_bytes / (i == 0 ? 1 : 2), uint64_t(sets[i]) * 4);
        const double rd = BestRate(a.reps, [&] { return ReadBandwidth(buf.data(), words, want); });
        const double wr = BestRate(a.reps, [&] { return WriteBandwidth(buf2.data(), words, want); });
        const double cp = BestRate(a.reps, [&] { return CopyBandwidth(buf2.data(), buf.data(), words, want); });
        std::printf("bw_read_%s_gbps_1core=%.1f\nbw_write_%s_gbps_1core=%.1f\nbw_memcpy_%s_gbps_1core=%.1f\n",
                    names[i], rd / 1e9, names[i], wr / 1e9, names[i], cp / 1e9);
    }
    {   // all threads reading disjoint DRAM slices: the aggregate DRAM roofline
        const size_t slice = (512u << 20) / size_t(a.threads) / 8;
        const double rd = BestOf(std::max(1, a.reps / 2), [&] {
            return ParallelRate(a.threads, [&](int t) { return ReadBandwidth(buf.data() + size_t(t) * slice, slice, bw_bytes / uint64_t(a.threads)); });
        });
        std::printf("bw_read_dram_gbps_%dthreads=%.1f\n", a.threads, rd / 1e9);
        const double cp = BestOf(std::max(1, a.reps / 2), [&] {
            return ParallelRate(a.threads, [&](int t) { return CopyBandwidth(buf2.data() + size_t(t) * slice, buf.data() + size_t(t) * slice, slice, bw_bytes / uint64_t(a.threads)); });
        });
        std::printf("bw_memcpy_dram_gbps_%dthreads=%.1f\n", a.threads, cp / 1e9);
    }

    // --- latency per level (dependent chase, one line per step) ---
    for (int i = 0; i < 4; ++i) {
        const size_t n = sets[i] / 64;   // one node per cache line
        std::vector<uint32_t> cyc = RandomCycle(n, 12345u + uint32_t(i));
        std::vector<uint32_t> spread(n * 16, 0);   // 64 B per node
        for (size_t j = 0; j < n; ++j) spread[j * 16] = cyc[j] * 16;
        const uint64_t steps = uint64_t((i == 0 ? 50e6 : i == 1 ? 20e6 : i == 2 ? 5e6 : 2e6) * k);
        double best = 1e9;
        for (int r = 0; r < a.reps; ++r) best = std::min(best, ChaseLatencyNs(spread.data(), n, steps));
        std::printf("lat_load_%s_ns=%.1f\n", names[i], best);
    }

    // --- sync primitives ---
    double best;
    best = 1e9; for (int r = 0; r < a.reps; ++r) best = std::min(best, ClockNs(uint64_t(5e6 * k)));
    std::printf("lat_steady_clock_now_ns=%.1f\n", best);
    best = 1e9; for (int r = 0; r < a.reps; ++r) best = std::min(best, YieldNs(uint64_t(1e6 * k)));
    std::printf("lat_thread_yield_ns=%.1f\n", best);
    best = 1e9; for (int r = 0; r < a.reps; ++r) best = std::min(best, MutexPairNs(uint64_t(1e7 * k)));
    std::printf("lat_mutex_lock_unlock_ns=%.1f\n", best);
    best = 1e9; for (int r = 0; r < a.reps; ++r) best = std::min(best, AtomicPingPongNs(uint64_t(1e6 * k)));
    std::printf("lat_atomic_pingpong_roundtrip_ns=%.1f\n", best);
    best = 1e9; for (int r = 0; r < a.reps; ++r) best = std::min(best, CondvarRoundTripUs(uint64_t(1e5 * k)));
    std::printf("lat_condvar_handoff_roundtrip_us=%.2f\n", best);
    best = 1e9; for (int r = 0; r < std::max(1, a.reps / 2); ++r) best = std::min(best, ThreadSpawnJoinUs(uint64_t(2000 * k)));
    std::printf("lat_thread_spawn_join_us=%.1f\n", best);
    {   // sleep_for(1 ms) overshoot: the pacer's wake precision floor
        double worst = 0, sum = 0; const int n = a.quick ? 50 : 200;
        for (int i = 0; i < n; ++i) {
            const auto t0 = Clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const double over = SecondsSince(t0) * 1e6 - 1000.0;
            worst = std::max(worst, over); sum += over;
        }
        std::printf("lat_sleep_1ms_overshoot_us=mean:%.0f:max:%.0f\n", sum / n, worst);
    }
    return 0;
}

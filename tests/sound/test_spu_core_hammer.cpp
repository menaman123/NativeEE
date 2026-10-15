// SpuCoreHammer: the REALTIME dispatch policy under load. One producer thread
// issues the libsd surface (voice params/addresses, KON/KOFF, ENDX, MVOL and
// the NAX/ENVX polls the module's watchdog makes) while this thread pumps the
// mixer in 64-frame blocks, under a bounded deadline. The ring and the
// per-voice readback words are the only shared state, so the run must
// terminate, every poll must read a value the model could have published,
// the ring's accounting must balance, and -- the eager-hold rule -- an ENVX
// poll made while a voice's key-on hold is up must never read 0 (the pump
// publishes the model only when the hold is 0). The producer is paced one
// KON sweep per pump so the consumer side runs against live holds. Output:
// one summary line and the verdict; nothing is written to disk.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "engine/sound/mixer.h"
#include "engine/sound/spu_core.h"

using ps2r::sound::Mixer;
using ps2r::sound::SpuCore;


static int g_failures = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

namespace {

constexpr uint32_t kToneAddr = 0x6000;
constexpr int kProducerIters = 20000;
constexpr int kDeadlineMs = 20000;
// One KON sweep over all 48 voices per batch; the producer lets the mixer
// pump at least once between batches so the consumer side of the eager-hold
// rule (the pump's compare-and-swap publish, taken only while the voice's
// hold is 0) runs against live holds.
constexpr int kBatchIters = SpuCore::kVoices;
constexpr int kPumpsFloor = kProducerIters / kBatchIters;

void WriteToneBlocks(SpuCore& core) {
    uint8_t* ram = core.SpuRam();
    for (int b = 0; b < 4; b++) {
        uint8_t* blk = ram + kToneAddr + b * VagDecoder::kBlockBytes;
        blk[0] = 0x00;
        blk[1] = b == 3 ? (VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat) : 0;
        for (int i = 2; i < VagDecoder::kBlockBytes; i++) blk[i] = static_cast<uint8_t>((i * 37 + b) & 0xFF);
    }
}

}  // namespace

int main() {
    SpuCore core;
    Mixer mixer(core);
    core.SdInit();
    WriteToneBlocks(core);

    std::atomic<bool> producer_done{false};
    std::atomic<uint64_t> polls{0};
    std::atomic<uint64_t> bad_polls{0};
    std::atomic<uint64_t> pumps{0};
    std::atomic<uint64_t> held_polls{0};        // ENVX polls made while the voice's hold was up
    std::atomic<uint64_t> held_envx_zero{0};    // ... that read 0 (the watchdog-abort value)
    std::atomic<bool> pacing_timed_out{false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kDeadlineMs);

    std::thread producer([&] {
        for (int it = 0; it < kProducerIters; it++) {
            const uint32_t voice = static_cast<uint32_t>(it % SpuCore::kVoicesPerCore);
            const uint32_t core_bit = static_cast<uint32_t>((it / SpuCore::kVoicesPerCore) & 1);
            const uint32_t low = (voice << 1) | core_bit;
            core.SetParam(0x0000 | low, 0x3FFF);            // VOLL
            core.SetParam(0x0100 | low, 0x3FFF);            // VOLR
            core.SetParam(0x0200 | low, 0x1000);            // PITCH 1.0
            core.SetParam(0x0300 | low, 0x000F);            // ADSR1: attack rate 0 (instant)
            core.SetParam(0x0400 | low, 0x1FC0);            // ADSR2
            core.SetAddr(0x2000 | low, kToneAddr);          // SSA
            core.SetAddr(0x2100 | low, kToneAddr);          // LSAX
            core.SetSwitch(0x1500 | core_bit, 1u << voice); // KON
            // The watchdog's polls: NAX must be a byte address inside the
            // 2 MB SPU RAM and ENVX a 15-bit value.
            const int vi = static_cast<int>(core_bit) * SpuCore::kVoicesPerCore + static_cast<int>(voice);
            const uint32_t hold_before = core.EagerHold(vi);
            const uint32_t nax = core.GetAddr(0x2200 | low);
            const uint32_t envx = core.GetParam(0x0500 | low);
            const uint32_t hold_after = core.EagerHold(vi);
            polls.fetch_add(2, std::memory_order_relaxed);
            if (nax >= ps2r::sound::kSpuRamSize || envx > 0x7FFF) bad_polls.fetch_add(1, std::memory_order_relaxed);
            // While the KON is still in the ring the published ENVX is the
            // eager attack value (ADSR1 attack rate 0 -> 0x1FFF); the pump
            // publishes the model's ENVX only once the hold is 0.
            if (hold_before != 0 && hold_after != 0) {
                held_polls.fetch_add(1, std::memory_order_relaxed);
                if (envx == 0) held_envx_zero.fetch_add(1, std::memory_order_relaxed);
            }
            if ((it & 7) == 7) core.SetSwitch(0x1600 | core_bit, 1u << voice);   // KOFF
            if ((it & 63) == 63) core.SetSwitch(0x1700 | core_bit, 0);           // ENDX clear
            (void)core.GetSwitch(0x1700 | core_bit);
            core.SetParam(0x0980 | core_bit, 0x3FFF);       // MVOLL
            core.SetParam(0x0A80 | core_bit, 0x3FFF);       // MVOLR
            if ((it % kBatchIters) == kBatchIters - 1) {
                // Pace: wait for the mixer to pump at least once past this batch.
                const uint64_t seen = pumps.load(std::memory_order_acquire);
                while (pumps.load(std::memory_order_acquire) == seen) {
                    if (std::chrono::steady_clock::now() > deadline) {
                        pacing_timed_out.store(true, std::memory_order_relaxed);
                        break;
                    }
                    std::this_thread::yield();
                }
                if (pacing_timed_out.load(std::memory_order_relaxed)) break;
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    bool timed_out = false;
    while (!producer_done.load(std::memory_order_acquire)) {
        mixer.Pump(64);   // short pumps: more consumer passes interleaved with the producer
        pumps.fetch_add(1, std::memory_order_release);
        if (std::chrono::steady_clock::now() > deadline) {
            timed_out = true;
            break;
        }
    }
    CHECK(!timed_out);
    if (timed_out) {
        // Do not join a thread that may never finish: report and fail.
        std::printf("test_spu_core_hammer: deadline of %d ms exceeded\n", kDeadlineMs);
        producer.detach();
        return 1;
    }
    producer.join();
    // Drain what the producer left queued.
    mixer.Pump();
    mixer.Pump();

    CHECK(!pacing_timed_out.load());
    // The pacing guarantees a consumer pass between KON batches: at least one
    // pump per batch, so the held-voice publish path ran ~400 times.
    CHECK(pumps.load() >= static_cast<uint64_t>(kPumpsFloor));
    CHECK(polls.load() == static_cast<uint64_t>(2 * kProducerIters));
    CHECK(bad_polls.load() == 0);
    // The eager-hold half was exercised and never published a 0 ENVX.
    CHECK(held_polls.load() > 0);
    CHECK(held_envx_zero.load() == 0);
    CHECK(core.KonCount() == static_cast<uint64_t>(kProducerIters));
    // The ring is either fully drained or lost commands only by drop-oldest.
    CHECK(core.Ring().Size() == 0);
    // Once quiescent, no eager hold survives a drained ring unless its KON
    // was dropped by an overflow (then SilenceAll is the re-zero point).
    uint64_t holds = 0;
    for (int vi = 0; vi < SpuCore::kVoices; vi++) holds += core.EagerHold(vi);
    if (core.Ring().Dropped() == 0) CHECK(holds == 0);
    // Every readback is a value the model could publish.
    for (int vi = 0; vi < SpuCore::kVoices; vi++) {
        const uint32_t low = static_cast<uint32_t>(((vi % SpuCore::kVoicesPerCore) << 1) | (vi / SpuCore::kVoicesPerCore));
        CHECK(core.GetAddr(0x2200 | low) < ps2r::sound::kSpuRamSize);
        CHECK(core.GetParam(0x0500 | low) <= 0x7FFF);
    }
    std::printf("test_spu_core_hammer: pumps=%llu (floor %d) polls=%llu held_polls=%llu held_envx_zero=%llu dropped=%llu holds=%llu\n",
                static_cast<unsigned long long>(pumps.load()), kPumpsFloor, static_cast<unsigned long long>(polls.load()),
                static_cast<unsigned long long>(held_polls.load()), static_cast<unsigned long long>(held_envx_zero.load()),
                static_cast<unsigned long long>(core.Ring().Dropped()), static_cast<unsigned long long>(holds));
    if (g_failures) {
        std::printf("test_spu_core_hammer: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("test_spu_core_hammer: all vectors passed\n");
    return 0;
}

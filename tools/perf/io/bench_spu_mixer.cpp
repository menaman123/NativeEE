// tools/perf/bench_spu_mixer.cpp -- the SPU mixer's roofline bench + equivalence oracle.
//
// Pumps SpuCore::MixInto exactly as the title's frame boundary does (800 stereo frames per pump, no
// VoiceSource, no PcmSource) over a synthetic scenario the game cannot beat: every one of the 48 voices
// keyed on looping ADPCM blocks, pitches spread over 0x400..0x3FFF, volume sweeps and envelope phases
// mixed, KON/KOFF churn every N pumps. Reports ns per voice-sample and ms per 800-frame pump (the
// per-frame cost on the EE thread), plus the FNV-1a64 hash of every float the mixer produced -- the
// oracle for a mixer change: same scenario, same hash = output-identical.
//
//   bench_spu_mixer [--pumps N] [--voices V] [--churn K] [--quiet]
//
// Portable: no platform calls beyond <chrono>.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/core/hash/hash.h"
#include "engine/platform/host/platform.h"
#include "engine/sound/mixer.h"
#include "engine/sound/spu_core.h"

using ps2r::sound::Mixer;
using ps2r::sound::SpuCore;

namespace {

constexpr uint32_t kBankAddr = 0x10000;
constexpr int kBlocksPerSample = 64;   // 64 blocks x 28 = 1792 source samples per loop
constexpr int kSamplesInBank = 48;     // one distinct sample per voice

uint32_t Lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

void WriteBank(SpuCore& core) {
    uint8_t* ram = core.SpuRam();
    uint32_t seed = 12345;
    for (int smp = 0; smp < kSamplesInBank; smp++) {
        for (int b = 0; b < kBlocksPerSample; b++) {
            uint8_t* blk = ram + kBankAddr + (static_cast<uint32_t>(smp * kBlocksPerSample + b)) * VagDecoder::kBlockBytes;
            const uint32_t r = Lcg(seed);
            blk[0] = static_cast<uint8_t>(((r % 5) << 4) | (4 + (r >> 4) % 8));   // filter 0..4, shift 4..11
            blk[1] = 0;
            if (b == 0) blk[1] |= VagDecoder::kFlagLoopStart;
            if (b == kBlocksPerSample - 1) blk[1] |= VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat;
            for (int i = 2; i < VagDecoder::kBlockBytes; i++) blk[i] = static_cast<uint8_t>(Lcg(seed));
        }
    }
}

int g_pitch = 0;   // --pitch P: every voice at P (0 = spread over 0x400..0x3FFF)

uint32_t Low(int vi) { return static_cast<uint32_t>(((vi % SpuCore::kVoicesPerCore) << 1) | (vi / SpuCore::kVoicesPerCore)); }

void ProgramVoice(SpuCore& core, int vi, uint32_t& seed) {
    const uint32_t low = Low(vi);
    const uint32_t r = Lcg(seed);
    // Volume: mostly fixed levels, every 4th voice a sweep (bit 15), a few phase-inverted.
    const uint32_t voll = (vi % 4 == 3) ? (0x8000u | 0x4000u | ((r & 0x1F) << 2) | 1u) : (0x1000u + (r & 0x2FFF));
    const uint32_t volr = (vi % 4 == 3) ? (0x8000u | 0x2000u | (((r >> 5) & 0x1F) << 2)) : (0x1000u + ((r >> 3) & 0x2FFF));
    core.SetParam(0x0000 | low, voll);
    core.SetParam(0x0100 | low, volr);
    const uint32_t pitch = 0x400u + (Lcg(seed) % 0x3C00u);
    core.SetParam(0x0200 | low, g_pitch ? static_cast<uint32_t>(g_pitch) : pitch);        // PITCH 0x400..0x3FFF
    core.SetParam(0x0300 | low, 0x8000u | ((Lcg(seed) & 0x1F) << 10) | (0x4u << 4) | 0xC); // ADSR1: exp attack, decay 4, SL 12
    core.SetParam(0x0400 | low, (vi & 1 ? 0xC000u : 0x0000u) | ((Lcg(seed) & 0x1F) << 8) | 0x10u);   // ADSR2: sustain, release 16
    core.SetAddr(0x2000 | low, kBankAddr + static_cast<uint32_t>((vi % kSamplesInBank) * kBlocksPerSample) * VagDecoder::kBlockBytes);
}

struct Row {
    uint64_t hash = 0;
    double med_ms = 0, min_ms = 0, max_ms = 0, total_ms = 0;
    int active = 0;
    uint64_t fast_chunks = 0;
};

Row RunScenario(bool fast, int pumps, int voices, int churn) {
    SpuCore::SetFastMix(fast);
    SpuCore core;
    Mixer mixer(core);
    core.SdInit();
    WriteBank(core);
    // The core gains as libsd leaves them after the title's init: MVOL full on both cores.
    core.SetParam(0x0980, 0x3FFF);
    core.SetParam(0x0A80, 0x3FFF);
    core.SetParam(0x0981, 0x3FFF);
    core.SetParam(0x0A81, 0x3FFF);
    uint32_t seed = 777;
    for (int vi = 0; vi < voices; vi++) ProgramVoice(core, vi, seed);
    uint32_t kon0 = 0, kon1 = 0;
    for (int vi = 0; vi < voices; vi++) (vi < SpuCore::kVoicesPerCore ? kon0 : kon1) |= 1u << (vi % SpuCore::kVoicesPerCore);
    core.SetSwitch(0x1500, kon0);
    core.SetSwitch(0x1501, kon1);
    mixer.Pump();   // the first pump applies the ring (not timed)

    Row row;
    uint64_t h = ps2r::hash::kFnv1a64Offset;
    std::vector<double> per_pump_ms;
    per_pump_ms.reserve(static_cast<size_t>(pumps));
    const auto t0 = std::chrono::steady_clock::now();
    for (int p = 0; p < pumps; p++) {
        if (churn > 0 && p % churn == churn - 1) {
            // KOFF a quarter of the voices, reprogram them and KON them again (release tails overlap attacks);
            // every 4th churn also sweeps MVOL0 so the master sweep path runs.
            uint32_t m0 = 0, m1 = 0;
            for (int vi = (p / churn) % 4; vi < voices; vi += 4) {
                (vi < SpuCore::kVoicesPerCore ? m0 : m1) |= 1u << (vi % SpuCore::kVoicesPerCore);
                ProgramVoice(core, vi, seed);
            }
            core.SetSwitch(0x1600, m0);
            core.SetSwitch(0x1601, m1);
            core.SetSwitch(0x1500, m0);
            core.SetSwitch(0x1501, m1);
            if ((p / churn) % 4 == 2) core.SetParam(0x0980, 0x8000u | 0x2000u | (0x10u << 2) | 1u);   // MVOLL0 linear decrease sweep
            if ((p / churn) % 4 == 3) core.SetParam(0x0980, 0x3FFF);
        }
        const auto a = std::chrono::steady_clock::now();
        mixer.Pump();
        const auto b = std::chrono::steady_clock::now();
        per_pump_ms.push_back(std::chrono::duration<double, std::milli>(b - a).count());
        h = ps2r::hash::Fnv1a64(mixer.Output(), static_cast<size_t>(mixer.OutputFrames()) * 2 * sizeof(float), h);
    }
    const auto t1 = std::chrono::steady_clock::now();
    row.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::vector<double> sorted = per_pump_ms;
    std::sort(sorted.begin(), sorted.end());
    row.med_ms = sorted.empty() ? 0 : sorted[sorted.size() / 2];
    row.min_ms = sorted.empty() ? 0 : sorted.front();
    row.max_ms = sorted.empty() ? 0 : sorted.back();
    row.hash = h;
    row.active = core.ActiveVoices();
    row.fast_chunks = core.FastChunks();
    return row;
}

}  // namespace

int main(int argc, char** argv) {
    int pumps = 600, voices = SpuCore::kVoices, churn = 30;
    bool quiet = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--pumps") && i + 1 < argc) pumps = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--voices") && i + 1 < argc) voices = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--churn") && i + 1 < argc) churn = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--pitch") && i + 1 < argc) g_pitch = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--quiet")) quiet = true;
    }
    if (voices < 0) voices = 0;
    if (voices > SpuCore::kVoices) voices = SpuCore::kVoices;
    // The EE thread's class (macOS: USER_INTERACTIVE keeps the bench off the efficiency cores -- the mixer runs on
    // the game thread, which the composition pins the same way).
    ps2r::platform::host::PinCurrentThreadInteractive("bench-mixer", ps2r::platform::host::ThreadRole::Game);
    const int denom = Mixer::kFramesPerVsync * (voices ? voices : 1);
    const Row slow = RunScenario(false, pumps, voices, churn);
    const Row fast = RunScenario(true, pumps, voices, churn);
    std::printf("bench_spu_mixer pumps=%d voices=%d churn=%d frames_per_pump=%d active=%d\n", pumps, voices, churn, Mixer::kFramesPerVsync, slow.active);
    std::printf("  sample-major (oracle): pump_ms median=%.3f min=%.3f max=%.3f ns_per_voice_sample=%.2f total_ms=%.1f hash=%016llx\n",
                slow.med_ms, slow.min_ms, slow.max_ms, slow.med_ms * 1e6 / denom, slow.total_ms, static_cast<unsigned long long>(slow.hash));
    std::printf("  voice-major  (fast)  : pump_ms median=%.3f min=%.3f max=%.3f ns_per_voice_sample=%.2f total_ms=%.1f hash=%016llx chunks=%llu\n",
                fast.med_ms, fast.min_ms, fast.max_ms, fast.med_ms * 1e6 / denom, fast.total_ms, static_cast<unsigned long long>(fast.hash),
                static_cast<unsigned long long>(fast.fast_chunks));
    std::printf("  %s speedup=%.2fx | budget: a 60 Hz frame is 16.667 ms; the fast pump is %.2f%% of it on the EE thread\n",
                slow.hash == fast.hash ? "IDENTICAL" : "*** DIFFERENT OUTPUT ***", fast.med_ms > 0 ? slow.med_ms / fast.med_ms : 0.0,
                fast.med_ms / 16.667 * 100.0);
    (void)quiet;
    return slow.hash == fast.hash ? 0 : 1;
}

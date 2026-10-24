// FmvPcmInput: the mixer's FMV PCM source. Pins (1) the FIFO: order, the
// closed gate (nothing pops, frames stay queued), drop-newest with the
// counter, wrap-around, Flush (gate closed + queue discarded), the GateOpen /
// Flush counters; (2) the mix rule in SpuCore::MixInto: frames are added after
// the voice mix at the core-0 BVOLL/BVOLR gain (0x7FFF = unity, 0x4000 = half,
// 0 = silent but still consumed, bit 15 = unity), before the master gain; a
// keyed voice and the FMV sum linearly; the gate closed = the voices alone;
// (3) through Mixer::Pump: 800 frames per pump, a partial queue leaves the tail
// silent, the core's mixed-frames tally.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/sound/fmv_pcm_input.h"
#include "engine/sound/mixer.h"
#include "engine/sound/spu_core.h"
#include "engine/sound/vag_decoder.h"

using ps2r::sound::FmvPcmInput;
using ps2r::sound::Mixer;
using ps2r::sound::SpuCore;


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

static bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// `frames` stereo frames whose L sample is `base + i` and R is `-(base + i)`.
static std::vector<int16_t> Ramp(int base, int frames) {
    std::vector<int16_t> v(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; i++) {
        v[static_cast<size_t>(i) * 2] = static_cast<int16_t>(base + i);
        v[static_cast<size_t>(i) * 2 + 1] = static_cast<int16_t>(-(base + i));
    }
    return v;
}

static void KeyTestTone(SpuCore& core, uint32_t tone_addr) {
    for (int b = 0; b < 4; b++) {
        uint8_t* t = core.SpuRam() + tone_addr + b * VagDecoder::kBlockBytes;
        t[0] = 0x04;
        t[1] = (b == 0) ? VagDecoder::kFlagLoopStart
             : (b == 3) ? (VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat)
                        : 0;
        std::memset(t + 2, (b & 1) ? 0x88 : 0x77, 14);
    }
    core.SetAddr(0x2000, tone_addr);   // voice 0 core 0 SSA
    core.SetAddr(0x2100, tone_addr);   // LSAX
    core.SetParam(0x000, 0x2000);      // VOLL
    core.SetParam(0x100, 0x2000);      // VOLR
    core.SetParam(0x200, 0x1000);      // pitch 1.0
    core.SetParam(0x300, 0x000F);      // instant attack, full sustain
    core.SetParam(0x400, 0x0000);      // instant release
    core.SetParam(0x980, 0x3FFF);      // MVOLL core 0
    core.SetParam(0xA80, 0x3FFF);      // MVOLR core 0
    core.SetParam(0x981, 0x3FFF);      // MVOLL core 1 (core 0 reaches the output through core 1)
    core.SetParam(0xA81, 0x3FFF);      // MVOLR core 1
    core.SetSwitch(0x1500, 0x1);       // KON voice 0
}

static void TestFifoGateAndOrder() {
    FmvPcmInput in(16);
    CHECK(in.Capacity() == 16);
    CHECK(!in.GateIsOpen());
    const std::vector<int16_t> a = Ramp(100, 5);
    in.Push(a.data(), 5);
    CHECK(in.Queued() == 5);
    CHECK(in.Pushed() == 5);
    int16_t out[32] = {};
    CHECK(in.Pop(out, 8) == 0);   // the gate is closed: nothing flows, nothing is lost
    CHECK(in.Queued() == 5);
    CHECK(in.Consumed() == 0);
    in.GateOpen();
    CHECK(in.GateIsOpen());
    CHECK(in.GateOpens() == 1);
    CHECK(in.Pop(out, 3) == 3);   // a partial pop leaves the rest queued, in order
    CHECK(out[0] == 100 && out[1] == -100 && out[2] == 101 && out[3] == -101 && out[4] == 102 && out[5] == -102);
    CHECK(in.Queued() == 2);
    CHECK(in.Pop(out, 8) == 2);
    CHECK(out[0] == 103 && out[2] == 104);
    CHECK(in.Pop(out, 8) == 0);   // empty
    CHECK(in.Consumed() == 5);
    CHECK(in.Dropped() == 0);
    // Degenerate pushes/pops are no-ops.
    in.Push(nullptr, 4);
    in.Push(a.data(), 0);
    CHECK(in.Queued() == 0 && in.Pushed() == 5);
    CHECK(in.Pop(out, 0) == 0);
}

static void TestDropNewestAndWrap() {
    FmvPcmInput in(8);
    const std::vector<int16_t> a = Ramp(0, 6);
    const std::vector<int16_t> b = Ramp(6, 4);
    in.Push(a.data(), 6);
    in.Push(b.data(), 4);   // only 2 of 4 fit: the newest 2 are dropped
    CHECK(in.Queued() == 8);
    CHECK(in.Dropped() == 2);
    CHECK(in.Pushed() == 8);
    in.GateOpen();
    int16_t out[16] = {};
    CHECK(in.Pop(out, 8) == 8);
    bool ordered = true;
    for (int i = 0; i < 8; i++) ordered = ordered && out[i * 2] == i && out[i * 2 + 1] == -i;
    CHECK(ordered);   // the queued frames were never overwritten
    // Wrap-around: the cursors are at 8 (= capacity); a 5-frame push crosses the end.
    const std::vector<int16_t> c = Ramp(20, 5);
    in.Push(c.data(), 5);
    CHECK(in.Queued() == 5);
    CHECK(in.Pop(out, 8) == 5);
    ordered = true;
    for (int i = 0; i < 5; i++) ordered = ordered && out[i * 2] == 20 + i && out[i * 2 + 1] == -(20 + i);
    CHECK(ordered);
    // A push larger than the whole capacity keeps the first `capacity` frames.
    const std::vector<int16_t> d = Ramp(50, 12);
    in.Push(d.data(), 12);
    CHECK(in.Queued() == 8);
    CHECK(in.Dropped() == 2 + 4);
    CHECK(in.Pop(out, 8) == 8);
    CHECK(out[0] == 50 && out[14] == 57);
}

static void TestFlush() {
    FmvPcmInput in(16);
    const std::vector<int16_t> a = Ramp(0, 6);
    in.Push(a.data(), 6);
    in.GateOpen();
    in.Flush();
    CHECK(in.Flushes() == 1);
    CHECK(!in.GateIsOpen());
    CHECK(in.Queued() == 0);   // the queued frames are discarded
    int16_t out[32] = {};
    CHECK(in.Pop(out, 8) == 0);
    in.Push(a.data(), 6);      // a new movie's audio queues behind the closed gate
    CHECK(in.Queued() == 6);
    CHECK(in.Pop(out, 8) == 0);
    in.GateOpen();
    CHECK(in.Pop(out, 8) == 6);
    CHECK(out[0] == 0 && out[10] == 5);
    CHECK(in.Consumed() == 6);
    // Space freed by the flush is reusable in full.
    in.Flush();
    const std::vector<int16_t> b = Ramp(0, 16);
    in.Push(b.data(), 16);
    CHECK(in.Queued() == 16 && in.Dropped() == 0);
}

static void TestGainRule() {
    CHECK(Near(SpuCore::ExternalInputGain(0x7FFF), 32767.0f / 32768.0f));
    CHECK(Near(SpuCore::ExternalInputGain(0x4000), 16384.0f / 32768.0f));
    CHECK(Near(SpuCore::ExternalInputGain(0), 0.0f));
    CHECK(Near(SpuCore::ExternalInputGain(0x8000), -1.0f));   // the register is a signed 16-bit level
    CHECK(Near(SpuCore::ExternalInputGain(0xFFFF), -1.0f / 32768.0f));

    SpuCore core;
    core.SdInit();
    // The chain to the output: MVOL0 and MVOL1 at full scale (0x3FFF), AVOL1 full after SdInit.
    core.SetParam(0x980, 0x3FFF); core.SetParam(0xA80, 0x3FFF);
    core.SetParam(0x981, 0x3FFF); core.SetParam(0xA81, 0x3FFF);
    const float kBv = SpuCore::ExternalInputGain(0x7FFF), kHalf = SpuCore::ExternalInputGain(0x4000);
    const float kChain = SpuCore::MasterRegisterGain(0x3FFF) * SpuCore::ExternalInputGain(0x7FFF) * SpuCore::MasterRegisterGain(0x3FFF);
    FmvPcmInput in(4096);
    core.SetFmvInput(&in);
    CHECK(core.FmvInput() == &in);
    // BVOLL/BVOLR are core attributes (bit 7): entry 0xF80 / 0x1080 for core 0.
    core.SetParam(0xF80, 0x7FFF);
    core.SetParam(0x1080, 0x4000);
    CHECK(Near(core.FmvGainL(), kBv));
    CHECK(Near(core.FmvGainR(), kHalf));
    std::vector<int16_t> pcm(256 * 2);
    for (int i = 0; i < 256; i++) {
        pcm[static_cast<size_t>(i) * 2] = 8192;
        pcm[static_cast<size_t>(i) * 2 + 1] = -16384;
    }
    in.Push(pcm.data(), 256);
    std::vector<float> out(256 * 2, 0.0f);
    core.MixInto(out.data(), 256);
    CHECK(core.FmvFramesMixed() == 0);   // the gate is closed: nothing mixed, nothing consumed
    CHECK(in.Queued() == 256);
    CHECK(out[0] == 0.0f && out[511] == 0.0f);
    in.GateOpen();
    core.MixInto(out.data(), 256);
    CHECK(core.FmvFramesMixed() == 256);
    CHECK(in.Queued() == 0);
    CHECK(Near(out[0], 8192.0f / 32768.0f * kBv * kChain));
    CHECK(Near(out[1], -16384.0f / 32768.0f * kHalf * kChain));
    CHECK(Near(out[510], 8192.0f / 32768.0f * kBv * kChain));
    // The master gain applies to the external input too (it is before the master).
    in.Push(pcm.data(), 256);
    core.SetMasterVolume(0.5f);
    core.MixInto(out.data(), 256);
    CHECK(Near(out[0], 0.5f * 8192.0f / 32768.0f * kBv * kChain));
    core.SetMasterVolume(1.0f);
    // BVOL 0: silent output, but the frames are still consumed (the A/V clock runs).
    core.SetParam(0xF80, 0);
    core.SetParam(0x1080, 0);
    in.Push(pcm.data(), 256);
    core.MixInto(out.data(), 256);
    CHECK(core.FmvFramesMixed() == 768);
    CHECK(in.Queued() == 0);
    CHECK(out[0] == 0.0f && out[1] == 0.0f);
    // Core 1's BVOL does not drive the FMV gain (the rule is core 0's).
    core.SetParam(0xF81, 0x7FFF);
    core.SetParam(0x1081, 0x7FFF);
    in.Push(pcm.data(), 256);
    core.MixInto(out.data(), 256);
    CHECK(out[0] == 0.0f);
    // The clamp applies to the sum.
    core.SetParam(0xF80, 0x7FFF);
    core.SetParam(0x1080, 0x7FFF);
    for (int i = 0; i < 256; i++) pcm[static_cast<size_t>(i) * 2] = -32768;
    in.Push(pcm.data(), 256);
    core.SetMasterVolume(2.0f);
    core.MixInto(out.data(), 256);
    CHECK(out[0] == -1.0f);                                   // -1 * bvol * chain * 2 saturates
    CHECK(Near(out[1], -0.5f * kBv * kChain * 2.0f));         // -16384 at full BVOL, doubled
}

static void TestMixedAfterVoices() {
    // Core A: the tone alone. Core B: the same tone plus the FMV. Both from the
    // same key-on so the voice samples agree frame by frame; B - A must be the
    // FMV contribution, i.e. the external input sums with the voices before the
    // master/clamp (amplitudes kept small so no sample clamps).
    SpuCore a, b;
    a.SdInit();
    b.SdInit();
    KeyTestTone(a, 0x6000);
    KeyTestTone(b, 0x6000);
    FmvPcmInput in(4096);
    b.SetFmvInput(&in);
    b.SetParam(0xF80, 0x7FFF);
    b.SetParam(0x1080, 0x7FFF);
    std::vector<int16_t> pcm(512 * 2);
    for (int i = 0; i < 512; i++) {
        pcm[static_cast<size_t>(i) * 2] = static_cast<int16_t>(1000 + (i % 7));
        pcm[static_cast<size_t>(i) * 2 + 1] = static_cast<int16_t>(-2000 + (i % 5));
    }
    in.Push(pcm.data(), 512);
    in.GateOpen();
    std::vector<float> oa(512 * 2), ob(512 * 2);
    a.MixInto(oa.data(), 512);
    b.MixInto(ob.data(), 512);
    bool voice_heard = false, sums = true, clamped = false;
    for (int i = 0; i < 512; i++) {
        if (oa[static_cast<size_t>(i) * 2] != 0.0f) voice_heard = true;
        const float el = pcm[static_cast<size_t>(i) * 2] / 32768.0f;
        const float er = pcm[static_cast<size_t>(i) * 2 + 1] / 32768.0f;
        if (std::fabs(oa[static_cast<size_t>(i) * 2]) >= 0.9f) clamped = true;
        sums = sums && Near(ob[static_cast<size_t>(i) * 2] - oa[static_cast<size_t>(i) * 2], el, 2e-5f) &&
               Near(ob[static_cast<size_t>(i) * 2 + 1] - oa[static_cast<size_t>(i) * 2 + 1], er, 2e-5f);
    }
    CHECK(voice_heard);
    CHECK(!clamped);
    CHECK(sums);
    CHECK(b.FmvFramesMixed() == 512);
    // Gate closed again (a Flush): B falls back to the voices alone.
    in.Push(pcm.data(), 512);
    in.Flush();
    a.MixInto(oa.data(), 512);
    b.MixInto(ob.data(), 512);
    bool same = true;
    for (size_t i = 0; i < oa.size(); i++) same = same && oa[i] == ob[i];
    CHECK(same);
    CHECK(b.FmvFramesMixed() == 512);
}

// The title's movie-start sequence as the console issues it (sceSdRemote
// rSdSetParam 0xF80 = 0x7FFF, 0x1080 = 0x7FFF; both back to 0 at movie end):
// the BVOL census counts every core-0 BVOLL/BVOLR set, the non-zero ones and
// the peak; FMV frames count as audible only while a BVOL gain is non-zero. A
// core-1 BVOL set (entry bit 0) is not core 0's.
static void TestBvolCensusAndAudibleFrames() {
    SpuCore core;
    core.SetParam(0x980, 0x3FFF); core.SetParam(0xA80, 0x3FFF);
    core.SetParam(0x981, 0x3FFF); core.SetParam(0xA81, 0x3FFF);
    const float kChain = SpuCore::MasterRegisterGain(0x3FFF) * SpuCore::ExternalInputGain(0x7FFF) * SpuCore::MasterRegisterGain(0x3FFF);
    core.SdInit();
    FmvPcmInput in(4096);
    core.SetFmvInput(&in);
    CHECK(core.BvolSets() == 0 && core.BvolSetsNonZero() == 0 && core.BvolPeak() == 0);
    std::vector<int16_t> pcm(256 * 2, 4096);
    std::vector<float> out(256 * 2, 0.0f);
    in.Push(pcm.data(), 256);
    in.GateOpen();
    // The boundary-only pump of the original: BVOL still 0 when the PCM is mixed -> consumed, inaudible.
    core.MixInto(out.data(), 128);
    CHECK(core.FmvFramesMixed() == 128);
    CHECK(core.FmvFramesMixedAudible() == 0);
    CHECK(out[0] == 0.0f);
    // The console's movie start.
    core.SetParam(0xF80, 0x7FFF);
    core.SetParam(0x1080, 0x7FFF);
    CHECK(core.BvolSets() == 2 && core.BvolSetsNonZero() == 2 && core.BvolPeak() == 0x7FFF);
    core.MixInto(out.data(), 128);
    CHECK(core.FmvFramesMixed() == 256);
    CHECK(core.FmvFramesMixedAudible() == 128);
    CHECK(Near(out[0], 4096.0f / 32768.0f * SpuCore::ExternalInputGain(0x7FFF) * kChain));
    // Movie end: back to 0 (counted, not non-zero, the peak stays).
    core.SetParam(0xF80, 0);
    core.SetParam(0x1080, 0);
    CHECK(core.BvolSets() == 4 && core.BvolSetsNonZero() == 2 && core.BvolPeak() == 0x7FFF);
    // Core 1's BVOL (entry bit 0 = core) does not enter core 0's census.
    core.SetParam(0xF81, 0x3FFF);
    CHECK(core.BvolSets() == 4 && core.BvolPeak() == 0x7FFF);
    // A later lower value does not lower the peak; a one-sided gain is audible.
    core.SetParam(0xF80, 0x1000);
    CHECK(core.BvolSets() == 5 && core.BvolSetsNonZero() == 3 && core.BvolPeak() == 0x7FFF);
    in.Push(pcm.data(), 128);
    core.MixInto(out.data(), 128);
    CHECK(core.FmvFramesMixedAudible() == 256);
    CHECK(Near(out[1], 0.0f));   // R gain is 0
    CHECK(Near(out[0], 4096.0f / 32768.0f * SpuCore::ExternalInputGain(0x1000) * kChain));
}

static void TestThroughMixerPump() {
    SpuCore core;
    core.SdInit();
    core.SetParam(0x980, 0x3FFF); core.SetParam(0xA80, 0x3FFF);
    core.SetParam(0x981, 0x3FFF); core.SetParam(0xA81, 0x3FFF);
    const float kGain = SpuCore::ExternalInputGain(0x7FFF) * SpuCore::MasterRegisterGain(0x3FFF) * SpuCore::ExternalInputGain(0x7FFF) * SpuCore::MasterRegisterGain(0x3FFF);
    FmvPcmInput in(8192);
    core.SetFmvInput(&in);
    core.SetParam(0xF80, 0x7FFF);
    core.SetParam(0x1080, 0x7FFF);
    Mixer mixer(core);
    std::vector<int16_t> pcm(1000 * 2, 4096);
    in.Push(pcm.data(), 1000);
    in.GateOpen();
    mixer.Pump();
    CHECK(mixer.OutputFrames() == Mixer::kFramesPerVsync);
    CHECK(core.FmvFramesMixed() == 800);   // one boundary = 800 frames of the movie
    CHECK(in.Queued() == 200);
    const float* out = mixer.Output();
    CHECK(Near(out[0], 4096.0f / 32768.0f * kGain));
    CHECK(Near(out[799 * 2 + 1], 4096.0f / 32768.0f * kGain));
    mixer.Pump();
    CHECK(core.FmvFramesMixed() == 1000);   // the partial 200, then silence
    out = mixer.Output();
    CHECK(Near(out[199 * 2], 4096.0f / 32768.0f * kGain));
    CHECK(out[200 * 2] == 0.0f);
    CHECK(out[799 * 2 + 1] == 0.0f);
    CHECK(in.Consumed() == 1000);
    // No input attached: the pump is the plain voice mix (no tally).
    SpuCore bare;
    bare.SdInit();
    Mixer bare_mixer(bare);
    bare_mixer.Pump();
    CHECK(bare.FmvFramesMixed() == 0);
    CHECK(bare.FmvInput() == nullptr);
}

int main() {
    TestFifoGateAndOrder();
    TestDropNewestAndWrap();
    TestFlush();
    TestGainRule();
    TestMixedAfterVoices();
    TestBvolCensusAndAudibleFrames();
    TestThroughMixerPump();
    std::printf("test_fmv_pcm_input: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures) {
        std::printf("test_fmv_pcm_input: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("test_fmv_pcm_input: PASS (FIFO, gate, drop-newest, flush, BVOL gain, after voices, BVOL census, pump)\n");
    return 0;
}

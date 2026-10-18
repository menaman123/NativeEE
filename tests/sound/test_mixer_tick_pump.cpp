#include <algorithm>
#include "tests/sound/mixer_voice_source.h"
// MixerTickPump / SpuCoreSelftest / SoundRing: the tick-clocked pump over the
// SPU2 core. Pins (1) the silence window: six 800-frame pumps of an idle core
// fill exactly one 100 ms AudioHasher window whose hash is the deterministic
// profile's silence value 0x0a6fb8af536edb25; (2) the pump cadence (800 frames
// per vsync, 4800 per window); (3) the core self-test tone (the libsd entry
// codes program voice 0 with a 4-block looped tone; the window becomes
// non-silent, NAX advances, ENDX latches, KOFF releases to silence);
// (4) the command ring's order and overflow accounting.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/core/verify/audio_hasher.h"
#include "engine/sound/command_ring.h"
#include "engine/sound/mixer.h"
#include "engine/sound/spu_core.h"
#include "engine/sound/fmv_pcm_input.h"
#include "engine/sound/voice_source.h"

using ps2r::sound::CommandRing;
using ps2r::sound::Mixer;
using ps2r::sound::SoundCommand;
using ps2r::sound::SoundOp;
using ps2r::sound::SpuCore;
using ps2r::verify::AudioHasher;

static int g_failures = 0;
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static constexpr uint64_t kSilenceWindowHash = 0x0a6fb8af536edb25ull;

static void KeyTestTone(SpuCore& core, uint32_t tone_addr) {
    // Four blocks = 112 samples: LOOP_START on the first, LOOP_END|REPEAT on the last.
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

static void TestSilenceWindowHash() {
    SpuCore core;
    core.SdInit();
    Mixer mixer(core);
    std::vector<uint64_t> windows;
    AudioHasher hasher([&](uint64_t, uint64_t h) { windows.push_back(h); });
    for (int i = 0; i < 6; i++) {
        mixer.Pump();
        CHECK(mixer.OutputFrames() == Mixer::kFramesPerVsync);
        hasher.Push(mixer.Output(), mixer.OutputFrames());
    }
    CHECK(Mixer::kFramesPerVsync == 800);
    CHECK(mixer.Pumps() == 6);
    CHECK(mixer.FramesMixed() == 4800);
    CHECK(windows.size() == 1);
    if (!windows.empty()) CHECK(windows[0] == kSilenceWindowHash);
    std::printf("silence window hash 0x%016llx (pumps=%llu frames=%llu)\n",
                windows.empty() ? 0ull : (unsigned long long)windows[0], (unsigned long long)mixer.Pumps(),
                (unsigned long long)mixer.FramesMixed());
    // The silence block landed at 0x5000 with LOOP_END|LOOP_REPEAT.
    CHECK(core.SpuRam()[SpuCore::kSilenceBlockAddr + 1] == 0x03);
}

static void TestSelftestToneIsAudible() {
    SpuCore core;
    core.SdInit();
    Mixer mixer(core);
    KeyTestTone(core, 0x6000);
    // The eager publish: NAX reads SSA and ENVX the first attack step before any pump.
    CHECK(core.GetAddr(0x2200) == 0x6000);
    CHECK(core.GetParam(0x500) == 0x3800);   // attack shift 0 step 0: the first rate step is 7 << 11
    CHECK(core.KonCount() == 1);
    std::vector<uint64_t> windows;
    AudioHasher hasher([&](uint64_t, uint64_t h) { windows.push_back(h); });
    for (int i = 0; i < 6; i++) {
        mixer.Pump();
        hasher.Push(mixer.Output(), mixer.OutputFrames());
    }
    CHECK(windows.size() == 1);
    if (!windows.empty()) CHECK(windows[0] != kSilenceWindowHash);
    CHECK(core.ActiveVoices() == 1);
    CHECK(core.LastRms() > 0.0f);
    CHECK((core.GetSwitch(0x1700) & 1u) != 0);   // 4800 frames > 112: ENDX latched
    const uint32_t nax = core.GetAddr(0x2200);
    CHECK(nax >= 0x6000 && nax < 0x6040);
    core.SetSwitch(0x1700, 0);                    // any write clears the latch
    CHECK(core.GetSwitch(0x1700) == 0);
    core.SetSwitch(0x1600, 0x1);                  // KOFF
    mixer.Pump();
    CHECK(core.ActiveVoices() == 0 || !core.VoiceAt(0).Keyed());
    CHECK(!core.VoiceAt(0).Keyed());
    CHECK(core.GetParam(0x500) == 0);
    // Core 1 voice 3 entry codes: core bit + voice<<1.
    CHECK(SpuCore::EntryVoiceIndex(0x2007) == 27);
    CHECK(SpuCore::EntryVoiceIndex(0x2031) == -1);   // voice 24 does not exist
}

static void TestVoiceTransMovesBytes() {
    SpuCore core;
    uint8_t src[64];
    for (int i = 0; i < 64; i++) src[i] = static_cast<uint8_t>(i * 3);
    CHECK(core.VoiceTrans(0, 0, src, 0x9000, 64) == 64);
    CHECK(std::memcmp(core.SpuRam() + 0x9000, src, 64) == 0);
    uint8_t back[64] = {};
    CHECK(core.VoiceTrans(0, 1, back, 0x9000, 64) == 64);
    CHECK(std::memcmp(back, src, 64) == 0);
    CHECK(core.VoiceTrans(0, 2, src, 0x9000, 64) == 0);   // STOP moves nothing
    CHECK(core.VoiceTrans(1, 0, src, ps2r::sound::kSpuRamSize - 16, 64) == 16);   // clamped at 2 MB
    CHECK(core.VoiceTransStatus(0, 0) == 1);
    core.SetTransIntrHandler(1, 0x1234, 0x99);
    uint32_t h = 0, a = 0;
    CHECK(core.GetTransIntrHandler(1, h, a) && h == 0x1234 && a == 0x99);
    CHECK(!core.GetTransIntrHandler(0, h, a));
}

static void TestRingOrderAndOverflow() {
    CommandRing ring;
    SoundCommand c{};
    CHECK(!ring.Pop(c));
    for (uint32_t i = 0; i < CommandRing::kCapacity; i++) ring.Push(SoundCommand{SoundOp::SetParam, i, i * 2});
    CHECK(ring.Size() == CommandRing::kCapacity);
    // Overflow drops the OLDEST (entry 0); the newest is kept at the tail.
    ring.Push(SoundCommand{SoundOp::Kon, 0, 1});
    CHECK(ring.Dropped() == 1);
    CHECK(ring.Size() == CommandRing::kCapacity);
    for (uint32_t i = 1; i < CommandRing::kCapacity; i++) {
        CHECK(ring.Pop(c));
        CHECK(c.op == SoundOp::SetParam && c.entry == i && c.value == i * 2);
    }
    CHECK(ring.Pop(c));
    CHECK(c.op == SoundOp::Kon && c.value == 1);
    CHECK(!ring.Pop(c));
    ring.Push(SoundCommand{SoundOp::SilenceAll, 0, 0});
    ring.Clear();
    CHECK(!ring.Pop(c));
    CHECK(ring.Size() == 0);
}

static void TestOwnedVoicePrecedesRetainedVoice() {
    MixerVoiceSource source;
    SpuCore core;
    core.SdInit();
    KeyTestTone(core, 0x6000);
    core.ApplyQueued();
    core.SetMvolL(0, 0.5f); core.SetMvolR(0, 0.75f);
    core.SetMvolL(1, 0.25f); core.SetMvolR(1, 0.5f);
    core.SetMasterVolume(2.0f);
    core.SetParam(0xf80, 0x7fff); core.SetParam(0x1080, 0x7fff);
    ps2r::sound::FmvPcmInput movie(16);
    const int16_t pcm[] = {16384, -16384};
    movie.Push(pcm, 1);
    movie.GateOpen();
    core.SetFmvInput(&movie);
    Mixer mixer(core);
    mixer.SetVoiceSource(&source);
    const auto cursor = core.VoiceAt(0).Nax();
    mixer.Pump(1);
    // The SPU2 chain: core 0 (voice 0 + the movie input at BVOL) clamped, MVOL0, into
    // core 1 at AVOL1 (full scale after SdInit), + voice 24, clamped, MVOL1, master. The
    // owned core-1 channel works even though that retained voice was never keyed.
    const float bvol = SpuCore::ExternalInputGain(0x7fff), avol = SpuCore::ExternalInputGain(0x7FFF);
    const float c0l = std::clamp(0.75f + 0.5f * bvol, -1.0f, 1.0f) * 0.5f * avol;
    const float c0r = std::clamp(-0.25f + -0.5f * bvol, -1.0f, 1.0f) * 0.75f * avol;
    const float exp_l = std::clamp(std::clamp(-0.5f + c0l, -1.0f, 1.0f) * 0.25f * 2.0f, -1.0f, 1.0f);
    const float exp_r = std::clamp(std::clamp(0.5f + c0r, -1.0f, 1.0f) * 0.5f * 2.0f, -1.0f, 1.0f);
    CHECK(mixer.Output()[0] == exp_l && mixer.Output()[1] == exp_r);
    CHECK(source.ticks == 1 && source.owned == 2 && movie.Consumed() == 1);
    CHECK(core.VoiceAt(0).Nax() == cursor);
    source.silent = true;
    mixer.Pump();
    for (int i = 0; i < Mixer::kFramesPerVsync * 2; ++i) CHECK(mixer.Output()[i] == 0);
    CHECK(core.VoiceAt(0).Nax() == cursor);
    CHECK(source.ticks == 2 && source.owned == 1602);
    mixer.Pump(0);
    CHECK(source.ticks == 2);
    source.handoff = true;
    // Cross two blocks without wrapping to the original loop block.
    mixer.Pump(64);
    CHECK(core.VoiceAt(0).Nax() != cursor);
    CHECK(core.LastRms() > 0);
    mixer.SetVoiceSource(nullptr);
}

int main() {
    TestSilenceWindowHash();
    TestSelftestToneIsAudible();
    TestVoiceTransMovesBytes();
    TestRingOrderAndOverflow();
    TestOwnedVoicePrecedesRetainedVoice();
    if (g_failures) {
        std::printf("test_mixer_tick_pump: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("test_mixer_tick_pump: all vectors passed\n");
    return 0;
}

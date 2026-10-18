// tests/sound/test_spu_register_rules.cpp -- the SPU2 register semantics the voice model keeps
// (vectors from the SPU2 register rules PCSX2's SPU2 encodes, the behavioural oracle):
//   * the envelope rate engine: shift/step -> counter increment 0x8000 >> max(0, shift-11) and level
//     increment step << max(0, 11-shift); exponential increase slows 4x above 0x6000; exponential
//     decrease scales by level/0x8000; attack reaches 0x7FFF, decay (exponential, -8) runs to
//     (SL+1)<<11, a sustain that decays to 0 stops the voice, release ends it;
//   * volume registers: fixed = the 15 bits << 1 as a signed 16-bit level (0x3FFF full, 0x7FFF = -2);
//     a sweep ramps on the same engine from the current level; VOLX = the signed level;
//   * MVOL is a volume register (0x3FFF = full scale), EVOL/AVOL/BVOL plain signed gains;
//   * routing: core 0 -> MVOL0 -> AVOL1 -> + core 1 -> MVOL1; AVOL1 full after SdInit;
//   * switches read back their last write (ENDX its latch); VMIXL/VMIXR gate the dry mix; the
//     trace records key-ons with the programmed registers.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/sound/spu_core.h"
#include "engine/sound/vag_decoder.h"
#include "engine/sound/voice.h"
#include "engine/sound/voice_trace.h"

using ps2r::sound::EnvPhase;
using ps2r::sound::SpuCore;

using ps2r::sound::Voice;
using ps2r::sound::VoiceTrace;

static int g_failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            g_failures++;                                                                 \
        }                                                                                 \
    } while (0)

// A constant-sample looping block set at `addr` in the given RAM (shift 0, filter 0, nibbles = 7).
static void PutTone(uint8_t* ram, uint32_t addr, int blocks) {
    for (int b = 0; b < blocks; b++) {
        uint8_t* t = ram + addr + b * VagDecoder::kBlockBytes;
        t[0] = 0x00;
        t[1] = (b == 0) ? VagDecoder::kFlagLoopStart : (b == blocks - 1) ? (VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat) : 0;
        std::memset(t + 2, 0x77, 14);
    }
}

static void TestRateEngine() {
    uint32_t counter = 0;
    // Linear increase, shift 0, step 7: 7 << 11 per sample (the counter wraps every sample).
    int32_t level = Voice::RateStep(false, false, 0, 7, 0, counter, 0, Voice::kEnvMax);
    CHECK(level == 7 << 11);
    // Shift 12: the counter increments by 0x4000 -> a step every second sample, of 7.
    counter = 0;
    level = 0;
    for (int i = 0; i < 4; i++) level = Voice::RateStep(false, false, 12, 7, level, counter, 0, Voice::kEnvMax);
    CHECK(level == 14);
    // Exponential decrease by 8 from full scale: the increment scales by level / 0x8000
    // ((-8 << 11) * 0x7FFF >> 15 = -16384, the arithmetic shift rounding toward -inf).
    counter = 0;
    level = Voice::RateStep(true, true, 0, -8, 0x7FFF, counter, 0, Voice::kEnvMax);
    CHECK(level == 0x3FFF);
    // Exponential increase above 0x6000 slows 4x: shift 11 -> counter 0x2000 per sample, a step every 4.
    counter = 0;
    level = 0x6001;
    int steps = 0;
    for (int i = 0; i < 4; i++) {
        const int32_t before = level;
        level = Voice::RateStep(true, false, 11, 7, level, counter, 0, Voice::kEnvMax);
        if (level != before) steps++;
    }
    CHECK(steps == 1);
    // The infinite rate (shift 31) still moves: counter increment clamps to 1.
    counter = 0;
    level = 0;
    for (int i = 0; i < 0x8000; i++) level = Voice::RateStep(false, false, 31, 4, level, counter, 0, Voice::kEnvMax);
    CHECK(level == 4);
}

static void TestEnvelopePhases() {
    std::vector<uint8_t> ram(ps2r::sound::kSpuRamSize, 0);
    PutTone(ram.data(), 0x8000, 4);
    Voice v;
    v.SetSsa(0x8000);
    v.SetPitch(0x1000);
    v.SetVolL(0x3FFF);
    v.SetVolR(0x3FFF);
    // Instant attack (shift 0), SL 15 (target 0x8000), decay shift 0: one exponential decay step
    // lands at 0x3FFF, then sustain (the oracle's rule: the step precedes the target test).
    v.SetAdsr1(0x000F);
    v.SetAdsr2(0x1FC0);   // sustain increase, shift 31, step 3: the slowest rise
    v.KeyOn(ram.data());
    float l, r;
    v.Step(ram.data(), l, r);
    CHECK(v.Phase() == EnvPhase::Attack && v.Envx() == 0x3800);
    v.Step(ram.data(), l, r);
    v.Step(ram.data(), l, r);
    CHECK(v.Phase() == EnvPhase::Decay && v.Envx() == 0x7FFF);
    v.Step(ram.data(), l, r);
    CHECK(v.Phase() == EnvPhase::Sustain && v.Envx() == 0x3FFF);
    // A lower sustain level: SL 7 -> target 0x4000 reached through the decay.
    Voice w;
    w.SetSsa(0x8000);
    w.SetPitch(0x1000);
    w.SetVolL(0x3FFF);
    w.SetVolR(0x3FFF);
    w.SetAdsr1(0x0057);   // decay shift 5, SL 7
    w.SetAdsr2(0x1FC0);
    w.KeyOn(ram.data());
    int n = 0;
    while (w.Phase() != EnvPhase::Sustain && n < 200000) {
        w.Step(ram.data(), l, r);
        n++;
    }
    CHECK(w.Phase() == EnvPhase::Sustain);
    CHECK(w.Envx() <= 0x4000 && w.Envx() > 0x2000);
    // Release: exponential (mode bit 5), shift 0 -> ends within a few hundred samples.
    w.SetAdsr2(0x0020);
    w.KeyOff();
    CHECK(w.Phase() == EnvPhase::Release);
    n = 0;
    while (w.Keyed() && n < 100000) {
        w.Step(ram.data(), l, r);
        n++;
    }
    CHECK(!w.Keyed() && n < 2000);
    // A decreasing sustain (ADSR2 bit 14) decays to silence and stops the voice.
    Voice s;
    s.SetSsa(0x8000);
    s.SetPitch(0x1000);
    s.SetVolL(0x3FFF);
    s.SetAdsr1(0x000F);
    s.SetAdsr2(0x4000);   // sustain decrease, linear, shift 0, step 0
    s.KeyOn(ram.data());
    n = 0;
    while (s.Keyed() && n < 100000) {
        s.Step(ram.data(), l, r);
        n++;
    }
    CHECK(!s.Keyed() && n < 100);
}

static void TestVolumeRegisters() {
    CHECK(Voice::FixedVolumeLevel(0x3FFF) == 0x7FFE);
    CHECK(Voice::FixedVolumeLevel(0x7FFF) == -2);
    CHECK(Voice::FixedVolumeLevel(0x4000) == -32768);
    CHECK(Voice::FixedVolumeLevel(0x0E00) == 0x1C00);
    Voice v;
    v.SetVolL(0x3FFF);
    v.SetVolR(0x7FFF);
    CHECK(v.VolxL() == 0x7FFE && v.VolxR() == 0xFFFE);
    // A sweep from 0: linear increase, shift 0, step 0 -> 7 << 11 per sample, saturating at 0x7FFF.
    std::vector<uint8_t> ram(ps2r::sound::kSpuRamSize, 0);
    PutTone(ram.data(), 0x8000, 4);
    v.SetSsa(0x8000);
    v.SetPitch(0x1000);
    v.SetAdsr1(0x000F);
    v.SetAdsr2(0x1FC0);
    v.SetVolL(0x0000);
    v.SetVolL(0x8000);
    v.KeyOn(ram.data());
    float l, r;
    v.Step(ram.data(), l, r);
    CHECK(v.VolxL() == 7 << 11);
    for (int i = 0; i < 10; i++) v.Step(ram.data(), l, r);
    CHECK(v.VolxL() == 0x7FFF);
    // A decreasing sweep (bit 13) from full scale heads to 0 on the engine, never below.
    v.SetVolL(0x3FFF);
    v.SetVolL(0xA000);
    for (int i = 0; i < 20; i++) v.Step(ram.data(), l, r);
    CHECK(v.VolxL() == 0);
    // Core gains.
    CHECK(std::fabs(SpuCore::MasterRegisterGain(0x3FFF) - 32766.0f / 32768.0f) < 1e-6f);
    CHECK(std::fabs(SpuCore::ExternalInputGain(0x7FFF) - 32767.0f / 32768.0f) < 1e-6f);
    CHECK(std::fabs(SpuCore::ExternalInputGain(0x3FFF) - 16383.0f / 32768.0f) < 1e-6f);
    CHECK(SpuCore::ExternalInputGain(0xFFFF) < 0.0f);
}

// Key voice `vi` of the core on the tone at `addr` with the given volumes.
static void Key(SpuCore& core, int core_idx, int v, uint32_t addr, uint32_t voll, uint32_t volr) {
    const uint32_t low = static_cast<uint32_t>((v << 1) | core_idx);
    core.SetAddr(0x2000 | low, addr);
    core.SetParam(0x000 | low, voll);
    core.SetParam(0x100 | low, volr);
    core.SetParam(0x200 | low, 0x1000);
    core.SetParam(0x300 | low, 0x000F);
    core.SetParam(0x400 | low, 0x1FC0);
    core.SetSwitch(0x1500 | static_cast<uint32_t>(core_idx), 1u << v);
}

static float Peak(const std::vector<float>& out, int ch) {
    float p = 0.0f;
    for (size_t i = static_cast<size_t>(ch); i < out.size(); i += 2) p = std::max(p, std::fabs(out[i]));
    return p;
}

static void TestRoutingAndGates() {
    SpuCore core;
    core.SdInit();
    PutTone(core.SpuRam(), 0x8000, 4);
    std::vector<float> out(512 * 2);
    // Core 0 voice at full volume, MVOL0 and MVOL1 full: audible at the output (AVOL1 full after SdInit).
    core.SetParam(0x980, 0x3FFF); core.SetParam(0xA80, 0x3FFF);
    core.SetParam(0x981, 0x3FFF); core.SetParam(0xA81, 0x3FFF);
    CHECK(core.CoreAttr(1, SpuCore::kAvoll) == 0x7FFF && core.CoreAttr(1, SpuCore::kAvolr) == 0x7FFF);
    Key(core, 0, 0, 0x8000, 0x3FFF, 0x3FFF);
    core.MixInto(out.data(), 512);   // the attack / decay transient
    core.MixInto(out.data(), 512);   // steady sustain
    const float full = Peak(out, 0);
    CHECK(full > 0.1f);
    // MVOL1 at 0x1FFF halves it (the register is the level << 1).
    core.SetParam(0x981, 0x1FFF); core.SetParam(0xA81, 0x1FFF);
    core.MixInto(out.data(), 512);
    CHECK(std::fabs(Peak(out, 0) - full * 0.5f) < full * 0.02f);
    core.SetParam(0x981, 0x3FFF); core.SetParam(0xA81, 0x3FFF);
    // AVOL1 at 0x3FFF halves core 0's contribution; a core 1 voice is unaffected by AVOL1.
    core.SetParam(0xD81, 0x3FFF); core.SetParam(0xE81, 0x3FFF);
    core.MixInto(out.data(), 512);
    CHECK(std::fabs(Peak(out, 0) - full * 0.5f) < full * 0.02f);
    Key(core, 1, 0, 0x8000, 0x3FFF, 0x0000);
    core.SetSwitch(0x1600, 1u);   // KOFF core 0 voice 0
    for (int i = 0; i < 8; i++) core.MixInto(out.data(), 512);
    CHECK(std::fabs(Peak(out, 0) - full) < full * 0.02f);
    CHECK(Peak(out, 1) == 0.0f);
    // A fixed VOLL of 0x7FFF is the phase-inverted near-zero, never full volume.
    core.SetParam(0x001, 0x7FFF);
    core.MixInto(out.data(), 512);
    CHECK(Peak(out, 0) < full * 0.001f);
    // Switch readback: VMIXL reads the last write; clearing core 1 voice 0's dry bit silences it.
    CHECK(core.GetSwitch(0x1801) == 0xFFFFFF);
    core.SetParam(0x001, 0x3FFF);
    core.SetSwitch(0x1801, 0xFFFFFE);
    CHECK(core.GetSwitch(0x1801) == 0xFFFFFE);
    core.MixInto(out.data(), 512);
    CHECK(Peak(out, 0) == 0.0f);
    core.SetSwitch(0x1801, 0xFFFFFF);
    core.MixInto(out.data(), 512);
    CHECK(Peak(out, 0) > 0.1f);
    // NON / PMON read back their writes too; ENDX reads its latch (cleared by any write).
    core.SetSwitch(0x1400, 0x5);
    CHECK(core.GetSwitch(0x1400) == 0x5);
    CHECK((core.GetSwitch(0x1700) & 1u) != 0);   // voice 0 looped its LOOP_END block
    core.SetSwitch(0x1700, 0);
    CHECK(core.GetSwitch(0x1700) == 0);
    // SdInit resets the gates to all-on and the switches to 0.
    core.SetSwitch(0x1800, 0);
    core.SdInit();
    CHECK(core.GetSwitch(0x1800) == 0xFFFFFF && core.GetSwitch(0x1400) == 0);
}

static void TestMasterSweep() {
    SpuCore core;
    core.SdInit();
    PutTone(core.SpuRam(), 0x8000, 4);
    core.SetParam(0x980, 0x0000); core.SetParam(0xA80, 0x3FFF);
    core.SetParam(0x981, 0x3FFF); core.SetParam(0xA81, 0x3FFF);
    Key(core, 0, 0, 0x8000, 0x3FFF, 0x3FFF);
    std::vector<float> out(256 * 2);
    core.MixInto(out.data(), 256);
    CHECK(Peak(out, 0) == 0.0f && Peak(out, 1) > 0.1f);
    // A sweep write on MVOLL (bit 15, linear increase, shift 0): the left level ramps up to full.
    core.SetParam(0x980, 0x8000);
    core.MixInto(out.data(), 256);
    CHECK(core.MvolL(0) > 0.99f);
    CHECK(std::fabs(Peak(out, 0) - Peak(out, 1)) < 0.02f);
}

static void TestTraceRecordsKeyOn() {
    SpuCore core;
    core.SdInit();
    PutTone(core.SpuRam(), 0x8000, 4);
    const char* path = "test_spu_register_rules_trace.txt";
    VoiceTrace trace;
    CHECK(trace.Open(path));
    core.SetTrace(&trace);
    Key(core, 0, 3, 0x8000, 0x1234, 0x0000);
    std::vector<float> out(128 * 2);
    core.MixInto(out.data(), 128);
    core.SetTrace(nullptr);
    trace.Close();
    std::FILE* f = std::fopen(path, "r");
    CHECK(f != nullptr);
    std::string text;
    if (f) {
        char buf[512];
        while (std::fgets(buf, sizeof buf, f)) text += buf;
        std::fclose(f);
        std::remove(path);
    }
    CHECK(text.find("KON v3 ssa=008000 lsax=008000 pitch=1000 adsr1=000f adsr2=1fc0 voll=1234 volr=0000") != std::string::npos);
    CHECK(text.find("SW c0 func=15 value=00000008") != std::string::npos);
    CHECK(text.find("silence_all") != std::string::npos);
}

int main() {
    TestRateEngine();
    TestEnvelopePhases();
    TestVolumeRegisters();
    TestRoutingAndGates();
    TestMasterSweep();
    TestTraceRecordsKeyOn();
    if (g_failures) {
        std::printf("test_spu_register_rules: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("test_spu_register_rules: all vectors passed\n");
    return 0;
}

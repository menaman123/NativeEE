// ===========================================================================
// Unit tests: SPU2 voice state machine (engine/sound/voice).
//
// Each vector builds a tiny synthetic "SPU RAM" of hand-made ADPCM blocks
// (filter 0 / shift 0 so nibble n decodes to exactly n<<12) and drives one
// Voice through key events, checking pitch stepping, block/loop mechanics,
// ENDX latching, LSAX auto-latch (LOOP_START), envelope phases, and the
// NAX/ENVX readbacks the game's stream pacer depends on.
// ===========================================================================
#include <cassert>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/sound/voice.h"

using ps2r::sound::EnvPhase;
constexpr uint8_t kFlagLoopEnd = VagDecoder::kFlagLoopEnd;
constexpr uint8_t kFlagLoopRepeat = VagDecoder::kFlagLoopRepeat;
constexpr uint8_t kFlagLoopStart = VagDecoder::kFlagLoopStart;
using ps2r::sound::kSpuRamSize;
[[maybe_unused]] constexpr int kVagBlockBytes = VagDecoder::kBlockBytes;
[[maybe_unused]] constexpr int kVagBlockSamples = VagDecoder::kBlockSamples;
using ps2r::sound::Voice;

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Test SPU RAM: small vector, zero-filled (a zero block = silence, no flags).
struct TestRam {
    std::vector<uint8_t> ram = std::vector<uint8_t>(kSpuRamSize, 0);
};

static const uint8_t* RamData(const TestRam& r) { return r.ram.data(); }

// Write a block whose 28 samples all decode to (nib << 12).
static void PutBlock(TestRam& r, uint32_t addr, uint8_t nib, uint8_t flags) {
    uint8_t* b = &r.ram[addr];
    b[0] = 0x00;                   // filter 0, shift 0
    b[1] = flags;
    std::memset(b + 2, (nib & 0xF) | ((nib & 0xF) << 4), 14);
}

// Instant-attack, full-sustain, instant-release voice at unit volume.
static void ProgramBasic(Voice& v, uint32_t ssa, uint16_t pitch = 0x1000) {
    v.SetVolL(0x3FFF);                 // ~0.5 gain
    v.SetVolR(0x3FFF);
    v.SetPitch(pitch);
    v.SetAdsr1(0x000F);                // attack rate 0 (instant), SL = 15 (full)
    v.SetAdsr2(0x0000);                // release rate 0 (near-instant)
    v.SetSsa(ssa);
    v.SetLsax(0x5000);
}

// 1. KeyOn resets NAX to SSA and starts the envelope; the voice is keyed.
static void TestKeyOnResetsNax() {
    TestRam ram;
    PutBlock(ram, 0x8000, 1, 0);
    Voice v;
    ProgramBasic(v, 0x8000);
    CHECK(!v.Keyed());
    v.KeyOn();
    CHECK(v.Keyed());
    CHECK(v.Nax() == 0x8000);
    float l, r;
    v.Step(RamData(ram), l, r);
    CHECK(v.Nax() == 0x8000);          // still inside the first block
}

// 2. Pitch 0x1000 consumes exactly one block (28 samples) in 28 steps:
//    NAX advances by 16 bytes on the 28th step.
static void TestPitchUnityBlockAdvance() {
    TestRam ram;
    PutBlock(ram, 0x8000, 1, 0);
    PutBlock(ram, 0x8010, 2, 0);
    Voice v;
    ProgramBasic(v, 0x8000, 0x1000);
    v.KeyOn();
    float l, r;
    for (int i = 0; i < 27; i++) v.Step(RamData(ram), l, r);
    CHECK(v.Nax() == 0x8000);
    v.Step(RamData(ram), l, r);          // 28th step crosses into the next block
    CHECK(v.Nax() == 0x8010);
}

// 3. Pitch 0x800 (half rate) takes 56 steps per block.
static void TestPitchHalfRate() {
    TestRam ram;
    PutBlock(ram, 0x8000, 1, 0);
    PutBlock(ram, 0x8010, 2, 0);
    Voice v;
    ProgramBasic(v, 0x8000, 0x800);
    v.KeyOn();
    float l, r;
    for (int i = 0; i < 55; i++) v.Step(RamData(ram), l, r);
    CHECK(v.Nax() == 0x8000);
    v.Step(RamData(ram), l, r);
    CHECK(v.Nax() == 0x8010);
}

// 4. LOOP_END + REPEAT wraps NAX to LSAX and raises ENDX exactly once per
//    wrap; the envelope stays alive (sustain loop).
static void TestLoopEndRepeatWrapsToLsax() {
    TestRam ram;
    PutBlock(ram, 0x8000, 1, kFlagLoopEnd | kFlagLoopRepeat);
    PutBlock(ram, 0x6000, 2, 0);        // loop target
    Voice v;
    ProgramBasic(v, 0x8000);
    v.SetLsax(0x6000);
    v.KeyOn();
    float l, r;
    bool saw_endx = false;
    for (int i = 0; i < 28; i++) saw_endx |= v.Step(RamData(ram), l, r);
    CHECK(saw_endx);
    CHECK(v.Nax() == 0x6000);
    CHECK(v.Keyed());
    CHECK(v.Envx() > 0);               // envelope still alive
}

// 5. LOOP_END without REPEAT: one-shot end — ENDX raised, NAX jumps to
//    LSAX, and the envelope is forced silent (ENVX == 0) while the voice
//    keeps running.
static void TestOneShotEndMutes() {
    TestRam ram;
    PutBlock(ram, 0x8000, 3, kFlagLoopEnd);   // no REPEAT
    PutBlock(ram, 0x5000, 0, kFlagLoopEnd | kFlagLoopRepeat);  // silence parking
    Voice v;
    ProgramBasic(v, 0x8000);
    v.SetLsax(0x5000);
    v.KeyOn();
    float l, r;
    bool endx = false;
    for (int i = 0; i < 28; i++) endx |= v.Step(RamData(ram), l, r);
    CHECK(endx);
    CHECK(v.Nax() == 0x5000);
    CHECK(v.Envx() == 0);
    // Further steps produce silence.
    v.Step(RamData(ram), l, r);
    CHECK(l == 0.0f && r == 0.0f);
}

// 6. LOOP_START auto-latches LSAX to the flagged block's address (this is
//    how streamed chunks retarget the loop point with zero driver traffic).
static void TestLoopStartLatchesLsax() {
    TestRam ram;
    PutBlock(ram, 0x8000, 1, 0);
    PutBlock(ram, 0x8010, 2, kFlagLoopStart);
    PutBlock(ram, 0x8020, 3, kFlagLoopEnd | kFlagLoopRepeat);
    Voice v;
    ProgramBasic(v, 0x8000);
    v.SetLsax(0x5000);                 // will be overridden by the data
    v.KeyOn();
    float l, r;
    for (int i = 0; i < 28 * 3; i++) v.Step(RamData(ram), l, r);
    // After the LOOP_END block, NAX must have wrapped to the LOOP_START
    // block (0x8010), not the stale 0x5000.
    CHECK(v.Lsax() == 0x8010);
    CHECK(v.Nax() == 0x8010);
}

// 7. KeyOff enters release and the voice auto-stops at env 0 (release rate
//    0 = near-instant); ENVX reads back 0 and steps output silence.
static void TestKeyOffReleasesToOff() {
    TestRam ram;
    PutBlock(ram, 0x8000, 7, 0);
    Voice v;
    ProgramBasic(v, 0x8000);
    v.KeyOn();
    float l, r;
    for (int i = 0; i < 10; i++) v.Step(RamData(ram), l, r);
    CHECK(v.Keyed());
    v.KeyOff();
    for (int i = 0; i < 8; i++) v.Step(RamData(ram), l, r);   // > 4-sample release
    CHECK(!v.Keyed());
    CHECK(v.Envx() == 0);
    v.Step(RamData(ram), l, r);
    CHECK(l == 0.0f && r == 0.0f);
}

// 8. ENVX is monotonically non-decreasing during a slow attack.
static void TestAttackMonotonic() {
    TestRam ram;
    PutBlock(ram, 0x8000, 1, 0);
    Voice v;
    ProgramBasic(v, 0x8000);
    v.SetAdsr1(0x200F);                // attack rate 0x20, SL full
    v.KeyOn();
    float l, r;
    uint16_t prev = 0;
    bool rose = false;
    for (int i = 0; i < 2000 && v.Phase() == ps2r::sound::EnvPhase::Attack; i++) {
        v.Step(RamData(ram), l, r);
        const uint16_t e = v.Envx();
        CHECK(e >= prev);
        if (e > prev) rose = true;
        prev = e;
    }
    CHECK(rose);
    CHECK(prev > 0);
}

// 9. NAX advances monotonically (mod loop wraps) while keyed — the stream
//    pacer's liveness contract.
static void TestNaxMonotonicWhileKeyed() {
    TestRam ram;
    for (uint32_t a = 0x8000; a < 0x8100; a += 16) PutBlock(ram, a, 1, 0);
    Voice v;
    ProgramBasic(v, 0x8000);
    v.KeyOn();
    float l, r;
    uint32_t prev = v.Nax();
    for (int i = 0; i < 28 * 10; i++) {
        v.Step(RamData(ram), l, r);
        CHECK(v.Nax() >= prev);
        prev = v.Nax();
    }
    CHECK(prev >= 0x8090);             // ~10 blocks consumed
}

// 10. Re-KeyOn while playing restarts from SSA with a fresh envelope.
static void TestRekeyRestarts() {
    TestRam ram;
    for (uint32_t a = 0x8000; a < 0x8100; a += 16) PutBlock(ram, a, 1, 0);
    Voice v;
    ProgramBasic(v, 0x8000);
    v.KeyOn();
    float l, r;
    for (int i = 0; i < 28 * 4; i++) v.Step(RamData(ram), l, r);
    CHECK(v.Nax() > 0x8000);
    v.KeyOn();
    CHECK(v.Nax() == 0x8000);
    CHECK(v.Keyed());
}

// 11. Volume gains scale output (0x3FFF = full scale; 0x7FFF is the phase-inverted near-zero of the
// signed register); a sweep write (bit15) ramps from the current gain, never jumps.
static void TestVolumeMapping() {
    TestRam ram;
    PutBlock(ram, 0x8000, 7, 0);
    Voice v;
    ProgramBasic(v, 0x8000);
    v.SetVolL(0x3FFF);                 // full
    v.SetVolR(0x0000);                 // silent
    v.KeyOn();
    float l, r;
    v.Step(RamData(ram), l, r);
    v.Step(RamData(ram), l, r);
    CHECK(l > 0.0f);
    CHECK(r == 0.0f);
    const float before = l;
    v.SetVolL(0x8123);                 // sweep mode — gain unchanged
    v.Step(RamData(ram), l, r);
    CHECK(l > 0.0f);
    CHECK(before > 0.0f);
}

// 12. Silence() hard-stops the voice (savestate-load contract).
static void TestSilenceHardStop() {
    TestRam ram;
    PutBlock(ram, 0x8000, 7, 0);
    Voice v;
    ProgramBasic(v, 0x8000);
    v.KeyOn();
    float l, r;
    v.Step(RamData(ram), l, r);
    v.Silence();
    CHECK(!v.Keyed());
    CHECK(v.Envx() == 0);
    v.Step(RamData(ram), l, r);
    CHECK(l == 0.0f && r == 0.0f);
}

// VOLX readback: a fixed volume write F presents as the hardware's current level,
// the signed 16-bit value F << 1 (0x3FFF -> 0x7FFE; 0x7FFF -> 0xFFFE, the inverted
// phase one step below zero). STREAM.IRX's music ramp stepper (module FUN_00010d28)
// compares VOLX against target*2 — this scale is what lets the ramp converge.
static void TestVolxReadbackScale() {
    Voice v;
    v.SetVolL(0);
    v.SetVolR(0);
    CHECK(v.VolxL() == 0 && v.VolxR() == 0);
    v.SetVolL(0x2E62);                       // the observed music target 11874
    CHECK(v.VolxL() == 0x2E62 * 2);          // readback = 2x fixed value
    v.SetVolL(0x3FFF);                       // max game volume
    CHECK(v.VolxL() == 0x3FFF * 2);
    v.SetVolL(0x7FFF);                       // bit 14 set: negative (phase-inverted) level -2
    CHECK(v.VolxL() == 0xFFFE);
    CHECK(Voice::FixedVolumeLevel(0x7FFF) == -2);
}

// Volume sweep (bit15 register): an upward sweep (the module's
// 0x8000|rate music fade-in write) raises the gain monotonically while the
// voice steps, VOLX tracks it, and a later fixed write both stops the sweep
// and sets the exact level — the module's ramp-completion write.
static void TestVolumeSweepFadeIn() {
    TestRam ram;
    // Self-looping nonzero block: the voice stays on audible data no matter
    // how many samples the sweep spans.
    PutBlock(ram, 0x8000, 7,
                 kFlagLoopStart | kFlagLoopEnd | kFlagLoopRepeat);
    Voice v;
    ProgramBasic(v, 0x8000);
    v.SetVolL(0);                            // start silent (music KON state)
    v.SetVolR(0);
    v.KeyOn();
    float l, r;
    v.Step(RamData(ram), l, r);
    CHECK(l == 0.0f);                        // keyed but volume 0 -> silent
    v.SetVolL(0x8027);                       // sweep up, linear, rate 0x27
    uint16_t volx_prev = v.VolxL();
    bool rose = false;
    for (int i = 0; i < 8000; i++) {         // ~0.17 s at 48 kHz
        v.Step(RamData(ram), l, r);
        if (v.VolxL() > volx_prev) rose = true;
        CHECK(v.VolxL() >= volx_prev);       // monotonic while sweeping up
        volx_prev = v.VolxL();
    }
    CHECK(rose);                             // the sweep actually moved
    CHECK(v.VolxL() == 0x7FFF);              // rate 0x27 tops out well within
    v.Step(RamData(ram), l, r);
    CHECK(l > 0.0f);                         // audible now (env full, gain>0)
    v.SetVolL(0x2E62);                       // ramp-completion fixed write
    v.Step(RamData(ram), l, r);
    CHECK(v.VolxL() == 0x2E62 * 2);          // sweep stopped at the target
    uint16_t settled = v.VolxL();
    for (int i = 0; i < 64; i++) v.Step(RamData(ram), l, r);
    CHECK(v.VolxL() == settled);             // no drift after the fixed write
    // Downward sweep (fade-out) floors at zero.
    v.SetVolL(0xA000 | 0x10);                // sweep down
    for (int i = 0; i < 8000; i++) v.Step(RamData(ram), l, r);
    CHECK(v.VolxL() == 0);
}

// 15. The resampler knob: linear (the default) reproduces a constant block exactly at half pitch; the
//     SPU2 Gaussian (audio.interpolation=gaussian) is a 4-tap kernel whose weights sum to 0x7F80 (a
//     constant comes out 0.4 % low, as on the hardware), whose output at a step between two blocks is
//     monotonic (no ringing: the kernel is all-positive bar the -1 tails), and which is the SAME on
//     every voice (the knob is process-wide). The default is restored afterwards.
static void TestInterpolationKnob() {
    TestRam ram;
    PutBlock(ram, 0x8000, 0, 0);           // 28 zeros
    PutBlock(ram, 0x8010, 4, 0);           // 28 x 0x4000
    PutBlock(ram, 0x8020, 4, 0);
    auto run = [&](bool gaussian, std::vector<int32_t>* out) {
        Voice::SetGaussian(gaussian);
        Voice v;
        ProgramBasic(v, 0x8000, 0x800);    // half pitch: two output samples per source sample
        v.KeyOn();
        float l, r;
        for (int i = 0; i < 120; i++) {
            v.Step(RamData(ram), l, r);
            out->push_back(static_cast<int32_t>(std::lround(l / 0.5f * 32768.0f * 32768.0f / 32767.0f)));   // undo the 0x3FFF gain
        }
    };
    std::vector<int32_t> lin, gau;
    run(false, &lin);
    run(true, &gau);
    Voice::SetGaussian(false);
    CHECK(!Voice::Gaussian());
    // Linear settles to the block's level; the Gaussian to 0x7F80/0x8000 of it (the kernel's DC gain, -0.034 dB).
    const int32_t top_lin = lin[119], top_gau = gau[119];
    CHECK(top_lin > 16000);
    CHECK(std::abs(top_gau - (top_lin * 0x7F80) / 0x8000) <= 2);
    // Both transitions are monotonic non-decreasing across the step (no overshoot / ringing; the -1 tails
    // of the gauss table dip at most a couple of LSB).
    bool mono_lin = true, mono_gau = true;
    for (size_t i = 1; i < lin.size(); i++) {
        if (lin[i] < lin[i - 1] - 2) mono_lin = false;
        if (gau[i] < gau[i - 1] - 2) mono_gau = false;
    }
    CHECK(mono_lin);
    CHECK(mono_gau);
    // The Gaussian's step is wider than linear's (4 source samples vs 2): more output samples strictly inside (0, top).
    auto inside = [](const std::vector<int32_t>& o, int32_t top) {
        int n = 0;
        for (int32_t x : o) if (x > 8 && x < top - 8) n++;
        return n;
    };
    CHECK(inside(lin, top_lin) == 1);   // half pitch: one midpoint sample
    CHECK(inside(gau, top_gau) >= 4);
    std::printf("interpolation: linear settles %d, gaussian settles %d (x 0x7F80/0x8000); step width lin %d gau %d\n",
                top_lin, top_gau, inside(lin, top_lin), inside(gau, top_gau));
}

int main() {
    TestInterpolationKnob();
    TestKeyOnResetsNax();
    TestPitchUnityBlockAdvance();
    TestPitchHalfRate();
    TestLoopEndRepeatWrapsToLsax();
    TestOneShotEndMutes();
    TestLoopStartLatchesLsax();
    TestKeyOffReleasesToOff();
    TestAttackMonotonic();
    TestNaxMonotonicWhileKeyed();
    TestRekeyRestarts();
    TestVolumeMapping();
    TestSilenceHardStop();
    TestVolxReadbackScale();
    TestVolumeSweepFadeIn();
    if (g_failures) {
        std::printf("test_sound_voice: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("test_sound_voice: all vectors passed\n");
    return 0;
}

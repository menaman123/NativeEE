// SoundState: the SNDC/SDRX savestate codec round trip -- the reference's
// tests/test_sound_state.cpp core vectors ported onto SoundStateCodec: a
// driven core (transfer, three programmed voices, MVOL/BVOL, KON, a handler,
// a pump, then a pending KOFF and a pending pitch) serialises to the exact
// payload size, restores byte-identically, re-serialises identically, a
// perturbation changes the payload, restoring the original brings it back;
// the validate guards on truncated / extended / corrupted payloads; the SDRX
// channel state through the same codec; and both sections through the
// SectionRegistry (registration order SDRX, SNDC; file version 4).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/sound/hle/sd_block_trans.h"
#include "engine/sound/mixer.h"
#include "engine/sound/sound_state_codec.h"
#include "engine/sound/spu_core.h"

using ps2r::sound::Mixer;
using ps2r::sound::SoundStateCodec;
using ps2r::sound::SpuCore;
using ps2r::sound::VoiceState;
using ps2r::sound::hle::SdBlockTrans;

static int g_failures = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

namespace {

struct Rig {
    SpuCore core;
    Mixer mixer{core};
    SdBlockTrans block{8, [this] { return mixer.FramesMixed(); }};
    SoundStateCodec codec{core, block};
};

void DriveCore(SpuCore& core, Mixer& mixer) {
    std::vector<uint8_t> blob(0x400);
    for (size_t i = 0; i < blob.size(); i++) blob[i] = static_cast<uint8_t>(i * 7 + 3);
    blob[0x3F0] = 0x00;
    blob[0x3F1] = VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat;
    CHECK(core.VoiceTrans(0, 0, blob.data(), 0x9000, static_cast<uint32_t>(blob.size())) == blob.size());
    for (int v = 0; v < 3; v++) {
        const uint32_t lo = static_cast<uint32_t>(v << 1);   // core 0, voice v
        core.SetParam(0x000 | lo, 0x2000 + v);   // VOLL
        core.SetParam(0x100 | lo, 0x2100 + v);   // VOLR
        core.SetParam(0x200 | lo, 0x0F00 + v);   // PITCH
        core.SetParam(0x300 | lo, 0x200F);       // ADSR1
        core.SetParam(0x400 | lo, 0x1FC0);       // ADSR2
        core.SetAddr(0x2000 | lo, 0x9000);       // SSA
        core.SetAddr(0x2100 | lo, 0x9000);       // LSAX
    }
    core.SetParam(0x980, 0x3FFF);    // MVOLL core 0
    core.SetParam(0xA80, 0x3FFF);    // MVOLR core 0
    core.SetParam(0xF80, 0x7FFF);    // BVOLL core 0
    core.SetParam(0x1080, 0x7FFF);   // BVOLR core 0
    core.SetSwitch(0x1500, 0x7);     // KON voices 0..2
    core.SetTransIntrHandler(1, 0x13A8, 0x12F20);
    mixer.Pump(480);
    core.SetSwitch(0x1600, 0x2);     // KOFF voice 1 (pending in the ring)
    core.SetParam(0x204, 0x0ABC);    // pitch voice 2 (pending in the ring)
}

void TestCoreRoundTrip() {
    Rig rig;
    rig.core.SdInit();
    DriveCore(rig.core, rig.mixer);
    std::vector<uint8_t> a;
    rig.codec.SerializeSndc(a);
    const size_t expect = 4 + ps2r::sound::kSpuRamSize + 4 + 48 * sizeof(VoiceState) + 2 * 4 + 48 * 5 * 4 +
                          48 * 2 * 4 + 4 * 4 + 2 * 0x20 * 4 + 4 * 4;
    CHECK(a.size() == expect);
    CHECK(SoundStateCodec::SndcBytes() == expect);
    // The capture applied the pending ring: nothing is queued afterwards.
    CHECK(rig.core.Ring().Size() == 0);
    std::string err;
    CHECK(SoundStateCodec::ValidateSndc(a, &err));
    // Restore over a LIVE eager hold: a KON still in the ring publishes its
    // eager NAX/ENVX for voice 5 (hold up) right before the restore; the
    // restore must clear the hold and publish the file's values, not the
    // eager ones (OLD zeroes the holds before republishing).
    rig.core.SetAddr(0x2000 | (5u << 1), 0xA000);   // SSA voice 5 -> the eager NAX
    rig.core.SetSwitch(0x1500, 0x20);               // KON voice 5 (unapplied, in the ring)
    CHECK(rig.core.EagerHold(5) == 1);
    CHECK(rig.core.GetAddr(0x2200 | (5u << 1)) == 0xA000);
    CHECK(rig.core.GetParam(0x0500 | (5u << 1)) != 0);
    CHECK(rig.codec.RestoreSndc(a, &err));
    CHECK(rig.core.EagerHold(5) == 0);
    CHECK(rig.core.GetAddr(0x2200 | (5u << 1)) == rig.core.VoiceAt(5).Nax());
    CHECK(rig.core.GetParam(0x0500 | (5u << 1)) == rig.core.VoiceAt(5).Envx());
    CHECK(rig.core.GetAddr(0x2200 | (5u << 1)) != 0xA000);
    std::vector<uint8_t> b;
    rig.codec.SerializeSndc(b);
    CHECK(a == b);
    // The restored core answers the polls from the file's voices.
    CHECK(rig.core.GetAddr(0x2200) == rig.core.VoiceAt(0).Nax());
    CHECK(rig.core.GetParam(0x0500) == rig.core.VoiceAt(0).Envx());
    for (int vi = 0; vi < SpuCore::kVoices; vi++) CHECK(rig.core.EagerHold(vi) == 0);
    // A perturbation is visible; restoring the original brings it back.
    rig.mixer.Pump(1024);
    rig.core.SetSwitch(0x1500, 0x8);   // KON voice 3
    std::vector<uint8_t> c;
    rig.codec.SerializeSndc(c);
    CHECK(c != a);
    CHECK(rig.codec.RestoreSndc(a, &err));
    std::vector<uint8_t> d;
    rig.codec.SerializeSndc(d);
    CHECK(d == a);
    // The handler registry rode the section.
    uint32_t h = 0, arg = 0;
    CHECK(rig.core.GetTransIntrHandler(1, h, arg) && h == 0x13A8 && arg == 0x12F20);
    // A restore into a second, fresh core reproduces the mix exactly.
    Rig other;
    CHECK(other.codec.RestoreSndc(a, &err));
    std::vector<float> m1(2 * 800), m2(2 * 800);
    rig.mixer.Pump();
    other.mixer.Pump();
    CHECK(std::memcmp(rig.mixer.Output(), other.mixer.Output(), sizeof(float) * 2 * 800) == 0);
    std::printf("core round-trip: ok (payload %zu bytes)\n", a.size());
}

void TestValidateGuards() {
    Rig rig;
    rig.core.SdInit();
    DriveCore(rig.core, rig.mixer);
    std::vector<uint8_t> good;
    rig.codec.SerializeSndc(good);
    std::string err;
    CHECK(SoundStateCodec::ValidateSndc(good, &err));
    const size_t cuts[] = {0, 3, 4, 100, ps2r::sound::kSpuRamSize + 4, ps2r::sound::kSpuRamSize + 8 + 10,
                           good.size() - 1};
    for (size_t cut : cuts) {
        std::vector<uint8_t> t(good.begin(), good.begin() + static_cast<long>(cut));
        CHECK(!SoundStateCodec::ValidateSndc(t, &err));
        CHECK(!rig.codec.RestoreSndc(t, &err));
    }
    {
        std::vector<uint8_t> t = good;
        t.push_back(0xAA);
        CHECK(!SoundStateCodec::ValidateSndc(t, &err));
    }
    {
        std::vector<uint8_t> t = good;
        t[0] ^= 0xFF;   // spu_size
        CHECK(!SoundStateCodec::ValidateSndc(t, &err));
        CHECK(err.find("SPU RAM") != std::string::npos);
    }
    {
        std::vector<uint8_t> t = good;
        t[4 + ps2r::sound::kSpuRamSize] ^= 0xFF;   // voice_count
        CHECK(!SoundStateCodec::ValidateSndc(t, &err));
        CHECK(err.find("voice count") != std::string::npos);
    }
    // A rejected restore leaves a good restore intact afterwards.
    CHECK(rig.codec.RestoreSndc(good, &err));
    std::vector<uint8_t> again;
    rig.codec.SerializeSndc(again);
    CHECK(again == good);
    std::printf("validate guards: ok\n");
}

void TestSdrxRoundTrip() {
    Rig rig;
    rig.block.Arm(0, 0x13, 0x6000);
    rig.mixer.Pump(100);
    std::vector<uint8_t> a;
    rig.codec.SerializeSdrx(a);
    CHECK(a.size() == SoundStateCodec::SdrxBytes());
    CHECK(a[0] == 1);   // channel 0 active
    std::string err;
    Rig other;
    other.mixer.Pump(5000);   // a different clock: Import re-bases
    CHECK(other.codec.RestoreSdrx(a, &err));
    bool active = false;
    uint32_t buf = 0;
    uint64_t elapsed = 0;
    other.block.Export(0, active, buf, elapsed);
    CHECK(active && buf == 0 && elapsed == 100);
    CHECK(other.block.Status(0) == rig.block.Status(0));
    std::vector<uint8_t> t(a.begin(), a.end() - 1);
    CHECK(!SoundStateCodec::ValidateSdrx(t, &err));
    std::printf("sdrx round-trip: ok\n");
}

void TestRegistryPath() {
    Rig rig;
    rig.core.SdInit();
    DriveCore(rig.core, rig.mixer);
    ps2r::savestate::SectionRegistry reg;
    rig.codec.Register(reg);
    CHECK(reg.Count() == 2);
    CHECK(reg.Specs()[0].tag == ps2r::savestate::TAG_SDRX);
    CHECK(reg.Specs()[1].tag == ps2r::savestate::TAG_SNDC);
    CHECK(!reg.Specs()[0].required && !reg.Specs()[1].required);
    const auto sections = reg.Capture();
    CHECK(sections.size() == 2);
    CHECK(ps2r::savestate::SectionRegistry::FileVersionFor(sections) == 4);
    std::string err;
    Rig other;
    ps2r::savestate::SectionRegistry reg2;
    other.codec.Register(reg2);
    CHECK(reg2.Restore(sections, &err));
    std::vector<uint8_t> a, b;
    rig.codec.SerializeSndc(a);
    other.codec.SerializeSndc(b);
    CHECK(a == b);
    // A file without the sound sections restores (both optional).
    Rig third;
    ps2r::savestate::SectionRegistry reg3;
    third.codec.Register(reg3);
    CHECK(reg3.Restore({}, &err));
    std::printf("registry path: ok\n");
}

}  // namespace

int main() {
    TestCoreRoundTrip();
    TestValidateGuards();
    TestSdrxRoundTrip();
    TestRegistryPath();
    if (g_failures) {
        std::printf("test_sound_state: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("test_sound_state: all vectors passed\n");
    return 0;
}

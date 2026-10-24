#pragma once
// Voice: one of the SPU2's 48 hardware voices (2 cores x 24) at the level the
// libsd API exposes -- key-on/off, pitch-stepped ADPCM playback out of the
// caller's 2 MB SPU RAM (byte addressed, as libsd presents SSA/LSAX/NAX), the
// block loop flags, the ADSR envelope, the two volume registers, and the
// readbacks the IOP code polls (NAX current address, ENVX envelope level, VOLX
// current volume).
//
// Pitch: 0x1000 == 1.0 (48 kHz), clamped to 0x3FFF as the hardware does. Each
// output sample advances the source by pitch/0x1000 input samples; blocks are
// 28 samples / 16 bytes; linear interpolation between adjacent source samples, or the SPU2's 4-tap Gaussian
// over the last four decoded samples (Voice::SetGaussian(true), Config audio.interpolation=gaussian).
//
// Envelope (the SPU2's 15-bit level 0..0x7FFF, stepped by the shared rate
// engine every sample: a shift / step pair gives a counter increment of
// 0x8000 >> max(0, shift - 11) and a level increment of step << max(0, 11 -
// shift); the level moves when the 15-bit counter overflows; exponential
// increase slows 4x above 0x6000, exponential decrease scales the increment by
// level / 0x8000):
//   ADSR1  bit 15 attack mode (0 linear / 1 exponential), bits 14-10 attack
//          shift, bits 9-8 attack step (increment 7 - step), bits 7-4 decay
//          shift (exponential decrease by 8 to the sustain level), bits 3-0
//          sustain level ((SL + 1) << 11);
//   ADSR2  bit 15 sustain mode, bit 14 sustain direction (0 increase /
//          1 decrease), bits 12-8 sustain shift, bits 7-6 sustain step, bit 5
//          release mode, bits 4-0 release shift (decrease by 8 to 0).
// A voice in sustain whose level reaches 0 stops; a LOOP_END block without
// LOOP_REPEAT stops the voice (level 0, NAX at LSAX); a LOOP_START block latches
// LSAX unless LSAX was programmed explicitly since the key-on (loop mode).
//
// Volume registers (VOLL / VOLR): bit 15 clear = a FIXED value, the 15 bits
// read as a SIGNED 16-bit level after a left shift by one (0x3FFF = full scale
// +0x7FFE, 0x4000..0x7FFF = negative = phase inverted, the SPU2's rule -- so a
// driver's slightly negative pan gain is near silence, never full volume);
// bit 15 set = a SWEEP: bit 14 exponential, bit 13 decrease, bit 12 negative
// phase, bits 6-2 shift, bits 1-0 step, ramped by the same rate engine. VOLX is
// the current signed 16-bit level, the scale the SPU2 reports.
//
// Pure: no globals, no I/O. VoiceState is the packed, field-exact savestate
// mirror (SNDC section) -- 119 bytes, no padding; its layout is frozen, the
// register / counter fields replaced the earlier derived-step floats in place
// (a restore re-derives every register from the core's parameter shadows).
#include <cstdint>

#include "engine/sound/vag_decoder.h"

namespace ps2r::sound {

constexpr uint32_t kSpuRamSize = 2u * 1024u * 1024u;

enum class EnvPhase : uint8_t { Off, Attack, Decay, Sustain, Release };

#pragma pack(push, 1)
struct VoiceState {
    float vol_l, vol_r;          // the current volume levels, signed 16-bit / 32768
    uint16_t pitch;
    uint32_t ssa, lsax, nax;
    uint8_t phase;
    float env;                   // the envelope level / 32767
    uint16_t adsr1, adsr2;       // the programmed envelope registers
    uint16_t vol_reg_l, vol_reg_r;   // the programmed volume registers
    uint32_t env_counter;        // the envelope's rate counter
    uint8_t loop_mode;           // LSAX programmed since the key-on
    uint8_t reserved[3];
    int32_t hist_h1, hist_h2;
    int16_t block[VagDecoder::kBlockSamples];
    uint8_t block_flags;
    uint8_t block_valid;
    int32_t sample_idx;
    uint32_t frac;
    int16_t prev_sample;
};
#pragma pack(pop)
static_assert(sizeof(VoiceState) == 119, "VoiceState layout (SNDC codec)");

class Voice {
public:
    static constexpr int32_t kEnvMax = 0x7FFF;

    void SetVolL(uint16_t v) { SetVol(0, v); }
    void SetVolR(uint16_t v) { SetVol(1, v); }
    void SetPitch(uint16_t p) { pitch_ = p; }
    void SetAdsr1(uint16_t v) { adsr1_ = v; }
    void SetAdsr2(uint16_t v) { adsr2_ = v; }
    void SetSsa(uint32_t byte_addr) { ssa_ = byte_addr & ~0xFu; }
    // An explicit LSAX: the voice stops latching LOOP_START blocks until the next key-on.
    void SetLsax(uint32_t byte_addr);

    // Restart from SSA, envelope -> attack; with spu_ram the first block is
    // fetched eagerly (hardware latches a LOOP_START block into LSAX at key-on).
    void KeyOn(const uint8_t* spu_ram = nullptr);
    void KeyOff();

    uint32_t Nax() const { return nax_; }
    uint16_t Envx() const { return static_cast<uint16_t>(env_); }
    uint16_t VolxL() const { return static_cast<uint16_t>(static_cast<int16_t>(vol_[0])); }
    uint16_t VolxR() const { return static_cast<uint16_t>(static_cast<int16_t>(vol_[1])); }
    bool Keyed() const { return phase_ != EnvPhase::Off; }
    bool Audible() const { return Keyed() && env_ > 3; }
    EnvPhase Phase() const { return phase_; }
    uint32_t Lsax() const { return lsax_; }
    // The envelope level as 0..1 (the trace / census scale).
    float Env() const { return static_cast<float>(env_) / 32767.0f; }
    uint16_t Adsr1() const { return adsr1_; }
    uint16_t Adsr2() const { return adsr2_; }
    uint16_t Pitch() const { return pitch_; }
    uint8_t BlockFlags() const { return block_flags_; }
    // The LOOP_REPEAT bit of the last LOOP_END block Step consumed (for the owner's trace).
    bool LastEndRepeat() const { return last_end_repeat_; }

    // One output sample pair (pre master volume). Returns true when this step
    // consumed a LOOP_END block (the core latches ENDX). Off voices are silent
    // and do not advance.
    bool Step(const uint8_t* spu_ram, float& out_l, float& out_r);

    // Hard stop: phase Off, envelope 0.
    void Silence();

    // Savestate codec: the complete voice state. Import clamps an out-of-range
    // phase byte to Off and an out-of-range sample index to 0.
    void Export(VoiceState& out) const;
    void Import(const VoiceState& in);

    // The SPU2 rate engine shared by the envelope and the volume sweeps: one
    // sample's advance of `level` (0..0x7FFF for the envelope) under the mode.
    // Returns the new level; `counter` is the 15-bit rate counter.
    static int32_t RateStep(bool exponential, bool decrease, int shift, int step, int32_t level, uint32_t& counter,
                            int32_t floor, int32_t ceiling);
    // The signed level a FIXED volume register programs (reg << 1 as int16).
    static int32_t FixedVolumeLevel(uint16_t reg) { return static_cast<int16_t>(static_cast<uint16_t>(reg << 1)); }

private:
    void SetVol(int ch, uint16_t v);
    void LoadBlock(const uint8_t* spu_ram);
    void AdvanceEnvelope();
    void AdvanceSweep(int ch);
    void Stop();

    uint16_t pitch_ = 0;
    uint16_t adsr1_ = 0, adsr2_ = 0;
    uint16_t vol_reg_[2] = {0, 0};
    int32_t vol_[2] = {0, 0};            // current signed 16-bit levels
    uint32_t vol_counter_[2] = {0, 0};   // sweep rate counters
    uint32_t ssa_ = 0, lsax_ = 0, nax_ = 0;
    bool loop_mode_ = false;

    EnvPhase phase_ = EnvPhase::Off;
    int32_t env_ = 0;                    // 0..0x7FFF
    uint32_t env_counter_ = 0;

    VagDecoder::History hist_{};
    int16_t block_[VagDecoder::kBlockSamples] = {};
    uint8_t block_flags_ = 0;
    bool block_valid_ = false;
    int sample_idx_ = 0;
    uint32_t frac_ = 0;
    int16_t prev_sample_ = 0;
    int16_t older_[2] = {0, 0};      // the two samples before prev_sample_ (Gaussian taps; seeded from prev_sample_ at a restore)
    bool last_end_repeat_ = false;   // transient (trace only)

public:
    // The resampler every voice uses (Config audio.interpolation; set once at composition, before any Step):
    // 2-tap linear (the default) or the SPU2's 4-tap Gaussian.
    static void SetGaussian(bool on) { gaussian_ = on; }
    static bool Gaussian() { return gaussian_; }

private:
    static bool gaussian_;
};

}  // namespace ps2r::sound

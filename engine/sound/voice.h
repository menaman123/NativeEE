#pragma once
// Voice: one of the SPU2's 48 hardware voices (2 cores x 24) at the level the
// libsd API exposes -- key-on/off, pitch-stepped ADPCM playback out of the
// caller's 2 MB SPU RAM (byte addressed, as libsd presents SSA/LSAX/NAX), the
// block loop flags, an ADSR envelope, and the readbacks the IOP code polls
// (NAX current address, ENVX envelope level, VOLX current volume).
//
// Pitch: 0x1000 == 1.0 (48 kHz). Each output sample advances the source by
// pitch/0x1000 input samples; blocks are 28 samples / 16 bytes; linear
// interpolation between adjacent source samples.
//
// ADSR (ps2tek SPU2 register layout; linear ramps):
//   ADSR1 bits 14-8 attack rate, bits 7-4 decay rate, bits 3-0 sustain level
//   (SL/15); ADSR2 bits 5-0 release rate (sustain rate holds -- the title
//   programs 0x1FC0, sustain "off"). Attack reaches 1.0 over 4*2^(rate/4)
//   samples (rate 0 = instant), decay reaches SL over 4*2^rate, release
//   reaches 0 over 4*2^(rate/2) (rate 0 = 4 samples).
//
// Loop flags: a LOOP_START block latches its address into LSAX when read;
// consuming a LOOP_END block jumps NAX to LSAX and reports ENDX; without
// LOOP_REPEAT the envelope is forced to 0 (the voice keeps running silently on
// the libsd silence block).
//
// Volume writes with bit 15 set select the hardware SWEEP mode: the level ramps
// toward the ceiling/floor at an envelope-style rate until a fixed write lands.
// VOLX presents 2x the fixed 15-bit level (0..0x7FFF), the scale the module's
// fade stepper compares against.
//
// Pure: no globals, no I/O. VoiceState is the packed, field-exact savestate
// mirror (SNDC section) -- 119 bytes, no padding.
#include <cstdint>

#include "engine/sound/vag_decoder.h"

namespace ps2r::sound {

constexpr uint32_t kSpuRamSize = 2u * 1024u * 1024u;

enum class EnvPhase : uint8_t { Off, Attack, Decay, Sustain, Release };

#pragma pack(push, 1)
struct VoiceState {
    float vol_l, vol_r;
    uint16_t pitch;
    uint32_t ssa, lsax, nax;
    uint8_t phase;
    float env;
    float attack_step;
    float decay_step;
    float sustain_level;
    float release_step;
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
    void SetVolL(uint16_t v);
    void SetVolR(uint16_t v);
    void SetPitch(uint16_t p) { pitch_ = p; }
    void SetAdsr1(uint16_t v);
    void SetAdsr2(uint16_t v);
    void SetSsa(uint32_t byte_addr) { ssa_ = byte_addr & ~0xFu; }
    void SetLsax(uint32_t byte_addr) { lsax_ = byte_addr & ~0xFu; }

    // Restart from SSA, envelope -> attack; with spu_ram the first block is
    // fetched eagerly (hardware latches a LOOP_START block into LSAX at key-on).
    void KeyOn(const uint8_t* spu_ram = nullptr);
    void KeyOff();

    uint32_t Nax() const { return nax_; }
    uint16_t Envx() const { return static_cast<uint16_t>(env_ * 32767.0f); }
    uint16_t VolxL() const;
    uint16_t VolxR() const;
    bool Keyed() const { return phase_ != EnvPhase::Off; }
    bool Audible() const { return Keyed() && env_ > 0.0001f; }
    EnvPhase Phase() const { return phase_; }
    uint32_t Lsax() const { return lsax_; }

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

private:
    static float VolumeGain(uint16_t reg, float current);
    void LoadBlock(const uint8_t* spu_ram);
    void AdvanceEnvelope();
    void AdvanceSweeps();

    float vol_l_ = 0.0f, vol_r_ = 0.0f;
    uint16_t swp_l_ = 0, swp_r_ = 0;   // active sweep registers (0 = fixed); transient
    uint16_t pitch_ = 0;
    uint32_t ssa_ = 0, lsax_ = 0, nax_ = 0;

    EnvPhase phase_ = EnvPhase::Off;
    float env_ = 0.0f;
    float attack_step_ = 1.0f;
    float decay_step_ = 1.0f;
    float sustain_level_ = 1.0f;
    float release_step_ = 1.0f;

    VagDecoder::History hist_{};
    int16_t block_[VagDecoder::kBlockSamples] = {};
    uint8_t block_flags_ = 0;
    bool block_valid_ = false;
    int sample_idx_ = 0;
    uint32_t frac_ = 0;
    int16_t prev_sample_ = 0;
};

}  // namespace ps2r::sound

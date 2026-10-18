#include "engine/sound/voice.h"

#include <algorithm>

namespace ps2r::sound {

namespace {

// ADSR1 / ADSR2 bit fields (the SPU2 register layout).
int AttackMode(uint16_t a1) { return (a1 >> 15) & 1; }
int AttackShift(uint16_t a1) { return (a1 >> 10) & 0x1F; }
int AttackStep(uint16_t a1) { return (a1 >> 8) & 0x3; }
int DecayShift(uint16_t a1) { return (a1 >> 4) & 0xF; }
int SustainLevel(uint16_t a1) { return a1 & 0xF; }
int SustainMode(uint16_t a2) { return (a2 >> 15) & 1; }
int SustainDecrease(uint16_t a2) { return (a2 >> 14) & 1; }
int SustainShift(uint16_t a2) { return (a2 >> 8) & 0x1F; }
int SustainStep(uint16_t a2) { return (a2 >> 6) & 0x3; }
int ReleaseMode(uint16_t a2) { return (a2 >> 5) & 1; }
int ReleaseShift(uint16_t a2) { return a2 & 0x1F; }

constexpr uint16_t kPitchMax = 0x3FFF;

}  // namespace

int32_t Voice::RateStep(bool exponential, bool decrease, int shift, int step, int32_t level, uint32_t& counter,
                        int32_t floor, int32_t ceiling) {
    uint32_t counter_inc = 0x8000u >> std::max(0, shift - 11);
    int32_t level_inc = step * (1 << std::max(0, 11 - shift));
    if (exponential) {
        if (!decrease && level > 0x6000) counter_inc >>= 2;
        if (decrease) level_inc = static_cast<int16_t>((level_inc * level) >> 15);
    }
    if (counter_inc == 0) counter_inc = 1;
    counter += counter_inc;
    if (counter >= 0x8000u) {
        counter = 0;
        level = std::clamp(level + level_inc, floor, ceiling);
    }
    return level;
}

void Voice::SetLsax(uint32_t byte_addr) {
    lsax_ = byte_addr & ~0xFu;
    loop_mode_ = true;
}

void Voice::SetVol(int ch, uint16_t v) {
    vol_reg_[ch] = v;
    if (!(v & 0x8000)) vol_[ch] = FixedVolumeLevel(v);
}

void Voice::AdvanceSweep(int ch) {
    const uint16_t reg = vol_reg_[ch];
    if (!(reg & 0x8000)) return;
    const bool exp = (reg & 0x4000) != 0;
    const bool decr = (reg & 0x2000) != 0;
    const bool neg = (reg & 0x1000) != 0;   // the negative phase: the ramp runs in the inverted half
    const int shift = (reg >> 2) & 0x1F;
    const int step_field = reg & 0x3;
    int32_t step = 7 - step_field;
    if (decr) step = ~step;
    uint32_t counter_inc = 0x8000u >> std::max(0, shift - 11);
    int32_t level_inc = step * (1 << std::max(0, 11 - shift));
    int32_t level = vol_[ch];
    if (exp) {
        if (!decr && level > 0x6000) counter_inc >>= 2;
        if (decr) level_inc = static_cast<int16_t>((level_inc * level) >> 15);
    }
    // The all-ones rate (shift 0x1F, step 3) is the one genuinely infinite hold.
    if (!(step_field == 3 && shift == 0x1F) && counter_inc == 0) counter_inc = 1;
    if (!(exp && decr) && neg) level_inc = -level_inc;
    vol_counter_[ch] += counter_inc;
    if (vol_counter_[ch] < 0x8000u) return;
    vol_counter_[ch] = 0;
    int32_t lo = INT16_MIN, hi = INT16_MAX;
    if (decr) {
        lo = neg ? INT16_MIN : 0;
        hi = neg ? 0 : INT16_MAX;
        if (exp) {
            lo = 0;
            hi = INT16_MAX;
        }
    }
    vol_[ch] = std::clamp(level + level_inc, lo, hi);
}

void Voice::KeyOn(const uint8_t* spu_ram) {
    nax_ = ssa_;
    phase_ = EnvPhase::Attack;
    env_ = 0;
    env_counter_ = 0;
    loop_mode_ = false;
    hist_ = VagDecoder::History{};
    block_valid_ = false;
    sample_idx_ = 0;
    frac_ = 0;
    prev_sample_ = 0;
    if (spu_ram) LoadBlock(spu_ram);
}

void Voice::KeyOff() {
    if (phase_ == EnvPhase::Off) return;
    phase_ = EnvPhase::Release;
    env_counter_ = 0;
}

void Voice::Stop() {
    phase_ = EnvPhase::Off;
    env_ = 0;
}

void Voice::Silence() {
    Stop();
    block_valid_ = false;
}

void Voice::Export(VoiceState& out) const {
    out.vol_l = static_cast<float>(vol_[0]) / 32768.0f;
    out.vol_r = static_cast<float>(vol_[1]) / 32768.0f;
    out.pitch = pitch_;
    out.ssa = ssa_;
    out.lsax = lsax_;
    out.nax = nax_;
    out.phase = static_cast<uint8_t>(phase_);
    out.env = static_cast<float>(env_) / 32767.0f;
    out.adsr1 = adsr1_;
    out.adsr2 = adsr2_;
    out.vol_reg_l = vol_reg_[0];
    out.vol_reg_r = vol_reg_[1];
    out.env_counter = env_counter_;
    out.loop_mode = loop_mode_ ? 1 : 0;
    out.reserved[0] = out.reserved[1] = out.reserved[2] = 0;
    out.hist_h1 = hist_.h1;
    out.hist_h2 = hist_.h2;
    for (int i = 0; i < VagDecoder::kBlockSamples; i++) out.block[i] = block_[i];
    out.block_flags = block_flags_;
    out.block_valid = block_valid_ ? 1 : 0;
    out.sample_idx = sample_idx_;
    out.frac = frac_;
    out.prev_sample = prev_sample_;
}

void Voice::Import(const VoiceState& in) {
    vol_[0] = std::clamp(static_cast<int32_t>(in.vol_l * 32768.0f), static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX));
    vol_[1] = std::clamp(static_cast<int32_t>(in.vol_r * 32768.0f), static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX));
    vol_counter_[0] = vol_counter_[1] = 0;
    pitch_ = in.pitch;
    ssa_ = in.ssa;
    lsax_ = in.lsax;
    nax_ = in.nax;
    phase_ = in.phase <= static_cast<uint8_t>(EnvPhase::Release) ? static_cast<EnvPhase>(in.phase) : EnvPhase::Off;
    env_ = std::clamp(static_cast<int32_t>(in.env * 32767.0f), 0, kEnvMax);
    adsr1_ = in.adsr1;
    adsr2_ = in.adsr2;
    vol_reg_[0] = in.vol_reg_l;
    vol_reg_[1] = in.vol_reg_r;
    env_counter_ = in.env_counter < 0x8000u ? in.env_counter : 0;
    loop_mode_ = in.loop_mode != 0;
    hist_.h1 = in.hist_h1;
    hist_.h2 = in.hist_h2;
    for (int i = 0; i < VagDecoder::kBlockSamples; i++) block_[i] = in.block[i];
    block_flags_ = in.block_flags;
    block_valid_ = in.block_valid != 0;
    sample_idx_ = (in.sample_idx >= 0 && in.sample_idx < VagDecoder::kBlockSamples) ? in.sample_idx : 0;
    frac_ = in.frac;
    prev_sample_ = in.prev_sample;
}

void Voice::LoadBlock(const uint8_t* spu_ram) {
    nax_ &= kSpuRamSize - 1;
    nax_ &= ~0xFu;
    block_flags_ = VagDecoder::DecodeBlock(spu_ram + nax_, hist_, block_);
    block_valid_ = true;
    sample_idx_ = 0;
    if ((block_flags_ & VagDecoder::kFlagLoopStart) && !loop_mode_) lsax_ = nax_;
}

void Voice::AdvanceEnvelope() {
    switch (phase_) {
        case EnvPhase::Attack:
            env_ = RateStep(AttackMode(adsr1_) != 0, false, AttackShift(adsr1_), 7 - AttackStep(adsr1_), env_, env_counter_, 0, kEnvMax);
            if (env_ >= kEnvMax) {
                env_ = kEnvMax;
                phase_ = EnvPhase::Decay;
                env_counter_ = 0;
            }
            break;
        case EnvPhase::Decay: {
            const int32_t target = (SustainLevel(adsr1_) + 1) << 11;
            env_ = RateStep(true, true, DecayShift(adsr1_), -8, env_, env_counter_, 0, kEnvMax);
            if (env_ <= target) {
                phase_ = EnvPhase::Sustain;
                env_counter_ = 0;
            }
            break;
        }
        case EnvPhase::Sustain: {
            const bool decr = SustainDecrease(adsr2_) != 0;
            const int step = decr ? ~(7 - SustainStep(adsr2_)) : 7 - SustainStep(adsr2_);
            env_ = RateStep(SustainMode(adsr2_) != 0, decr, SustainShift(adsr2_), step, env_, env_counter_, 0, kEnvMax);
            if (env_ <= 0) Stop();   // a sustain that decays to silence ends the voice
            break;
        }
        case EnvPhase::Release:
            env_ = RateStep(ReleaseMode(adsr2_) != 0, true, ReleaseShift(adsr2_), -8, env_, env_counter_, 0, kEnvMax);
            if (env_ <= 0) Stop();
            break;
        case EnvPhase::Off:
            break;
    }
}

bool Voice::Step(const uint8_t* spu_ram, float& out_l, float& out_r) {
    out_l = 0.0f;
    out_r = 0.0f;
    if (phase_ == EnvPhase::Off) return false;
    bool endx = false;
    if (!block_valid_) LoadBlock(spu_ram);
    frac_ += std::min<uint32_t>(pitch_, kPitchMax);
    while (frac_ >= 0x1000) {
        frac_ -= 0x1000;
        prev_sample_ = block_[sample_idx_];
        sample_idx_++;
        if (sample_idx_ >= VagDecoder::kBlockSamples) {
            if (block_flags_ & VagDecoder::kFlagLoopEnd) {
                endx = true;
                last_end_repeat_ = (block_flags_ & VagDecoder::kFlagLoopRepeat) != 0;
                nax_ = lsax_;
                if (!last_end_repeat_) {
                    Stop();
                    LoadBlock(spu_ram);
                    return endx;
                }
            } else {
                nax_ += VagDecoder::kBlockBytes;
            }
            LoadBlock(spu_ram);
        }
    }
    const int32_t cur = block_[sample_idx_];
    const int32_t s = prev_sample_ + (((cur - prev_sample_) * static_cast<int32_t>(frac_)) >> 12);
    AdvanceEnvelope();
    AdvanceSweep(0);
    AdvanceSweep(1);
    const float sf = static_cast<float>(s) / 32768.0f * (static_cast<float>(env_) / 32768.0f);
    out_l = sf * (static_cast<float>(vol_[0]) / 32768.0f);
    out_r = sf * (static_cast<float>(vol_[1]) / 32768.0f);
    return endx;
}

}  // namespace ps2r::sound

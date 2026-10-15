#include "engine/sound/voice.h"

#include <cmath>

namespace ps2r::sound {

namespace {

float AttackStep(uint32_t rate7) {
    if (rate7 == 0) return 1.0f;
    const float samples = 4.0f * std::pow(2.0f, static_cast<float>(rate7) / 4.0f);
    return 1.0f / samples;
}

float DecayStep(uint32_t rate4) {
    if (rate4 == 0) return 1.0f;
    const float samples = 4.0f * std::pow(2.0f, static_cast<float>(rate4));
    return 1.0f / samples;
}

float ReleaseStep(uint32_t rate5) {
    if (rate5 == 0) return 0.25f;
    const float samples = 4.0f * std::pow(2.0f, static_cast<float>(rate5) / 2.0f);
    return 1.0f / samples;
}

}  // namespace

float Voice::VolumeGain(uint16_t reg, float current) {
    if (reg & 0x8000) return current;
    return static_cast<float>(reg & 0x7FFF) / 32767.0f;
}

void Voice::SetVolL(uint16_t v) {
    if (v & 0x8000) {
        swp_l_ = v;
        return;
    }
    swp_l_ = 0;
    vol_l_ = VolumeGain(v, vol_l_);
}

void Voice::SetVolR(uint16_t v) {
    if (v & 0x8000) {
        swp_r_ = v;
        return;
    }
    swp_r_ = 0;
    vol_r_ = VolumeGain(v, vol_r_);
}

uint16_t Voice::VolxL() const {
    const float l = vol_l_ * 65534.0f;
    return static_cast<uint16_t>(l >= 32767.0f ? 32767.0f : l);
}

uint16_t Voice::VolxR() const {
    const float r = vol_r_ * 65534.0f;
    return static_cast<uint16_t>(r >= 32767.0f ? 32767.0f : r);
}

void Voice::SetAdsr1(uint16_t v) {
    attack_step_ = AttackStep((v >> 8) & 0x7F);
    decay_step_ = DecayStep((v >> 4) & 0x0F);
    sustain_level_ = static_cast<float>(v & 0x0F) / 15.0f;
}

void Voice::SetAdsr2(uint16_t v) { release_step_ = ReleaseStep(v & 0x1F); }

void Voice::AdvanceSweeps() {
    if (swp_l_ == 0 && swp_r_ == 0) return;
    auto step_one = [](uint16_t reg, float g) -> float {
        const float d = 0.5f * AttackStep(reg & 0x7F);
        if (reg & 0x2000) {   // decrease toward the floor
            g -= d;
            return g < 0.0f ? 0.0f : g;
        }
        g += d;               // increase toward the ceiling (2x fixed full scale)
        return g > 0.5f ? 0.5f : g;
    };
    if (swp_l_) vol_l_ = step_one(swp_l_, vol_l_);
    if (swp_r_) vol_r_ = step_one(swp_r_, vol_r_);
}

void Voice::KeyOn(const uint8_t* spu_ram) {
    nax_ = ssa_;
    phase_ = EnvPhase::Attack;
    env_ = 0.0f;
    hist_ = VagDecoder::History{};
    block_valid_ = false;
    sample_idx_ = 0;
    frac_ = 0;
    prev_sample_ = 0;
    if (spu_ram) LoadBlock(spu_ram);
}

void Voice::KeyOff() {
    if (phase_ != EnvPhase::Off) phase_ = EnvPhase::Release;
}

void Voice::Silence() {
    phase_ = EnvPhase::Off;
    env_ = 0.0f;
    block_valid_ = false;
}

void Voice::Export(VoiceState& out) const {
    out.vol_l = vol_l_;
    out.vol_r = vol_r_;
    out.pitch = pitch_;
    out.ssa = ssa_;
    out.lsax = lsax_;
    out.nax = nax_;
    out.phase = static_cast<uint8_t>(phase_);
    out.env = env_;
    out.attack_step = attack_step_;
    out.decay_step = decay_step_;
    out.sustain_level = sustain_level_;
    out.release_step = release_step_;
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
    vol_l_ = in.vol_l;
    vol_r_ = in.vol_r;
    pitch_ = in.pitch;
    ssa_ = in.ssa;
    lsax_ = in.lsax;
    nax_ = in.nax;
    phase_ = in.phase <= static_cast<uint8_t>(EnvPhase::Release) ? static_cast<EnvPhase>(in.phase) : EnvPhase::Off;
    env_ = in.env;
    attack_step_ = in.attack_step;
    decay_step_ = in.decay_step;
    sustain_level_ = in.sustain_level;
    release_step_ = in.release_step;
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
    if (block_flags_ & VagDecoder::kFlagLoopStart) lsax_ = nax_;
}

void Voice::AdvanceEnvelope() {
    switch (phase_) {
        case EnvPhase::Attack:
            env_ += attack_step_;
            if (env_ >= 1.0f) {
                env_ = 1.0f;
                phase_ = (sustain_level_ >= 1.0f) ? EnvPhase::Sustain : EnvPhase::Decay;
            }
            break;
        case EnvPhase::Decay:
            env_ -= decay_step_;
            if (env_ <= sustain_level_) {
                env_ = sustain_level_;
                phase_ = EnvPhase::Sustain;
            }
            break;
        case EnvPhase::Sustain:
            break;
        case EnvPhase::Release:
            env_ -= release_step_;
            if (env_ <= 0.0f) {
                env_ = 0.0f;
                phase_ = EnvPhase::Off;
            }
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
    frac_ += pitch_;
    while (frac_ >= 0x1000) {
        frac_ -= 0x1000;
        prev_sample_ = block_[sample_idx_];
        sample_idx_++;
        if (sample_idx_ >= VagDecoder::kBlockSamples) {
            if (block_flags_ & VagDecoder::kFlagLoopEnd) {
                endx = true;
                nax_ = lsax_;
                if (!(block_flags_ & VagDecoder::kFlagLoopRepeat)) {
                    env_ = 0.0f;
                    if (phase_ != EnvPhase::Release) phase_ = EnvPhase::Sustain;
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
    AdvanceSweeps();
    const float sf = static_cast<float>(s) / 32768.0f * env_;
    out_l = sf * vol_l_;
    out_r = sf * vol_r_;
    return endx;
}

}  // namespace ps2r::sound

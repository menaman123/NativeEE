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
    older_[0] = older_[1] = 0;
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
    older_[0] = older_[1] = in.prev_sample;   // not in the SNDC image: the Gaussian's outer taps settle within two source samples
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

bool Voice::gaussian_ = false;

namespace {

// The SPU's 512-entry Gaussian interpolation table (PS1 SPU and PS2 SPU2 alike), as documented by
// no$psx / psx-spx "SPU ADPCM Pitch -- 4-Point Gaussian Interpolation" and used by PCSX2 (SPU2/Mixer.cpp,
// the Gaussian interpolation mode) and DuckStation (spu.cpp). For the interpolation index
// i = pitch counter bits 4..11 (our frac_ >> 4) and the four most recent samples s[-3..0]:
//   out = (gauss[0x0FF-i]*s[-3] + gauss[0x1FF-i]*s[-2] + gauss[0x100+i]*s[-1] + gauss[i]*s[0]) >> 15
// (the weights sum to 0x7F80: the kernel is centred between s[-2] and s[-1], one sample behind the
// linear interpolator's point, as on the hardware).
constexpr int16_t kGauss[512] = {
    -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001,
    -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001, -0x0001,
    0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00001,
    0x00001, 0x00001, 0x00001, 0x00002, 0x00002, 0x00002, 0x00003, 0x00003,
    0x00003, 0x00004, 0x00004, 0x00005, 0x00005, 0x00006, 0x00007, 0x00007,
    0x00008, 0x00009, 0x00009, 0x0000a, 0x0000b, 0x0000c, 0x0000d, 0x0000e,
    0x0000f, 0x00010, 0x00011, 0x00012, 0x00013, 0x00015, 0x00016, 0x00018,
    0x00019, 0x0001b, 0x0001c, 0x0001e, 0x00020, 0x00021, 0x00023, 0x00025,
    0x00027, 0x00029, 0x0002c, 0x0002e, 0x00030, 0x00033, 0x00035, 0x00038,
    0x0003a, 0x0003d, 0x00040, 0x00043, 0x00046, 0x00049, 0x0004d, 0x00050,
    0x00054, 0x00057, 0x0005b, 0x0005f, 0x00063, 0x00067, 0x0006b, 0x0006f,
    0x00074, 0x00078, 0x0007d, 0x00082, 0x00087, 0x0008c, 0x00091, 0x00096,
    0x0009c, 0x000a1, 0x000a7, 0x000ad, 0x000b3, 0x000ba, 0x000c0, 0x000c7,
    0x000cd, 0x000d4, 0x000db, 0x000e3, 0x000ea, 0x000f2, 0x000fa, 0x00101,
    0x0010a, 0x00112, 0x0011b, 0x00123, 0x0012c, 0x00135, 0x0013f, 0x00148,
    0x00152, 0x0015c, 0x00166, 0x00171, 0x0017b, 0x00186, 0x00191, 0x0019c,
    0x001a8, 0x001b4, 0x001c0, 0x001cc, 0x001d9, 0x001e5, 0x001f2, 0x00200,
    0x0020d, 0x0021b, 0x00229, 0x00237, 0x00246, 0x00255, 0x00264, 0x00273,
    0x00283, 0x00293, 0x002a3, 0x002b4, 0x002c4, 0x002d6, 0x002e7, 0x002f9,
    0x0030b, 0x0031d, 0x00330, 0x00343, 0x00356, 0x0036a, 0x0037e, 0x00392,
    0x003a7, 0x003bc, 0x003d1, 0x003e7, 0x003fc, 0x00413, 0x0042a, 0x00441,
    0x00458, 0x00470, 0x00488, 0x004a0, 0x004b9, 0x004d2, 0x004ec, 0x00506,
    0x00520, 0x0053b, 0x00556, 0x00572, 0x0058e, 0x005aa, 0x005c7, 0x005e4,
    0x00601, 0x0061f, 0x0063e, 0x0065c, 0x0067c, 0x0069b, 0x006bb, 0x006dc,
    0x006fd, 0x0071e, 0x00740, 0x00762, 0x00784, 0x007a7, 0x007cb, 0x007ef,
    0x00813, 0x00838, 0x0085d, 0x00883, 0x008a9, 0x008d0, 0x008f7, 0x0091e,
    0x00946, 0x0096f, 0x00998, 0x009c1, 0x009eb, 0x00a16, 0x00a40, 0x00a6c,
    0x00a98, 0x00ac4, 0x00af1, 0x00b1e, 0x00b4c, 0x00b7a, 0x00ba9, 0x00bd8,
    0x00c07, 0x00c38, 0x00c68, 0x00c99, 0x00ccb, 0x00cfd, 0x00d30, 0x00d63,
    0x00d97, 0x00dcb, 0x00e00, 0x00e35, 0x00e6b, 0x00ea1, 0x00ed7, 0x00f0f,
    0x00f46, 0x00f7f, 0x00fb7, 0x00ff1, 0x0102a, 0x01065, 0x0109f, 0x010db,
    0x01116, 0x01153, 0x0118f, 0x011cd, 0x0120b, 0x01249, 0x01288, 0x012c7,
    0x01307, 0x01347, 0x01388, 0x013c9, 0x0140b, 0x0144d, 0x01490, 0x014d4,
    0x01517, 0x0155c, 0x015a0, 0x015e6, 0x0162c, 0x01672, 0x016b9, 0x01700,
    0x01747, 0x01790, 0x017d8, 0x01821, 0x0186b, 0x018b5, 0x01900, 0x0194b,
    0x01996, 0x019e2, 0x01a2e, 0x01a7b, 0x01ac8, 0x01b16, 0x01b64, 0x01bb3,
    0x01c02, 0x01c51, 0x01ca1, 0x01cf1, 0x01d42, 0x01d93, 0x01de5, 0x01e37,
    0x01e89, 0x01edc, 0x01f2f, 0x01f82, 0x01fd6, 0x0202a, 0x0207f, 0x020d4,
    0x02129, 0x0217f, 0x021d5, 0x0222c, 0x02282, 0x022da, 0x02331, 0x02389,
    0x023e1, 0x02439, 0x02492, 0x024eb, 0x02545, 0x0259e, 0x025f8, 0x02653,
    0x026ad, 0x02708, 0x02763, 0x027be, 0x0281a, 0x02876, 0x028d2, 0x0292e,
    0x0298b, 0x029e7, 0x02a44, 0x02aa1, 0x02aff, 0x02b5c, 0x02bba, 0x02c18,
    0x02c76, 0x02cd4, 0x02d33, 0x02d91, 0x02df0, 0x02e4f, 0x02eae, 0x02f0d,
    0x02f6c, 0x02fcc, 0x0302b, 0x0308b, 0x030ea, 0x0314a, 0x031aa, 0x03209,
    0x03269, 0x032c9, 0x03329, 0x03389, 0x033e9, 0x03449, 0x034a9, 0x03509,
    0x03569, 0x035c9, 0x03629, 0x03689, 0x036e8, 0x03748, 0x037a8, 0x03807,
    0x03867, 0x038c6, 0x03926, 0x03985, 0x039e4, 0x03a43, 0x03aa2, 0x03b00,
    0x03b5f, 0x03bbd, 0x03c1b, 0x03c79, 0x03cd7, 0x03d35, 0x03d92, 0x03def,
    0x03e4c, 0x03ea9, 0x03f05, 0x03f62, 0x03fbd, 0x04019, 0x04074, 0x040d0,
    0x0412a, 0x04185, 0x041df, 0x04239, 0x04292, 0x042eb, 0x04344, 0x0439c,
    0x043f4, 0x0444c, 0x044a3, 0x044fa, 0x04550, 0x045a6, 0x045fc, 0x04651,
    0x046a6, 0x046fa, 0x0474e, 0x047a1, 0x047f4, 0x04846, 0x04898, 0x048e9,
    0x0493a, 0x0498a, 0x049d9, 0x04a29, 0x04a77, 0x04ac5, 0x04b13, 0x04b5f,
    0x04bac, 0x04bf7, 0x04c42, 0x04c8d, 0x04cd7, 0x04d20, 0x04d68, 0x04db0,
    0x04df7, 0x04e3e, 0x04e84, 0x04ec9, 0x04f0e, 0x04f52, 0x04f95, 0x04fd7,
    0x05019, 0x0505a, 0x0509a, 0x050da, 0x05118, 0x05156, 0x05194, 0x051d0,
    0x0520c, 0x05247, 0x05281, 0x052ba, 0x052f3, 0x0532a, 0x05361, 0x05397,
    0x053cc, 0x05401, 0x05434, 0x05467, 0x05499, 0x054ca, 0x054fa, 0x05529,
    0x05558, 0x05585, 0x055b2, 0x055de, 0x05609, 0x05632, 0x0565b, 0x05684,
    0x056ab, 0x056d1, 0x056f6, 0x0571b, 0x0573e, 0x05761, 0x05782, 0x057a3,
    0x057c3, 0x057e2, 0x057ff, 0x0581c, 0x05838, 0x05853, 0x0586d, 0x05886,
    0x0589e, 0x058b5, 0x058cb, 0x058e0, 0x058f4, 0x05907, 0x05919, 0x0592a,
    0x0593a, 0x05949, 0x05958, 0x05965, 0x05971, 0x0597c, 0x05986, 0x0598f,
    0x05997, 0x0599e, 0x059a4, 0x059a9, 0x059ad, 0x059b0, 0x059b2, 0x059b3,
};

}  // namespace

bool Voice::Step(const uint8_t* spu_ram, float& out_l, float& out_r) {
    out_l = 0.0f;
    out_r = 0.0f;
    if (phase_ == EnvPhase::Off) return false;
    bool endx = false;
    if (!block_valid_) LoadBlock(spu_ram);
    frac_ += std::min<uint32_t>(pitch_, kPitchMax);
    while (frac_ >= 0x1000) {
        frac_ -= 0x1000;
        older_[0] = older_[1];
        older_[1] = prev_sample_;
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
    int32_t s;
    if (gaussian_) {
        const uint32_t i = (frac_ >> 4) & 0xFF;
        s = (kGauss[0x0FF - i] * older_[0] + kGauss[0x1FF - i] * older_[1] + kGauss[0x100 + i] * prev_sample_ +
             kGauss[i] * cur) >> 15;
        s = std::clamp<int32_t>(s, -32768, 32767);
    } else {
        s = prev_sample_ + (((cur - prev_sample_) * static_cast<int32_t>(frac_)) >> 12);
    }
    AdvanceEnvelope();
    AdvanceSweep(0);
    AdvanceSweep(1);
    const float sf = static_cast<float>(s) / 32768.0f * (static_cast<float>(env_) / 32768.0f);
    out_l = sf * (static_cast<float>(vol_[0]) / 32768.0f);
    out_r = sf * (static_cast<float>(vol_[1]) / 32768.0f);
    return endx;
}

}  // namespace ps2r::sound

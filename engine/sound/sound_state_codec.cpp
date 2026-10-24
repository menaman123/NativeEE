#include "engine/sound/sound_state_codec.h"

#include "engine/core/savestate/payload_reader.h"
#include "engine/sound/hle/sd_block_trans.h"
#include "engine/sound/spu_core.h"

namespace ps2r::sound {

using savestate::Blob;
using savestate::PayloadReader;
using savestate::PutBytes;
using savestate::PutPod;
using savestate::SectionSpec;

SoundStateCodec::SoundStateCodec(SpuCore& core, hle::SdBlockTrans& block) : core_(core), block_(block) {}

void SoundStateCodec::Register(savestate::SectionRegistry& registry) {
    registry.Register(SdrxSpec());
    registry.Register(SndcSpec());
}

SectionSpec SoundStateCodec::SndcSpec() {
    SectionSpec s;
    s.tag = savestate::TAG_SNDC;
    s.required = false;
    s.owner = "SoundStateCodec";
    s.serialize = [this](std::vector<uint8_t>& out) { SerializeSndc(out); };
    s.validate = [](const Blob& b, std::string* err) { return ValidateSndc(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return RestoreSndc(b.payload, err); };
    return s;
}

SectionSpec SoundStateCodec::SdrxSpec() {
    SectionSpec s;
    s.tag = savestate::TAG_SDRX;
    s.required = false;
    s.owner = "SoundStateCodec";
    s.serialize = [this](std::vector<uint8_t>& out) { SerializeSdrx(out); };
    s.validate = [](const Blob& b, std::string* err) { return ValidateSdrx(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return RestoreSdrx(b.payload, err); };
    return s;
}

// --- SNDC --------------------------------------------------------------------

size_t SoundStateCodec::SndcBytes() {
    return 4 + kSpuRamSize + 4 + static_cast<size_t>(SpuCore::kVoices) * sizeof(VoiceState) +
           SpuCore::kCores * 4 + static_cast<size_t>(SpuCore::kVoices) * 5 * 4 +
           static_cast<size_t>(SpuCore::kVoices) * 2 * 4 + SpuCore::kCores * 4 * 2 +
           static_cast<size_t>(SpuCore::kCores) * SpuCore::kCoreParams * 4 + SpuCore::kCores * 4 * 2;
}

void SoundStateCodec::SerializeSndc(std::vector<uint8_t>& out) {
    core_.ApplyQueued();
    out.reserve(out.size() + SndcBytes());
    PutPod<uint32_t>(out, kSpuRamSize);
    PutBytes(out, core_.SpuRam(), kSpuRamSize);
    PutPod<uint32_t>(out, static_cast<uint32_t>(SpuCore::kVoices));
    for (int vi = 0; vi < SpuCore::kVoices; vi++) {
        VoiceState vs{};
        core_.VoiceAt(vi).Export(vs);
        PutPod(out, vs);
    }
    for (int c = 0; c < SpuCore::kCores; c++) PutPod<uint32_t>(out, core_.Endx(c));
    for (int vi = 0; vi < SpuCore::kVoices; vi++)
        for (int fn = 0; fn < 5; fn++) PutPod<uint32_t>(out, core_.ParamShadow(vi, fn));
    for (int vi = 0; vi < SpuCore::kVoices; vi++)
        for (int k = 0; k < 2; k++) PutPod<uint32_t>(out, core_.AddrShadow(vi, k));
    for (int c = 0; c < SpuCore::kCores; c++) PutPod<float>(out, core_.MvolL(c));
    for (int c = 0; c < SpuCore::kCores; c++) PutPod<float>(out, core_.MvolR(c));
    for (int c = 0; c < SpuCore::kCores; c++)
        for (int fn = 0; fn < SpuCore::kCoreParams; fn++) PutPod<uint32_t>(out, core_.CoreAttr(c, fn));
    uint32_t haddr[SpuCore::kCores], harg[SpuCore::kCores];
    for (int c = 0; c < SpuCore::kCores; c++) core_.GetTransIntrHandler(c, haddr[c], harg[c]);
    for (int c = 0; c < SpuCore::kCores; c++) PutPod<uint32_t>(out, haddr[c]);
    for (int c = 0; c < SpuCore::kCores; c++) PutPod<uint32_t>(out, harg[c]);
}

bool SoundStateCodec::ValidateSndc(const std::vector<uint8_t>& payload, std::string* err) {
    PayloadReader r(payload);
    uint32_t spu_size = 0, nvoices = 0;
    if (!r.Get(&spu_size) || spu_size != kSpuRamSize) {
        if (err) *err = "SNDC: SPU RAM size mismatch";
        return false;
    }
    if (!r.Skip(kSpuRamSize) || !r.Get(&nvoices) || nvoices != static_cast<uint32_t>(SpuCore::kVoices)) {
        if (err) *err = "SNDC: voice count mismatch";
        return false;
    }
    if (payload.size() != SndcBytes()) {
        if (err) *err = "SNDC: bad payload length";
        return false;
    }
    return true;
}

bool SoundStateCodec::RestoreSndc(const std::vector<uint8_t>& payload, std::string* err) {
    if (!ValidateSndc(payload, err)) return false;
    PayloadReader r(payload);
    uint32_t u32 = 0;
    float f = 0.0f;
    // Whatever the pre-load boot queued or held is not part of the file.
    core_.Ring().Clear();
    core_.ClearEagerHolds();
    r.Get(&u32);
    r.GetBytes(core_.SpuRam(), kSpuRamSize);
    r.Get(&u32);
    for (int vi = 0; vi < SpuCore::kVoices; vi++) {
        VoiceState vs{};
        if (!r.Get(&vs)) return false;
        core_.VoiceAt(vi).Import(vs);
    }
    for (int c = 0; c < SpuCore::kCores; c++) {
        if (!r.Get(&u32)) return false;
        core_.SetEndx(c, u32);
    }
    for (int vi = 0; vi < SpuCore::kVoices; vi++)
        for (int fn = 0; fn < 5; fn++) {
            if (!r.Get(&u32)) return false;
            core_.SetParamShadow(vi, fn, u32);
        }
    // The voices' registers are the shadows (a file written before the VoiceState carried
    // the registers holds derived floats in those bytes): re-latch VOLL/VOLR/PITCH/ADSR1/ADSR2.
    for (int vi = 0; vi < SpuCore::kVoices; vi++) {
        Voice& v = core_.VoiceAt(vi);
        v.SetVolL(static_cast<uint16_t>(core_.ParamShadow(vi, 0)));
        v.SetVolR(static_cast<uint16_t>(core_.ParamShadow(vi, 1)));
        v.SetPitch(static_cast<uint16_t>(core_.ParamShadow(vi, 2)));
        v.SetAdsr1(static_cast<uint16_t>(core_.ParamShadow(vi, 3)));
        v.SetAdsr2(static_cast<uint16_t>(core_.ParamShadow(vi, 4)));
    }
    for (int vi = 0; vi < SpuCore::kVoices; vi++)
        for (int k = 0; k < 2; k++) {
            if (!r.Get(&u32)) return false;
            core_.SetAddrShadow(vi, k, u32);
        }
    for (int c = 0; c < SpuCore::kCores; c++) {
        if (!r.Get(&f)) return false;
        core_.SetMvolL(c, f);
    }
    for (int c = 0; c < SpuCore::kCores; c++) {
        if (!r.Get(&f)) return false;
        core_.SetMvolR(c, f);
    }
    for (int c = 0; c < SpuCore::kCores; c++)
        for (int fn = 0; fn < SpuCore::kCoreParams; fn++) {
            if (!r.Get(&u32)) return false;
            core_.SetCoreAttrShadow(c, fn, u32);
        }
    // The MVOL gains follow the restored registers (the file's floats were derived by the
    // model that wrote it; a register the title programmed is the truth, a never-written
    // register keeps the file's gain).
    for (int c = 0; c < SpuCore::kCores; c++)
        for (int fn : {SpuCore::kMvoll, SpuCore::kMvolr})
            if (const uint32_t reg = core_.CoreAttr(c, fn)) core_.SetCoreParamShadow(c, fn, reg);
    uint32_t haddr[SpuCore::kCores] = {}, harg[SpuCore::kCores] = {};
    for (int c = 0; c < SpuCore::kCores; c++)
        if (!r.Get(&haddr[c])) return false;
    for (int c = 0; c < SpuCore::kCores; c++)
        if (!r.Get(&harg[c])) return false;
    for (int c = 0; c < SpuCore::kCores; c++) core_.SetTransIntrHandler(c, haddr[c], harg[c]);
    core_.RepublishReadbacks();
    return true;
}

// --- SDRX --------------------------------------------------------------------

size_t SoundStateCodec::SdrxBytes() { return static_cast<size_t>(hle::SdBlockTrans::kChannels) * (1 + 4 + 8); }

void SoundStateCodec::SerializeSdrx(std::vector<uint8_t>& out) const {
    for (int c = 0; c < hle::SdBlockTrans::kChannels; c++) {
        bool active = false;
        uint32_t buf_index = 0;
        uint64_t elapsed = 0;
        block_.Export(c, active, buf_index, elapsed);
        PutPod<uint8_t>(out, active ? 1 : 0);
        PutPod<uint32_t>(out, buf_index);
        PutPod<uint64_t>(out, elapsed);
    }
}

bool SoundStateCodec::ValidateSdrx(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() != SdrxBytes()) {
        if (err) *err = "SDRX: bad payload length";
        return false;
    }
    return true;
}

bool SoundStateCodec::RestoreSdrx(const std::vector<uint8_t>& payload, std::string* err) {
    if (!ValidateSdrx(payload, err)) return false;
    PayloadReader r(payload);
    for (int c = 0; c < hle::SdBlockTrans::kChannels; c++) {
        uint8_t active = 0;
        uint32_t buf_index = 0;
        uint64_t elapsed = 0;
        if (!r.Get(&active) || !r.Get(&buf_index) || !r.Get(&elapsed)) return false;
        block_.Import(c, active != 0, buf_index, elapsed);
    }
    return true;
}

}  // namespace ps2r::sound

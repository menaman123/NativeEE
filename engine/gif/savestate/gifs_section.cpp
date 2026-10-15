#include "engine/gif/savestate/gifs_section.h"

#include "engine/core/savestate/payload_reader.h"
#include "engine/gif/gif_regs.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::PutPod;
using ps2r::savestate::SectionSpec;

GifsSection::GifsSection(GifRegs& regs) : regs_(regs) {}

void GifsSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec GifsSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_GIFS;
    s.required = true;
    s.owner = "GifsSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    return s;
}

void GifsSection::Serialize(std::vector<uint8_t>& out) const {
    out.reserve(out.size() + kPayloadBytes);
    PutPod<uint32_t>(out, regs_.Ctrl());
    PutPod<uint32_t>(out, regs_.Mode());
    PutPod<uint32_t>(out, 0);  // the reference's never-written STAT mirror
    PutPod<uint8_t>(out, regs_.Path3MaskedByVif() ? 1 : 0);
}

bool GifsSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() != kPayloadBytes) {
        if (err) *err = "GIFS: payload is " + std::to_string(payload.size()) + " bytes, expected " + std::to_string(kPayloadBytes);
        return false;
    }
    return true;
}

bool GifsSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    PayloadReader r(payload);
    uint32_t ctrl = 0, mode = 0, stat = 0;
    uint8_t path3_masked = 0;
    if (!r.Get(&ctrl) || !r.Get(&mode) || !r.Get(&stat) || !r.Get(&path3_masked)) return false;
    regs_.RestoreRegisters(ctrl, mode, path3_masked != 0);
    if (on_restored_) on_restored_();
    ++restored_;
    return true;
}

#include "engine/vif/savestate/vifs_section.h"

#include <cstring>

#include "engine/vif/vif.h"

using ps2r::savestate::Blob;
using ps2r::savestate::SectionSpec;

static_assert(sizeof(VifRegs) == VifsSection::kUnitBytes, "VifRegs is the frozen 92-byte record");

VifsSection::VifsSection(Vif& vif) : vif_(vif) {}

void VifsSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec VifsSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_VIFS;
    s.required = true;
    s.owner = "VifsSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    return s;
}

void VifsSection::Serialize(std::vector<uint8_t>& out) const {
    out.reserve(out.size() + kPayloadBytes);
    for (int unit = 0; unit < 2; ++unit) ps2r::savestate::PutPod(out, vif_.regs(unit));
}

bool VifsSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() != kPayloadBytes) {
        if (err) *err = "VIFS: payload is " + std::to_string(payload.size()) + " bytes, expected " + std::to_string(kPayloadBytes);
        return false;
    }
    return true;
}

bool VifsSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    for (int unit = 0; unit < 2; ++unit) {
        std::memcpy(&vif_.regs(unit), payload.data() + static_cast<size_t>(unit) * kUnitBytes, kUnitBytes);
        vif_.ResetTransient(unit);
    }
    ++restored_;
    return true;
}

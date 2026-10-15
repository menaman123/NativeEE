#include "engine/vu/savestate/vu1_register_section.h"

#include <cstddef>
#include <cstring>

#include "engine/core/savestate/payload_reader.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/vu/vu_unit.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::PutPod;
using ps2r::savestate::SectionSpec;

// The image is the frozen EE register block: its size and the VUregs offset
// are the layout the reference wrote (cpu_state.h pins both).
static_assert(sizeof(CpuContext) == Vu1RegisterSection::kContextBytes, "VU1R image is one CpuContext");
static_assert(offsetof(CpuContext, vuRegs) == Vu1RegisterSection::kVuRegsOffset, "VUregs sits at 992");
static_assert(sizeof(VUregs) == Vu1RegisterSection::kVuRegsBytes, "VUregs is 608 bytes");

Vu1RegisterSection::Vu1RegisterSection(VuUnit& vu1) : vu1_(vu1) {}

void Vu1RegisterSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec Vu1RegisterSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_VU1R;
    s.required = false;
    s.owner = "Vu1RegisterSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    // Absent: the unit keeps its live register file.
    s.absent = [] {};
    return s;
}

void Vu1RegisterSection::Serialize(std::vector<uint8_t>& out) const {
    out.reserve(out.size() + kPayloadBytes);
    PutPod<uint8_t>(out, 0);  // running: VU1 runs inline under the deterministic profile
    PutPod<uint32_t>(out, static_cast<uint32_t>(kContextBytes));
    uint8_t image[kContextBytes];
    std::memset(image, 0, sizeof(image));
    std::memcpy(image + kVuRegsOffset, &vu1_.regs(), kVuRegsBytes);
    ps2r::savestate::PutBytes(out, image, sizeof(image));
}

bool Vu1RegisterSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() != kPayloadBytes) {
        if (err) *err = "VU1R: payload is " + std::to_string(payload.size()) + " bytes, expected " + std::to_string(kPayloadBytes);
        return false;
    }
    PayloadReader r(payload);
    uint8_t running = 0;
    uint32_t size = 0;
    if (!r.Get(&running) || !r.Get(&size)) return false;
    if (size != kContextBytes) {
        if (err) *err = "VU1R: context size is " + std::to_string(size) + ", expected " + std::to_string(kContextBytes);
        return false;
    }
    return true;
}

bool Vu1RegisterSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    PayloadReader r(payload);
    uint8_t running = 0;
    uint32_t size = 0;
    if (!r.Get(&running) || !r.Get(&size) || !r.Skip(kVuRegsOffset)) return false;
    if (!r.GetBytes(&vu1_.regs(), kVuRegsBytes)) return false;
    ++restored_;
    if (running) ++restored_running_;
    if (on_restored_) on_restored_();
    return true;
}

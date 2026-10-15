#include "engine/vu/savestate/vu_memory_section.h"

#include <cstring>

#include "engine/vu/vu_unit.h"

using ps2r::savestate::Blob;
using ps2r::savestate::SectionSpec;
using ps2r::savestate::TAG_VU0C;
using ps2r::savestate::TAG_VU0D;
using ps2r::savestate::TAG_VU1C;
using ps2r::savestate::TAG_VU1D;

VuMemorySection::VuMemorySection(VuUnit& vu0, VuUnit& vu1) : vu0_(vu0), vu1_(vu1) {}

void VuMemorySection::Register(ps2r::savestate::SectionRegistry& registry) {
    registry.Register(Spec(TAG_VU0C));
    registry.Register(Spec(TAG_VU0D));
    registry.Register(Spec(TAG_VU1C));
    registry.Register(Spec(TAG_VU1D));
}

SectionSpec VuMemorySection::Spec(uint32_t tag) {
    SectionSpec s;
    s.tag = tag;
    s.required = true;
    s.owner = "VuMemorySection";
    s.serialize = [this, tag](std::vector<uint8_t>& out) { Serialize(tag, out); };
    s.validate = [tag](const Blob& b, std::string* err) { return Validate(tag, b.payload, err); };
    s.restore = [this, tag](const Blob& b, std::string* err) { return Restore(tag, b.payload, err); };
    return s;
}

size_t VuMemorySection::PayloadBytes(uint32_t tag) {
    switch (tag) {
        case TAG_VU0C:
        case TAG_VU0D: return kVu0Bytes;
        case TAG_VU1C:
        case TAG_VU1D: return kVu1Bytes;
        default: return 0;
    }
}

const uint8_t* VuMemorySection::Source(uint32_t tag) const {
    switch (tag) {
        case TAG_VU0C: return vu0_.state().micro;
        case TAG_VU0D: return vu0_.state().data;
        case TAG_VU1C: return vu1_.state().micro;
        case TAG_VU1D: return vu1_.state().data;
        default: return nullptr;
    }
}

uint8_t* VuMemorySection::Target(uint32_t tag) {
    switch (tag) {
        case TAG_VU0C: return vu0_.state().micro;
        case TAG_VU0D: return vu0_.state().data;
        case TAG_VU1C: return vu1_.state().micro;
        case TAG_VU1D: return vu1_.state().data;
        default: return nullptr;
    }
}

void VuMemorySection::Serialize(uint32_t tag, std::vector<uint8_t>& out) const {
    const uint8_t* src = Source(tag);
    if (!src) return;
    ps2r::savestate::PutBytes(out, src, PayloadBytes(tag));
}

bool VuMemorySection::Validate(uint32_t tag, const std::vector<uint8_t>& payload, std::string* err) {
    const size_t want = PayloadBytes(tag);
    if (want == 0) {
        if (err) *err = "not a VU memory tag";
        return false;
    }
    if (payload.size() != want) {
        if (err)
            *err = ps2r::savestate::TagName(tag) + ": payload is " + std::to_string(payload.size()) +
                   " bytes, expected " + std::to_string(want);
        return false;
    }
    return true;
}

bool VuMemorySection::Restore(uint32_t tag, const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(tag, payload, err)) return false;
    std::memcpy(Target(tag), payload.data(), payload.size());
    // A micro-memory store invalidates the decoded image, exactly as a VIF MPG
    // or an EE-window store does.
    if (tag == TAG_VU0C) vu0_.BumpMicroEpoch();
    if (tag == TAG_VU1C) vu1_.BumpMicroEpoch();
    if ((tag == TAG_VU1C || tag == TAG_VU1D) && on_vu1_restored_) on_vu1_restored_();
    ++restored_;
    return true;
}

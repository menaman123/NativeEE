#pragma once
// VuMemorySection: the four raw VU memory sections of a .ps2st file -- VU0C /
// VU0D (4096 bytes each: the VU0 micro and data memories) and VU1C / VU1D
// (16384 bytes each: the VU1 micro and data memories) -- over the two VuUnit
// records. One SectionSpec per tag, registered in the file order VU0C, VU0D,
// VU1C, VU1D. All four are REQUIRED: the reference loader fails the load when
// any memory section is missing or has a different length (its restore is a
// blind copy of exactly the memory's size).
//
// Wire layout: the memory bytes, nothing else (the reference writer's MemBlob
// of each memory vector). Restoring a micro memory invalidates every decoded
// image of that unit (the unit's micro epoch) and, for VU1, notifies the HLE
// runtime through the bound callback so its external generation moves too.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class VuUnit;

class VuMemorySection {
public:
    using OnVu1Restored = std::function<void()>;

    static constexpr size_t kVu0Bytes = 4096;
    static constexpr size_t kVu1Bytes = 16384;

    VuMemorySection(VuUnit& vu0, VuUnit& vu1);

    // Runs after a VU1C or VU1D restore (the root binds the HLE runtime's
    // BumpExternalGeneration).
    void SetOnVu1Restored(OnVu1Restored fn) { on_vu1_restored_ = std::move(fn); }

    // Registers VU0C, VU0D, VU1C, VU1D in that order.
    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec(uint32_t tag);

    // The exact payload length of `tag` (0 for a tag this codec does not own).
    static size_t PayloadBytes(uint32_t tag);
    void Serialize(uint32_t tag, std::vector<uint8_t>& out) const;
    static bool Validate(uint32_t tag, const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(uint32_t tag, const std::vector<uint8_t>& payload, std::string* err);

    // Sections restored since construction (run.meta evidence).
    unsigned RestoredCount() const { return restored_; }

private:
    const uint8_t* Source(uint32_t tag) const;
    uint8_t* Target(uint32_t tag);

    VuUnit& vu0_;
    VuUnit& vu1_;
    OnVu1Restored on_vu1_restored_;
    unsigned restored_ = 0;
};

#pragma once
// KernelSections: the SEMA and EVFL savestate sections over Semaphores and
// EventFlags (the kernel object tables).
//
// Wire layouts (frozen; the anchor states carry them):
//   SEMA: u32 count; count x { i32 sid; i32 count; i32 max_count;
//                              i32 init_count; u32 attr; u32 option }
//   EVFL: u32 count; count x { i32 eid; u32 attr; u32 option; u32 init_bits;
//                              u32 bits }
// Wait queues are not part of the file: a state is captured only with every
// guest thread parked outside an object wait (the capture admissibility
// rule), so the tables restore with empty waiter lists.
// Serialize emits restored ids first in file order, then newer ids
// ascending, so a restored image re-serializes byte-identical.
// Alarms have no section: the original never persisted the alarm table.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class Semaphores;
class EventFlags;

class KernelSections {
public:
    KernelSections(Semaphores& semas, EventFlags& flags);

    // Registers SEMA then EVFL.
    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec SemaSpec();
    ps2r::savestate::SectionSpec EvflSpec();

    void SerializeSema(std::vector<uint8_t>& out) const;
    void SerializeEvfl(std::vector<uint8_t>& out) const;
    static bool ValidateSema(const std::vector<uint8_t>& payload, std::string* err);
    static bool ValidateEvfl(const std::vector<uint8_t>& payload, std::string* err);
    bool RestoreSema(const std::vector<uint8_t>& payload, std::string* err);
    bool RestoreEvfl(const std::vector<uint8_t>& payload, std::string* err);

private:
    static constexpr uint32_t kMaxObjects = 4096;
    Semaphores& semas_;
    EventFlags& flags_;
    std::vector<int> sema_order_;  // restored ids, file order
    std::vector<int> evfl_order_;
};

#pragma once
// IntcSection: the INTC savestate section over Intc (STAT / MASK) and the
// SyscallTable handler registry (AddIntcHandler records).
//
// Wire layout (frozen; the anchor states carry it):
//   u32 stat; u32 mask; i32 next_handler_id; u32 count;
//   count x { i32 id; i32 cause; u32 pc; u32 gp; u32 arg; i32 flag; u8 active }
// Records are emitted grouped by cause ascending, stable within a cause
// (the original kept one queue per cause in a sorted map), which is also the
// dispatch order; a restored image re-serializes byte-identical.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class Intc;
class SyscallTable;

class IntcSection {
public:
    IntcSection(Intc& intc, SyscallTable& syscalls);

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    void Serialize(std::vector<uint8_t>& out) const;
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

private:
    static constexpr uint32_t kMaxHandlers = 1024;
    static constexpr size_t kRecordBytes = 25;
    Intc& intc_;
    SyscallTable& syscalls_;
};

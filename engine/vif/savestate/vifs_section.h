#pragma once
// VifsSection: the VIFS savestate section -- both VIF units' register files
// -- over Vif. REQUIRED (the reference loader reads it unconditionally).
//
// Wire layout (184 bytes, little-endian): VifRegs for VIF0 then VIF1, each
// the frozen 23-word (92-byte) record in field order -- STAT, FBRST, ERR, MARK,
// CYCLE, MODE, NUM, MASK, CODE, ITOPS, BASE, OFST, TOPS, ITOP, TOP, R0-R3,
// C0-C3 (vif_regs.h pins the size). A state is captured with both command
// state machines idle, so only the registers persist: restore copies both
// records and resets each unit's transient state (parser state, word latch,
// FIFO, DIRECT buffer, pending command) without touching the registers.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class Vif;

class VifsSection {
public:
    static constexpr size_t kUnitBytes = 92;
    static constexpr size_t kPayloadBytes = 2 * kUnitBytes;

    explicit VifsSection(Vif& vif);

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    void Serialize(std::vector<uint8_t>& out) const;
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

    unsigned RestoredCount() const { return restored_; }

private:
    Vif& vif_;
    unsigned restored_ = 0;
};

#pragma once
// PadsSection: the PADS savestate section -- the pad HLE's host-side state
// over PadProtocol: the pad area each port opened and when (in vsync
// boundaries), the staging each port had reached, the active-low word of the
// last stamp, whether each port's slots hold a full READY image, and the
// title's readiness event. The slots' CONTENTS live in guest RAM and travel
// with the memory sections. OPTIONAL: a file without PADS puts the protocol
// at boot (no port open, idle wire, no readiness event), so libpad's PortOpen
// restarts the staging as a fresh boot does.
//
// Wire layout (166 bytes, little-endian, the original writer's pad section;
// its field order and widths are kept so the frozen anchors load unchanged):
//   +0x00 u32 area[0]      +0x04 u32 area[1]        the ports' 256-byte pad areas (0 = closed)
//   +0x08 u32 opened_at[0] +0x0C u32 opened_at[1]   PortOpen boundary (low 32 bits of the u64 clock)
//   +0x10 u8  stage[0]     +0x11 u8  stage[1]       PadProtocol::Stage: 0 DISCONN, 1 CONFIG, 2 READY
//   +0x12 u16 last_wire                             the last stamp's active-low button word
//   +0x14 u8  ready_stamped[0]  +0x15 [1]           1 = the slots hold a full READY image
//   +0x16 u32 readiness_event                       the title's readiness boundary (0 = none)
//   +0x1A u32 read_count[0]  +0x1E u32 read_count[1]   |
//   +0x22 u16 latch_bits[0]  +0x24 u16 latch_bits[1]    | the original's press latch (140 bytes)
//   +0x26 u32 latch_read_at[2][16]                     /
// The first 26 bytes are modelled here. The 140 latch bytes belong to a press
// latch this tree does not implement (decision P4-PAD-LATCH-DELETED: libpad
// reports the wire as sampled; no host-side hold-over across reads), so they
// are WRITTEN at the latch's boot values -- all zero -- and NEVER consumed on
// restore: Validate checks their presence (the length), not their content.
// A re-save of a loaded anchor therefore reproduces the 26 modelled bytes and
// zeros the latch bytes (EQUIVALENCE_PLAN 7.1 P4-PADS-LATCH-BYTES).
//
// The slot sequence counter (+0x58 of each slot) is NOT a field of this
// section: on the console it is PADMAN's frame counter, and the only place a
// saved console state keeps it is the pad area itself (the slot words in guest
// RAM, which travel with the memory sections). Restore therefore DERIVES the
// protocol's counter from the restored image -- slot 0's +0x58 word of the
// first open port (slot 1 carries that value + 1) -- so the next stamp
// publishes image + 1, the value the console's PADMAN would land. The format is
// unchanged (166 bytes); every frozen anchor decodes the console's value.
// SeqRestore::Restart is the one-line toggle back to the original's behaviour
// (its counter was a process static that restarted at 1 after every load).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/pad/hle/pad_protocol.h"

class PadsSection {
public:
    static constexpr size_t kModelledBytes = 2 * 4 + 2 * 4 + 2 + 2 + 2 + 4;   // 26
    static constexpr size_t kLatchBytes = 2 * 4 + 2 * 2 + 2 * 16 * 4;         // 140
    static constexpr size_t kPayloadBytes = kModelledBytes + kLatchBytes;    // 166

    // One aligned u32 read of guest RAM (the pad area's slot words).
    using Reader = std::function<uint32_t(uint32_t addr)>;
    // Where the slot sequence counter continues from after a restore.
    enum class SeqRestore : uint8_t {
        FromImage = 0,   // the console: the restored slot 0 word of the first open port
        Restart,         // the original: the counter restarts at 1
    };

    // `read32` null = no derivation (the counter is left as it is).
    explicit PadsSection(PadProtocol& protocol, Reader read32 = nullptr,
                         SeqRestore seq_restore = SeqRestore::FromImage);

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    // The protocol's boot state for the fields this section owns: both ports
    // closed at DISCONN with no READY image, wire idle, no readiness event,
    // sequence counter at its boot value.
    void SeedBootDefault();

    void Serialize(std::vector<uint8_t>& out) const;
    // Exact length; stage in {0, 1, 2}; each area 0 or a 256-byte pad area
    // inside EE main RAM (KUSEG/KSEG0/KSEG1). The latch bytes are not inspected.
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

    // The counter the last Restore derived (0 = none: no open port, no reader,
    // or SeqRestore::Restart).
    uint32_t DerivedSeq() const { return derived_seq_; }
    SeqRestore seq_restore() const { return seq_restore_; }

private:
    struct Fields {
        uint32_t area[PadProtocol::kPorts] = {};
        uint32_t opened_at[PadProtocol::kPorts] = {};
        uint8_t stage[PadProtocol::kPorts] = {};
        uint16_t last_wire = 0xFFFF;
        uint8_t ready_stamped[PadProtocol::kPorts] = {};
        uint32_t readiness_event = 0;
    };
    // The 26 modelled bytes; false on a short payload.
    static bool Decode(const std::vector<uint8_t>& payload, Fields* f);
    static bool AreaPlausible(uint32_t area);

    PadProtocol& protocol_;
    Reader read32_;
    SeqRestore seq_restore_;
    uint32_t derived_seq_ = 0;
};

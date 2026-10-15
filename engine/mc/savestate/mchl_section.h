#pragma once
// MchlSection: the MCHL savestate section -- the libmc HLE statics that are
// guest-visible across a load -- over McCommandModel. OPTIONAL: a file
// without MCHL leaves the model in its boot state (no pending completion,
// no card seen), so the first sceMcGetInfo after such a load reports the
// card-detection edge again, as a fresh boot does.
//
// Wire layout (12 bytes, little-endian, the reference savestate writer's
// libmc section: i32 pending cmd, i32 pending result, then u8 seen[port][slot]
// for ports 0..1 x slots 0..1, 1 = the (port, slot) has answered a GetInfo):
//   +0x00 i32 pending_cmd      the undelivered completion's command code
//   +0x04 i32 pending_result   ... and its result, consumed by the next sceMcSync
//   +0x08 u8  seen[0][0]  +0x09 seen[0][1]  +0x0A seen[1][0]  +0x0B seen[1][1]
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class McCommandModel;

class MchlSection {
public:
    static constexpr size_t kPayloadBytes = 4 + 4 + 2 * 2;

    explicit MchlSection(McCommandModel& model);

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    void Serialize(std::vector<uint8_t>& out) const;
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

private:
    McCommandModel& model_;
};

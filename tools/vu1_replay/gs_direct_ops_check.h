#pragma once
// The `direct=` proof of the HLE oracle: a raw GIF packet converted to the
// typed register-op stream the renderer's direct path consumes (PacketToOps),
// projected back to (address, value) writes (OpsToWrites) and required to
// equal the byte decoder's canonical writes exactly. Pure GIFtag arithmetic:
// PACKED items keep both 64-bit halves for the PACKED-layout registers (PRIM
// RGBAQ ST UV XYZF2 XYZ2 FOG XYZF3 XYZ3), A+D and every other descriptor route
// the low 64 bits as a REGLIST-mode write, NOP is dropped; REGLIST items are
// one 64-bit each (A+D / NOP descriptors skipped, odd counts pad to a QW);
// NLOOP=0 keeps only EOP; IMAGE / DISABLE tags and truncated payloads are
// not convertible (the runtime submits such a kick raw).
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "tools/vu1_replay/gs_packet_diff.h"

class GsDirectOpsCheck {
public:
    struct Op {
        uint64_t lo = 0;
        uint64_t hi = 0;
        uint16_t reg = 0;
    };
    static constexpr uint16_t kModeReglist = 0x100;  // reg = kModeReglist | gs_register
    static constexpr uint16_t kOpFlush = 0xFFFF;     // GIFtag EOP boundary

    static bool PacketToOps(const uint8_t* p, size_t n, std::vector<Op>& out);
    static void OpsToWrites(const std::vector<Op>& ops, std::vector<std::pair<uint8_t, uint64_t>>& w);

    // The projected writes of `ops` equal `decoded` (which must be error-free) exactly.
    static bool Matches(const std::vector<std::pair<uint8_t, uint64_t>>& typed, const GsPacketDiff::Stream& decoded);
};

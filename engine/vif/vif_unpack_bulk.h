#pragma once
// The two bulk UNPACK lanes for VIF1: whole-command kernels for the shapes
// the title uses most (mode 0, unmasked). Each lane declines (returns false)
// when the payload is shorter than the command needs; the scalar loop then
// keeps the partial-read semantics. Both lanes are byte-identical to the scalar
// reference (VifUnpack::Execute) -- the equivalence arm proves it. NEON bodies
// under PS2R_SIMD, scalar bodies otherwise.
#include <cstddef>
#include <cstdint>

class VifUnpackBulk {
public:
    // Lane A: CL == WL (every cycle writes), formats V3-32 (0x8), V4-32 (0xC),
    // V4-16 (0xD), V4-8 (0xE). Writes `n` consecutive quadwords from
    // `addr_qw` (wrapping at 1024) into `dmem` (the 16 KB VU1 data memory).
    static bool Unpack(uint32_t format, bool usn, uint32_t addr_qw, uint32_t n,
                       const uint8_t* payload, size_t payload_size, uint8_t phase, uint8_t* dmem);

    // Lane B: WL == 1, CL > 1 (write one, skip CL-1), formats V3-32, V4-32,
    // V4-8. Vector k lands at (addr_qw + k*cl) & 0x3FF.
    static bool UnpackSkip(uint32_t format, bool usn, uint32_t addr_qw, uint32_t n,
                           const uint8_t* payload, size_t payload_size, uint8_t phase, uint32_t cl,
                           uint8_t* dmem);
};

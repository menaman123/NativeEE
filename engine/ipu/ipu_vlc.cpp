#include "engine/ipu/ipu_vlc.h"

#include <cstddef>

namespace ps2r::ipu {
namespace {

struct Code {
    uint16_t bits;
    uint8_t length;
    int16_t value;
};

template <size_t N>
uint32_t Match(uint32_t bits, const Code (&codes)[N]) {
    for (const auto& code : codes) {
        if ((bits >> (32 - code.length)) == code.bits)
            return (static_cast<uint32_t>(code.length) << 16) | static_cast<uint16_t>(code.value);
    }
    return 0;
}

// Numeric VLC assignments from EE User's Manual 8.5.3, pp. 202-204.
constexpr Code kAddress[] = {
    {1,1,1}, {3,3,2}, {2,3,3}, {3,4,4}, {2,4,5}, {3,5,6}, {2,5,7},
    {7,7,8}, {6,7,9}, {11,8,10}, {10,8,11}, {9,8,12}, {8,8,13}, {7,8,14}, {6,8,15},
    {23,10,16}, {22,10,17}, {21,10,18}, {20,10,19}, {19,10,20}, {18,10,21},
    {35,11,22}, {34,11,23}, {33,11,24}, {32,11,25}, {31,11,26}, {30,11,27},
    {29,11,28}, {28,11,29}, {27,11,30}, {26,11,31}, {25,11,32}, {24,11,33},
    {8,11,35}
};
constexpr Code kIntra[] = {{1,1,1}, {1,2,17}};
constexpr Code kPredicted[] = {
    {1,1,10}, {1,2,2}, {1,3,8}, {3,5,1}, {2,5,26}, {1,5,18}, {1,6,17}
};
constexpr Code kBidirectional[] = {
    {2,2,12}, {3,2,14}, {2,3,4}, {3,3,6}, {2,4,8}, {3,4,10},
    {3,5,1}, {2,5,30}, {3,6,26}, {2,6,22}, {1,6,17}
};
constexpr Code kDcOnly[] = {{1,1,1}};
// Positive motion codes; the final bit selects the sign for nonzero values.
constexpr Code kMotion[] = {
    {1,1,0}, {2,3,1}, {2,4,2}, {2,5,3}, {6,7,4}, {10,8,5}, {8,8,6},
    {6,8,7}, {22,10,8}, {20,10,9}, {18,10,10}, {34,11,11}, {32,11,12},
    {30,11,13}, {28,11,14}, {26,11,15}, {24,11,16}
};
constexpr Code kDmVector[] = {{0,1,0}, {2,2,1}, {3,2,-1}};

}  // namespace

uint32_t IpuVlc::Decode(uint32_t bits, unsigned table, unsigned picture_type, bool mpeg1) {
    switch (table) {
        case 0:
            if (mpeg1 && (bits >> 21) == 15) return 0x000b0022u;
            return Match(bits, kAddress);
        case 1:
            switch (picture_type) {
                case 1: return Match(bits, kIntra);
                case 2: return Match(bits, kPredicted);
                case 3: return Match(bits, kBidirectional);
                case 4: return Match(bits, kDcOnly);
                default: return 0;
            }
        case 2:
            for (const auto& code : kMotion) {
                const uint32_t prefix = bits >> (32 - code.length);
                if (prefix == code.bits || (code.value && prefix == (code.bits | 1u))) {
                    const int16_t value = (code.value && (prefix & 1)) ? -code.value : code.value;
                    return (static_cast<uint32_t>(code.length) << 16) | static_cast<uint16_t>(value);
                }
            }
            return 0;
        case 3: return Match(bits, kDmVector);
        default: return 0;
    }
}

}  // namespace ps2r::ipu

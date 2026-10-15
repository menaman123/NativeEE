// core/hash/hash.cpp -- FNV-1a64 and an independent xxHash64 implementation.
#include "engine/core/hash/hash.h"

#include <cstring>

namespace ps2r {
namespace hash {

uint64_t Fnv1a64(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = seed;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= kFnv1a64Prime;
    }
    return h;
}

namespace {

// xxHash64 constants from the published specification.
constexpr uint64_t kPrime1 = 0x9E3779B185EBCA87ull;
constexpr uint64_t kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr uint64_t kPrime3 = 0x165667B19E3779F9ull;
constexpr uint64_t kPrime4 = 0x85EBCA77C2B2AE63ull;
constexpr uint64_t kPrime5 = 0x27D4EB2F165667C5ull;

inline uint64_t RotateLeft(uint64_t v, unsigned bits) {
    return (v << bits) | (v >> (64u - bits));
}

inline uint64_t LoadLe64(const uint8_t* p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) v |= static_cast<uint64_t>(p[i]) << (8u * i);
    return v;
}

inline uint64_t LoadLe32(const uint8_t* p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++) v |= static_cast<uint32_t>(p[i]) << (8u * i);
    return v;
}

// One lane step: fold a 64-bit word into an accumulator.
inline uint64_t LaneStep(uint64_t lane, uint64_t word) {
    lane += word * kPrime2;
    lane = RotateLeft(lane, 31);
    return lane * kPrime1;
}

// Fold one finished lane into the converged hash.
inline uint64_t FoldLane(uint64_t h, uint64_t lane) {
    h ^= LaneStep(0, lane);
    return h * kPrime1 + kPrime4;
}

// Final avalanche.
inline uint64_t Avalanche(uint64_t h) {
    h ^= h >> 33;
    h *= kPrime2;
    h ^= h >> 29;
    h *= kPrime3;
    h ^= h >> 32;
    return h;
}

}  // namespace

uint64_t Xxh64(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t remaining = len;
    uint64_t h;

    if (remaining >= 32) {
        // Four independent lanes consume 32-byte stripes.
        uint64_t lanes[4] = {seed + kPrime1 + kPrime2, seed + kPrime2, seed,
                             seed - kPrime1};
        while (remaining >= 32) {
            for (unsigned i = 0; i < 4; i++) {
                lanes[i] = LaneStep(lanes[i], LoadLe64(p));
                p += 8;
            }
            remaining -= 32;
        }
        h = RotateLeft(lanes[0], 1) + RotateLeft(lanes[1], 7) +
            RotateLeft(lanes[2], 12) + RotateLeft(lanes[3], 18);
        for (unsigned i = 0; i < 4; i++) h = FoldLane(h, lanes[i]);
    } else {
        h = seed + kPrime5;
    }

    h += static_cast<uint64_t>(len);

    // Tail: whole 8-byte words, then one 4-byte word, then single bytes.
    while (remaining >= 8) {
        h ^= LaneStep(0, LoadLe64(p));
        h = RotateLeft(h, 27) * kPrime1 + kPrime4;
        p += 8;
        remaining -= 8;
    }
    if (remaining >= 4) {
        h ^= LoadLe32(p) * kPrime1;
        h = RotateLeft(h, 23) * kPrime2 + kPrime3;
        p += 4;
        remaining -= 4;
    }
    while (remaining > 0) {
        h ^= static_cast<uint64_t>(*p) * kPrime5;
        h = RotateLeft(h, 11) * kPrime1;
        p++;
        remaining--;
    }
    return Avalanche(h);
}

}  // namespace hash
}  // namespace ps2r

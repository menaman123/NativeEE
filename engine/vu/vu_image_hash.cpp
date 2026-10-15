// VuImageHash -- the frozen 4-lane FNV-1a image hash (rules in the header).
#include "engine/vu/vu_image_hash.h"

#include <cstring>

namespace {

inline uint64_t Le64(const uint8_t* p) {
    uint64_t w;
    std::memcpy(&w, p, sizeof(w));  // host is little-endian; the byte order is the file order
    return w;
}

}  // namespace

uint64_t VuImageHash::Fast(const uint8_t* p, size_t n) {
    constexpr uint64_t kPrime = 1099511628211ull;
    uint64_t h0 = 1469598103934665603ull;
    uint64_t h1 = h0 ^ 0x9E3779B97F4A7C15ull;
    uint64_t h2 = h0 + 0x517CC1B727220A95ull;
    uint64_t h3 = h0 ^ 0xFF51AFD7ED558CCDull;
    const size_t n32 = n & ~static_cast<size_t>(31);
    size_t i = 0;
    for (; i < n32; i += 32) {
        h0 = (h0 ^ Le64(p + i)) * kPrime;
        h1 = (h1 ^ Le64(p + i + 8)) * kPrime;
        h2 = (h2 ^ Le64(p + i + 16)) * kPrime;
        h3 = (h3 ^ Le64(p + i + 24)) * kPrime;
    }
    // Trailing whole 8-byte words feed lane 0 only; a final partial word is ignored.
    for (; i + 8 <= n; i += 8) h0 = (h0 ^ Le64(p + i)) * kPrime;
    uint64_t h = h0;
    h = (h ^ h1) * kPrime;
    h = (h ^ h2) * kPrime;
    h = (h ^ h3) * kPrime;
    return h;
}

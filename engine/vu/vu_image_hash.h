#pragma once
// The VU1 micro-program image hash: the cached tier's program key and the
// registry / catalog identity of every recorded image (image, prefix and
// variant hashes are all this function over the respective byte range).
// A 4-lane interleaved FNV-1a over little-endian 64-bit words with a fixed
// lane fold; the value is frozen by the recorded catalogs, so the algorithm
// is carried verbatim (it is NOT engine/core/hash's FNV-1a).
#include <cstddef>
#include <cstdint>

class VuImageHash {
public:
    static uint64_t Fast(const uint8_t* p, size_t n);
};

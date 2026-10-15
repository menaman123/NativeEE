// gif/gs_sink.cpp -- GsSink's one default body: an IMAGE span as its per-quadword
// ImageData sequence (see gs_sink.h).
#include "engine/gif/gs_sink.h"

#include <cstring>

void GsSink::ImageSpan(const uint8_t* data, size_t qwords) {
    for (size_t i = 0; i < qwords; ++i, data += 16) {
        uint64_t lo, hi;
        std::memcpy(&lo, data, 8);
        std::memcpy(&hi, data + 8, 8);
        ImageData(lo, hi);
    }
}

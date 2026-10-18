// gs/vram/page_occupancy.cpp -- PageOccupancy (see .h).
#include "engine/gs/vram/page_occupancy.h"

namespace ps2r {
namespace gs {

void PageOccupancy::Fold(uint32_t start, uint32_t size, uint32_t* first, uint32_t* count) {
    constexpr uint64_t kImage = uint64_t(kPageBytes) * kPageCount;   // 4 MiB
    if (size == 0) {
        *first = 0;
        *count = 0;
        return;
    }
    const uint64_t a0 = start % kImage;
    const uint64_t a1 = a0 + size;                     // may exceed the image: the wrap
    const uint64_t p0 = a0 / kPageBytes;
    const uint64_t p1 = (a1 + kPageBytes - 1) / kPageBytes;   // exclusive
    const uint64_t n = p1 - p0;
    *first = static_cast<uint32_t>(p0);
    *count = n >= kPageCount ? kPageCount : static_cast<uint32_t>(n);
}

void PageOccupancy::Add(uint32_t start, uint32_t size) {
    uint32_t first, n;
    Fold(start, size, &first, &n);
    if (n == 0) return;
    for (uint32_t i = 0; i < n; ++i) ++count_[(first + i) % kPageCount];
    ++live_;
}

void PageOccupancy::Remove(uint32_t start, uint32_t size) {
    uint32_t first, n;
    Fold(start, size, &first, &n);
    if (n == 0) return;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t& c = count_[(first + i) % kPageCount];
        if (c > 0) --c;
    }
    if (live_ > 0) --live_;
}

bool PageOccupancy::AnyIn(uint32_t start, uint32_t size) const {
    if (live_ == 0) return false;
    uint32_t first, n;
    Fold(start, size, &first, &n);
    for (uint32_t i = 0; i < n; ++i)
        if (count_[(first + i) % kPageCount] != 0) return true;
    return false;
}

void PageOccupancy::Clear() {
    count_.fill(0);
    live_ = 0;
}

}  // namespace gs
}  // namespace ps2r

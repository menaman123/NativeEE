// core/verify/memory_checkpointer.cpp
#include "engine/core/verify/memory_checkpointer.h"

#include "engine/core/hash/hash.h"

namespace ps2r::verify {

uint64_t MemoryCheckpointer::Hash(const std::vector<MemorySpan>& spans) {
    uint64_t h = hash::kFnv1a64Offset;
    for (const MemorySpan& s : spans)
        if (s.data && s.size) h = hash::Fnv1a64(s.data, s.size, h);
    return h;
}

bool MemoryCheckpointer::MaybeHash(uint32_t boundary, const std::vector<MemorySpan>& spans,
                                   uint64_t* out_hash) const {
    if (boundary % kEveryBoundaries != 0) return false;
    *out_hash = Hash(spans);
    return true;
}

}  // namespace ps2r::verify

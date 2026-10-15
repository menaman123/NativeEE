// core/verify/memory_checkpointer.h -- FNV-1a64 of guest memory at the
// quiescent frame boundary every N boundaries (EQUIVALENCE_PLAN section
// 1.4 / 2, mem.mhash).
//
// Hashed spans, in order: main RAM (32 MB), scratchpad (16 KB), the HLE meta
// arena (2 MB) -- supplied by the memory owner (ee/mem, Phase 3) as a span
// list so this module has no global memory coupling. Runs on the boundary
// thread where every guest fiber is parked, so the read is quiescent by
// construction. A read-only hash, not the savestate CRC path: it needs no
// drain and touches nothing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2r::verify {

struct MemorySpan {
    const uint8_t* data = nullptr;
    size_t         size = 0;
};

class MemoryCheckpointer {
public:
    static constexpr uint32_t kEveryBoundaries = 60;

    // Hash of the spans, chained in order from the FNV offset.
    static uint64_t Hash(const std::vector<MemorySpan>& spans);

    // Called at every boundary; hashes when boundary % kEveryBoundaries == 0
    // and returns true with *out_hash set.
    bool MaybeHash(uint32_t boundary, const std::vector<MemorySpan>& spans, uint64_t* out_hash) const;
};

}  // namespace ps2r::verify

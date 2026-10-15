#pragma once
// HeapSection: the HEAP savestate section -- the host-side allocator
// bookkeeping a restored image needs so that every allocation made AFTER the
// restore lands where the saved run's would have (BUG-085: without it the
// first routed metadata allocation re-vends cell 0 of the META arena, which
// the restored title still holds as its live compaction-job object; the
// compactor then copies through garbage descriptors and the title's allocator
// bins are overwritten -- a resumed cold-boot image never reaches a boundary).
//
// Wire layout (frozen; the anchor states carry it), two parts on one payload:
//   1. the original tree's HLE-heap bookkeeping, all u32 little-endian:
//        heap_start, heap_end, current_top, current_bottom,
//        n_free,  n_free  x { addr, size },
//        n_alloc, n_alloc x { addr, size }
//      This runtime keeps no such heap: on restore the part is parsed
//      structurally and skipped; on save it is written EMPTY (four zero words,
//      n_free = 0, n_alloc = 0), which the original grammar accepts.
//   2. the tail: the META arena's bookkeeping (capacity, state bytes, bump,
//      quarantine, counters), owned by the arena codec the composition root
//      supplies -- the engine never names the title's arena.
// The section is OPTIONAL: an image without it restores with the arena's
// bookkeeping untouched (the pre-fix behaviour, kept for images that never
// carried one).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class HeapSection {
public:
    // The tail codec (the META arena's SerializeState / ValidateState /
    // RestoreState shape): validate/restore read from `p` (`len` bytes) and
    // report how many bytes they consumed.
    struct TailCodec {
        std::function<void(std::vector<uint8_t>& out)> serialize;
        std::function<bool(const uint8_t* p, size_t len, size_t* consumed, std::string* err)> validate;
        std::function<bool(const uint8_t* p, size_t len, size_t* consumed)> restore;
    };

    explicit HeapSection(TailCodec tail);

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    // Length of part 1 (the original HLE-heap bookkeeping) at the payload's
    // start; false (with `err`) when its grammar does not parse.
    static bool PrefixLength(const std::vector<uint8_t>& payload, size_t* len, std::string* err);
    // Part 1 as this runtime writes it (the empty heap).
    static void EmitEmptyPrefix(std::vector<uint8_t>& out);

    void Serialize(std::vector<uint8_t>& out) const;
    bool Validate(const std::vector<uint8_t>& payload, std::string* err) const;
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

    // Number of HEAP sections restored so far (run.meta `heap_section_restored`).
    size_t RestoredCount() const { return restored_; }

private:
    TailCodec tail_;
    size_t restored_ = 0;
};

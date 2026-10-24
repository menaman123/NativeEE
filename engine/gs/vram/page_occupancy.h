// gs/vram/page_occupancy.h -- PageOccupancy: how many recorded byte ranges cover
// each 8 KiB page of GS local memory.
//
// The texture-side records (decoded views, uploaded textures, owned slots) are
// evicted by VRAM range after every committed draw and every EE write; each
// record set used to scan all of its entries per draw (thousands of draws per
// frame x hundreds of entries: a quarter of the GS consumer thread). This ledger
// answers "does ANY record cover a page of [start, start+size)?" in at most 512
// counter reads, so the exact scan runs only when it can evict something.
//
// Conservative by construction: a record's footprint marks every page it
// touches modulo the 4 MiB image (a range that wraps marks both ends; a range of
// 4 MiB or more marks every page), the same fold for the query -- two byte
// ranges that overlap (with or without the wrap) always share a page, so a
// false `AnyIn` is impossible; a true `AnyIn` only means the caller's own
// overlap test must decide, as before.
#pragma once

#include <array>
#include <cstdint>

namespace ps2r {
namespace gs {

class PageOccupancy {
public:
    static constexpr uint32_t kPageBytes = 8192;
    static constexpr uint32_t kPageCount = 512;

    // A record's footprint enters / leaves the ledger (Remove exactly once per Add,
    // with the same range).
    void Add(uint32_t start, uint32_t size);
    void Remove(uint32_t start, uint32_t size);
    // Some record covers a page of [start, start+size). False for an empty range or
    // an empty ledger.
    bool AnyIn(uint32_t start, uint32_t size) const;
    bool Empty() const { return live_ == 0; }
    uint32_t Live() const { return live_; }
    void Clear();

    // The page fold both sides use: the first page of `start` modulo the image and
    // the number of consecutive pages (modulo kPageCount) the range touches, at most
    // kPageCount. 0 pages for an empty range.
    static void Fold(uint32_t start, uint32_t size, uint32_t* first, uint32_t* count);

private:
    std::array<uint32_t, kPageCount> count_{};
    uint32_t live_ = 0;   // records added and not removed
};

}  // namespace gs
}  // namespace ps2r

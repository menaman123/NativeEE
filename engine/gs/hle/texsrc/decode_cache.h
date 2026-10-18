// gs/hle/texsrc/decode_cache.h -- DecodeCache: the two caches of decoded textures.
//
//   L1 -- keyed by the ContentKey of the current decode inputs; one entry per
//         key with its texture, dimensions and the VRAM footprint it was decoded
//         from (EvictInRange drops entries whose footprint an EE upload touched).
//   L2 -- keyed by the same content key; every finished decode is recorded here
//         too, so the title's double-buffered uploads (identical bytes at a
//         rotated address) and content evicted from L1 by an upload re-hit
//         without a decode. Bounded by an entry cap and a byte budget (w*h*4
//         texels), LRU. A live insert enters PROBATION when the caller flags the
//         content as a single-use suspect (the register tuple's bytes moved on
//         while the previous generation is still recorded): a probation entry
//         counts against a smaller single-use pool (entries + bytes) and, when
//         that pool overflows, the OLDEST unrescued probation entry is evicted
//         first. A lookup hit earns tenure (leaves the pool).
//   Ownership: a surface recorded in both caches is destroyed when the LAST of
//   the two releases it -- an L1 eviction keeps the surface while L2 holds it
//   (and vice versa); DropAll destroys everything once.
#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>

#include "engine/gs/vram/page_occupancy.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class DecodeCache {
public:
    static constexpr size_t kL2MaxEntries = 16384;
    static constexpr uint64_t kL2MaxBytes = 320ull * 1024u * 1024u;
    static constexpr size_t kL2SuMaxEntries = 2048;
    static constexpr uint64_t kL2SuMaxBytes = 32ull * 1024u * 1024u;

    struct L1Entry {
        render::SurfaceId tex;
        int w = 0, h = 0;
        uint32_t fp_start = 0, fp_size = 0;   // VRAM footprint (eviction by range)
        uint64_t last_use = 0;
    };

    explicit DecodeCache(render::Backend& be);
    ~DecodeCache();

    // ---- L1 --------------------------------------------------------------------
    const L1Entry* L1Find(uint64_t key, int w, int h);
    void L1Insert(uint64_t key, render::SurfaceId tex, int w, int h, uint32_t fp_start,
                  uint32_t fp_size);
    // Drop L1 entries whose footprint overlaps [start, start+size); a surface L2
    // still records survives the eviction. The per-page ledger (`l1_pages_`) says
    // in O(pages) whether any entry can overlap; the exact scan runs only then.
    void EvictInRange(uint32_t start, uint32_t size);
    size_t L1Size() const { return l1_.size(); }
    bool L1Contains(uint64_t key) const { return l1_.count(key) != 0; }

    // ---- L2 --------------------------------------------------------------------
    // Hit: returns the texture (dims must match), bumps LRU, clears probation.
    render::SurfaceId L2Lookup(uint64_t content_hash, int w, int h);
    // Insert; `probation` = the single-use suspect flag (see the header comment);
    // enforces the single-use pool, then the cap / budget.
    void L2Insert(uint64_t content_hash, render::SurfaceId tex, int w, int h, bool probation);
    // Demote a tenured entry to probation (its tuple's content moved on).
    void L2MarkProbation(uint64_t content_hash);
    bool L2Contains(uint64_t content_hash) const { return l2_.count(content_hash) != 0; }
    bool L2IsProbation(uint64_t content_hash) const;
    size_t L2Size() const { return l2_.size(); }
    size_t L2ProbCount() const { return prob_count_; }
    uint64_t L2ProbBytes() const { return prob_bytes_; }
    uint64_t L2Bytes() const { return l2_bytes_; }
    void DropAll();

private:
    struct L2Entry {
        render::SurfaceId tex;
        int w = 0, h = 0;
        uint64_t bytes = 0;
        bool probation = false;
        std::list<uint64_t>::iterator lru;    // position in lru_
        std::list<uint64_t>::iterator su;     // position in su_ (valid iff probation)
    };
    void L2Drop(std::unordered_map<uint64_t, L2Entry>::iterator it);
    void L2EnforceSingleUse();
    void L2EnforceBudget();
    // Destroy `tex` unless the other cache still records it under `key`.
    void ReleaseFromL1(uint64_t key, render::SurfaceId tex);
    void ReleaseFromL2(uint64_t key, render::SurfaceId tex);

    render::Backend& be_;
    std::unordered_map<uint64_t, L1Entry> l1_;
    PageOccupancy l1_pages_;   // every L1 entry's footprint, by page
    uint64_t tick_ = 0;
    std::unordered_map<uint64_t, L2Entry> l2_;
    std::list<uint64_t> lru_;   // front = most recent
    std::list<uint64_t> su_;    // probation entries, front = newest
    uint64_t l2_bytes_ = 0;
    size_t prob_count_ = 0;
    uint64_t prob_bytes_ = 0;
};

}  // namespace gs
}  // namespace ps2r

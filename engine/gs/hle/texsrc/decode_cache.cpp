// gs/hle/texsrc/decode_cache.cpp -- DecodeCache.
#include "engine/gs/hle/texsrc/decode_cache.h"

#include <algorithm>
#include <vector>

namespace ps2r {
namespace gs {

DecodeCache::DecodeCache(render::Backend& be) : be_(be) {}

DecodeCache::~DecodeCache() { DropAll(); }

void DecodeCache::DropAll() {
    // A surface recorded in both caches is destroyed exactly once.
    for (auto& kv : l1_) {
        auto l2 = l2_.find(kv.first);
        const bool shared = l2 != l2_.end() && l2->second.tex.id == kv.second.tex.id;
        if (kv.second.tex.id && !shared) be_.DestroySurface(kv.second.tex);
    }
    l1_.clear();
    for (auto& kv : l2_)
        if (kv.second.tex.id) be_.DestroySurface(kv.second.tex);
    l2_.clear();
    lru_.clear();
    su_.clear();
    l2_bytes_ = prob_bytes_ = 0;
    prob_count_ = 0;
}

void DecodeCache::ReleaseFromL1(uint64_t key, render::SurfaceId tex) {
    if (!tex.id) return;
    auto l2 = l2_.find(key);
    if (l2 != l2_.end() && l2->second.tex.id == tex.id) return;   // L2 keeps the surface
    be_.DestroySurface(tex);
}

void DecodeCache::ReleaseFromL2(uint64_t key, render::SurfaceId tex) {
    if (!tex.id) return;
    auto l1 = l1_.find(key);
    if (l1 != l1_.end() && l1->second.tex.id == tex.id) return;   // L1 still holds the view
    be_.DestroySurface(tex);
}

const DecodeCache::L1Entry* DecodeCache::L1Find(uint64_t key, int w, int h) {
    auto it = l1_.find(key);
    if (it == l1_.end()) return nullptr;
    if (it->second.w != w || it->second.h != h) return nullptr;   // dims re-checked on every hit
    it->second.last_use = ++tick_;
    return &it->second;
}

void DecodeCache::L1Insert(uint64_t key, render::SurfaceId tex, int w, int h, uint32_t fp_start,
                           uint32_t fp_size) {
    L1Entry& e = l1_[key];
    if (e.tex.id && e.tex.id != tex.id) ReleaseFromL1(key, e.tex);
    e.tex = tex;
    e.w = w;
    e.h = h;
    e.fp_start = fp_start;
    e.fp_size = fp_size;
    e.last_use = ++tick_;
}

void DecodeCache::EvictInRange(uint32_t start, uint32_t size) {
    if (size == 0 || l1_.empty()) return;
    // Wrap-aware overlap of [start, start+size) with each entry's footprint on
    // the unwrapped [0, 8 MiB) line: a range crossing the 4 MiB boundary occupies
    // [x0, x1) with x1 > 4 MiB, so both images of each range are tested.
    constexpr uint64_t kVram = 4ull * 1024u * 1024u;
    const uint64_t a0 = start % kVram;
    const uint64_t a1 = a0 + std::min<uint64_t>(size, kVram);
    std::vector<uint64_t> gone;
    for (auto& kv : l1_) {
        const uint64_t b0 = kv.second.fp_start % kVram;
        const uint64_t b1 = b0 + std::min<uint64_t>(kv.second.fp_size, kVram);
        const bool overlap = (a0 < b1 && b0 < a1) || (a0 + kVram < b1) || (b0 + kVram < a1);
        if (overlap) gone.push_back(kv.first);
    }
    for (uint64_t k : gone) {
        auto it = l1_.find(k);
        const render::SurfaceId tex = it->second.tex;
        l1_.erase(it);                // unlink first: the L2 check must not see this view
        ReleaseFromL1(k, tex);
    }
}

bool DecodeCache::L2IsProbation(uint64_t key) const {
    auto it = l2_.find(key);
    return it != l2_.end() && it->second.probation;
}

void DecodeCache::L2Drop(std::unordered_map<uint64_t, L2Entry>::iterator it) {
    const uint64_t key = it->first;
    L2Entry& e = it->second;
    if (e.probation) {
        su_.erase(e.su);
        prob_count_--;
        prob_bytes_ -= e.bytes;
    }
    lru_.erase(e.lru);
    l2_bytes_ -= e.bytes;
    const render::SurfaceId tex = e.tex;
    l2_.erase(it);
    ReleaseFromL2(key, tex);
}

render::SurfaceId DecodeCache::L2Lookup(uint64_t key, int w, int h) {
    auto it = l2_.find(key);
    if (it == l2_.end()) return render::SurfaceId{};
    L2Entry& e = it->second;
    if (e.w != w || e.h != h) return render::SurfaceId{};
    lru_.erase(e.lru);
    lru_.push_front(key);
    e.lru = lru_.begin();
    if (e.probation) {   // rescue: tenure earned, leaves the single-use pool
        su_.erase(e.su);
        e.probation = false;
        prob_count_--;
        prob_bytes_ -= e.bytes;
    }
    return e.tex;
}

void DecodeCache::L2EnforceSingleUse() {
    while (!su_.empty() && (prob_count_ > kL2SuMaxEntries || prob_bytes_ > kL2SuMaxBytes)) {
        const uint64_t oldest = su_.back();   // the oldest unrescued probation entry
        L2Drop(l2_.find(oldest));
    }
}

void DecodeCache::L2EnforceBudget() {
    while (!lru_.empty() && (l2_.size() > kL2MaxEntries || l2_bytes_ > kL2MaxBytes)) {
        const uint64_t victim = lru_.back();
        L2Drop(l2_.find(victim));
    }
}

void DecodeCache::L2MarkProbation(uint64_t key) {
    auto it = l2_.find(key);
    if (it == l2_.end() || it->second.probation) return;
    L2Entry& e = it->second;
    e.probation = true;
    su_.push_front(key);
    e.su = su_.begin();
    prob_count_++;
    prob_bytes_ += e.bytes;
    L2EnforceSingleUse();
}

void DecodeCache::L2Insert(uint64_t key, render::SurfaceId tex, int w, int h, bool probation) {
    auto old = l2_.find(key);
    if (old != l2_.end()) {
        if (old->second.tex.id == tex.id) {   // the same surface re-recorded: unlink only
            old->second.tex = render::SurfaceId{};
        }
        L2Drop(old);
    }
    L2Entry e;
    e.tex = tex;
    e.w = w;
    e.h = h;
    e.bytes = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * 4u;
    e.probation = probation;
    lru_.push_front(key);
    e.lru = lru_.begin();
    l2_bytes_ += e.bytes;
    if (probation) {
        su_.push_front(key);
        e.su = su_.begin();
        prob_count_++;
        prob_bytes_ += e.bytes;
    }
    l2_[key] = e;
    L2EnforceSingleUse();
    L2EnforceBudget();
}

}  // namespace gs
}  // namespace ps2r

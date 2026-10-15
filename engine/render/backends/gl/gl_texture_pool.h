// render/backends/gl/gl_texture_pool.h -- GlTexturePool: recycled RGBA8 level-0 texture
// storage plus the mip-tail synthesis helper.
//
// A retired texture keeps its storage and drops a fence at the retire point; an
// acquire of the same dimensions prefers an entry whose fence has signalled (every
// command that referenced it has completed, so an in-place glTexSubImage2D refill is
// stall-free) and otherwise hands back a busy entry the caller must re-specify
// (glTexImage2D with data orphans the old allocation). The pool is bounded by entry
// and byte caps; oldest retirees are really deleted first.
#pragma once

#include <cstdint>
#include <vector>

namespace ps2r {
namespace render {

class GlStateCache;

class GlTexturePool {
public:
    static constexpr size_t kMaxEntries = 128;
    static constexpr uint64_t kMaxBytes = 96ull * 1024 * 1024;

    // Returns 0 on a miss. *storage_idle = true when the storage may be refilled in place.
    uint32_t Acquire(int width, int height, bool* storage_idle);
    void Retire(uint32_t tex, int width, int height, GlStateCache& st);
    void DropAll(GlStateCache& st);
    size_t Size() const { return entries_.size(); }
    uint64_t Bytes() const { return bytes_; }

    // 2x2 box average (round half up) of an RGBA8 image to the next smaller level;
    // a 1-wide or 1-tall axis collapses with step 1. Returns the new dims.
    static void BoxHalve(const std::vector<uint8_t>& src, int cw, int ch, std::vector<uint8_t>& dst,
                         int* nw, int* nh);

private:
    struct Entry {
        uint32_t tex = 0;
        uint16_t w = 0, h = 0;
        void* fence = nullptr;
        uint64_t retire_seq = 0;
    };
    std::vector<Entry> entries_;
    uint64_t bytes_ = 0;
    uint64_t seq_ = 0;
};

}  // namespace render
}  // namespace ps2r

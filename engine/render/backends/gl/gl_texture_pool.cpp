// render/backends/gl/gl_texture_pool.cpp -- GlTexturePool implementation.
#include "engine/render/backends/gl/gl_texture_pool.h"

#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

#include "engine/render/backends/gl/gl_state_cache.h"

namespace ps2r {
namespace render {

uint32_t GlTexturePool::Acquire(int width, int height, bool* storage_idle) {
    *storage_idle = false;
    if (width <= 0 || height <= 0) return 0;
    int found = -1;
    for (size_t i = 0; i < entries_.size(); i++) {
        Entry& e = entries_[i];
        if (e.w != static_cast<uint16_t>(width) || e.h != static_cast<uint16_t>(height)) continue;
        if (e.fence) {
            const GLenum r = glClientWaitSync(static_cast<GLsync>(e.fence), 0, 0);
            if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) {
                glDeleteSync(static_cast<GLsync>(e.fence));
                e.fence = nullptr;
            }
        }
        if (!e.fence) {
            found = static_cast<int>(i);
            *storage_idle = true;
            break;
        }
        if (found < 0) found = static_cast<int>(i);
    }
    if (found < 0) return 0;
    Entry taken = entries_[static_cast<size_t>(found)];
    entries_[static_cast<size_t>(found)] = entries_.back();
    entries_.pop_back();
    bytes_ -= static_cast<uint64_t>(taken.w) * taken.h * 4ull;
    if (taken.fence) glDeleteSync(static_cast<GLsync>(taken.fence));
    return taken.tex;
}

void GlTexturePool::Retire(uint32_t tex, int width, int height, GlStateCache& st) {
    if (tex == 0) return;
    if (width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF) {
        st.DeleteTextures(1, &tex);
        return;
    }
    Entry e;
    e.tex = tex;
    e.w = static_cast<uint16_t>(width);
    e.h = static_cast<uint16_t>(height);
    e.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    e.retire_seq = ++seq_;
    entries_.push_back(e);
    bytes_ += static_cast<uint64_t>(e.w) * e.h * 4ull;
    while (entries_.size() > kMaxEntries || bytes_ > kMaxBytes) {
        size_t oldest = 0;
        for (size_t i = 1; i < entries_.size(); i++)
            if (entries_[i].retire_seq < entries_[oldest].retire_seq) oldest = i;
        Entry& v = entries_[oldest];
        if (v.fence) glDeleteSync(static_cast<GLsync>(v.fence));
        st.DeleteTextures(1, &v.tex);
        bytes_ -= static_cast<uint64_t>(v.w) * v.h * 4ull;
        entries_[oldest] = entries_.back();
        entries_.pop_back();
    }
}

void GlTexturePool::DropAll(GlStateCache& st) {
    for (Entry& e : entries_) {
        if (e.fence) glDeleteSync(static_cast<GLsync>(e.fence));
        st.DeleteTextures(1, &e.tex);
    }
    entries_.clear();
    bytes_ = 0;
}

void GlTexturePool::BoxHalve(const std::vector<uint8_t>& src, int cw, int ch,
                             std::vector<uint8_t>& dst, int* nw, int* nh) {
    const int w = cw > 1 ? cw >> 1 : 1;
    const int h = ch > 1 ? ch >> 1 : 1;
    const int sx = cw > 1 ? 2 : 1;
    const int sy = ch > 1 ? 2 : 1;
    dst.assign(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 4; c++) {
                unsigned s = 0, cnt = 0;
                for (int dy = 0; dy < sy; dy++)
                    for (int dx = 0; dx < sx; dx++) {
                        const int px = x * sx + dx, py = y * sy + dy;
                        s += src[(static_cast<size_t>(py) * cw + px) * 4 + c];
                        cnt++;
                    }
                dst[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>((s + cnt / 2) / cnt);
            }
        }
    }
    *nw = w;
    *nh = h;
}

}  // namespace render
}  // namespace ps2r

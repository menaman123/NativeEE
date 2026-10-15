// gs/hle/texsrc/window_in_target.cpp -- WindowInTarget geometry.
#include "engine/gs/hle/texsrc/window_in_target.h"

namespace ps2r {
namespace gs {

bool WindowInTarget::Contains(uint32_t tbp, uint32_t base, int width, int height,
                              uint32_t render_psm, uint32_t* page_off) {
    if (!IsCt16Psm(render_psm)) return false;
    if (width <= 0 || height <= 0) return false;
    const uint64_t foot = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 2ull;
    if (tbp <= base) return false;                                   // inside only
    if (static_cast<uint64_t>(tbp) >= static_cast<uint64_t>(base) + foot) return false;
    const uint32_t delta = tbp - base;
    if ((delta & 0x1FFFu) != 0) return false;                        // page-aligned only
    const uint32_t off = delta >> 13;                                // / 8192
    if (off > 63u) return false;                                     // 6-bit pack budget
    if (page_off) *page_off = off;
    return true;
}

int WindowInTarget::PackSelf6(uint32_t ppr8, uint32_t tw_log, uint32_t th_log, uint32_t page_off) {
    return static_cast<int>((ppr8 & 0xFu) | ((tw_log & 0xFu) << 4) | ((th_log & 0xFu) << 8) |
                            ((page_off & 0x3Fu) << 16));
}

uint32_t WindowInTarget::UnpackPageOff(int self) {
    return (static_cast<uint32_t>(self) >> 16) & 0x3Fu;
}

WindowInTarget::Fmt6Texel WindowInTarget::Fmt6Remap(int x, int y, uint32_t pgoff, uint32_t ppr8,
                                                    int target_w) {
    const int ppr16 = target_w >> 6;
    const int page = static_cast<int>(pgoff) + (y >> 6) * static_cast<int>(ppr8) + (x >> 7);
    const int br = (x >> 4) & 7;
    const int bc = (y >> 4) & 3;
    const int lx = x & 15, ly = y & 15;
    Fmt6Texel t;
    t.lane = (ly >> 1) & 1;
    const int cr = ((ly >> 2) << 1) | (ly & 1);
    const int cc = lx ^ ((((ly + 2) >> 2) & 1) << 2);
    t.X = (page % ppr16) * 64 + bc * 16 + cc;
    t.Y = (page / ppr16) * 64 + br * 8 + cr;
    return t;
}

uint32_t WindowInTarget::Fmt6IndexFromRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a, int lane) {
    const uint32_t hw = static_cast<uint32_t>(r >> 3) | (static_cast<uint32_t>(g >> 3) << 5) |
                        (static_cast<uint32_t>(b >> 3) << 10) | ((a > 127u) ? 0x8000u : 0u);
    return lane ? ((hw >> 8) & 0xFFu) : (hw & 0xFFu);
}

}  // namespace gs
}  // namespace ps2r

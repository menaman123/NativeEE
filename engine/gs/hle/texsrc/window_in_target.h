// gs/hle/texsrc/window_in_target.h -- WindowInTarget: the geometry of a PSMT8
// texture window that lies INSIDE a live 16-bit render target.
//
// A CT16 page is 64x64 halfwords = 8 KiB; a PSMT8 page is 128x64 bytes = the same
// 8 KiB (GS User's Manual 8.1). A T8 read whose base sits a whole number of pages
// past a CT16 target's base therefore addresses the target's own pages, one byte
// lane of each halfword at a time. Contains() decides the containment (inside
// only, page-aligned, at most 63 pages so the offset packs into 6 bits);
// Fmt6Remap maps a T8 texel (x, y) of that window to the CT16 pixel (X, Y) and
// byte lane holding its index by walking the T8 block/column layout (section
// 8.3: T8 block 16x16, column 16x4) onto the CT16 page grid; Fmt6IndexFromRgba
// re-packs the target's RGBA8 texel to 5551 and selects the lane's byte.
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

class WindowInTarget {
public:
    struct Fmt6Texel {
        int X;
        int Y;
        int lane;   // 0 = low byte of the CT16 halfword, 1 = high byte
    };
    static bool IsCt16Psm(uint32_t render_psm) { return render_psm == 0x02 || render_psm == 0x0A; }
    // T8 window at `tbp` inside the target (base, width x height, render_psm);
    // *page_off receives the page offset (1..63).
    static bool Contains(uint32_t tbp, uint32_t base, int width, int height, uint32_t render_psm,
                         uint32_t* page_off);
    // The fmt 6 metadata word: pages-per-row of the T8 view | tw << 4 | th << 8 | page_off << 16.
    static int PackSelf6(uint32_t ppr8, uint32_t tw_log, uint32_t th_log, uint32_t page_off);
    static uint32_t UnpackPageOff(int self);
    static Fmt6Texel Fmt6Remap(int x, int y, uint32_t pgoff, uint32_t ppr8, int target_w);
    static uint32_t Fmt6IndexFromRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a, int lane);
};

}  // namespace gs
}  // namespace ps2r

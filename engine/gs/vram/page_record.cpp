// gs/vram/page_record.cpp -- PageRecord band math (see page_record.h).
#include "engine/gs/vram/page_record.h"

namespace ps2r {
namespace gs {

void PageRecord::ClearBand() {
    dirty_y0 = 0;
    dirty_y1 = 0;
    stale = false;
}

void PageRecord::UnionBand(int32_t y0, int32_t y1) {
    if (y0 >= y1) return;
    if (BandEmpty()) {
        dirty_y0 = y0;
        dirty_y1 = y1;
    } else {
        if (y0 < dirty_y0) dirty_y0 = y0;
        if (y1 > dirty_y1) dirty_y1 = y1;
    }
    stale = true;
}

int PageRecord::PageHeightForPsm(uint32_t psm) {
    return (psm == 0x02 || psm == 0x0A) ? 64 : 32;
}

bool PageRecord::FoldByteRangeToBand(uint32_t write_start, uint32_t write_size, uint32_t fbp,
                                     uint32_t fbw_px, uint32_t bpp, int h, bool full_band,
                                     int* inout_y0, int* inout_y1) {
    if (write_size == 0 || h <= 0) return false;
    const uint64_t row_bytes = static_cast<uint64_t>(fbw_px) * bpp;
    if (row_bytes == 0) return false;
    const uint64_t region = row_bytes * static_cast<uint64_t>(h);
    const uint64_t ws = write_start;
    const uint64_t we = ws + write_size;
    const uint64_t t0 = fbp;
    const uint64_t t1 = t0 + region;
    const uint64_t i0 = ws > t0 ? ws : t0;
    const uint64_t i1 = we < t1 ? we : t1;
    if (i1 <= i0) return false;  // no overlap
    int y0, y1;
    if (full_band) {
        y0 = 0;
        y1 = h;
    } else {
        y0 = static_cast<int>((i0 - t0) / row_bytes);
        y1 = static_cast<int>((i1 - t0 + row_bytes - 1) / row_bytes);  // ceil
        if (y0 < 0) y0 = 0;
        if (y1 > h) y1 = h;
        if (y1 <= y0) return false;
    }
    const int old0 = *inout_y0, old1 = *inout_y1;
    const int n0 = (old0 < old1) ? (y0 < old0 ? y0 : old0) : y0;  // union
    const int n1 = (old0 < old1) ? (y1 > old1 ? y1 : old1) : y1;
    if (n0 == old0 && n1 == old1) return false;
    *inout_y0 = n0;
    *inout_y1 = n1;
    return true;
}

bool PageRecord::ClampToRowBand(int ry0, int ry1, int target_h, int* out_y0, int* out_y1) {
    int y0 = ry0 < 0 ? 0 : (ry0 > target_h ? target_h : ry0);
    int y1 = ry1 < 0 ? 0 : (ry1 > target_h ? target_h : ry1);
    if (y1 <= y0) return false;
    *out_y0 = y0;
    *out_y1 = y1;
    return true;
}

bool PageRecord::FoldByteRangeToPageRect(uint32_t write_start, uint32_t write_size, uint32_t fbp,
                                         uint32_t fbw_px, uint32_t psm, int w, int h, int* out_x0,
                                         int* out_y0, int* out_x1, int* out_y1) {
    if (write_size == 0 || w <= 0 || h <= 0) return false;
    if (fbw_px == 0 || (fbw_px % 64) != 0) return false;
    const int page_h = PageHeightForPsm(psm);
    const uint32_t pages_per_row = fbw_px / 64;
    const int page_rows = (h + page_h - 1) / page_h;
    const uint64_t region = static_cast<uint64_t>(pages_per_row) * page_rows * kPageBytes;
    const uint64_t ws = write_start, we = ws + write_size;
    const uint64_t t0 = fbp, t1 = t0 + region;
    const uint64_t i0 = ws > t0 ? ws : t0;
    const uint64_t i1 = we < t1 ? we : t1;
    if (i1 <= i0) return false;
    const uint64_t pg0 = (i0 - t0) / kPageBytes;      // first touched page
    const uint64_t pg1 = (i1 - t0 - 1) / kPageBytes;  // last touched page
    const int row0 = static_cast<int>(pg0 / pages_per_row);
    const int row1 = static_cast<int>(pg1 / pages_per_row);
    int col0, col1;
    if (row0 == row1) {
        col0 = static_cast<int>(pg0 % pages_per_row);
        col1 = static_cast<int>(pg1 % pages_per_row);
    } else {
        col0 = 0;                                       // spans page rows:
        col1 = static_cast<int>(pages_per_row) - 1;     // full-width union
    }
    int x0 = col0 * 64;
    int x1 = (col1 + 1) * 64;
    int y0 = row0 * page_h;
    int y1 = (row1 + 1) * page_h;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x1 <= x0 || y1 <= y0) return false;
    *out_x0 = x0;
    *out_y0 = y0;
    *out_x1 = x1;
    *out_y1 = y1;
    return true;
}

bool PageRecord::FullyCoveredPages(uint32_t psm, uint32_t fbp, uint32_t fbw, int w, int band_y0,
                                   int band_y1, uint32_t* out_first, uint32_t* out_last) {
    if (fbw == 0 || (fbw % 64) != 0) return false;
    if (w < static_cast<int>(fbw)) return false;  // right columns unseen: never full
    const int ph = PageHeightForPsm(psm);
    const uint32_t pages_per_row = fbw / 64;
    const int k0 = (band_y0 + ph - 1) / ph;  // ceil
    const int k1 = band_y1 / ph;             // floor
    if (k1 <= k0) return false;
    const uint64_t base = fbp / kPageBytes;
    uint64_t first = base + static_cast<uint64_t>(k0) * pages_per_row;
    uint64_t last = base + static_cast<uint64_t>(k1) * pages_per_row - 1;
    if (first > kPageCount - 1) return false;
    if (last > kPageCount - 1) last = kPageCount - 1;
    *out_first = static_cast<uint32_t>(first);
    *out_last = static_cast<uint32_t>(last);
    return true;
}

}  // namespace gs
}  // namespace ps2r

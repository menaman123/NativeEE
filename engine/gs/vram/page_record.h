// gs/vram/page_record.h -- PageRecord: the ONE per-page ledger of GS local
// memory (one record per 8 KiB page, 512 per VramImage) plus the page-band math
// every reader of a page range shares.
//
// A page has one AUTHOR (who last produced its bytes: nobody yet, the EE through
// a host->local transfer, or a GPU render target identified by its FBP), two
// epoch families that never mix (the UPLOAD epoch stamped when the EE writes and
// the DEPOSIT epoch stamped when a GPU writeback is committed), a commit STATE
// for the single-writer protocol (Clean / Encoding while the GPU side fills the
// shadow / Pending once filled / Applying while the commit copies), the precise
// 128-byte sub-page write bits of the current EE write burst, the row band a
// pending refresh must cover, and the reader flag (this page is in the set a
// texture read is known to observe, so a deposit must land eagerly).
//
// Band math (GS User's Manual 8.2 page layout: a buffer of width FBW pixels is
// FBW/64 pages per page row; CT32-class pages are 64x32, 16-bit pages 64x64):
//   FoldByteRangeToBand    : a byte write range -> the target rows it touches,
//                            unioned into a [y0,y1) band (empty when y0 >= y1)
//   ClampToRowBand         : clamp a band to the target height
//   FoldByteRangeToPageRect: a byte write range -> the page-tile rect it covers
//   FullyCoveredPages      : the pages a band covers in FULL (only those may be
//                            cleared by a refresh -- the alias-refresh rule)
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

struct PageRecord {
    enum class Author : uint8_t { Cold = 0, Cpu = 1, GpuTarget = 2 };
    enum class State : uint8_t { Clean = 0, Pending = 1, Encoding = 2, Applying = 3 };

    static constexpr uint32_t kPageBytes = 8192;
    static constexpr uint32_t kPageCount = 512;
    static constexpr uint32_t kSubPageBytes = 128;   // one write bit per 128 B
    static constexpr uint32_t kSubPagesPerPage = kPageBytes / kSubPageBytes;  // 64

    Author author = Author::Cold;
    uint32_t author_fbp = 0;       // GpuTarget: the FRAME.FBP (bytes) that drew it
    uint64_t upload_epoch = 0;     // EE-side write family
    uint64_t deposit_epoch = 0;    // GPU-writeback commit family
    uint64_t shadow_epoch = 0;     // max batch epoch of the pending shadow content
    State state = State::Clean;
    uint64_t write_bits = 0;       // 128 B sub-pages written since the last settle
    // Observable-view generation: bumped by every mutation that can change what a
    // reader of this page sees through VramImage::ReadView -- the bytes, the
    // pending shadow's content, or the overlay predicate's inputs (state,
    // upload_epoch, shadow_epoch). Two reads of a page span whose pages all carry
    // the same generations observed the same bytes.
    uint64_t view_gen = 0;
    int32_t dirty_y0 = 0;          // stale row band [y0, y1); empty when y0 >= y1
    int32_t dirty_y1 = 0;
    bool stale = false;            // a GPU deposit was elided; the band is owed
    bool reader_flag = false;      // in the reader page set (eager deposit)

    bool BandEmpty() const { return dirty_y0 >= dirty_y1; }
    void ClearBand();
    void UnionBand(int32_t y0, int32_t y1);

    // Page height in pixels for a frame PSM (16-bit modes 64, else 32).
    static int PageHeightForPsm(uint32_t psm);

    // Fold [write_start, +write_size) against a target (fbp bytes, fbw_px, bpp, h
    // rows) into *inout_y0/*inout_y1 (union). full_band forces [0,h). Returns
    // true iff the band changed.
    static bool FoldByteRangeToBand(uint32_t write_start, uint32_t write_size, uint32_t fbp,
                                    uint32_t fbw_px, uint32_t bpp, int h, bool full_band,
                                    int* inout_y0, int* inout_y1);

    // Clamp [ry0, ry1) to [0, target_h); false when empty.
    static bool ClampToRowBand(int ry0, int ry1, int target_h, int* out_y0, int* out_y1);

    // Fold a byte range into the page-tile rect of a (fbp, fbw_px, psm, w, h)
    // target: one page = one 64 x PageHeight tile; a range spanning page rows
    // widens to the full width. False when disjoint or the width is not a page
    // multiple.
    static bool FoldByteRangeToPageRect(uint32_t write_start, uint32_t write_size, uint32_t fbp,
                                        uint32_t fbw_px, uint32_t psm, int w, int h, int* out_x0,
                                        int* out_y0, int* out_x1, int* out_y1);

    // Pages [first, last] whose every row lies inside [band_y0, band_y1) when
    // the decode width covers the full FBW; false when no page is fully covered.
    static bool FullyCoveredPages(uint32_t psm, uint32_t fbp, uint32_t fbw, int w, int band_y0,
                                  int band_y1, uint32_t* out_first, uint32_t* out_last);
};

}  // namespace gs
}  // namespace ps2r

// gs/hle/texsrc/texres_trace.h -- TexresTrace: the per-decode record of WHAT a
// textured read's source view was composed of at resolve time.
//
// A decode reads the composed VRAM view (committed bytes overlaid with the pending
// GPU deposits a commit would apply). Whether the texels a draw samples are the
// ones the EE transferred depends on that composition: for every page of the
// level-0 window and of the CLUT page the trace records the page's commit state,
// its author (EE transfer / GPU target fbp), the UPLOAD epoch the EE stamped, the
// pending shadow's epoch, and whether the view substituted the shadow; plus the
// deferred writebacks the resolve flushed under the footprint before reading
// (their fbp / recorded epoch / span), the cache outcome (L1 / L2 / fresh decode),
// the content key, the window and CLUT byte hashes, and -- when the filter asks --
// the window and CLUT bytes themselves, so the decode can be reproduced offline.
// Lines of the frame in progress rotate into the last completed frame's at the
// frame end (the DrawRouteTrace pattern). A filter (TBP / CBP) keeps the record
// small; disabled, the trace costs one branch per resolve and records nothing.
// The trace decides nothing and is not consulted by the resolver.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/gs/hle/ir/tex_read_view.h"
#include "engine/gs/hle/targets/pending_rt_sync_ledger.h"
#include "engine/gs/vram/vram_image.h"

namespace ps2r {
namespace gs {

class TexresTrace {
public:
    static constexpr uint32_t kAny = 0xFFFFFFFFu;
    struct Filter {
        bool enabled = false;
        uint32_t tbp = kAny;       // record only reads of this TEX0.TBP (bytes)
        uint32_t cbp = kAny;       // ... and / or this CLUT base (bytes)
        bool keep_bytes = false;   // retain the window + CLUT bytes of each recorded read
    };
    enum class Outcome : uint8_t { None = 0, L1Hit, L2Hit, Decoded, TargetServe, DepthServe, SelfRead, NoSnapshot, OwnedServe };
    struct Page {
        uint32_t addr = 0;         // page start (bytes)
        uint8_t state = 0;         // PageRecord::State
        uint8_t author = 0;        // PageRecord::Author
        uint32_t author_fbp = 0;
        uint64_t upload_epoch = 0;
        uint64_t deposit_epoch = 0;
        uint64_t shadow_epoch = 0;
        uint64_t write_bits = 0;
        bool overlay = false;      // the view substituted the pending shadow here
    };
    struct Flushed {
        uint32_t fbp = 0, start = 0, size = 0;
        uint64_t epoch = 0;
    };
    struct Line {
        uint32_t draw_seq = 0;
        uint32_t frame_count = 0;
        uint64_t draw_epoch = 0;   // the draw's vram epoch (DrawIr::vram_epoch)
        TexReadView tex;
        Outcome outcome = Outcome::None;
        uint64_t key = 0;
        uint8_t levels = 0;
        bool sub = false;
        int rgn_x0 = 0, rgn_y0 = 0, rgn_w = 0, rgn_h = 0;
        uint32_t fp_start = 0, fp_size = 0;
        uint64_t window_hash = 0, clut_hash = 0;
        std::vector<Flushed> flushed;
        std::vector<Page> pages;        // the level-0 window's pages, then the CLUT page
        std::vector<uint8_t> window;    // kept only with Filter::keep_bytes
        std::vector<uint8_t> clut;
        int tex_w = 0, tex_h = 0;       // the bound texture's level 0 (RGBA8), kept with the bytes
        std::vector<uint8_t> texels;
    };

    void SetFilter(const Filter& f) { filter_ = f; }
    const Filter& GetFilter() const { return filter_; }
    bool Enabled() const { return filter_.enabled; }
    // Enabled and the read matches the TBP / CBP filter.
    bool Wants(const TexReadView& v) const;

    // The deferred entries a flush over [start, start+size) would take (recorded
    // BEFORE the resolver flushes them).
    static void NoteOverlapping(const PendingRtSyncLedger& ledger, uint32_t start, uint32_t size,
                                std::vector<Flushed>* out);
    // One Page per 8 KiB page of [start, start+size) from the image's records.
    static void FillPages(const VramImage& vram, uint32_t start, uint32_t size, std::vector<Page>* out);
    static uint64_t Hash(const uint8_t* p, size_t n);

    void Note(Line&& l) { cur_.push_back(std::move(l)); }
    // Frame boundary: the frame in progress becomes the last completed frame.
    void OnFrameEnd();
    const std::vector<Line>& LastFrame() const { return last_; }
    // One text line per recorded read of the last completed frame, each followed by
    // its flushed entries and page rows.
    void Write(std::FILE* f) const;
    // The retained window / CLUT bytes of the last frame's reads as
    // <dir>/<prefix><draw_seq>_win.bin and _clut.bin, and the bound texture's level 0
    // as <prefix><draw_seq>_tex.ppm (P6) + _tex_alpha.pgm (P5, the stored alpha byte). Returns files written.
    uint32_t WriteBytes(const std::string& dir, const std::string& prefix) const;
    static const char* OutcomeName(Outcome o);

private:
    Filter filter_;
    std::vector<Line> cur_;
    std::vector<Line> last_;
};

}  // namespace gs
}  // namespace ps2r

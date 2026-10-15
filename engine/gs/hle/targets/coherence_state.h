// gs/hle/targets/coherence_state.h -- CoherenceState: everything a render target
// remembers about the relation between its GPU surfaces and GS local memory.
//
// Rules it carries (plain words):
//   * drawn_since_sync / z_drawn_since_sync: the union of the rectangles drawn
//     since the last writeback; a writeback encodes ONLY those rows (a target that
//     has not drawn since its sync writes back nothing).
//   * valid_rect: the lifetime union of the main image's drawn triangle bboxes
//     (unclamped); a writeback requires it non-empty, a rescue is bounded by it.
//   * sync_keep_mask16: the 5551 bits the last 16-bit draw did NOT write (from
//     FRAME.FBMSK); a masked writeback preserves them from current VRAM.
//   * alt16_stale: the rect of the main attachment the alt16 companion no longer
//     mirrors (written through the main route since the companion was refreshed).
//   * alt16_drawn: the union of what the companion route drew; a main-route
//     write covering all of it drops the companion's content (on hardware none
//     of the 16-bit-authored bits survive a full 32-bit overwrite).
//   * per-frame clear: a target that has been consumed by a composite into the
//     scanout target is cleared when the consumer marks it (needs_frame_clear),
//     otherwise once per EE game frame (last_clear_frame).
//   * per-VIEW author counters (main image / alt16 companion): bumped by every
//     author of that view -- committed draws by route, the main -> companion
//     mirror for alt16, the mirror -> attachment refresh for main -- and the
//     per-view lane records {seq, frame} of the last Z16-view masked lane draw;
//     the fmt=5 lane serve proves "the lane draw is still the last author of
//     its view" by lane.seq == that view's author counter in the same frame.
//   * the indexed-serve voucher (vram_coherent_epoch / class), has_content, the
//     Z-write history and the Cd-copy tracker.
#pragma once

#include <cstdint>

#include "engine/gs/hle/targets/logical_rect.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

struct CoherenceState {
    struct LaneRecord {
        uint64_t seq = 0;      // the view's author counter when the lane draw landed
        uint32_t frame = 0;    // frame_count of that draw; 0 = never
    };

    LogicalRect drawn_since_sync;
    LogicalRect z_drawn_since_sync;
    LogicalRect valid_rect;             // lifetime union of the main image's tri bboxes
    LogicalRect alt16_stale;
    LogicalRect alt16_drawn;
    uint16_t sync_keep_mask16 = 0;
    bool has_content = false;
    uint32_t last_draw_frame = 0;
    uint64_t last_draw_gfx_epoch = 0;
    bool needs_frame_clear = false;
    bool ever_composited = false;
    uint32_t last_clear_frame = 0xFFFFFFFFu;
    uint32_t last_zbp = 0xFFFFFFFFu;    // Z-write history for the depth-as-texture match
    uint32_t last_zpsm = 0;
    bool last_z_wrote = false;          // sticky: true once any draw wrote Z
    uint64_t last_z_seq = 0;            // draw ordinal of the last Z WRITE (0 = never); orders
                                        // the depth attachments that alias one ZBP
    uint32_t last_z_write_frame = 0;    // frame_count of that Z write (stamped with last_z_seq);
                                        // the Z-source refresh accepts a writer <= 2 frames old
    uint64_t main_author_seq = 0;       // authors of the main image
    uint64_t alt_author_seq = 0;        // authors of the alt16 companion
    LaneRecord lane_main;
    LaneRecord lane_alt;
    uint64_t vram_coherent_epoch = 0;   // indexed-serve voucher
    uint8_t vram_coherent_class = 0;    // 0 none, 1 = 32-bit class, 2 = 16-bit class
    render::CdCopyState cd_copy;

    bool HasDrawnSinceSync() const { return !drawn_since_sync.Empty(); }
    bool HasZDrawnSinceSync() const { return !z_drawn_since_sync.Empty(); }
    bool HasValidContent() const { return !valid_rect.Empty(); }
    bool Alt16HasDrawn() const { return !alt16_drawn.Empty(); }
    void ClearDrawnSinceSync() { drawn_since_sync.Reset(); }
    void ClearZDrawnSinceSync() { z_drawn_since_sync.Reset(); }
};

}  // namespace gs
}  // namespace ps2r

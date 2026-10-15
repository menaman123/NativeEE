// engine/ipu/hle/fmv_context_layout.h -- FmvContextLayout: byte offsets, relative
// to the SceMpeg context the game allocated in EE RAM, of the libmpeg fields the
// HLE reads or maintains, the offsets of the sceMpegAlalc allocator words and of
// the inner work area's reference-picture slots, plus the two guest WORD VALUES
// the HLE must recognise or write (`play_to_eof_total`, `mode_raw8`). libmpeg is
// statically linked into each title, so these layouts are the TITLE'S ABI: every
// field here is a parameter with no default, filled by the title module at
// integration. Nothing under engine/ names a value.
#pragma once

#include <cstdint>

namespace ps2r::ipu {

struct FmvContextLayout {
    // Bitstream ring the game's demux callback fills for the IPU: base EE
    // address of the ring buffer (0 = this context owns no ring, i.e. it is
    // not the movie player).
    uint32_t ring_base;
    // Ring capacity in bytes.
    uint32_t ring_size;
    // Ring write cursor (byte offset the next video payload lands at).
    uint32_t ring_write;
    // Ring read cursor (byte offset the IPU has consumed up to).
    uint32_t ring_read;
    // Anchor pointer: EE address at which the game's sequence-header parse
    // reads the picture dimensions (bytes 4..6 of the sequence header).
    uint32_t ring_anchor;
    // Pointer to the inner sceMpeg work area whose first word is the
    // end-of-stream status (1 = stream ended) the game polls.
    uint32_t inner_status_ptr;
    // Signed total of pictures the player intends to show (a large positive
    // value means "play to end of stream"); compared SIGNED against
    // `presented` by the driver's completion gate.
    uint32_t total;
    // The VALUE (not an offset) the completion-gate hold writes at `total`
    // while the movie is still playing: a large positive count so the signed
    // compare stays open until the real end of stream.
    int32_t play_to_eof_total;
    // Presented-picture counter word; bit 0 of its value selects whether the
    // RAW8 path double-buffers its output planes.
    uint32_t presented;
    // Player mode word (which output path -- RGB32 GetPicture or the RAW8
    // 16-bit plane path -- the title's driver runs).
    uint32_t mode;
    // The VALUE of the mode word that means "the movie player's RAW8 plane
    // path" (the only mode whose completion gate the HLE holds).
    uint32_t mode_raw8;
    // Alternate-plane flag word: when double buffering is on (bit 0 of
    // `presented`) a non-zero flag selects plane1 over plane0.
    uint32_t plane_select;
    // EE addresses of the two 16-bit output planes.
    uint32_t plane0;
    uint32_t plane1;
    // Output plane geometry (pixels) the title allocated.
    uint32_t target_w;
    uint32_t target_h;
    // Abort request flag the scene updater sets to tear the movie down.
    uint32_t abort_flag;

    // sceMpegAlalc bump allocator words (offsets within the allocator struct
    // the title passes to _sceMpegAlalcInit / _sceMpegAlalcAlloc): the region
    // base, its capacity in bytes, the watermark cursor the next allocation
    // is aligned up from, and a second base copy the library keeps.
    uint32_t alalc_base;
    uint32_t alalc_capacity;
    uint32_t alalc_watermark;
    uint32_t alalc_base_copy;

    // sceMpegClearRefBuff: the inner work area (reached through the word at
    // `inner_status_ptr`) holds the reference-picture slot pointers -- three
    // luma and three chroma buffers (the IPU's forward, backward and current
    // reference pictures) -- at these offsets; the clear zeroes the status
    // word at `ref_slot_status` inside each non-null slot.
    static constexpr uint32_t kRefSlots = 6;
    uint32_t ref_slot[kRefSlots];
    uint32_t ref_slot_status;
};

}  // namespace ps2r::ipu

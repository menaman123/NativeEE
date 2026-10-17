// gs/xfer/transfer_engine.h -- TransferEngine: the GS local-memory transfer unit
// (GS User's Manual 4.2 / 6.x: BITBLTBUF, TRXPOS, TRXREG, TRXDIR, HWREG).
//
// BITBLTBUF / TRXPOS / TRXREG writes only STAGE their fields; TRXDIR latches the
// staged set as the active transfer (a later write to the three registers does
// not affect a transfer in flight) and starts it: host->local (XDIR 0) arms the
// HWREG pixel cursor at (DSAX, DSAY) and stamps the destination pages' upload
// epoch; local->local (XDIR 2) copies synchronously in the TRXPOS.DIR order and
// stamps; local->host (XDIR 1) records the request (no readback path); XDIR 3
// is idle. Only the XDIR 0 arm touches the cursor's active flag: an unfinished
// upload stays armed across TRXDIR 1/2/3 (its data is refused while XDIR != 0)
// and is flushed partially by the next host->local start.
//
// HWREG (IMAGE data) delivers 64 bits per write; the pixels-per-word by PSM is
// 2 (32-bit), 4 (16-bit), 8 (T8), 16 (T4). Each pixel is stored at
// PsmLayout::PixelByteAddress(dpsm, x, y, dbp, dbw); the cursor advances x, wraps
// to DSAX at DSAX+RRW, and the transfer completes when RRW*RRH pixels landed
// (InvalidateTextures + UploadVRAMTexture jobs to the sink; the RGBA8 linear copy
// of the upload is kept for the consumer). A completed transfer is RE-ARMED by
// the next IMAGE data at the same latched geometry whatever the direction,
// re-stamping the destination when XDIR is 0 or 2.
//
// The store is STAGED per 64-byte column (the layout's contiguous unit: a
// column's pixels are scattered inside it, GS User's Manual 8.3): a pixel's bytes
// land in the column's staging slot and the column reaches the VramImage as ONE
// WriteStream when all 64 bytes are present, or as the contiguous runs present
// when the stage is flushed -- at the end of every WriteImage span and WriteHwreg
// word, before a transfer completes or re-arms, before TRXDIR. The bytes, their
// order of arrival within a span (no observer sits between the words of one
// span), the page marks (write bits, author, settle set, watermark) and the
// VramOpLog runs (it coalesces contiguous writes itself) are the per-pixel
// store's exactly; only the image's per-call counters (stream_writes, a page's
// view generation) count one write per run instead of per pixel.
//
// Store rules:
//   CT24 stores its 3 colour bytes and forces the 4th byte to 0x80 (kept bit-exact
//   with the original runtime);
//   PSMT4 IMAGE data is 16 nibbles per HWREG word (GS User's Manual 4.2 / 8.3.4):
//   each lands in its nibble of the byte PsmLayout addresses (the two nibbles of
//   a byte are rows y and y+2 of the column, so the stage seeds the byte from the
//   image before the first nibble and keeps the other half); PSMT8H data is 8 bits
//   per pixel into byte 3 of the CT32-layout word, PSMT4HL / PSMT4HH 4 bits into
//   the low / high nibble of that byte (section 8.3.1 index-in-word modes). The
//   original runtime stored none of these (its first title never issued them); a
//   title whose textures are PSMT4 uploads sampled every one as whatever the
//   pages held before, which is what the texture oracle caught.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "engine/gs/hle/ir/draw_ir_sink.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/vram/vram_image.h"

namespace ps2r {
namespace gs {

class TransferEngine {
public:
    struct Regs {
        uint32_t sbp = 0, sbw = 0, spsm = 0;  // bytes / pixels / psm
        uint32_t dbp = 0, dbw = 0, dpsm = 0;
        uint16_t ssax = 0, ssay = 0, dsax = 0, dsay = 0;
        uint8_t dir = 0;
        uint16_t rrw = 0, rrh = 0;
    };
    struct Cursor {
        uint8_t xdir = 3;
        uint16_t cur_x = 0, cur_y = 0;
        bool active = false;
        size_t pixels_written = 0;
    };
    struct Counters {
        uint64_t bitbltbuf = 0, trxdir = 0, hwreg = 0, completed = 0, local_local = 0, local_host = 0;
        uint64_t local_local_over_gpu = 0;  // source pages authored by a GPU target (the mirror may lag)
        uint64_t owned = 0;                 // NoteOwnedUpload: uploads an owner placed without bytes
        uint64_t host_local_by_dpsm[64] = {};   // host->local starts per BITBLTBUF.DPSM field value
    };

    explicit TransferEngine(VramImage* vram) : vram_(vram) {}

    void SetSink(DrawIrSink* sink) { sink_ = sink; }
    // Pending-draw flush: any draw queued before a transfer starts or IMAGE data
    // arrives is flushed first (draw order before transfer order).
    void SetFlushHook(std::function<void()> hook) { flush_pending_ = std::move(hook); }
    // Sequence / frame / epoch context for emitted jobs.
    void SetJobContext(uint32_t* seq_counter, const uint32_t* src_frame);
    // Before a local->local copy reads its source span: the host brings into the
    // engine's image the pipeline-side bytes the copy needs that the engine's own
    // image lacks (the pipelined GS queue's split image keeps the GPU side's
    // deposits on the consumer image). The deterministic hook drains that queue
    // and copies its image wholesale; the play hook takes the GPU-authored pages
    // from the consumer's last completed frame without waiting. Returns true when
    // a source page a GPU target authored was involved (local_local_over_gpu).
    // Without a hook the engine's image is the source as it is. The copy's
    // writes land on the engine's image either way.
    using LocalToLocalPrepare = std::function<bool(VramImage::Span source, VramImage& image)>;
    void SetLocalToLocalPrepare(LocalToLocalPrepare prepare) { l2l_prepare_ = std::move(prepare); }

    // Register writes (A+D addresses 0x50..0x54). Returns false for other addresses.
    bool WriteRegister(uint8_t addr, uint64_t value);
    // IMAGE-mode data: `bytes` must be a multiple of 8 (two HWREG writes per quadword).
    void WriteImage(const uint8_t* data, size_t bytes);
    void WriteHwreg(uint64_t value);
    // The word store after the pending-draw flush (WriteImage flushes once per span).
    void WriteHwregWord(uint64_t value);
    // An owner placed a texture at `dbp` by another route (its converted image
    // serves the reads -- the texture resolver's owned slots): the pending-draw
    // flush, the destination marks (upload epoch, CPU authorship, write bits, the
    // settle) and the InvalidateTextures job a completed host->local transfer of
    // `bytes` bytes produces, without the bytes, the payload copy or the
    // UploadVRAMTexture job. Counted `owned`.
    void NoteOwnedUpload(uint32_t dbp, uint32_t bytes);

    // Bytes per pixel the original runtime charges a PSM for the destination span
    // it stamps / invalidates (T4 counts 1; the index-in-word modes 4).
    static uint32_t PixelSize(uint32_t psm);
    // Pixels one 64-bit HWREG word carries: 2 / 4 / 8 / 16 by the IMAGE data width
    // (32 / 16 / 8 / 4 bits: T8H is 8-bit data, T4HL / T4HH 4-bit data).
    static uint32_t PixelsPerWord(uint32_t psm);

    const Regs& Staged() const { return stage_; }
    const Regs& Active() const { return xfer_; }
    const Cursor& State() const { return cur_; }
    const std::vector<uint8_t>& LastUploadPixels() const { return last_upload_; }
    const Counters& Stats() const { return counters_; }

private:
    void SetTrxDir(uint64_t value);
    void ArmHostToLocal();
    void StampDest();
    // `addr` = the byte PsmLayout names for the pixel; `nibble_shift` = 0 / 4 for the
    // 4-bit modes (the nibble inside that byte, T4) or inside byte 3 (T4HL / T4HH).
    void StorePixel(uint32_t addr, uint32_t nibble_shift, uint32_t px, uint32_t lin_index);
    // The staged column store: `bytes[0..size)` at `addr` (size 4 / 2 / 1) into the
    // column slot `col` (the row-relative column index), flushed as runs when a
    // slot changes column or the stage is flushed.
    void StageBytes(uint32_t col, uint32_t addr, const uint8_t* bytes, uint32_t size);
    // One nibble (`value` & 0xF at bit `shift`) of the byte at `addr`: the byte is
    // seeded from the image when the slot does not hold it yet, so a flush writes
    // the other nibble as it was.
    void StageNibble(uint32_t col, uint32_t addr, uint32_t shift, uint32_t value);
    void FlushStageSlot(size_t slot);
    void FlushStage();
    // The row-segment walk over `nwords` IMAGE words that lie entirely inside the
    // active host->local transfer (no completion, no re-arm can occur): pixels are
    // consumed in raster order per column segment -- one address per segment, the
    // column offsets of the row from a per-row table -- with the same stores,
    // linear copy, cursor and counters as the per-pixel walk. Returns the words
    // consumed (all of them; 0 when the fast walk does not apply to this mode).
    size_t WriteWordsInside(const uint8_t* data, size_t nwords);
    void BeginRowOffsets(Psm mode, uint32_t y);
    void FinishHostToLocal();
    void LocalToLocal();
    void PushJob(JobKind kind, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                 const std::vector<uint8_t>* payload);

    VramImage* vram_;
    DrawIrSink* sink_ = nullptr;
    std::function<void()> flush_pending_;
    LocalToLocalPrepare l2l_prepare_;
    // The host->local walk's per-row address terms (WriteHwregWord).
    PsmLayout::RowAddresser row_addr_;
    bool row_addr_valid_ = false;
    uint32_t* seq_ = nullptr;
    const uint32_t* src_frame_ = nullptr;
    Regs stage_;
    Regs xfer_;
    Regs prev_flush_;  // previous latched dest for the partial-flush rule
    Cursor cur_;
    std::vector<uint8_t> linear_;  // RGBA8 linear copy of the upload in flight
    std::vector<uint8_t> last_upload_;
    Counters counters_;
    // The column stage: one slot per column the current row touches (index =
    // x / column_w - dsax / column_w); `base` = the column's VRAM byte address
    // (64-aligned), `mask` = the bytes present.
    struct ColumnSlot {
        uint32_t base = 0xFFFFFFFFu;
        uint64_t mask = 0;
        uint8_t bytes[64];
    };
    std::vector<ColumnSlot> stage_cols_;
    uint32_t stage_live_ = 0;      // slots with a non-empty mask (0 = nothing staged)
    uint32_t stage_col_shift_ = 3; // log2(column_w) of the transfer in flight
    uint32_t stage_col0_ = 0;      // dsax >> stage_col_shift_
    // The byte offset inside a 64-byte column of pixel bx (0..block_w-1) on the
    // row BeginRowOffsets was called for (PsmLayout::ElementInBlock minus the
    // column's base, times the storage size).
    uint8_t row_off_[32] = {};
    uint32_t row_off_y_ = 0xFFFFFFFFu;
    Psm row_off_psm_ = Psm::CT32;
};

}  // namespace gs
}  // namespace ps2r

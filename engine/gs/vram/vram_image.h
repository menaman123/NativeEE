// gs/vram/vram_image.h -- VramImage: the single-writer mirror of GS local memory.
//
// 4 MiB of bytes owned by the EE side, one PageRecord per 8 KiB page and a lazily
// allocated 4 MiB shadow the GPU side deposits into. The rules:
//   * EE writes (host->local transfers, HLE image feeds) land in the bytes at
//     once (WriteStream) and mark the page's precise 128 B write bits and CPU
//     authorship (NoteWriteNoBytes marks without a payload); SettleWrites stamps
//     the touched pages with a fresh UPLOAD epoch at the next observation
//     boundary; NoteStreamWrite stamps a destination span eagerly (the transfer
//     start / re-arm rule).
//   * GPU writebacks never touch the bytes directly: StageDeposit copies the
//     current page into the shadow on the first deposit (Encoding), overlays the
//     deposit, records the batch epoch that produced it and leaves the page
//     Pending. CommitDeposits, run only at EE observation boundaries, copies a
//     Pending page into the bytes iff no EE write newer than the deposit exists
//     (upload_epoch <= deposit batch epoch) and drops it otherwise, bumping the
//     separate DEPOSIT epoch family; a page is Applying for the copy so a
//     concurrent stage waits rather than reusing it.
//   * Every reader observes ReadView = bytes overlaid with the pending shadow of
//     every non-Clean page whose deposit a commit WOULD apply (upload_epoch <=
//     shadow_epoch) -- never the raw bytes -- so a snapshot sees exactly the
//     content a later commit would produce; a page a newer EE transfer stamped
//     shows the transferred bytes (the deposit is the one CommitDeposits drops).
//   * Materialize / stale bookkeeping: elided GPU deposits leave a row band on the
//     page (NoteGpuElided); MaterializePages clears it once the band was
//     refreshed; AssertNoStaleAtSave demands zero stale pages when the pending
//     ledger is empty (the save contract).
//   * The DEFERRED-DEPOSIT GATE (perf, gsperf L2): a colour writeback whose GPU
//     read is still in flight (RenderTargetCache's deferred scanout deposit) owes
//     a span of this image its bytes. The owner registers the span (NoteDeferred)
//     and a gate callback; EVERY operation that would observe or alter that span
//     -- ReadView, ReadMirror, ViewSignature, StageDeposit, the EE write paths,
//     CommitDeposits, the save assertion -- first runs the gate over the span,
//     which completes the writeback (staging it as it would have been staged at
//     once). The image then evolves exactly as it did without the deferral: no
//     reader can tell the deposit landed late. Spans that do not overlap pay a
//     range test. A CommitDeposits that found a deferred span owing records the
//     commit it owes; the gate's completion commits those pages itself
//     (CommitDepositsIn) so ReadMirror after the boundary reads committed bytes.
// GS User's Manual 8.1: local memory is 4 MB, addressed modulo 4 MB; a page is
// 8 KiB; there are 512 pages.
#pragma once
#include <functional>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/gs/vram/page_record.h"
#include "engine/gs/vram/vram_mirror.h"

namespace ps2r {
namespace gs {

class VramImage {
public:
    static constexpr uint32_t kBytes = 4u * 1024u * 1024u;
    static constexpr uint32_t kPageBytes = PageRecord::kPageBytes;
    static constexpr uint32_t kPageCount = PageRecord::kPageCount;

    enum class Reason : uint8_t { Decode, Serve, Refresh, PreDraw, Scanout, Destroy, Save };

    struct Span {
        uint32_t start;  // byte address (wrapped to 4 MiB)
        uint32_t size;   // bytes
    };
    struct Counters {
        uint64_t stream_writes = 0;
        uint64_t deposits_staged = 0;
        uint64_t deposits_committed = 0;
        uint64_t deposits_dropped_newer = 0;
        uint64_t settles = 0;
        uint64_t reader_stale_touches = 0;
        uint64_t save_reconciled = 0;
        uint64_t deferred_gates = 0;   // deferred-deposit completions forced by a reader / writer
    };

    VramImage();

    // ---- EE side --------------------------------------------------------------
    // Store `len` bytes at `byte_off` (wrapping at 4 MiB); marks write bits,
    // CPU authorship and the pending-settle watermark.
    void WriteStream(uint32_t byte_off, const uint8_t* src, size_t len);
    // The same marks for an EE write of `len` bytes whose payload is not kept
    // (a transfer PSM the store path drops): the pages settle as written.
    void NoteWriteNoBytes(uint32_t byte_off, size_t len);
    // Stamp the destination span with a fresh upload epoch and CPU authorship
    // before its bytes arrive (transfer start / IMAGE re-arm / HLE feed).
    uint64_t NoteStreamWrite(Span dest);
    // Observation boundary: every page written since the last settle gets a fresh
    // upload epoch; then pending deposits are committed.
    void SettleWrites();
    // CPU bytes landed in the mirror over the span (the InvalidateTextures job
    // after an upload): the mirror is truth again for these pages -- author CPU,
    // no owing target, no owed band. Marks only; the bytes arrived by WriteStream.
    void NoteCpuWrite(Span span);

    // ---- GPU side ---------------------------------------------------------------
    // Stage a writeback of `len` bytes at `byte_off` produced by the draw with
    // `batch_epoch` (the vram epoch the draw was pushed with).
    void StageDeposit(uint32_t byte_off, const uint8_t* src, size_t len, uint64_t batch_epoch);
    // Commit every Pending page whose upload epoch is not newer than its deposit.
    void CommitDeposits();
    // A GPU target drew into the span (author GpuTarget(fbp), not stale).
    void NoteGpuAuthor(Span span, uint32_t fbp);
    // A 16-bit deposit was elided; the rows [y0,y1) are owed to the span's pages.
    void NoteGpuElided(Span span, uint32_t fbp, int32_t y0, int32_t y1);

    // ---- readers ----------------------------------------------------------------
    // bytes overlaid with the pending shadow (wrapping at 4 MiB).
    void ReadView(uint32_t byte_off, size_t len, uint8_t* out) const;
    // The COMMITTED bytes only (no pending shadow): the source of a mirror ->
    // attachment refresh, which must observe what the EE has committed, never a
    // deposit the GPU staged and the EE has not yet accepted.
    void ReadMirror(uint32_t byte_off, size_t len, uint8_t* out) const;
    // A refresh/decode/... covered the span in full: clears the owed band (and
    // the stale mark) of every page in the span; `reason` names the consumer.
    void MaterializePages(Span span, Reason reason);
    // Save contract: with no pending deposits no page may be stale. Returns the
    // stale count found (0 = contract held) after reconciling.
    uint32_t AssertNoStaleAtSave();
    // The fold of the view generations of the span's pages (PageRecord::view_gen):
    // two calls that return the same value bracket no mutation of what ReadView
    // over the span returns, so a reader may reuse a result derived from the bytes
    // (a content hash) without reading them again. O(pages), not O(bytes).
    uint64_t ViewSignature(Span span) const;
    // Reader-set seed from the title profile (pages a texture read observes).
    void ReaderPagesSeed(Span span);
    bool NeedsEagerDeposit(Span span) const;
    void ReaderTouch(Span span);

    // ---- deferred-deposit gate ----------------------------------------------------
    // The owner of a writeback whose bytes are not here yet: `span` is owed (its
    // readers / writers run `gate` first; the gate must ClearDeferred before it
    // stages). `commit_owed` set by the gate's completion when a CommitDeposits ran
    // while the span was owed: the completion commits the span's pages itself.
    void SetDeferredGate(std::function<void()> gate) { gate_ = std::move(gate); }
    void NoteDeferred(Span span);
    void ClearDeferred();
    bool HasDeferred() const { return !deferred_.empty(); }
    bool DeferredCommitOwed() const { return deferred_commit_owed_; }
    // Commit the Pending pages of `span` only (the gate's completion of an owed commit).
    void CommitDepositsIn(Span span);
    // Diagnostics: print the first 40 gated (operation, span, owed range) rows on stderr.
    void SetGateTrace(bool on) { gate_trace_ = on; }
    // A raw reader (Bytes() / Page()) over `span`: runs the gate when the span is owed.
    void GateRaw(Span span) const { if (!deferred_.empty()) Gate(span, "raw"); }
    // ---- mirror -----------------------------------------------------------------
    // A VramMirror observes every EE-side mutation of this image (WriteStream,
    // NoteWriteNoBytes, NoteStreamWrite, SettleWrites, NoteCpuWrite) in call
    // order, so a SECOND image replaying the same calls in the same order against
    // its own GPU-side deposits evolves exactly as a shared image would have: the
    // pipelined GS queue's producer/consumer split (one image per thread, the
    // EE's calls carried in the DrawIR stream). The upload epoch depends on the
    // EE-side calls alone, so both images agree on UploadEpoch() by construction.
    void SetMirror(VramMirror* mirror) { mirror_ = mirror; }
    VramMirror* GetMirror() const { return mirror_; }

    // ---- accessors ---------------------------------------------------------------
    static uint32_t PageOf(uint32_t byte_addr) { return (byte_addr % kBytes) / kPageBytes; }
    // First page and page count of a span (count clamped to 512).
    static void PagesOf(Span span, uint32_t* first, uint32_t* count);

    const uint8_t* Bytes() const { return bytes_.data(); }
    uint8_t* MutableBytes() { return bytes_.data(); }
    const PageRecord& Page(uint32_t page) const { return pages_[page % kPageCount]; }
    PageRecord& MutablePage(uint32_t page) { return pages_[page % kPageCount]; }
    uint64_t UploadEpoch() const { return upload_seq_; }
    uint64_t DepositSeq() const { return deposit_seq_; }
    uint32_t PendingPages() const { return pending_; }
    bool HasShadow() const { return static_cast<bool>(shadow_); }
    const Counters& Stats() const { return counters_; }

private:
    void EnsureShadow();
    void StampPages(Span span, uint64_t epoch);
    void MarkWrittenByte(uint32_t addr);  // write bit, CPU author, settle set + watermark

    std::vector<uint8_t> bytes_;
    std::vector<PageRecord> pages_;
    std::unique_ptr<uint8_t[]> shadow_;  // lazily allocated 4 MiB
    uint64_t upload_seq_ = 0;
    uint64_t deposit_seq_ = 0;
    uint32_t pending_ = 0;
    uint32_t settle_lo_ = 0xFFFFFFFFu;  // watermark of unsettled EE writes
    uint32_t settle_hi_ = 0;
    std::vector<uint64_t> settle_pages_;  // 512-bit set of pages written since settle
    mutable Counters counters_;
    VramMirror* mirror_ = nullptr;
    // The deferred-deposit gate (see the header comment). Mutable: the const
    // readers run the gate, which completes a writeback into this image.
    struct Owed { uint32_t lo, hi; };   // [lo, hi) bytes, clamped to 4 MiB (no wrap)
    mutable std::vector<Owed> deferred_;
    mutable bool deferred_commit_owed_ = false;
    std::function<void()> gate_;
    void Gate(Span span, const char* what) const;
    bool gate_trace_ = false;   // SetGateTrace: the first gated spans on stderr
    mutable uint32_t gate_traced_ = 0;
    bool GateAll() const;   // a whole-image observation: the gate when anything is owed
};

}  // namespace gs
}  // namespace ps2r

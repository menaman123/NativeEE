// gs/vram/vram_image.cpp -- VramImage (see vram_image.h).
#include "engine/gs/vram/vram_image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ps2r {
namespace gs {

VramImage::VramImage() : bytes_(kBytes, 0), pages_(kPageCount), settle_pages_(kPageCount / 64, 0) {}

void VramImage::PagesOf(Span span, uint32_t* first, uint32_t* count) {
    const uint32_t start = span.start % kBytes;
    *first = start / kPageBytes;
    uint64_t n = (static_cast<uint64_t>(start % kPageBytes) + span.size + kPageBytes - 1) / kPageBytes;
    if (span.size == 0) n = 0;
    if (n > kPageCount) n = kPageCount;
    *count = static_cast<uint32_t>(n);
}

void VramImage::EnsureShadow() {
    if (!shadow_) {
        shadow_.reset(new uint8_t[kBytes]);
        std::memset(shadow_.get(), 0, kBytes);
    }
}

void VramImage::StampPages(Span span, uint64_t epoch) {
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i) {
        PageRecord& p = pages_[(first + i) % kPageCount];
        p.upload_epoch = epoch;
        p.author = PageRecord::Author::Cpu;
        p.author_fbp = 0;
        p.ClearBand();
        ++p.view_gen;   // the overlay predicate's input moved
    }
}

uint64_t VramImage::ViewSignature(Span span) const {
    Gate(span, "ViewSignature");
    uint32_t first, count;
    PagesOf(span, &first, &count);
    // FNV-1a over (page index, view generation) pairs: a span whose pages all kept
    // their generations folds to the same value; any bump changes it.
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t pg = (first + i) % kPageCount;
        const uint64_t v[2] = {pg, pages_[pg].view_gen};
        const uint8_t* b = reinterpret_cast<const uint8_t*>(v);
        for (size_t k = 0; k < sizeof(v); ++k) h = (h ^ b[k]) * 1099511628211ull;
    }
    return h;
}

// ---- deferred-deposit gate ------------------------------------------------------------

void VramImage::NoteDeferred(Span span) {
    if (span.size == 0) return;
    const uint64_t lo = span.start % kBytes;
    const uint64_t hi = std::min<uint64_t>(lo + span.size, kBytes);
    deferred_.push_back(Owed{static_cast<uint32_t>(lo), static_cast<uint32_t>(hi)});
}

void VramImage::ClearDeferred() {
    deferred_.clear();
    deferred_commit_owed_ = false;
}

// The gate over `span`: when an owed range overlaps it, the owner completes every
// deferred writeback (its completion clears the registration first, so the
// staging it performs passes through here unhindered).
void VramImage::Gate(Span span, const char* what) const {
    if (deferred_.empty() || span.size == 0 || !gate_) return;
    const uint64_t lo = span.start % kBytes;
    uint64_t hi = lo + span.size;
    const bool wraps = hi > kBytes;
    if (wraps) hi = kBytes;
    for (const Owed& o : deferred_) {
        if ((lo < o.hi && o.lo < hi) || (wraps && o.lo < (span.start + span.size) % kBytes)) {
            if (gate_trace_ && gate_traced_ < 40) {
                ++gate_traced_;
                std::fprintf(stderr, "[vram-gate] %s span=%x+%x owed=%x..%x\n", what, span.start, span.size, o.lo, o.hi);
            }
            counters_.deferred_gates++;
            gate_();
            return;
        }
    }
}

bool VramImage::GateAll() const {
    if (deferred_.empty() || !gate_) return false;
    counters_.deferred_gates++;
    gate_();
    return true;
}

void VramImage::CommitDepositsIn(Span span) {
    if (pending_ == 0 || !shadow_) return;
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t pg = (first + i) % kPageCount;
        PageRecord& p = pages_[pg];
        if (p.state != PageRecord::State::Pending) continue;
        p.state = PageRecord::State::Applying;
        if (p.upload_epoch <= p.shadow_epoch) {
            std::memcpy(bytes_.data() + pg * kPageBytes, shadow_.get() + pg * kPageBytes, kPageBytes);
            p.deposit_epoch = ++deposit_seq_;
            counters_.deposits_committed++;
        } else {
            counters_.deposits_dropped_newer++;
        }
        p.shadow_epoch = 0;
        p.state = PageRecord::State::Clean;
        ++p.view_gen;
        pending_--;
    }
}

// ---- EE side ----------------------------------------------------------------------

void VramImage::MarkWrittenByte(uint32_t a) {
    PageRecord& p = pages_[a / kPageBytes];
    p.write_bits |= 1ull << ((a % kPageBytes) / PageRecord::kSubPageBytes);
    p.author = PageRecord::Author::Cpu;
    settle_pages_[(a / kPageBytes) / 64] |= 1ull << ((a / kPageBytes) % 64);
    if (a < settle_lo_) settle_lo_ = a;
    if (a > settle_hi_) settle_hi_ = a;
}

void VramImage::WriteStream(uint32_t byte_off, const uint8_t* src, size_t len) {
    if (len == 0) return;
    Gate(Span{byte_off, static_cast<uint32_t>(std::min<size_t>(len, kBytes))}, "WriteStream");
    if (mirror_) mirror_->OnWriteStream(byte_off, src, len);
    counters_.stream_writes++;
    // Per page run: copy the bytes, set the 128 B write bits the run covers, mark
    // the page (author, settle set, watermark) and bump its view generation once.
    // Byte-for-byte the same marks as a per-byte walk (the watermark is the min /
    // max address touched; a wrap at 4 MiB splits into two runs).
    uint32_t addr = byte_off % kBytes;
    size_t done = 0;
    while (done < len) {
        const uint32_t page = addr / kPageBytes;
        const uint32_t in_page = addr % kPageBytes;
        size_t n = kPageBytes - in_page;
        if (n > len - done) n = len - done;
        std::memcpy(bytes_.data() + addr, src + done, n);
        PageRecord& p = pages_[page];
        const uint32_t sub0 = in_page / PageRecord::kSubPageBytes;
        const uint32_t sub1 = (in_page + static_cast<uint32_t>(n) - 1) / PageRecord::kSubPageBytes;
        const uint32_t nsub = sub1 - sub0 + 1;
        const uint64_t bits = nsub >= 64 ? ~0ull : (((1ull << nsub) - 1) << sub0);
        p.write_bits |= bits;
        p.author = PageRecord::Author::Cpu;
        ++p.view_gen;
        settle_pages_[page / 64] |= 1ull << (page % 64);
        if (addr < settle_lo_) settle_lo_ = addr;
        const uint32_t last = addr + static_cast<uint32_t>(n) - 1;
        if (last > settle_hi_) settle_hi_ = last;
        done += n;
        addr = (addr + static_cast<uint32_t>(n)) % kBytes;
    }
}

void VramImage::NoteWriteNoBytes(uint32_t byte_off, size_t len) {
    if (len == 0) return;
    Gate(Span{byte_off, static_cast<uint32_t>(std::min<size_t>(len, kBytes))}, "NoteWriteNoBytes");
    if (mirror_) mirror_->OnNoteWriteNoBytes(byte_off, len);
    // Per page run, the same marks the per-byte walk (MarkWrittenByte) sets --
    // the 128 B write bits the run covers, the CPU author, the settle set, the
    // watermark -- without a per-byte loop: an owned texture upload notes a
    // whole slot footprint (up to 256 KiB) here, ~80 times a frame.
    uint32_t addr = byte_off % kBytes;
    size_t done = 0;
    while (done < len) {
        const uint32_t page = addr / kPageBytes;
        const uint32_t in_page = addr % kPageBytes;
        size_t n = kPageBytes - in_page;
        if (n > len - done) n = len - done;
        PageRecord& p = pages_[page];
        const uint32_t sub0 = in_page / PageRecord::kSubPageBytes;
        const uint32_t sub1 = (in_page + static_cast<uint32_t>(n) - 1) / PageRecord::kSubPageBytes;
        const uint32_t nsub = sub1 - sub0 + 1;
        const uint64_t bits = nsub >= 64 ? ~0ull : (((1ull << nsub) - 1) << sub0);
        p.write_bits |= bits;
        p.author = PageRecord::Author::Cpu;
        settle_pages_[page / 64] |= 1ull << (page % 64);
        if (addr < settle_lo_) settle_lo_ = addr;
        const uint32_t last = addr + static_cast<uint32_t>(n) - 1;
        if (last > settle_hi_) settle_hi_ = last;
        done += n;
        addr = (addr + static_cast<uint32_t>(n)) % kBytes;
    }
}

uint64_t VramImage::NoteStreamWrite(Span dest) {
    if (dest.size == 0) return upload_seq_;
    Gate(dest, "NoteStreamWrite");
    if (mirror_) mirror_->OnNoteStreamWrite(dest.start, dest.size);
    const uint64_t epoch = ++upload_seq_;
    StampPages(dest, epoch);
    return epoch;
}

void VramImage::NoteCpuWrite(Span span) {
    if (span.size == 0) return;
    Gate(span, "NoteCpuWrite");
    if (mirror_) mirror_->OnNoteCpuWrite(span.start, span.size);
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i) {
        PageRecord& p = pages_[(first + i) % kPageCount];
        p.author = PageRecord::Author::Cpu;
        p.author_fbp = 0;
        p.ClearBand();
    }
}

void VramImage::SettleWrites() {
    if (mirror_) mirror_->OnSettleWrites();
    if (settle_lo_ <= settle_hi_) {
        counters_.settles++;
        const uint64_t epoch = ++upload_seq_;
        for (uint32_t pg = 0; pg < kPageCount; ++pg) {
            if (!(settle_pages_[pg / 64] & (1ull << (pg % 64)))) continue;
            PageRecord& p = pages_[pg];
            p.upload_epoch = epoch;
            p.author = PageRecord::Author::Cpu;
            p.author_fbp = 0;
            p.ClearBand();
            p.write_bits = 0;
            ++p.view_gen;   // upload_epoch moved: the overlay predicate may flip
        }
        for (auto& w : settle_pages_) w = 0;
        settle_lo_ = 0xFFFFFFFFu;
        settle_hi_ = 0;
    }
    CommitDeposits();
}

// ---- GPU side -----------------------------------------------------------------------

void VramImage::StageDeposit(uint32_t byte_off, const uint8_t* src, size_t len, uint64_t batch_epoch) {
    if (len == 0) return;
    Gate(Span{byte_off, static_cast<uint32_t>(std::min<size_t>(len, kBytes))}, "StageDeposit");
    EnsureShadow();
    counters_.deposits_staged++;
    uint32_t first, count;
    PagesOf(Span{byte_off, static_cast<uint32_t>(len)}, &first, &count);
    // Bracket: Clean/Pending -> Encoding (a page mid-commit is never reused).
    for (uint32_t i = 0; i < count; ++i) {
        PageRecord& p = pages_[(first + i) % kPageCount];
        if (p.state == PageRecord::State::Clean) {
            const uint32_t base = ((first + i) % kPageCount) * kPageBytes;
            std::memcpy(shadow_.get() + base, bytes_.data() + base, kPageBytes);
            pending_++;
        }
        p.state = PageRecord::State::Encoding;
    }
    const uint32_t addr = byte_off % kBytes;
    for (size_t i = 0; i < len; ++i) shadow_[(addr + static_cast<uint32_t>(i)) % kBytes] = src[i];
    for (uint32_t i = 0; i < count; ++i) {
        PageRecord& p = pages_[(first + i) % kPageCount];
        if (batch_epoch > p.shadow_epoch) p.shadow_epoch = batch_epoch;
        p.state = PageRecord::State::Pending;
        ++p.view_gen;   // shadow content / state / shadow_epoch moved
    }
}

void VramImage::CommitDeposits() {
    // A deferred writeback is not staged yet: the commit it would have joined is
    // owed to it (its completion commits its own pages); the rest commits now.
    if (!deferred_.empty()) deferred_commit_owed_ = true;
    if (pending_ == 0 || !shadow_) return;
    for (uint32_t pg = 0; pg < kPageCount; ++pg) {
        PageRecord& p = pages_[pg];
        if (p.state != PageRecord::State::Pending) continue;
        p.state = PageRecord::State::Applying;
        if (p.upload_epoch <= p.shadow_epoch) {
            std::memcpy(bytes_.data() + pg * kPageBytes, shadow_.get() + pg * kPageBytes, kPageBytes);
            p.deposit_epoch = ++deposit_seq_;
            counters_.deposits_committed++;
        } else {
            counters_.deposits_dropped_newer++;  // a newer EE writer owns the page
        }
        p.shadow_epoch = 0;
        p.state = PageRecord::State::Clean;
        ++p.view_gen;   // bytes replaced, or the overlay withdrawn
        pending_--;
    }
}

void VramImage::NoteGpuAuthor(Span span, uint32_t fbp) {
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i) {
        PageRecord& p = pages_[(first + i) % kPageCount];
        p.author = PageRecord::Author::GpuTarget;
        p.author_fbp = fbp;
        p.ClearBand();
    }
}

void VramImage::NoteGpuElided(Span span, uint32_t fbp, int32_t y0, int32_t y1) {
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i) {
        PageRecord& p = pages_[(first + i) % kPageCount];
        p.author = PageRecord::Author::GpuTarget;
        p.author_fbp = fbp;
        p.UnionBand(y0, y1);
    }
}

// ---- readers --------------------------------------------------------------------------

void VramImage::ReadMirror(uint32_t byte_off, size_t len, uint8_t* out) const {
    Gate(Span{byte_off, static_cast<uint32_t>(std::min<size_t>(len, kBytes))}, "ReadMirror");
    const uint32_t start = byte_off % kBytes;
    for (size_t done = 0; done < len;) {
        const uint32_t a = (start + static_cast<uint32_t>(done)) % kBytes;
        size_t n = kBytes - a;
        if (n > len - done) n = len - done;
        std::memcpy(out + done, bytes_.data() + a, n);
        done += n;
    }
}

void VramImage::ReadView(uint32_t byte_off, size_t len, uint8_t* out) const {
    Gate(Span{byte_off, static_cast<uint32_t>(std::min<size_t>(len, kBytes))}, "ReadView");
    const uint32_t start = byte_off % kBytes;
    for (size_t done = 0; done < len;) {
        const uint32_t a = (start + static_cast<uint32_t>(done)) % kBytes;
        size_t n = kBytes - a;
        if (n > len - done) n = len - done;
        std::memcpy(out + done, bytes_.data() + a, n);
        done += n;
    }
    if (pending_ == 0 || !shadow_) return;
    for (size_t done = 0; done < len;) {
        const uint32_t a = (start + static_cast<uint32_t>(done)) % kBytes;
        const uint32_t page_end = (a / kPageBytes + 1) * kPageBytes;
        size_t n = page_end - a;
        if (n > len - done) n = len - done;
        // The pending shadow stands in only where a commit would apply it: a page
        // stamped by a NEWER EE write (upload_epoch > the deposit's batch epoch) shows
        // its bytes -- CommitDeposits drops that deposit, and the GS orders a
        // host->local transfer after the draw that produced the deposit, so a texture
        // read of the page sees the transferred texels, never the older frame-buffer
        // content (GS User's Manual 4.2: local memory writes complete in stream order).
        const PageRecord& p = pages_[a / kPageBytes];
        if (p.state != PageRecord::State::Clean && p.upload_epoch <= p.shadow_epoch)
            std::memcpy(out + done, shadow_.get() + a, n);
        done += n;
    }
}

void VramImage::MaterializePages(Span span, Reason reason) {
    (void)reason;
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i) pages_[(first + i) % kPageCount].ClearBand();
}

uint32_t VramImage::AssertNoStaleAtSave() {
    GateAll();
    uint32_t stale = 0;
    for (PageRecord& p : pages_)
        if (p.stale) stale++;
    if (pending_ == 0 && stale) {
        for (PageRecord& p : pages_) p.ClearBand();
        counters_.save_reconciled += stale;
    }
    return stale;
}

void VramImage::ReaderPagesSeed(Span span) {
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i) pages_[(first + i) % kPageCount].reader_flag = true;
}

bool VramImage::NeedsEagerDeposit(Span span) const {
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i)
        if (pages_[(first + i) % kPageCount].reader_flag) return true;
    return false;
}

void VramImage::ReaderTouch(Span span) {
    uint32_t first, count;
    PagesOf(span, &first, &count);
    for (uint32_t i = 0; i < count; ++i)
        if (pages_[(first + i) % kPageCount].stale) counters_.reader_stale_touches++;
}

}  // namespace gs
}  // namespace ps2r

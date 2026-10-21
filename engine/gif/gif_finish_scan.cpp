#include "engine/gif/gif_finish_scan.h"

#include <cstring>

namespace {
constexpr size_t kQw = 16;
constexpr uint8_t kDescAd = 0x0E;
constexpr uint8_t kRegFinish = 0x61;

uint64_t LoadU64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}
}  // namespace

void GifFinishScan::Reset() {
    in_transfer_ = false;
    tag_ = GifTag{};
    cls_ = GifTagClass{};
    loop_ = 0;
    reg_ = 0;
    tail_fill_ = 0;
}

bool GifFinishScan::MidImage() const { return in_transfer_ && tag_.flg == GifFormat::Image && loop_ < tag_.nloop; }

// One 16-byte unit: a tag when idle (GifParser::AcceptTag), else one payload
// quadword (ConsumePackedGeneric / ConsumePackedBulk / ConsumeReglist / one
// IMAGE quadword / FLG 3), closing the tag when its loops are done.
uint32_t GifFinishScan::ScanUnit(const uint8_t* qw) {
    if (!in_transfer_) {
        tag_.Parse(LoadU64(qw), LoadU64(qw + 8));
        cls_ = ClassifyGifTag(tag_);
        loop_ = 0;
        reg_ = 0;
        // NLOOP 0 with EOP completes at once; without EOP the parser keeps the tag
        // accepted and its next unit decides (below).
        in_transfer_ = !(tag_.nloop == 0 && tag_.eop);
        return 0;
    }
    if (loop_ >= tag_.nloop) {
        // Only an NLOOP 0 tag without EOP gets here. IMAGE and the bulk PACKED lane
        // consume nothing and close (this unit is the next tag); the generic PACKED
        // lane, REGLIST and FLG 3 consume this one quadword.
        const bool zero_len = tag_.flg == GifFormat::Image ||
                              (tag_.flg == GifFormat::Packed && bulk_lanes_ && cls_.all_known && !cls_.has_ad);
        if (zero_len) {
            in_transfer_ = false;
            return ScanUnit(qw);
        }
    }
    uint32_t found = 0;
    switch (tag_.flg) {
        case GifFormat::Packed:
            if (tag_.regs[reg_] == kDescAd && qw[8] == kRegFinish) ++found;
            if (++reg_ >= tag_.nregs) { reg_ = 0; ++loop_; }
            break;
        case GifFormat::Reglist:
            // Two 64-bit registers per quadword; descriptors are 4-bit, so no FINISH here.
            if (++reg_ >= tag_.nregs) { reg_ = 0; ++loop_; }
            if (loop_ < tag_.nloop) {
                if (++reg_ >= tag_.nregs) { reg_ = 0; ++loop_; }
            }
            break;
        case GifFormat::Image:
            ++loop_;
            break;
        case GifFormat::Disabled:
            break;   // FLG 3: consumed, nothing advances
    }
    if (loop_ >= tag_.nloop) in_transfer_ = false;
    return found;
}

uint32_t GifFinishScan::Feed(const uint8_t* data, size_t size) {
    uint32_t found = 0;
    size_t i = 0;
    if (tail_fill_ != 0) {
        const size_t take = kQw - tail_fill_ < size ? kQw - tail_fill_ : size;
        std::memcpy(tail_ + tail_fill_, data, take);
        tail_fill_ += take;
        i = take;
        if (tail_fill_ < kQw) return 0;
        found += ScanUnit(tail_);
        tail_fill_ = 0;
    }
    for (; i + kQw <= size; i += kQw) found += ScanUnit(data + i);
    if (i < size) {
        tail_fill_ = size - i;
        std::memcpy(tail_, data + i, tail_fill_);
    }
    return found;
}

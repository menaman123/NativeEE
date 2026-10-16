#include "engine/gif/gif_parser.h"

#include <algorithm>
#include <cstring>

#include "engine/core/log/log.h"
#include "engine/gif/gif_register_dispatch.h"

#if defined(PS2R_SIMD) && defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace {

constexpr size_t kQw = 16;

uint64_t LoadU64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

#if defined(PS2R_SIMD) && defined(__aarch64__) && defined(__ARM_NEON)
// A decoded vertex register for the NEON strip3 lane: x,y in pixels, z as the
// raw integer, the fog byte and the ADC (no-draw) bit.
struct DecodedXyz {
    float x;
    float y;
    uint32_t zu;
    uint8_t f;
    bool adc;
};

void KickPacked(GsSink& sink, const DecodedXyz& v, bool kick_reg) {
    sink.KickVertex(v.x, v.y, static_cast<float>(v.zu), v.f, kick_reg && !v.adc);
}
#endif

}  // namespace

GifParser::GifParser(GsSink& sink, GifRegs& regs, GifParserSettings settings)
    : sink_(sink), regs_(regs), settings_(settings) {}

void GifParser::ReceiveData(GifPath path, const uint8_t* data, size_t size) {
    if (size == 0) return;
    ++receives_[static_cast<unsigned>(path) & 3];
    received_bytes_[static_cast<unsigned>(path) & 3] += size;
    GifTransferState& t = regs_.Transfer();
    if (path == GifPath::Path3 && t.in_transfer) {
        t.in_transfer = false;
        t.loop = 0;
        t.reg = 0;
        tail_.clear();
    }
    active_path_ = path;
    PS2R_LOG(GIF, Narrate, "gif: path%d receive %zu bytes (tail=%zu in_transfer=%d loop=%u m3r=%d)", static_cast<int>(path),
             size, tail_.size(), t.in_transfer ? 1 : 0, t.loop, regs_.Path3Masked() ? 1 : 0);
    if (tail_.empty() && !regs_.Path3Masked()) {
        const size_t consumed = ParseSpan(data, size);
        if (consumed < size) tail_.assign(data + consumed, data + size);
        return;
    }
    tail_.insert(tail_.end(), data, data + size);
    ProcessBuffer();
}

void GifParser::ProcessBuffer() {
    if (tail_.empty()) return;
    if (regs_.Path3Masked()) return;
    const size_t n = ParseSpan(tail_.data(), tail_.size());
    if (n == tail_.size()) {
        tail_.clear();
    } else if (n > 0) {
        tail_.erase(tail_.begin(), tail_.begin() + static_cast<std::ptrdiff_t>(n));
    }
}

// Accept the GIFtag at `p`: the GS resets Q to 1.0, PRE writes PRIM, and an
// NLOOP of 0 completes immediately (EOP still flushes). Returns false when the
// tag has no payload to walk.
bool GifParser::AcceptTag(const uint8_t* p) {
    GifTransferState& t = regs_.Transfer();
    t.tag.Parse(LoadU64(p), LoadU64(p + 8));
    t.in_transfer = true;
    t.loop = 0;
    t.reg = 0;
    sink_.BeginTag(t.tag, active_path_);
    tag_class_ = ClassifyGifTag(t.tag);
    if (t.tag.pre) sink_.WriteRegister(static_cast<uint8_t>(GsReg::Prim), t.tag.prim);
    if (t.tag.nloop == 0) {
        if (t.tag.eop) {
            sink_.EndOfPacket();
            t.in_transfer = false;
        }
        return false;
    }
    return true;
}

// Walk as much of `buf` as forms whole units: a 16-byte tag when idle, then
// 16-byte payload quadwords until the tag's NLOOP loops are done. Returns the
// bytes consumed; a trailing partial unit is left for the caller to buffer.
size_t GifParser::ParseSpan(const uint8_t* buf, size_t len) {
    GifTransferState& t = regs_.Transfer();
    size_t processed = 0;
    while (processed < len) {
        if (!t.in_transfer) {
            if (len - processed < kQw) break;
            const uint8_t* tag_ptr = buf + processed;
            processed += kQw;
            if (!AcceptTag(tag_ptr)) continue;
        }
        if (len - processed < kQw) break;
        const uint8_t* p = buf + processed;
        const size_t avail_qw = (len - processed) / kQw;
        size_t consumed = kQw;
        switch (t.tag.flg) {
            case GifFormat::Image:
                consumed = ConsumeImage(p, avail_qw);
                break;
            case GifFormat::Packed:
                if (settings_.bulk_lanes && tag_class_.all_known && !tag_class_.has_ad) {
                    consumed = ConsumePackedBulk(p, avail_qw);
                } else {
                    ConsumePackedGeneric(p);
                }
                break;
            case GifFormat::Reglist:
                ConsumeReglist(p);
                break;
            case GifFormat::Disabled:
                // FLG 3: the quadword is consumed and nothing advances.
                break;
        }
        processed += consumed;
        if (t.loop >= t.tag.nloop) {
            t.in_transfer = false;
            if (t.tag.eop) sink_.EndOfPacket();
        }
    }
    return processed;
}

// IMAGE: every quadword is transfer data; take as many as remain for the tag
// and are available.
size_t GifParser::ConsumeImage(const uint8_t* p, size_t avail_qw) {
    GifTransferState& t = regs_.Transfer();
    const size_t want_qw = static_cast<uint32_t>(t.tag.nloop) - t.loop;
    const size_t n = std::min(want_qw, avail_qw);
    sink_.ImageSpan(p, n);
    t.loop += static_cast<uint32_t>(n);
    return kQw * n;
}

// Advance the descriptor cursor by one quadword: wrap to the next loop after
// the last descriptor.
void GifParser::AdvanceDescriptor() {
    GifTransferState& t = regs_.Transfer();
    ++t.reg;
    if (t.reg >= t.tag.nregs) {
        t.reg = 0;
        ++t.loop;
    }
}

// PACKED tags whose descriptors are all in the decoded set: consume the whole
// available run, whole strip3 triples or whole rows through the lanes when the
// run starts on a row boundary, the remainder one descriptor at a time.
size_t GifParser::ConsumePackedBulk(const uint8_t* p, size_t avail_qw) {
    GifTransferState& t = regs_.Transfer();
    const uint32_t nregs_eff = t.tag.nregs;
    const uint64_t remaining =
        static_cast<uint64_t>(static_cast<uint32_t>(t.tag.nloop) - t.loop) * nregs_eff - t.reg;
    const size_t run = static_cast<size_t>(std::min<uint64_t>(remaining, avail_qw));
    bulk_quads_ += run;
    size_t left = run;
    if (tag_class_.strip3 && t.reg == 0 && left >= 3) {
        const bool xyzf = t.tag.regs[2] == static_cast<uint8_t>(GsReg::Xyzf2);
        const size_t triples = left / 3;
        for (size_t i = 0; i < triples; ++i, p += 3 * kQw) StageTriple(p, xyzf);
        left -= triples * 3;
        t.loop += static_cast<uint32_t>(triples);
        strip3_triples_ += triples;
    }
    if (tag_class_.rowable && t.reg == 0 && left >= nregs_eff) {
        const size_t rows = left / nregs_eff;
        for (size_t r = 0; r < rows; ++r) {
            for (uint32_t j = 0; j < nregs_eff; ++j, p += kQw) ProcessPackedKnown(t.tag.regs[j], p);
        }
        left -= rows * nregs_eff;
        t.loop += static_cast<uint32_t>(rows);
        rowable_rows_ += rows;
    }
    while (left > 0) {
        ProcessPackedKnown(t.tag.regs[t.reg], p);
        p += kQw;
        --left;
        AdvanceDescriptor();
    }
    return kQw * run;
}

// One strip3 vertex: ST, RGBAQ, then the XYZ2/XYZF2 kick, in that order.
// The NEON body is a bit copy of the scalar one: the ST/RGBAQ lanes are moved
// verbatim, u16 -> u32 -> f32 is exact below 2^24 and the divide by 16.0f is
// the same correctly rounded IEEE division, so the sink sees identical values.
#if defined(PS2R_SIMD) && defined(__aarch64__) && defined(__ARM_NEON)
void GifParser::StageTriple(const uint8_t* p, bool xyzf) {
    const uint8x16_t q0 = vld1q_u8(p);
    const uint8x16_t q1 = vld1q_u8(p + 16);
    const uint8x16_t q2 = vld1q_u8(p + 32);
    const float32x4_t stq = vreinterpretq_f32_u8(q0);
    sink_.PackedSt(vgetq_lane_f32(stq, 0), vgetq_lane_f32(stq, 1), vgetq_lane_f32(stq, 2));
    sink_.PackedRgbaq(vgetq_lane_u8(q1, 0), vgetq_lane_u8(q1, 4), vgetq_lane_u8(q1, 8), vgetq_lane_u8(q1, 12));
    const uint32x4_t xy32 = vmovl_u16(vget_low_u16(vreinterpretq_u16_u8(q2)));
    const float32x4_t xyf = vdivq_f32(vcvtq_f32_u32(xy32), vdupq_n_f32(16.0f));
    DecodedXyz v;
    v.x = vgetq_lane_f32(xyf, 0);
    v.y = vgetq_lane_f32(xyf, 2);
    const uint64_t hi2 = vgetq_lane_u64(vreinterpretq_u64_u8(q2), 1);
    if (xyzf) {
        v.zu = static_cast<uint32_t>((hi2 >> 4) & 0xFFFFFFu);
        v.f = static_cast<uint8_t>((hi2 >> 36) & 0xFFu);
    } else {
        v.zu = static_cast<uint32_t>(hi2 & 0xFFFFFFFFu);
        v.f = 0;
    }
    v.adc = ((hi2 >> 47) & 1u) != 0;
    KickPacked(sink_, v, true);
}
#else
void GifParser::StageTriple(const uint8_t* p, bool xyzf) {
    DispatchPackedQuadword(sink_, static_cast<uint8_t>(GsReg::St), LoadU64(p), LoadU64(p + 8));
    DispatchPackedQuadword(sink_, static_cast<uint8_t>(GsReg::Rgbaq), LoadU64(p + 16), LoadU64(p + 24));
    DispatchPackedQuadword(sink_, static_cast<uint8_t>(xyzf ? GsReg::Xyzf2 : GsReg::Xyz2), LoadU64(p + 32),
                           LoadU64(p + 40));
}
#endif

// One PACKED quadword: the decoded set, A+D and the generic descriptors all go
// through the shared DispatchPackedQuadword (gif_register_dispatch.h); the
// bulk lanes above decode the same forms in place (GifParserEquivalence).
void GifParser::ProcessPackedKnown(uint8_t reg, const uint8_t* qw) {
    DispatchPackedQuadword(sink_, reg, LoadU64(qw), LoadU64(qw + 8));
}

void GifParser::ProcessPacked(uint8_t reg, const uint8_t* qw) {
    DispatchPackedQuadword(sink_, reg, LoadU64(qw), LoadU64(qw + 8));
}

void GifParser::ConsumePackedGeneric(const uint8_t* qw) {
    GifTransferState& t = regs_.Transfer();
    ProcessPacked(t.tag.regs[t.reg], qw);
    AdvanceDescriptor();
}

// REGLIST: two 64-bit register values per quadword, low half first; the high
// half of the final quadword is padding when NLOOP x NREG is odd.
void GifParser::ConsumeReglist(const uint8_t* qw) {
    GifTransferState& t = regs_.Transfer();
    DispatchRegister(t.tag.regs[t.reg], LoadU64(qw));
    AdvanceDescriptor();
    if (t.loop < t.tag.nloop) {
        DispatchRegister(t.tag.regs[t.reg], LoadU64(qw + 8));
        AdvanceDescriptor();
    }
}

// The one 64-bit register dispatcher (REGLIST halves and A+D pairs) is shared
// with the HLE kick lowering (gif_register_dispatch.h).
void GifParser::DispatchRegister(uint8_t reg, uint64_t value) {
    DispatchGifRegister(sink_, reg, value);
}

void GifParser::FinishPath3() {
    GifTransferState& t = regs_.Transfer();
    if (t.in_transfer) {
        if (t.tag.flg == GifFormat::Image && t.loop < t.tag.nloop) return;
        t.in_transfer = false;
    }
    sink_.FinishPath3();
    tail_.clear();
}

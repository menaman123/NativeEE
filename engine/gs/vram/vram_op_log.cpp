// gs/vram/vram_op_log.cpp -- VramOpLog (see vram_op_log.h).
#include "engine/gs/vram/vram_op_log.h"

#include <cstring>

namespace ps2r {
namespace gs {

namespace {
uint32_t GetU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
}  // namespace

void VramOpLog::PutU32(uint32_t v) {
    const size_t at = buf_.size();
    buf_.resize(at + 4);
    std::memcpy(buf_.data() + at, &v, 4);
}

void VramOpLog::BeginOp(Op op) {
    run_at_ = SIZE_MAX;
    buf_.push_back(static_cast<uint8_t>(op));
    ++ops_;
}

void VramOpLog::OnWriteStream(uint32_t byte_off, const uint8_t* src, size_t len) {
    if (len == 0) return;
    const uint32_t off = byte_off % VramImage::kBytes;
    if (run_at_ != SIZE_MAX && off == run_end_) {
        // Continue the open run: grow its length, append the bytes.
        const uint32_t cur = GetU32(buf_.data() + run_at_ + 5);
        const uint32_t grown = cur + static_cast<uint32_t>(len);
        std::memcpy(buf_.data() + run_at_ + 5, &grown, 4);
    } else {
        BeginOp(kWriteStream);
        run_at_ = buf_.size() - 1;
        PutU32(off);
        PutU32(static_cast<uint32_t>(len));
    }
    buf_.insert(buf_.end(), src, src + len);
    run_end_ = static_cast<uint32_t>((static_cast<uint64_t>(off) + len) % VramImage::kBytes);
}

void VramOpLog::OnNoteWriteNoBytes(uint32_t byte_off, size_t len) {
    if (len == 0) return;
    BeginOp(kNoteWriteNoBytes);
    PutU32(byte_off);
    PutU32(static_cast<uint32_t>(len));
}

void VramOpLog::OnNoteStreamWrite(uint32_t start, uint32_t size) {
    BeginOp(kNoteStreamWrite);
    PutU32(start);
    PutU32(size);
}

void VramOpLog::OnSettleWrites() { BeginOp(kSettleWrites); }

void VramOpLog::OnNoteCpuWrite(uint32_t start, uint32_t size) {
    BeginOp(kNoteCpuWrite);
    PutU32(start);
    PutU32(size);
}

size_t VramOpLog::Replay(VramImage& target) const {
    size_t n = 0;
    const uint8_t* p = buf_.data();
    const uint8_t* end = p + buf_.size();
    while (p < end) {
        const Op op = static_cast<Op>(*p++);
        switch (op) {
            case kWriteStream: {
                const uint32_t off = GetU32(p), len = GetU32(p + 4);
                p += 8;
                target.WriteStream(off, p, len);
                p += len;
                break;
            }
            case kNoteWriteNoBytes: {
                target.NoteWriteNoBytes(GetU32(p), GetU32(p + 4));
                p += 8;
                break;
            }
            case kNoteStreamWrite: {
                target.NoteStreamWrite(VramImage::Span{GetU32(p), GetU32(p + 4)});
                p += 8;
                break;
            }
            case kSettleWrites:
                target.SettleWrites();
                break;
            case kNoteCpuWrite: {
                target.NoteCpuWrite(VramImage::Span{GetU32(p), GetU32(p + 4)});
                p += 8;
                break;
            }
        }
        ++n;
    }
    return n;
}

void VramOpLog::Clear() {
    buf_.clear();
    ops_ = 0;
    run_at_ = SIZE_MAX;
    run_end_ = 0;
}

}  // namespace gs
}  // namespace ps2r

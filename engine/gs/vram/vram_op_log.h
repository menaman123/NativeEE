// gs/vram/vram_op_log.h -- VramOpLog: a VramImage::Mirror that records the EE-side
// mutations of one image as a compact op stream and replays them, in order, on
// another image.
//
// The pipelined GS queue runs the GS pipeline on its own thread with its OWN
// VramImage; the EE's image keeps the EE-side state (bytes, write marks, the upload
// epoch) for the EE's own reads, and every EE-side call is logged here and carried
// to the consumer image in the DrawIR stream, right before the next record. The
// consumer image then sees the EE's writes exactly where the in-thread consumer
// saw them (nothing runs on the consumer between two records), interleaved with
// its own GPU deposits in stream order -- the shared-image semantics without the
// shared image.
//
// Consecutive WriteStream calls that continue the previous run are coalesced
// into one op (the transfer engine stores one pixel per call): the replayed
// WriteStream sets the same write bits, author, settle set and watermark as the
// separate calls (pinned by Gs_VramViewSignature's per-run == per-byte check).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/gs/vram/vram_image.h"

namespace ps2r {
namespace gs {

class VramOpLog : public VramMirror {
public:
    enum Op : uint8_t { kWriteStream = 1, kNoteWriteNoBytes, kNoteStreamWrite, kSettleWrites, kNoteCpuWrite };

    VramOpLog() = default;
    VramOpLog(VramOpLog&&) noexcept = default;
    VramOpLog& operator=(VramOpLog&&) noexcept = default;

    // VramMirror (the producer side).
    void OnWriteStream(uint32_t byte_off, const uint8_t* src, size_t len) override;
    void OnNoteWriteNoBytes(uint32_t byte_off, size_t len) override;
    void OnNoteStreamWrite(uint32_t start, uint32_t size) override;
    void OnSettleWrites() override;
    void OnNoteCpuWrite(uint32_t start, uint32_t size) override;

    // Apply every recorded op to `target`, in order. Returns the op count.
    size_t Replay(VramImage& target) const;
    // The encoded op stream as bytes (a capture writes it; LoadEncoded rebuilds a
    // log from such bytes -- the op count is recovered by Replay's return).
    const std::vector<uint8_t>& Encoded() const { return buf_; }
    void LoadEncoded(const uint8_t* bytes, size_t n);

    bool Empty() const { return buf_.empty(); }
    size_t Bytes() const { return buf_.size(); }
    size_t Ops() const { return ops_; }
    void Clear();

private:
    void PutU32(uint32_t v);
    void BeginOp(Op op);

    std::vector<uint8_t> buf_;
    size_t ops_ = 0;
    // The open WriteStream run (an op header at `run_at_`, its bytes end at
    // `run_end_` in VRAM); SIZE_MAX = none.
    size_t run_at_ = SIZE_MAX;
    uint32_t run_end_ = 0;
};

}  // namespace gs
}  // namespace ps2r

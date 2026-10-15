// engine/ipu/hle/pss_demux.h -- PssDemux: the ISO/IEC 13818-1 program-stream
// packet walk of the sceMpegDemuxPss path. Bytes are appended as the game (or the
// sector pump) delivers them; Process() consumes whole packs / system headers /
// PES packets from the front of the buffer and reports payloads to the listener.
// Pure: no decoder, no guest access, no clock.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/ipu/hle/pss_demux_listener.h"

namespace ps2r::ipu {

class PssDemux {
public:
    static constexpr size_t kStartCodeNotFound = static_cast<size_t>(-1);
    static constexpr uint8_t kPackHeader = 0xBA;
    static constexpr uint8_t kSystemHeader = 0xBB;
    static constexpr uint8_t kProgramEnd = 0xB9;
    static constexpr uint8_t kSequenceEnd = 0xB7;

    explicit PssDemux(PssDemuxListener& listener);

    // Appends program-stream bytes to the pending buffer (no parsing yet).
    void Append(const uint8_t* data, size_t size);

    // Walks every complete packet at the front of the buffer. A packet that
    // straddles the buffer end waits for the next Append unless `final_chunk`
    // (then whatever is present is taken as the packet's tail). Without a
    // visible start code the last 3 bytes are kept (a prefix of a start code
    // may straddle chunks) unless `final_chunk`. Once the stream has ended
    // (program end code, or a video payload with a sequence end code, or
    // MarkEnded) all pending bytes are discarded.
    void Process(bool final_chunk);

    // Drops pending bytes and clears the ended state (sceMpegReset).
    void Reset();

    // The stream ended by an external event (CD stream end of file): every
    // later byte is discarded.
    void MarkEnded();

    bool Ended() const { return ended_; }
    size_t Buffered() const { return buffer_.size(); }
    // Bytes the walk has taken off the front of the buffer as parsed packets,
    // skipped headers or resync junk -- everything except the bytes discarded
    // because the stream had already ended. After Ended() this is the offset
    // just past the packet that ended the stream.
    uint64_t Consumed() const { return consumed_; }
    uint64_t PacksSeen() const { return packs_; }
    uint64_t SystemHeadersSeen() const { return system_headers_; }

    // Pure classifiers of the packet walk (exposed for the unit tests).
    static size_t FindStartCode(const std::vector<uint8_t>& buf, size_t from);
    static bool ContainsSequenceEnd(const uint8_t* data, size_t size);
    static bool IsVideoStreamId(uint8_t id);
    static bool IsAudioStreamId(uint8_t id);
    static bool IsLengthPrefixedHeader(uint8_t id);
    // Offset of the elementary-stream payload inside a PES packet that begins
    // at `packet` (the start code) and spans `packet_size` bytes.
    static size_t ParsePesPayloadOffset(const uint8_t* packet, size_t packet_size);

private:
    // Removes `n` bytes from the front; `walked` bytes count towards Consumed().
    void ErasePrefix(size_t n, bool walked = true);

    PssDemuxListener& listener_;
    std::vector<uint8_t> buffer_;
    bool ended_ = false;
    uint64_t consumed_ = 0;
    uint64_t packs_ = 0;
    uint64_t system_headers_ = 0;
};

}  // namespace ps2r::ipu

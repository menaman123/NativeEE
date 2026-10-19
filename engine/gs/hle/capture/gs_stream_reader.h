// gs/hle/capture/gs_stream_reader.h -- GsStreamReader: the streaming reader of a
// `.gsstream` capture (gs_stream_format.h). Records are handed out one at a time
// in file order -- a capture of a minute of play is gigabytes, so nothing is held
// beyond the current record. The payload bytes of kVramInit / kVramOps / an upload
// job stay in `bytes`; a kDraw is lifted to DrawIr (DrawIr::FromDrawRec with its
// vertex array); a kFrame yields the VSync JobRec and the unpacked CRTC snapshot.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/gs/hle/capture/gs_stream_format.h"
#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/ir/job_rec.h"
#include "engine/gs/hle/present/crtc_classifier.h"

namespace ps2r {
namespace gs {

class GsStreamReader {
public:
    struct Record {
        uint32_t kind = 0;                    // GsStreamFormat::Kind
        DrawIr draw;                          // kDraw
        JobRec job{};                         // kJob / kFrame
        CrtcClassifier::Snapshot crtc{};      // kFrame
        std::vector<uint8_t> bytes;           // kVramInit / kVramOps payload; kJob upload pixels
    };

    GsStreamReader() = default;
    ~GsStreamReader();
    GsStreamReader(const GsStreamReader&) = delete;
    GsStreamReader& operator=(const GsStreamReader&) = delete;

    // Opens and validates the header. False with `Error()` set otherwise.
    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const { return file_ != nullptr; }
    const GsStreamFileHeader& Header() const { return header_; }
    // The next record in file order; false at the end (or on a truncated /
    // malformed record: `Error()` says which).
    bool Next(Record* out);
    const std::string& Error() const { return error_; }
    uint64_t Records() const { return records_; }

private:
    bool ReadExact(void* dst, size_t n);

    std::FILE* file_ = nullptr;
    std::vector<char> iobuf_;
    GsStreamFileHeader header_{};
    std::string error_;
    uint64_t records_ = 0;
    std::vector<uint8_t> scratch_;
};

}  // namespace gs
}  // namespace ps2r

// gs/hle/capture/gs_stream_format.h -- the `.gsstream` GS consumer-stream capture
// format: EVERYTHING the DrawIR consumer thread receives, in stream order, so a
// replay through the GS pipeline alone (tools/gs_replay stream) sees the inputs
// the live consumer saw -- the draws with their vertices, the jobs WITH the upload
// payloads, the frame boundaries WITH their CRTC snapshot, and the EE-side local
// memory writes (the VramOpLog records of the split image) between them, plus the
// local-memory image as it stood when the capture opened.
//
// Layout (little-endian, every struct packed to 1):
//   GsStreamFileHeader                            once, at offset 0
//   ( GsStreamRecordHeader payload )*             payload size in the header
//     kVramInit : vram_bytes bytes                  the image at open (first record)
//     kVramOps  : VramOpLog::Encoded() bytes        the EE image's calls since the
//                                                   previous record
//     kDraw     : DrawRec + VertexRec[vertex_count] (DrawIr::ToDrawRec, verts_incl 1)
//     kJob      : JobRec [+ payload_bytes pixels]   iff payload_incl (UploadVRAMTexture)
//     kFrame    : JobRec + uint32 CRTC snapshot     CrtcClassifier::Pack of the frame
// No title data, no texture hashes: the bytes themselves travel.
#pragma once

#include <cstdint>

#include "engine/gs/hle/ir/gsdl_format.h"
#include "engine/gs/hle/ir/job_rec.h"

namespace ps2r {
namespace gs {

#pragma pack(push, 1)
struct GsStreamFileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;          // reserved (0)
    uint32_t vram_bytes;     // size of the kVramInit payload (VramImage::kBytes)
    uint64_t start_unix_ns;  // wall clock at open; not content
};

struct GsStreamRecordHeader {
    uint32_t kind;           // GsStreamFormat::Kind
    uint32_t size;           // payload bytes following this header
};

struct GsStreamFrameRec {
    JobRec vsync;
    uint32_t crtc_packed;    // CrtcClassifier::Pack
};
#pragma pack(pop)

class GsStreamFormat {
public:
    static constexpr uint32_t kMagic = 0x4D525453u;  // "STRM"
    static constexpr uint32_t kVersion = 1;

    enum Kind : uint32_t { kVramInit = 1, kVramOps = 2, kDraw = 3, kJob = 4, kFrame = 5 };

    static bool ValidateHeader(const GsStreamFileHeader& h) {
        return h.magic == kMagic && h.version >= 1 && h.version <= kVersion;
    }
};

static_assert(sizeof(GsStreamFileHeader) == 24, "GsStreamFileHeader is 24 bytes");
static_assert(sizeof(GsStreamRecordHeader) == 8, "GsStreamRecordHeader is 8 bytes");
static_assert(sizeof(GsStreamFrameRec) == 56, "GsStreamFrameRec is JobRec + u32");

}  // namespace gs
}  // namespace ps2r

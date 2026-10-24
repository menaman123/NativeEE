// gs/hle/capture/gs_stream_reader.cpp -- GsStreamReader (see the header).
#include "engine/gs/hle/capture/gs_stream_reader.h"

#include <cstring>

namespace ps2r {
namespace gs {

namespace {
constexpr size_t kIoBufferBytes = 4u << 20;
}  // namespace

GsStreamReader::~GsStreamReader() { Close(); }

bool GsStreamReader::Open(const std::string& path) {
    Close();
    error_.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error_ = "cannot open " + path;
        return false;
    }
    iobuf_.resize(kIoBufferBytes);
    std::setvbuf(f, iobuf_.data(), _IOFBF, iobuf_.size());
    file_ = f;
    if (!ReadExact(&header_, sizeof(header_)) || !GsStreamFormat::ValidateHeader(header_)) {
        error_ = "not a .gsstream capture: " + path;
        Close();
        return false;
    }
    records_ = 0;
    return true;
}

void GsStreamReader::Close() {
    if (!file_) return;
    std::fclose(file_);
    file_ = nullptr;
}

bool GsStreamReader::ReadExact(void* dst, size_t n) {
    return file_ && std::fread(dst, 1, n, file_) == n;
}

bool GsStreamReader::Next(Record* out) {
    if (!file_) return false;
    GsStreamRecordHeader rh{};
    if (std::fread(&rh, 1, sizeof(rh), file_) != sizeof(rh)) return false;   // the end
    out->kind = rh.kind;
    out->bytes.clear();
    switch (rh.kind) {
        case GsStreamFormat::kVramInit:
        case GsStreamFormat::kVramOps:
            out->bytes.resize(rh.size);
            if (!ReadExact(out->bytes.data(), rh.size)) {
                error_ = "truncated payload record";
                return false;
            }
            break;
        case GsStreamFormat::kDraw: {
            if (rh.size < sizeof(DrawRec)) {
                error_ = "short draw record";
                return false;
            }
            DrawRec rec{};
            if (!ReadExact(&rec, sizeof(rec))) {
                error_ = "truncated draw record";
                return false;
            }
            const size_t tail = rh.size - sizeof(rec);
            const size_t nverts = rec.verts_incl ? tail / sizeof(VertexRec) : 0;
            scratch_.resize(tail);
            if (tail && !ReadExact(scratch_.data(), tail)) {
                error_ = "truncated vertex array";
                return false;
            }
            out->draw = DrawIr::FromDrawRec(rec, reinterpret_cast<const VertexRec*>(scratch_.data()), nverts);
            break;
        }
        case GsStreamFormat::kJob: {
            if (rh.size < sizeof(JobRec)) {
                error_ = "short job record";
                return false;
            }
            if (!ReadExact(&out->job, sizeof(JobRec))) {
                error_ = "truncated job record";
                return false;
            }
            const size_t tail = rh.size - sizeof(JobRec);
            out->bytes.resize(tail);
            if (tail && !ReadExact(out->bytes.data(), tail)) {
                error_ = "truncated upload payload";
                return false;
            }
            break;
        }
        case GsStreamFormat::kFrame: {
            if (rh.size != sizeof(GsStreamFrameRec)) {
                error_ = "frame record size";
                return false;
            }
            GsStreamFrameRec fr{};
            if (!ReadExact(&fr, sizeof(fr))) {
                error_ = "truncated frame record";
                return false;
            }
            out->job = fr.vsync;
            out->crtc = CrtcClassifier::Unpack(fr.crtc_packed);
            break;
        }
        default:
            error_ = "unknown record kind " + std::to_string(rh.kind);
            return false;
    }
    ++records_;
    return true;
}

}  // namespace gs
}  // namespace ps2r

// core/diag/drawlist_recorder.cpp -- DrawlistRecorder (see the header).
#include "engine/core/diag/drawlist_recorder.h"

#include <chrono>
#include <cstring>

#include "engine/gs/hle/ir/gsdl_format.h"

namespace ps2r {
namespace diag {

DrawlistRecorder::DrawlistRecorder(gs::DrawIrSink* next, NameResolver names)
    : next_(next), names_(std::move(names)) {}

DrawlistRecorder::~DrawlistRecorder() { Close(); }

bool DrawlistRecorder::Open(const std::string& path) {
    Close();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    gs::GsdlFileHeader h{};
    h.magic = gs::GsdlFormat::kMagic;
    h.version = gs::GsdlFormat::kVersion;
    h.flags = gs::GsdlFormat::kFlagVerts;  // pixels never reach the sink boundary
    h.reserved = 0;
    h.start_unix_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    if (std::fwrite(&h, 1, sizeof(h), f) != sizeof(h)) {
        std::fclose(f);
        return false;
    }
    file_ = f;
    path_ = path;
    bytes_ = sizeof(h);
    named_.clear();
    return true;
}

void DrawlistRecorder::Close() {
    if (!file_) return;
    std::fclose(file_);
    file_ = nullptr;
}

void DrawlistRecorder::WriteRecord(uint32_t kind, const void* payload, size_t size, const void* tail,
                                   size_t tail_size) {
    if (!file_) return;
    gs::GsdlRecordHeader rh{};
    rh.kind = kind;
    rh.size = static_cast<uint32_t>(size + tail_size);
    std::fwrite(&rh, 1, sizeof(rh), file_);
    std::fwrite(payload, 1, size, file_);
    if (tail && tail_size) std::fwrite(tail, 1, tail_size, file_);
    bytes_ += sizeof(rh) + size + tail_size;
}

void DrawlistRecorder::OnDraw(const gs::DrawIr& draw) {
    if (file_) {
        const uint32_t handler = draw.identity.hle_handler;
        if (handler != 0 && names_ && named_.insert(handler).second) {
            if (const char* name = names_(handler)) {
                gs::GsdlNameRec nr{};
                nr.handler = handler;
                nr.name_len = static_cast<uint32_t>(std::strlen(name));
                WriteRecord(gs::GsdlFormat::kName, &nr, sizeof(nr), name, nr.name_len);
            }
        }
        gs::DrawRec rec = draw.ToDrawRec();
        const bool whole = !draw.verts.empty() && draw.verts.size() == draw.vertex_count;
        rec.verts_incl = whole ? 1u : 0u;
        WriteRecord(gs::GsdlFormat::kDraw, &rec, sizeof(rec), whole ? draw.verts.data() : nullptr,
                    whole ? draw.verts.size() * sizeof(gs::VertexRec) : 0);
        ++draws_;
    }
    if (next_) next_->OnDraw(draw);
}

void DrawlistRecorder::OnJob(const gs::JobRec& job) {
    if (file_) {
        gs::JobRec j = job;
        j.payload_incl = 0;  // the pixel bytes are not available here
        WriteRecord(gs::GsdlFormat::kJob, &j, sizeof(j));
        ++jobs_;
    }
    if (next_) next_->OnJob(job);
}

void DrawlistRecorder::OnFrame(const gs::JobRec& vsync) {
    if (file_) {
        gs::JobRec j = vsync;
        j.payload_incl = 0;
        WriteRecord(gs::GsdlFormat::kFrame, &j, sizeof(j));
        ++frames_;
        std::fflush(file_);
    }
    if (next_) next_->OnFrame(vsync);
}

std::string DrawlistRecorder::Report() const {
    return "path=" + (path_.empty() ? std::string("off") : path_) + " draws=" + std::to_string(draws_) +
           " jobs=" + std::to_string(jobs_) + " frames=" + std::to_string(frames_) + " bytes=" + std::to_string(bytes_);
}

}  // namespace diag
}  // namespace ps2r

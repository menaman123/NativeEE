// core/diag/drawlist_recorder.h -- DrawlistRecorder: a DrawIrSink decorator that
// writes every draw, job and frame boundary it sees as a `.gsdl` recording
// (GsdlFormat version 2) and forwards each call to the next sink. Diagnostics
// builds only (the ps2r_diag target): it sits between the counting sink and the
// consumer in the root's DrawIR chain, so the file holds exactly the stream the
// consumer rendered.
//
// Records: kName once per distinct hle_handler before that handler's first draw
// (when the name resolver knows it), kDraw = DrawIr::ToDrawRec() followed by the
// vertex array when the draw carries it whole, kJob = the JobRec as received,
// kFrame = the VSync job. Upload pixels do not cross the sink boundary (JobRec
// carries their size and hash only), so the header flag says vertices only and
// every kJob has payload_incl 0.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <string>

#include "engine/gs/hle/ir/draw_ir_sink.h"

namespace ps2r {
namespace diag {

class DrawlistRecorder : public gs::DrawIrSink {
public:
    // handler id -> program name (nullptr when unknown); may be empty.
    using NameResolver = std::function<const char*(uint32_t)>;

    explicit DrawlistRecorder(gs::DrawIrSink* next, NameResolver names = nullptr);
    ~DrawlistRecorder() override;

    // Opens `path` and writes the file header; false (nothing recorded, calls still
    // forwarded) when the file cannot be created.
    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const { return file_ != nullptr; }
    const std::string& Path() const { return path_; }

    void OnDraw(const gs::DrawIr& draw) override;
    void OnJob(const gs::JobRec& job) override;
    void OnFrame(const gs::JobRec& vsync) override;

    uint64_t Draws() const { return draws_; }
    uint64_t Jobs() const { return jobs_; }
    uint64_t Frames() const { return frames_; }
    uint64_t Bytes() const { return bytes_; }
    // "path=<p> draws=<n> jobs=<n> frames=<n> bytes=<n>" (the registry report).
    std::string Report() const;

private:
    void WriteRecord(uint32_t kind, const void* payload, size_t size, const void* tail = nullptr,
                     size_t tail_size = 0);

    gs::DrawIrSink* next_;
    NameResolver names_;
    std::FILE* file_ = nullptr;
    std::string path_;
    std::set<uint32_t> named_;
    uint64_t draws_ = 0, jobs_ = 0, frames_ = 0, bytes_ = 0;
};

}  // namespace diag
}  // namespace ps2r

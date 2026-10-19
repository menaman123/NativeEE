// gs/hle/capture/gs_stream_recorder.cpp -- GsStreamRecorder (see the header).
#include "engine/gs/hle/capture/gs_stream_recorder.h"

#include <chrono>
#include <utility>

#include "engine/gs/hle/capture/gs_stream_format.h"
#include "engine/gs/hle/ir/draw_ir.h"

namespace ps2r {
namespace gs {

namespace {
constexpr size_t kIoBufferBytes = 4u << 20;   // the stdio buffer: a frame's records per flush
}  // namespace

GsStreamRecorder::GsStreamRecorder(DrawIrSink* next, VramMirror* next_mirror, Hooks hooks)
    : next_(next), next_mirror_(next_mirror), hooks_(std::move(hooks)) {}

GsStreamRecorder::~GsStreamRecorder() { Close(); }

bool GsStreamRecorder::Open(const std::string& path, const VramImage& initial) {
    Close();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    iobuf_.resize(kIoBufferBytes);
    std::setvbuf(f, iobuf_.data(), _IOFBF, iobuf_.size());
    GsStreamFileHeader h{};
    h.magic = GsStreamFormat::kMagic;
    h.version = GsStreamFormat::kVersion;
    h.flags = 0;
    h.vram_bytes = VramImage::kBytes;
    h.start_unix_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    if (std::fwrite(&h, 1, sizeof(h), f) != sizeof(h)) {
        std::fclose(f);
        return false;
    }
    file_ = f;
    path_ = path;
    n_ = Counters{};
    n_.bytes = sizeof(h);
    ops_.Clear();
    pending_valid_ = false;
    WriteRecord(GsStreamFormat::kVramInit, initial.Bytes(), VramImage::kBytes);
    // The display state in force (set by jobs before this open): restated first.
    if (have_window_) {
        WriteRecord(GsStreamFormat::kJob, &last_window_, sizeof(last_window_));
        n_.jobs++;
    }
    if (have_front_) {
        WriteRecord(GsStreamFormat::kJob, &last_front_, sizeof(last_front_));
        n_.jobs++;
    }
    return true;
}

void GsStreamRecorder::Close() {
    if (!file_) return;
    BeforeEvent();
    std::fclose(file_);
    file_ = nullptr;
}

void GsStreamRecorder::WriteRecord(uint32_t kind, const void* payload, size_t size, const void* tail,
                                   size_t tail_size) {
    if (!file_) return;
    GsStreamRecordHeader rh{};
    rh.kind = kind;
    rh.size = static_cast<uint32_t>(size + tail_size);
    std::fwrite(&rh, 1, sizeof(rh), file_);
    if (size) std::fwrite(payload, 1, size, file_);
    if (tail && tail_size) std::fwrite(tail, 1, tail_size, file_);
    n_.bytes += sizeof(rh) + size + tail_size;
}

void GsStreamRecorder::BeforeEvent() {
    if (!file_) return;
    if (!ops_.Empty()) {
        const std::vector<uint8_t>& enc = ops_.Encoded();
        WriteRecord(GsStreamFormat::kVramOps, enc.data(), enc.size());
        n_.vram_ops++;
        n_.vram_op_bytes += enc.size();
        ops_.Clear();
    }
    if (pending_valid_) {
        pending_valid_ = false;
        const std::vector<uint8_t>* px = hooks_.upload_pixels ? hooks_.upload_pixels() : nullptr;
        JobRec j = pending_;
        const bool incl = px && !px->empty() && px->size() == j.payload_bytes;
        j.payload_incl = incl ? 1u : 0u;
        WriteRecord(GsStreamFormat::kJob, &j, sizeof(j), incl ? px->data() : nullptr, incl ? px->size() : 0);
        if (incl) n_.payload_bytes += px->size();
        n_.jobs++;
    }
}

void GsStreamRecorder::WriteDraw(const DrawIr& draw) {
    BeforeEvent();
    DrawRec rec = draw.ToDrawRec();
    const bool whole = !draw.verts.empty() && draw.verts.size() == draw.vertex_count;
    rec.verts_incl = whole ? 1u : 0u;
    WriteRecord(GsStreamFormat::kDraw, &rec, sizeof(rec), whole ? draw.verts.data() : nullptr,
                whole ? draw.verts.size() * sizeof(VertexRec) : 0);
    n_.draws++;
}

void GsStreamRecorder::OnDraw(const DrawIr& draw) {
    if (file_) WriteDraw(draw);
    if (next_) next_->OnDraw(draw);
}

void GsStreamRecorder::OnDrawOwned(DrawIr&& draw) {
    if (file_) WriteDraw(draw);
    if (next_) next_->OnDrawOwned(std::move(draw));
}

void GsStreamRecorder::OnJob(const JobRec& job) {
    if (static_cast<JobKind>(job.type) == JobKind::SetWindow) {
        last_window_ = job;
        last_window_.payload_incl = 0;
        have_window_ = true;
    } else if (static_cast<JobKind>(job.type) == JobKind::SetFrontBuffer) {
        last_front_ = job;
        last_front_.payload_incl = 0;
        have_front_ = true;
    }
    if (file_) {
        BeforeEvent();
        if (static_cast<JobKind>(job.type) == JobKind::UploadVRAMTexture && job.payload_bytes > 0) {
            pending_ = job;   // written with its pixels at the next event
            pending_valid_ = true;
        } else {
            JobRec j = job;
            j.payload_incl = 0;
            WriteRecord(GsStreamFormat::kJob, &j, sizeof(j));
            n_.jobs++;
        }
    }
    if (next_) next_->OnJob(job);
}

void GsStreamRecorder::Arm(const std::string& path, const VramImage* image, uint64_t first, uint64_t count) {
    armed_path_ = path;
    armed_image_ = image;
    armed_first_ = first;
    armed_count_ = count;
    armed_ = image != nullptr;
    if (armed_ && first == 0) {
        armed_ = false;
        if (!Open(armed_path_, *armed_image_)) std::fprintf(stderr, "gs capture: cannot create %s\n", armed_path_.c_str());
    }
}

void GsStreamRecorder::OnFrame(const JobRec& vsync) {
    bool opened_here = false;
    if (armed_ && frames_seen_ == armed_first_) {
        armed_ = false;
        if (!Open(armed_path_, *armed_image_)) std::fprintf(stderr, "gs capture: cannot create %s\n", armed_path_.c_str());
        opened_here = true;   // this boundary's frame record is not written: the frame it closes was not captured
    }
    if (file_ && armed_count_ && n_.frames >= armed_count_) Close();
    ++frames_seen_;
    if (file_ && !opened_here) {
        BeforeEvent();
        GsStreamFrameRec fr{};
        fr.vsync = vsync;
        fr.vsync.payload_incl = 0;
        fr.crtc_packed = hooks_.crtc ? CrtcClassifier::Pack(hooks_.crtc()) : 0u;
        WriteRecord(GsStreamFormat::kFrame, &fr, sizeof(fr));
        n_.frames++;
    }
    if (next_) next_->OnFrame(vsync);
}

void GsStreamRecorder::OnWriteStream(uint32_t byte_off, const uint8_t* src, size_t len) {
    if (file_) ops_.OnWriteStream(byte_off, src, len);
    if (next_mirror_) next_mirror_->OnWriteStream(byte_off, src, len);
}

void GsStreamRecorder::OnNoteWriteNoBytes(uint32_t byte_off, size_t len) {
    if (file_) ops_.OnNoteWriteNoBytes(byte_off, len);
    if (next_mirror_) next_mirror_->OnNoteWriteNoBytes(byte_off, len);
}

void GsStreamRecorder::OnNoteStreamWrite(uint32_t start, uint32_t size) {
    if (file_) ops_.OnNoteStreamWrite(start, size);
    if (next_mirror_) next_mirror_->OnNoteStreamWrite(start, size);
}

void GsStreamRecorder::OnSettleWrites() {
    if (file_) ops_.OnSettleWrites();
    if (next_mirror_) next_mirror_->OnSettleWrites();
}

void GsStreamRecorder::OnNoteCpuWrite(uint32_t start, uint32_t size) {
    if (file_) ops_.OnNoteCpuWrite(start, size);
    if (next_mirror_) next_mirror_->OnNoteCpuWrite(start, size);
}

std::string GsStreamRecorder::Report() const {
    return "path=" + (path_.empty() ? std::string("off") : path_) + " draws=" + std::to_string(n_.draws) +
           " jobs=" + std::to_string(n_.jobs) + " frames=" + std::to_string(n_.frames) +
           " vram_ops=" + std::to_string(n_.vram_ops) + " vram_op_bytes=" + std::to_string(n_.vram_op_bytes) +
           " payload_bytes=" + std::to_string(n_.payload_bytes) + " bytes=" + std::to_string(n_.bytes);
}

}  // namespace gs
}  // namespace ps2r

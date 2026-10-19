// tools/gs_replay/stream_replay_driver.cpp -- StreamReplayDriver (see the header).
#include "tools/gs_replay/stream_replay_driver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/gs/hle/capture/gs_stream_format.h"
#include "engine/gs/vram/vram_op_log.h"

namespace ps2r {
namespace tools {

namespace {
// The pipeline's native display rectangle (the play video stack's seed) and the
// hidden window's own size (never shown; the presented image is captured).
constexpr int kNativeWindowW = 640;
constexpr int kNativeWindowH = 448;
constexpr int kHiddenWindowW = 64;
constexpr int kHiddenWindowH = 64;

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = std::min(v.size() - 1, lo + 1);
    const double f = pos - static_cast<double>(lo);
    return v[lo] * (1.0 - f) + v[hi] * f;
}
}  // namespace

StreamReplayDriver::StreamReplayDriver(const Options& opt) : opt_(opt) {}

StreamReplayDriver::~StreamReplayDriver() { Teardown(); }

double StreamReplayDriver::NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool StreamReplayDriver::DrawLimitApplies(uint32_t frame_index) const {
    if (opt_.draw_limit_frame != kDrawLimitDumpRange) return frame_index == opt_.draw_limit_frame;
    return opt_.dump_range.Contains(frame_index);
}

bool StreamReplayDriver::CreateContext() {
    if (opt_.metal) {
#if defined(PS2R_HAS_METAL)
        metal_ = std::make_unique<render::MetalDevice>();
        if (!metal_->Init([this] { PresentWindow(); })) return false;
        be_ = metal_.get();
        api_name_ = "Metal";
        renderer_name_ = metal_->DeviceName();
#else
        return false;
#endif
    } else {
        if (!win_.Create(true, kHiddenWindowW, kHiddenWindowH, "gs_replay stream")) return false;
        if (!ctx_.Create(win_)) return false;
        ctx_.SetSwapInterval(0);
        if (!gl_.Init([this] { PresentWindow(); })) return false;
        be_ = &gl_;
        api_name_ = ctx_.Version() ? ctx_.Version() : "";
        renderer_name_ = ctx_.Renderer() ? ctx_.Renderer() : "";
        if (opt_.cd_barrier) gl_.SetCdCopyByBarrier(true);
        if (opt_.force_shader_blend) gl_.SetForceShaderBlend(true);
    }
    be_->SetWindowSize(kNativeWindowW, kNativeWindowH);
    be_->ArmPresentCapture(true);   // play: every Present's window image is read back for the session window
    be_->SetPresentCaptureDeferred(opt_.deferred_capture);   // play: the read lands one present late (B1)
    return true;
}

void StreamReplayDriver::BuildScene() {
    int scale = opt_.scale;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    vram_ = std::make_unique<gs::VramImage>();
    scale_ = std::make_unique<gs::ScalePolicy>(gs::ScaleSettings{scale}, &layout_);
    cache_ = std::make_unique<gs::RenderTargetCache>(*be_, *vram_, *scale_);
    refresher_ = std::make_unique<gs::TargetRefresher>(*be_, *vram_, *cache_);
    resolver_ = std::make_unique<gs::TextureResolver>(*be_, *vram_, *cache_, *refresher_, layout_);
    scanout_ = std::make_unique<gs::ScanoutComposer>(*be_, *cache_);
    presenter_ = std::make_unique<gs::Presenter>(*scanout_);
    gs::DrawPipeline::Deps deps;
    deps.be = be_;
    deps.vram = vram_.get();
    deps.cache = cache_.get();
    deps.refresher = refresher_.get();
    deps.resolver = resolver_.get();
    deps.scanout = scanout_.get();
    deps.presenter = presenter_.get();
    deps.layout = &layout_;
    deps.registry = nullptr;
    deps.sequence = nullptr;
    pipe_ = std::make_unique<gs::DrawPipeline>(deps);
    pipe_->SetLiveSnapshot(true);
    scanout_->SetScanoutFbp(gs::DrawPipeline::kNoFbp);
    scanout_->SetGrabPolicy(gs::ScanoutComposer::GrabPolicy::LocalMemory);
    gs::DrawIrQueueConsumer::Hooks hooks;
    hooks.upload_pixels = [this] { return last_payload_valid_ ? &last_payload_ : nullptr; };
    hooks.crtc = [this] { return crtc_; };
    hooks.window_resized = [this](int w, int h) {
        if (w > 0 && h > 0) be_->SetWindowSize(w, h);
    };
    consumer_ = std::make_unique<gs::DrawIrQueueConsumer>(*pipe_, std::move(hooks));
    consumer_->SetFrameHashScale(scale);   // play: the frames-lane hash where the grab is made
}

void StreamReplayDriver::Teardown() {
    consumer_.reset();
    pipe_.reset();
    presenter_.reset();
    scanout_.reset();
    resolver_.reset();
    refresher_.reset();
    cache_.reset();
    scale_.reset();
    vram_.reset();
    if (gl_.IsInitialized()) gl_.Shutdown();
#if defined(PS2R_HAS_METAL)
    metal_.reset();
#endif
    be_ = nullptr;
    ctx_.Destroy();
    win_.Destroy();
}

void StreamReplayDriver::PresentWindow() {
    int w = 0, h = 0;
    if (be_->ReadPresented(&presented_, &w, &h) && w > 0 && h > 0) ++presents_read_;
    if (opt_.swap_hidden_window && !opt_.metal) win_.Swap();
    else be_->FlushCommands();   // play: the hidden window is never shown -- a flush, not a swap
}

void StreamReplayDriver::OnFrameDone(const gs::JobRec& vsync) {
    const double t = NowMs();
    FrameStat st;
    st.ms = t - frame_t0_;
    frame_t0_ = t;
    st.draws = frame_draws_;
    st.jobs = frame_jobs_;
    frame_draws_ = 0;
    frame_jobs_ = 0;
    const gs::DrawPipeline::Frame& f = consumer_->LastFrame();
    st.grabbed = f.grabbed;
    st.hash = f.hash;
    if (const render::CallCensus* c = be_->Census()) {
        st.readbacks = c->readbacks - census_at_frame_.readbacks;
        st.copies = c->copies - census_at_frame_.copies;
        st.draw_calls = c->draw_calls - census_at_frame_.draw_calls;
        census_at_frame_ = *c;
    }
    stream_hash_ = (stream_hash_ * kHashPrime) ^ st.hash;
    stream_hash_ = (stream_hash_ * kHashPrime) ^ ((static_cast<uint64_t>(f.w) << 32) | static_cast<uint64_t>(f.h));
    stream_hash_ = (stream_hash_ * kHashPrime) ^ st.draws;
    const uint32_t frame_index = static_cast<uint32_t>(frames_.size());
    frames_.push_back(st);
    total_ms_ += st.ms;
    if (!opt_.quiet)
        std::printf("[GSSTREAM] frame %u src=%u ms=%.3f draws=%u jobs=%u hash=%016llx %s rb=%llu cp=%llu dc=%llu\n", frame_index,
                    vsync.src_frame, st.ms, st.draws, st.jobs, static_cast<unsigned long long>(st.hash),
                    st.grabbed ? "grab" : "nograb", static_cast<unsigned long long>(st.readbacks),
                    static_cast<unsigned long long>(st.copies), static_cast<unsigned long long>(st.draw_calls));
    if (opt_.dump_dir && opt_.dump_range.Contains(frame_index) && f.grabbed && !f.rgba.empty()) {
        char name[64];
        std::snprintf(name, sizeof(name), "/g%u.ppm", frame_index);
        WritePpm(std::string(opt_.dump_dir) + name, f.rgba, f.w, f.h);
        // The grab's alpha lane as a PGM (the stored GS alpha byte per pixel).
        std::snprintf(name, sizeof(name), "/a%u.pgm", frame_index);
        if (std::FILE* af = std::fopen((std::string(opt_.dump_dir) + name).c_str(), "wb")) {
            std::fprintf(af, "P5\n%d %d\n255\n", f.w, f.h);
            for (size_t i = 3; i < f.rgba.size(); i += 4) std::fputc(f.rgba[i], af);
            std::fclose(af);
        }
        // The route trace of the dumped frame (how every draw was lowered) next to its image.
        std::snprintf(name, sizeof(name), "/r%u.txt", frame_index);
        if (std::FILE* tf = std::fopen((std::string(opt_.dump_dir) + name).c_str(), "w")) {
            pipe_->RouteTrace().Write(tf);
            std::fclose(tf);
        }
    }
}

// The grab's rows as the pipeline hands them (the title's f<key>.ppm writes them the same way).
void StreamReplayDriver::WritePpm(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
        for (int x = 0; x < w; ++x) {
            row[static_cast<size_t>(x) * 3 + 0] = src[x * 4 + 0];
            row[static_cast<size_t>(x) * 3 + 1] = src[x * 4 + 1];
            row[static_cast<size_t>(x) * 3 + 2] = src[x * 4 + 2];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

void StreamReplayDriver::ReaderMain(gs::GsStreamReader* reader) {
    gs::GsStreamReader::Record rec;
    while (true) {
        if (!reader->Next(&rec)) break;
        std::unique_lock<std::mutex> lk(q_mu_);
        q_cv_.wait(lk, [this] { return q_.size() <= kPrefetchDepth - kPrefetchDepth / 4 || stop_reader_; });
        if (stop_reader_) break;
        q_.push_back(std::move(rec));
        const bool wake = q_.size() == 1;   // the consumer may be waiting on an empty queue
        lk.unlock();
        if (wake) q_cv_.notify_all();
    }
    std::lock_guard<std::mutex> lk(q_mu_);
    reader_error_ = reader->Error();
    reader_done_ = true;
    q_cv_.notify_all();
}

bool StreamReplayDriver::PopRecord(gs::GsStreamReader::Record* out) {
    std::unique_lock<std::mutex> lk(q_mu_);
    q_cv_.wait(lk, [this] { return !q_.empty() || reader_done_; });
    if (q_.empty()) return false;
    *out = std::move(q_.front());
    q_.pop_front();
    // Wake the reader once per quarter of the queue drained, not per record.
    const bool wake = q_.size() == kPrefetchDepth - kPrefetchDepth / 4;
    lk.unlock();
    if (wake) q_cv_.notify_all();
    return true;
}

int StreamReplayDriver::Run() {
    gs::GsStreamReader reader;
    if (!reader.Open(opt_.in_path)) {
        std::fprintf(stderr, "[GSSTREAM] %s\n", reader.Error().c_str());
        return 2;
    }
    reader_thread_ = std::thread([this, &reader] { ReaderMain(&reader); });
    if (!CreateContext()) {
        std::fprintf(stderr, "[GSSTREAM] no %s device (SKIP)\n", opt_.metal ? "Metal" : "GL");
        return 77;
    }
    BuildScene();
    if (!opt_.quiet)
        std::printf("[GSSTREAM] %s scale=%d api=%s renderer=%s\n", opt_.in_path, opt_.scale, api_name_.c_str(), renderer_name_.c_str());
    gs::GsStreamReader::Record rec;
    gs::VramOpLog ops;
    frame_t0_ = NowMs();
    if (const render::CallCensus* c = be_->Census()) census_at_frame_ = *c;
    while (PopRecord(&rec)) {
        switch (rec.kind) {
            case gs::GsStreamFormat::kVramInit:
                if (rec.bytes.size() == gs::VramImage::kBytes) {
                    vram_->WriteStream(0, rec.bytes.data(), rec.bytes.size());
                    vram_->SettleWrites();
                }
                frame_t0_ = NowMs();   // the image load is not the consumer's work
                break;
            case gs::GsStreamFormat::kVramOps:
                ops.LoadEncoded(rec.bytes.data(), rec.bytes.size());
                vram_ops_ += ops.Replay(*vram_);
                break;
            case gs::GsStreamFormat::kDraw:
                // The bisection aid limits only the dumped frames (every earlier frame runs whole so the
                // render-target / depth state the limited frame starts from is the real one).
                if (!opt_.draw_limit || !DrawLimitApplies(static_cast<uint32_t>(frames_.size())) ||
                    frame_draws_ < opt_.draw_limit)
                    consumer_->OnDraw(rec.draw);
                ++frame_draws_;
                break;
            case gs::GsStreamFormat::kJob:
                if (rec.job.payload_incl && !rec.bytes.empty()) {
                    last_payload_ = rec.bytes;   // served to the consumer at its next event
                    last_payload_valid_ = true;
                    ++uploads_;
                }
                consumer_->OnJob(rec.job);
                ++frame_jobs_;
                break;
            case gs::GsStreamFormat::kFrame:
                crtc_ = rec.crtc;
                consumer_->OnFrame(rec.job);
                OnFrameDone(rec.job);
                if (opt_.max_frames && frames_.size() >= opt_.max_frames) goto done;
                break;
            default:
                break;
        }
    }
done:
    {
        std::lock_guard<std::mutex> lk(q_mu_);
        stop_reader_ = true;
    }
    q_cv_.notify_all();
    reader_thread_.join();
    if (!reader_error_.empty()) std::fprintf(stderr, "[GSSTREAM] note: %s (tail kept)\n", reader_error_.c_str());
    WriteOutputs();
    return 0;
}

void StreamReplayDriver::WriteOutputs() const {
    std::vector<double> ms;
    uint32_t over_bar = 0;
    double max_ms = 0, sum_ms = 0;
    uint64_t draws = 0;
    const size_t first = std::min<size_t>(opt_.warmup, frames_.size());
    for (size_t i = first; i < frames_.size(); ++i) {
        ms.push_back(frames_[i].ms);
        sum_ms += frames_[i].ms;
        draws += frames_[i].draws;
        if (frames_[i].ms > kFrameBarMs) ++over_bar;
        if (frames_[i].ms > max_ms) max_ms = frames_[i].ms;
    }
    const size_t n = ms.size();
    const double p50 = Percentile(ms, 0.50), p99 = Percentile(ms, 0.99);
    const double fps_mean = sum_ms > 0 ? 1000.0 * static_cast<double>(n) / sum_ms : 0.0;
    const double fps_p50 = p50 > 0 ? 1000.0 / p50 : 0.0;
    const gs::DrawIrQueueConsumer::Counters& cc = consumer_->Count();
    std::printf("[GSSTREAM] frames=%zu warmup=%zu measured=%zu scale=%d total_ms=%.1f fps_mean=%.1f fps_p50=%.1f ms_p50=%.3f "
                "ms_p99=%.3f ms_max=%.3f over_%gms=%u draws=%llu draws_per_frame=%.0f uploads=%llu vram_ops=%llu "
                "grab_empty=%llu presents=%llu presents_read=%llu\n",
                frames_.size(), first, n, opt_.scale, total_ms_, fps_mean, fps_p50, p50, p99, max_ms, kFrameBarMs, over_bar,
                static_cast<unsigned long long>(draws), n ? static_cast<double>(draws) / static_cast<double>(n) : 0.0,
                static_cast<unsigned long long>(uploads_), static_cast<unsigned long long>(vram_ops_),
                static_cast<unsigned long long>(cc.grab_empty), static_cast<unsigned long long>(cc.presents),
                static_cast<unsigned long long>(presents_read_));
    std::printf("STREAM_HASH %016llx\n", static_cast<unsigned long long>(stream_hash_));
    if (opt_.csv_path) {
        if (std::FILE* f = std::fopen(opt_.csv_path, "w")) {
            std::fprintf(f, "frame,ms,draws,jobs,hash,grabbed,readbacks,copies,draw_calls\n");
            for (size_t i = 0; i < frames_.size(); ++i) {
                const FrameStat& s = frames_[i];
                std::fprintf(f, "%zu,%.4f,%u,%u,%016llx,%d,%llu,%llu,%llu\n", i, s.ms, s.draws, s.jobs,
                             static_cast<unsigned long long>(s.hash), s.grabbed ? 1 : 0,
                             static_cast<unsigned long long>(s.readbacks), static_cast<unsigned long long>(s.copies),
                             static_cast<unsigned long long>(s.draw_calls));
            }
            std::fclose(f);
        }
    }
    if (opt_.json_path) {
        if (std::FILE* f = std::fopen(opt_.json_path, "w")) {
            std::fprintf(f,
                         "{\n  \"capture\": \"%s\",\n  \"scale\": %d,\n  \"frames\": %zu,\n  \"warmup\": %zu,\n  \"measured\": %zu,\n"
                         "  \"total_ms\": %.3f,\n  \"fps_mean\": %.3f,\n  \"fps_p50\": %.3f,\n  \"ms_p50\": %.4f,\n  \"ms_p99\": %.4f,\n"
                         "  \"ms_max\": %.4f,\n  \"frame_bar_ms\": %g,\n  \"over_bar\": %u,\n  \"draws\": %llu,\n  \"uploads\": %llu,\n"
                         "  \"vram_ops\": %llu,\n  \"stream_hash\": \"%016llx\",\n  \"gl_version\": \"%s\",\n  \"gl_renderer\": \"%s\"\n}\n",
                         opt_.in_path, opt_.scale, frames_.size(), first, n, total_ms_, fps_mean, fps_p50, p50, p99, max_ms,
                         kFrameBarMs, over_bar, static_cast<unsigned long long>(draws), static_cast<unsigned long long>(uploads_),
                         static_cast<unsigned long long>(vram_ops_), static_cast<unsigned long long>(stream_hash_),
                         api_name_.c_str(), renderer_name_.c_str());
            std::fclose(f);
        }
    }
}

}  // namespace tools
}  // namespace ps2r

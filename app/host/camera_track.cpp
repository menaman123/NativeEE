#include "app/host/camera_track.h"

#include <cmath>

namespace ps2r::app {

CameraTrack::~CameraTrack() {
    if (trace_) std::fclose(trace_);
}

bool CameraTrack::CameraPosition(const float* m, float out[3]) {
    // Row-vector convention (the PS2 title's Matrix4x4: p' = p * M, row 3 the
    // translation): for a rigid world->camera M = [R 0; t 1] the camera's world
    // position is -t * R^T. A general 3x3 inverse keeps a scaled basis honest.
    const double a00 = m[0], a01 = m[1], a02 = m[2];
    const double a10 = m[4], a11 = m[5], a12 = m[6];
    const double a20 = m[8], a21 = m[9], a22 = m[10];
    const double tx = m[12], ty = m[13], tz = m[14];
    const double det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
    if (!std::isfinite(det) || std::fabs(det) < 1e-12) return false;
    const double inv = 1.0 / det;
    // Inverse of the 3x3 (adjugate / det), row-major.
    const double i00 = (a11 * a22 - a12 * a21) * inv, i01 = (a02 * a21 - a01 * a22) * inv, i02 = (a01 * a12 - a02 * a11) * inv;
    const double i10 = (a12 * a20 - a10 * a22) * inv, i11 = (a00 * a22 - a02 * a20) * inv, i12 = (a02 * a10 - a00 * a12) * inv;
    const double i20 = (a10 * a21 - a11 * a20) * inv, i21 = (a01 * a20 - a00 * a21) * inv, i22 = (a00 * a11 - a01 * a10) * inv;
    // position = -t * R^-1 (row vector times matrix)
    const double px = -(tx * i00 + ty * i10 + tz * i20);
    const double py = -(tx * i01 + ty * i11 + tz * i21);
    const double pz = -(tx * i02 + ty * i12 + tz * i22);
    if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) return false;
    out[0] = static_cast<float>(px);
    out[1] = static_cast<float>(py);
    out[2] = static_cast<float>(pz);
    return true;
}

void CameraTrack::NoteCamera(const float* to_camera16) {
    float position[3];
    if (!to_camera16 || !CameraPosition(to_camera16, position)) return;
    std::lock_guard<std::mutex> lock(mu_);
    pending_.valid = true;
    for (int i = 0; i < 3; ++i) pending_.position[i] = position[i];
}

void CameraTrack::NoteBoundaryAt(Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!pending_.valid) return;
    previous_ = latest_;
    latest_ = pending_;
    latest_.ordinal = ++samples_;
    latest_.at = now;
    pending_ = {};
}

void CameraTrack::NoteCameraAt(const float* to_camera16, Clock::time_point now) {
    NoteCamera(to_camera16);
    NoteBoundaryAt(now);
}

CameraTrack::Presented CameraTrack::Present(Clock::time_point now) {
    Presented out;
    std::lock_guard<std::mutex> lock(mu_);
    ++presents_;
    if (!latest_.valid) { ++unsampled_; return out; }
    out.valid = true;
    out.ordinal = latest_.ordinal;
    out.duplicate = last_presented_ordinal_ == latest_.ordinal;
    last_presented_ordinal_ = latest_.ordinal;
    for (int i = 0; i < 3; ++i) out.latest[i] = latest_.position[i];
    double phase = 1.0;
    if (previous_.valid && latest_.at > previous_.at) {
        const double span = std::chrono::duration<double>(latest_.at - previous_.at).count();
        const double since = std::chrono::duration<double>(now - latest_.at).count();
        phase = span > 0 ? since / span : 1.0;
        if (phase < 0) phase = 0;
        if (phase > 1) phase = 1;
        for (int i = 0; i < 3; ++i)
            out.position[i] = static_cast<float>(previous_.position[i] + (latest_.position[i] - previous_.position[i]) * phase);
        out.interpolated = phase > 0 && phase < 1;
    } else {
        for (int i = 0; i < 3; ++i) out.position[i] = latest_.position[i];
    }
    out.phase = phase;
    phase_sum_ += phase;
    if (out.interpolated) ++interpolated_;
    if (out.duplicate) ++duplicates_;
    if (!trace_path_.empty() && !trace_ && !trace_failed_) {
        trace_ = std::fopen(trace_path_.c_str(), "w");
        if (!trace_) trace_failed_ = true;
        else std::fprintf(trace_, "# wall_ns tick phase x y z xn yn zn dup\n");
    }
    if (trace_) {
        const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        std::fprintf(trace_, "%lld %llu %.4f %.4f %.4f %.4f %.4f %.4f %.4f %d\n", static_cast<long long>(wall_ns),
                     static_cast<unsigned long long>(out.ordinal), phase, out.position[0], out.position[1], out.position[2],
                     out.latest[0], out.latest[1], out.latest[2], out.duplicate ? 1 : 0);
        std::fflush(trace_);   // a leg may end by exit(): every present on disk
    }
    return out;
}

void CameraTrack::SetTrace(std::string path) {
    std::lock_guard<std::mutex> lock(mu_);
    trace_path_ = std::move(path);
}

std::string CameraTrack::Census() const {
    std::lock_guard<std::mutex> lock(mu_);
    const long phase_mean = presents_ ? static_cast<long>(phase_sum_ / static_cast<double>(presents_) * 1000.0 + 0.5) : 0;
    return "samples=" + std::to_string(samples_) + ",presents=" + std::to_string(presents_) +
           ",interpolated=" + std::to_string(interpolated_) + ",duplicates=" + std::to_string(duplicates_) + ",unsampled=" + std::to_string(unsampled_) +
           ",phase_mean_x1000=" + std::to_string(phase_mean) + ",trace=" + (trace_ ? "on" : trace_failed_ ? "failed" : "off");
}

uint64_t CameraTrack::Samples() const { std::lock_guard<std::mutex> lock(mu_); return samples_; }
uint64_t CameraTrack::Presents() const { std::lock_guard<std::mutex> lock(mu_); return presents_; }
uint64_t CameraTrack::Interpolated() const { std::lock_guard<std::mutex> lock(mu_); return interpolated_; }
uint64_t CameraTrack::Duplicates() const { std::lock_guard<std::mutex> lock(mu_); return duplicates_; }

}  // namespace ps2r::app

// render/review/frame_review.cpp -- the display-rate re-view (see .h).
#include "engine/render/review/frame_review.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ps2r {
namespace render {

namespace {
constexpr double kPi = 3.14159265358979323846;

struct Quat { double w = 1, x = 0, y = 0, z = 0; };

// The 3x3 of a row-vector matrix (rows = basis vectors), rows normalised.
void Basis(const double* m, double r[3][3], bool* ok) {
    *ok = true;
    for (int i = 0; i < 3; ++i) {
        double len = 0;
        for (int j = 0; j < 3; ++j) len += m[i * 4 + j] * m[i * 4 + j];
        len = std::sqrt(len);
        if (!(len > 1e-12) || !std::isfinite(len)) { *ok = false; len = 1; }
        for (int j = 0; j < 3; ++j) r[i][j] = m[i * 4 + j] / len;
    }
}

Quat FromBasis(const double r[3][3]) {
    // Shepperd's method over the (consistently) row-indexed 3x3; the convention
    // only has to round-trip through ToBasis below.
    Quat q;
    const double trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0) {
        const double s = std::sqrt(trace + 1.0) * 2;
        q.w = 0.25 * s;
        q.x = (r[1][2] - r[2][1]) / s;
        q.y = (r[2][0] - r[0][2]) / s;
        q.z = (r[0][1] - r[1][0]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const double s = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2;
        q.w = (r[1][2] - r[2][1]) / s;
        q.x = 0.25 * s;
        q.y = (r[0][1] + r[1][0]) / s;
        q.z = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const double s = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2;
        q.w = (r[2][0] - r[0][2]) / s;
        q.x = (r[0][1] + r[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (r[1][2] + r[2][1]) / s;
    } else {
        const double s = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2;
        q.w = (r[0][1] - r[1][0]) / s;
        q.x = (r[0][2] + r[2][0]) / s;
        q.y = (r[1][2] + r[2][1]) / s;
        q.z = 0.25 * s;
    }
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n > 0) { q.w /= n; q.x /= n; q.y /= n; q.z /= n; }
    return q;
}

void ToBasis(const Quat& q, double r[3][3]) {
    const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r[0][0] = 1 - 2 * (yy + zz); r[0][1] = 2 * (xy + wz);     r[0][2] = 2 * (xz - wy);
    r[1][0] = 2 * (xy - wz);     r[1][1] = 1 - 2 * (xx + zz); r[1][2] = 2 * (yz + wx);
    r[2][0] = 2 * (xz + wy);     r[2][1] = 2 * (yz - wx);     r[2][2] = 1 - 2 * (xx + yy);
}

Quat Slerp(Quat a, Quat b, double t, double* angle_degrees) {
    double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot < 0) { b.w = -b.w; b.x = -b.x; b.y = -b.y; b.z = -b.z; dot = -dot; }
    dot = std::min(1.0, dot);
    const double theta = std::acos(dot);
    *angle_degrees = 2.0 * theta * 180.0 / kPi;
    double wa = 1 - t, wb = t;
    if (theta > 1e-6) {
        const double s = std::sin(theta);
        wa = std::sin((1 - t) * theta) / s;
        wb = std::sin(t * theta) / s;
    }
    Quat q{a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb};
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n > 0) { q.w /= n; q.x /= n; q.y /= n; q.z /= n; }
    return q;
}

// The camera's world position of a row-vector world->camera matrix: -t . R^-1.
bool Position(const double* m, double out[3]) {
    double inv[16];
    if (!FrameReview::Invert4(m, inv)) return false;
    // The inverse's translation row is the camera's world position.
    out[0] = inv[12]; out[1] = inv[13]; out[2] = inv[14];
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

bool Same(const SurfaceDesc& a, const SurfaceDesc& b) {
    return a.width == b.width && a.height == b.height && a.format == b.format && a.renderable == b.renderable &&
           a.scale == b.scale;
}
}  // namespace

// ---------------------------------------------------------------------------
// Matrix helpers (row-vector convention: out = a . b).

void FrameReview::Multiply4(const double a[16], const double b[16], double out[16]) {
    double r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    for (int i = 0; i < 16; ++i) out[i] = r[i];
}

bool FrameReview::Invert4(const double m[16], double out[16]) {
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!std::isfinite(det) || std::fabs(det) < 1e-300) return false;
    const double scale = 1.0 / det;
    for (int i = 0; i < 16; ++i) {
        out[i] = inv[i] * scale;
        if (!std::isfinite(out[i])) return false;
    }
    return true;
}

bool FrameReview::Interpolate(const double* a, const double* b, double phase, double out[16], bool* cut) {
    bool ok_a = false, ok_b = false;
    double ra[3][3], rb[3][3], pa[3], pb[3];
    Basis(a, ra, &ok_a);
    Basis(b, rb, &ok_b);
    if (!ok_a || !ok_b || !Position(a, pa) || !Position(b, pb)) return false;
    phase = std::clamp(phase, 0.0, 1.0);
    double angle = 0;
    const Quat q = Slerp(FromBasis(ra), FromBasis(rb), phase, &angle);
    const double travel = std::sqrt((pb[0] - pa[0]) * (pb[0] - pa[0]) + (pb[1] - pa[1]) * (pb[1] - pa[1]) +
                                    (pb[2] - pa[2]) * (pb[2] - pa[2]));
    if (cut) *cut = travel > kCutTravel || angle > kCutAngleDegrees;
    double r[3][3];
    ToBasis(q, r);
    double p[3];
    for (int i = 0; i < 3; ++i) p[i] = pa[i] + (pb[i] - pa[i]) * phase;
    // The world->camera of a camera at p with basis rows r: [R 0; -p.R 1].
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) out[i * 4 + j] = r[i][j];
        out[i * 4 + 3] = 0;
    }
    for (int j = 0; j < 3; ++j) out[12 + j] = -(p[0] * r[0][j] + p[1] * r[1][j] + p[2] * r[2][j]);
    out[15] = 1;
    return true;
}

bool FrameReview::ReviewVertex(const double K[16], float off_x, float off_y, double z_max, bool rescale_stq,
                               gs::VertexRec& v) {
    const double xg = (double(v.x) + double(off_x)) * 16.0, yg = (double(v.y) + double(off_y)) * 16.0, zg = double(v.z);
    const double hx = xg * K[0] + yg * K[4] + zg * K[8] + K[12];
    const double hy = xg * K[1] + yg * K[5] + zg * K[9] + K[13];
    const double hz = xg * K[2] + yg * K[6] + zg * K[10] + K[14];
    const double hw = xg * K[3] + yg * K[7] + zg * K[11] + K[15];
    if (!(hw > 1e-9) || !std::isfinite(hw) || !std::isfinite(hx) || !std::isfinite(hy) || !std::isfinite(hz)) return false;
    const double inv = 1.0 / hw;
    v.x = float(hx * inv / 16.0 - double(off_x));
    v.y = float(hy * inv / 16.0 - double(off_y));
    v.z = float(std::clamp(hz * inv, 0.0, z_max));
    if (rescale_stq) {
        v.s = float(double(v.s) * inv);
        v.t = float(double(v.t) * inv);
        v.q = float(double(v.q) * inv);
    }
    return true;
}

// ---------------------------------------------------------------------------

FrameReview::FrameReview(Backend& next) : next_(next) {}

FrameReview::~FrameReview() { ReleaseShadows(); }

void FrameReview::SetRecording(bool on) {
    if (on == recording_.load(std::memory_order_relaxed)) return;
    recording_.store(on, std::memory_order_relaxed);
    ReleaseRetained();
    if (!on) ReleaseShadows();
    UpdateArmed();
}

void FrameReview::ReleaseRetained() {
    current_ = Frame{};
    closed_ = Frame{};
    previous_ = Frame{};
    latest_ = Frame{};
}

void FrameReview::ReleaseShadows() {
    for (auto& [id, shadow] : shadows_) { (void)id; next_.DestroySurface(shadow.id); }
    shadows_.clear();
    n_.shadows = 0;
}

void FrameReview::UpdateArmed() {
    const bool armed = Recording() && previous_.valid && previous_.camera.valid && latest_.valid && latest_.camera.valid;
    latest_at_ns_.store(armed ? std::chrono::duration_cast<std::chrono::nanoseconds>(latest_.camera.at.time_since_epoch()).count() : 0,
                        std::memory_order_relaxed);
    armed_.store(armed, std::memory_order_release);
}

bool FrameReview::ArmedAt(Clock::time_point now) const {
    if (!armed_.load(std::memory_order_acquire)) return false;
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    const int64_t latest = latest_at_ns_.load(std::memory_order_relaxed);
    if (now_ns - latest > kFreshness.count()) {
        stale_cameras_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void FrameReview::NoteCamera(const Camera& camera) {
    ++n_.cameras;
    if (!Recording()) return;
    if (!closed_.valid) {
        // A camera with no completed frame (a dropped frame under frame skip):
        // the chain breaks; two consecutive complete frames re-arm it.
        ++n_.cameras_unframed;
        previous_ = Frame{};
        latest_ = Frame{};
        UpdateArmed();
        return;
    }
    closed_.camera = camera;
    closed_.camera.valid = true;
    closed_.ordinal = ++camera_ordinal_;
    previous_ = std::move(latest_);
    latest_ = std::move(closed_);
    closed_ = Frame{};
    UpdateArmed();
}

bool FrameReview::TickPresentSuppressed() {
    if (Armed()) { ++n_.tick_suppressed; return true; }
    ++n_.tick_presents;
    return false;
}

void FrameReview::Record(Command&& command) {
    if (current_.commands.empty()) current_.first_at = Clock::now();
    if (command.kind == Kind::Draw) ++current_.draws;
    current_.commands.push_back(std::move(command));
}

void FrameReview::CloseFrame(const ScanoutDesc& desc) {
    if (current_.draws == 0) {
        // A display tick (present only) or a frame with nothing drawn: the
        // commands so far (clears, copies) belong to no picture.
        ++n_.empty_presents;
        current_ = Frame{};
        return;
    }
    Command present;
    present.kind = Kind::Present;
    present.scanout = desc;
    current_.commands.push_back(std::move(present));
    current_.valid = true;
    if (closed_.valid) ++n_.frames_uncamera;
    closed_ = std::move(current_);
    current_ = Frame{};
    ++n_.frames;
    n_.commands_last = closed_.commands.size();
    const uint64_t span = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - closed_.first_at).count());
    n_.frame_span_us_sum += span;
    n_.frame_span_us_max = std::max(n_.frame_span_us_max, span);
    n_.live_draw_us_last = closed_.draw_us;
    n_.live_draw_us_sum += closed_.draw_us;
    n_.live_draw_us_max = std::max(n_.live_draw_us_max, closed_.draw_us);
}

SurfaceId FrameReview::ShadowOf(SurfaceId id, bool* stale) {
    if (!alive_.count(id.id)) { *stale = true; return SurfaceId{}; }
    const SurfaceDesc desc = next_.DescribeSurface(id);
    auto found = shadows_.find(id.id);
    if (found != shadows_.end()) {
        if (Same(found->second.desc, desc)) return found->second.id;
        next_.DestroySurface(found->second.id);
        shadows_.erase(found);
    }
    const SurfaceId shadow = next_.CreateSurface(desc);
    if (!shadow.id) { *stale = true; return SurfaceId{}; }
    shadows_.emplace(id.id, Shadow{shadow, desc});
    n_.shadows = shadows_.size();
    return shadow;
}

SurfaceId FrameReview::Redirect(const Frame& frame, SurfaceId id, bool* stale) {
    if (!id.id) return id;
    if (!alive_.count(id.id)) { *stale = true; return SurfaceId{}; }
    if (frame.written.count(id.id)) return ShadowOf(id, stale);
    return id;
}

void FrameReview::Issue(const Frame& frame, const Command& c, const double* K, bool review, Replayed& out,
                        std::unordered_map<const CdCopyState*, CdCopyState>& trackers) {
    bool stale = false;
    switch (c.kind) {
        case Kind::Clear: {
            const SurfaceId color = Redirect(frame, c.a, &stale), depth = Redirect(frame, c.b, &stale);
            if (stale) break;
            next_.Clear(color, depth, c.rgba, c.z, c.flags);
            break;
        }
        case Kind::ClearStencil: {
            const SurfaceId stencil = Redirect(frame, c.a, &stale);
            if (stale) break;
            next_.ClearStencil(stencil, uint8_t(c.rgba));
            break;
        }
        case Kind::ConstQuad: {
            const SurfaceId color = Redirect(frame, c.a, &stale), stencil = Redirect(frame, c.b, &stale);
            if (stale) break;
            next_.DrawConstQuad(color, stencil, c.rect, c.rgba, c.write_alpha, c.stencil);
            break;
        }
        case Kind::Copy: {
            const SurfaceId src = Redirect(frame, c.a, &stale), dst = Redirect(frame, c.b, &stale);
            if (stale) break;
            next_.CopyRect(src, c.rect, dst, c.rect2, c.transform);
            break;
        }
        case Kind::Present: {
            ScanoutDesc desc = c.scanout;
            desc.source = Redirect(frame, desc.source, &stale);
            if (stale) break;
            const auto started = Clock::now();
            next_.Present(desc);
            n_.replay_present_us_sum += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
            out.presented = true;
            break;
        }
        case Kind::Draw: {
            DrawParams p = c.params;
            p.color = Redirect(frame, p.color, &stale);
            p.depth = Redirect(frame, p.depth, &stale);
            p.rt_copy = Redirect(frame, p.rt_copy, &stale);
            p.alt16 = Redirect(frame, p.alt16, &stale);
            p.clut = Redirect(frame, p.clut, &stale);
            p.idx_partner = Redirect(frame, p.idx_partner, &stale);
            p.zsrc = Redirect(frame, p.zsrc, &stale);
            p.stencil = Redirect(frame, p.stencil, &stale);
            if (p.cd_copy) {
                // The target's Cd-copy tracker is the live target's: the shadow
                // keeps its own (invalid at the replay's start -> a full copy).
                auto found = trackers.find(p.cd_copy);
                if (found == trackers.end()) found = trackers.emplace(p.cd_copy, CdCopyState{}).first;
                p.cd_copy = &found->second;
            }
            gs::TextureSource tex = c.tex;
            tex.owner.surface = Redirect(frame, tex.owner.surface, &stale);
            tex.owner.clut = Redirect(frame, tex.owner.clut, &stale);
            tex.owner.partner = Redirect(frame, tex.owner.partner, &stale);
            if (stale) break;
            if (review && c.space == Space::Screen) {
                gs::DrawIr ir = *c.ir;
                const double z_max = ir.raster.zbuf.psm == 0 ? 4294967295.0 : ir.raster.zbuf.psm == 1 ? 16777215.0 : 65535.0;
                bool projectable = true;
                for (gs::VertexRec& v : ir.verts)
                    if (!ReviewVertex(K, ir.off_x, ir.off_y, z_max, !ir.use_fst, v)) { projectable = false; break; }
                if (projectable) {
                    gs::GsdlFormat::Bbox(ir.verts.data(), ir.verts.size(), ir.bbox);
                    next_.Draw(ir, tex, p);
                    ++out.reviewed_draws;
                } else {
                    ++n_.unprojectable_draws;
                    next_.Draw(*c.ir, tex, p);
                    ++out.fixed_draws;
                }
            } else {
                next_.Draw(*c.ir, tex, p);
                ++out.fixed_draws;
            }
            break;
        }
    }
    if (stale) ++out.stale;
}

FrameReview::Census FrameReview::Count() const {
    Census c = n_;
    c.stale_cameras = stale_cameras_.load(std::memory_order_relaxed);
    return c;
}

FrameReview::Replayed FrameReview::Replay(Clock::time_point now) {
    Replayed out;
    if (!armed_.load(std::memory_order_acquire)) { ++n_.replays_refused; return out; }
    const auto started = Clock::now();
    const Frame& frame = previous_;
    const Camera& a = previous_.camera;
    const Camera& b = latest_.camera;
    double phase = 1.0;
    if (b.at > a.at) {
        const double span = std::chrono::duration<double>(b.at - a.at).count();
        const double since = std::chrono::duration<double>(now - b.at).count();
        phase = span > 0 ? std::clamp(since / span, 0.0, 1.0) : 1.0;
    }
    double C[16], K[16], inv_p[16], inv_v[16];
    bool cut = false, review = Interpolate(a.to_camera, b.to_camera, phase, C, &cut);
    if (review && cut) {
        for (int i = 0; i < 16; ++i) C[i] = a.to_camera[i];
        phase = 0;
        ++n_.cuts;
    }
    if (review) review = Invert4(a.camera_to_screen, inv_p) && Invert4(a.to_camera, inv_v);
    if (review) {
        Multiply4(inv_p, inv_v, K);
        Multiply4(K, C, K);
        Multiply4(K, a.camera_to_screen, K);
    }
    out.cut = cut;
    out.phase = phase;
    out.tick = frame.ordinal;
    if (review) Position(C, out.position);
    else Position(a.to_camera, out.position);

    in_replay_ = true;
    replaying_.store(true, std::memory_order_relaxed);
    std::unordered_map<const CdCopyState*, CdCopyState> trackers;
    next_.EndFrame();
    try {
        for (const Command& command : frame.commands) Issue(frame, command, K, review, out, trackers);
    } catch (...) {
        // A refused backend command (a stale surface the alive set missed, a
        // crop outside its source): the replay ends without a picture; the
        // recording continues. Counted; never a fault of the game's stream.
        ++n_.faults;
        out.presented = false;
    }
    next_.EndFrame();
    replaying_.store(false, std::memory_order_relaxed);
    in_replay_ = false;

    out.micros = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
    n_.reviewed_draws += out.reviewed_draws;
    n_.fixed_draws += out.fixed_draws;
    n_.stale_commands += out.stale;
    n_.replay_us_last = out.micros;
    n_.replay_us_sum += out.micros;
    n_.replay_us_max = std::max(n_.replay_us_max, out.micros);
    if (out.presented) ++n_.replays; else ++n_.replays_refused;
    return out;
}

// ---------------------------------------------------------------------------
// Backend: pass-through, recording while on.

Caps FrameReview::QueryCaps() const { return next_.QueryCaps(); }

SurfaceId FrameReview::CreateSurface(const SurfaceDesc& desc) {
    const SurfaceId id = next_.CreateSurface(desc);
    if (id.id) alive_.insert(id.id);
    return id;
}

void FrameReview::DestroySurface(SurfaceId id) {
    alive_.erase(id.id);
    auto shadow = shadows_.find(id.id);
    if (shadow != shadows_.end()) {
        next_.DestroySurface(shadow->second.id);
        shadows_.erase(shadow);
        n_.shadows = shadows_.size();
    }
    next_.DestroySurface(id);
}

SurfaceDesc FrameReview::DescribeSurface(SurfaceId id) const { return next_.DescribeSurface(id); }

void FrameReview::Clear(SurfaceId color, SurfaceId depth, uint32_t rgba, float z, uint32_t flags) {
    if (Recording() && !in_replay_) {
        Command c;
        c.kind = Kind::Clear;
        c.a = color; c.b = depth; c.rgba = rgba; c.z = z; c.flags = flags;
        if (color.id) current_.written.insert(color.id);
        if (depth.id) current_.written.insert(depth.id);
        Record(std::move(c));
    }
    next_.Clear(color, depth, rgba, z, flags);
}

void FrameReview::Upload(SurfaceId id, const Rect& rect, const void* bytes, size_t stride) { next_.Upload(id, rect, bytes, stride); }
void FrameReview::UploadLevel(SurfaceId id, int level, int w, int h, const void* rgba) { next_.UploadLevel(id, level, w, h, rgba); }
void FrameReview::FinishMipChain(SurfaceId id, int authored_levels, bool synthesize_tail) {
    next_.FinishMipChain(id, authored_levels, synthesize_tail);
}

DrawResult FrameReview::Draw(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& params) {
    if (Recording() && !in_replay_) {
        Command c;
        c.kind = Kind::Draw;
        c.ir = std::make_unique<gs::DrawIr>(ir);
        c.tex = tex;
        c.params = params;
        c.space = classify_ ? classify_(ir) : Space::Fixed;
        for (SurfaceId written : {params.color, params.depth, params.rt_copy, params.alt16, params.stencil})
            if (written.id) current_.written.insert(written.id);
        Record(std::move(c));
        const auto started = Clock::now();
        const DrawResult result = next_.Draw(ir, tex, params);
        current_.draw_us += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
        return result;
    }
    return next_.Draw(ir, tex, params);
}

void FrameReview::ClearStencil(SurfaceId stencil, uint8_t value) {
    if (Recording() && !in_replay_) {
        Command c;
        c.kind = Kind::ClearStencil;
        c.a = stencil; c.rgba = value;
        if (stencil.id) current_.written.insert(stencil.id);
        Record(std::move(c));
    }
    next_.ClearStencil(stencil, value);
}

void FrameReview::DrawConstQuad(SurfaceId color, SurfaceId stencil, const Rect& scissor, uint32_t rgba, bool write_alpha,
                                const StencilState& st) {
    if (Recording() && !in_replay_) {
        Command c;
        c.kind = Kind::ConstQuad;
        c.a = color; c.b = stencil; c.rect = scissor; c.rgba = rgba; c.write_alpha = write_alpha; c.stencil = st;
        if (color.id) current_.written.insert(color.id);
        if (stencil.id) current_.written.insert(stencil.id);
        Record(std::move(c));
    }
    next_.DrawConstQuad(color, stencil, scissor, rgba, write_alpha, st);
}

void FrameReview::CopyRect(SurfaceId src, const Rect& src_rect, SurfaceId dst, const Rect& dst_rect, uint32_t transform) {
    if (Recording() && !in_replay_) {
        Command c;
        c.kind = Kind::Copy;
        c.a = src; c.b = dst; c.rect = src_rect; c.rect2 = dst_rect; c.transform = transform;
        if (dst.id) current_.written.insert(dst.id);
        Record(std::move(c));
    }
    next_.CopyRect(src, src_rect, dst, dst_rect, transform);
}

void FrameReview::Readback(SurfaceId id, const Rect& rect, void* out, size_t stride) { next_.Readback(id, rect, out, stride); }
FenceId FrameReview::BeginReadback(SurfaceId id, const Rect& rect) { return next_.BeginReadback(id, rect); }
bool FrameReview::ReadbackReady(FenceId fence) { return next_.ReadbackReady(fence); }
bool FrameReview::PollReadback(FenceId fence, void* out, size_t stride) { return next_.PollReadback(fence, out, stride); }

void FrameReview::Present(const ScanoutDesc& desc) {
    if (Recording() && !in_replay_) {
        CloseFrame(desc);
        const auto started = Clock::now();
        next_.Present(desc);
        n_.live_present_us_sum += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
        ++n_.live_presents;
        return;
    }
    next_.Present(desc);
}

void FrameReview::ArmPresentCapture(bool armed) { next_.ArmPresentCapture(armed); }
bool FrameReview::ReadPresented(std::vector<uint8_t>* rgba, int* w, int* h) { return next_.ReadPresented(rgba, w, h); }
void FrameReview::BeginFrame() { next_.BeginFrame(); }
void FrameReview::EndFrame() { next_.EndFrame(); }

}  // namespace render
}  // namespace ps2r

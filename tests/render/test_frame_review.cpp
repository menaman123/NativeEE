// tests/render/test_frame_review.cpp -- FrameReview's re-view arithmetic (no GL):
// the 4x4 inverse, the camera interpolation (slerp + position lerp, the cut
// thresholds) and the per-vertex re-projection through K = P^-1 V^-1 C P --
// identity at phase 0, a pure camera translation moving a point by the
// expected screen parallax, a 90-degree swing landing where the rotated camera
// sees it, the STQ rescale by the depth ratio, and the behind-camera refusal.
#include <cmath>
#include <cstdio>

#include "engine/render/review/frame_review.h"

using ps2r::render::FrameReview;

namespace {
int fails = 0;
void Check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}
bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// A camera at `pos` looking down +z (rows = basis; row-vector convention):
// world->camera = [I 0; -pos 1] with an optional yaw about y.
void Camera(double yaw, const double pos[3], double out[16]) {
    const double c = std::cos(yaw), s = std::sin(yaw);
    const double r[3][3] = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) out[i * 4 + j] = r[i][j];
        out[i * 4 + 3] = 0;
    }
    for (int j = 0; j < 3; ++j) out[12 + j] = -(pos[0] * r[0][j] + pos[1] * r[1][j] + pos[2] * r[2][j]);
    out[15] = 1;
}

// camera->screen: a perspective with focal f (GS 12.4 units per unit of x/z),
// centre (cx, cy), depth z_gs = A + B / z (A, B chosen so z_gs = (1 - 1/z) * 2^24),
// w = z (row-vector: X = x f + z cx, W = z).
void Projection(double f, double cx, double cy, double out[16]) {
    for (int i = 0; i < 16; ++i) out[i] = 0;
    out[0] = f;            // x -> X
    out[5] = -f;           // y -> Y (screen y down)
    out[10] = 16777216.0;  // z -> Z (A * z)
    out[11] = 1.0;         // z -> W
    out[14] = -16777216.0; // 1 -> Z (B)
    out[2 * 4 + 0] = cx;   // z -> X (centre times w)
    out[2 * 4 + 1] = cy;
}

ps2r::gs::VertexRec Project(const double world[3], const double V[16], const double P[16], float off) {
    double S[16], h[4] = {0, 0, 0, 0};
    FrameReview::Multiply4(V, P, S);
    const double p[4] = {world[0], world[1], world[2], 1};
    for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k) h[j] += p[k] * S[k * 4 + j];
    ps2r::gs::VertexRec v{};
    v.x = float(h[0] / h[3] / 16.0 - off);
    v.y = float(h[1] / h[3] / 16.0 - off);
    v.z = float(h[2] / h[3]);
    v.s = float(0.5 / h[3]);
    v.t = float(0.25 / h[3]);
    v.q = float(1.0 / h[3]);
    return v;
}

void Review(const double Va[16], const double Vb[16], const double P[16], double phase, double K[16], bool* cut) {
    double C[16], inv_p[16], inv_v[16];
    Check(FrameReview::Interpolate(Va, Vb, phase, C, cut), "Interpolate succeeds");
    Check(FrameReview::Invert4(P, inv_p) && FrameReview::Invert4(Va, inv_v), "P and V invert");
    FrameReview::Multiply4(inv_p, inv_v, K);
    FrameReview::Multiply4(K, C, K);
    FrameReview::Multiply4(K, P, K);
}
}  // namespace

int main() {
    const double f = 4096.0, cx = 2048.0 * 16, cy = 2048.0 * 16;
    const float off = 2048.0f - 256.0f;
    double P[16];
    Projection(f, cx, cy, P);
    {
        double inv[16], id[16];
        Check(FrameReview::Invert4(P, inv), "the perspective camera->screen matrix inverts");
        FrameReview::Multiply4(P, inv, id);
        bool ok = true;
        for (int i = 0; i < 16; ++i) ok = ok && Near(id[i], i % 5 == 0 ? 1 : 0, 1e-9);
        Check(ok, "P . P^-1 = I");
    }
    const double pa[3] = {0, 0, 0}, pb[3] = {1, 0, 0};
    double Va[16], Vb[16];
    Camera(0, pa, Va);
    Camera(0, pb, Vb);
    const double world[3] = {2, 1, 10};
    const ps2r::gs::VertexRec at_a = Project(world, Va, P, off), at_b = Project(world, Vb, P, off);
    {
        double K[16];
        bool cut = true;
        Review(Va, Vb, P, 0.0, K, &cut);
        Check(!cut, "a one-unit step is not a cut");
        ps2r::gs::VertexRec v = at_a;
        Check(FrameReview::ReviewVertex(K, off, off, 16777215.0, true, v), "phase 0 re-projects");
        Check(Near(v.x, at_a.x, 1e-3) && Near(v.y, at_a.y, 1e-3) && Near(v.z, at_a.z, 2.0) && Near(v.q, at_a.q, 1e-7),
              "phase 0 = the recorded vertex (x, y, z, q)");
    }
    {
        double K[16];
        bool cut = false;
        Review(Va, Vb, P, 1.0, K, &cut);
        ps2r::gs::VertexRec v = at_a;
        Check(FrameReview::ReviewVertex(K, off, off, 16777215.0, true, v), "phase 1 re-projects");
        Check(Near(v.x, at_b.x, 1e-2) && Near(v.y, at_b.y, 1e-2), "phase 1 = the point as the moved camera projects it");
        // The expected parallax: f * dx / z / 16 pixels.
        Check(Near(at_a.x - v.x, f * 1.0 / 10.0 / 16.0, 1e-2), "a one-unit camera translation at depth 10 shifts x by f/z/16 px");
        Check(Near(v.s, at_b.s, 1e-7) && Near(v.q, at_b.q, 1e-7), "STQ rescaled by the depth ratio (unchanged depth here)");
    }
    {
        // Half way: half the parallax.
        double K[16];
        bool cut = false;
        Review(Va, Vb, P, 0.5, K, &cut);
        ps2r::gs::VertexRec v = at_a;
        FrameReview::ReviewVertex(K, off, off, 16777215.0, true, v);
        Check(Near(at_a.x - v.x, 0.5 * f * 1.0 / 10.0 / 16.0, 1e-2), "phase 0.5 shifts by half the parallax (position lerp)");
    }
    {
        // A yaw of 90 degrees over the tick: the point at (0,0,10) seen by the
        // rotated camera lands where its camera-space (10,0,0)... behind the
        // frustum edge; use a 10-degree swing instead and compare against the
        // direct projection; the slerp midway = 5 degrees.
        double Vr[16], Vh[16];
        Camera(10.0 * M_PI / 180.0, pa, Vr);
        Camera(5.0 * M_PI / 180.0, pa, Vh);
        const double w2[3] = {0.5, 0.2, 8};
        const ps2r::gs::VertexRec a = Project(w2, Va, P, off), half = Project(w2, Vh, P, off), full = Project(w2, Vr, P, off);
        double K[16];
        bool cut = true;
        Review(Va, Vr, P, 0.5, K, &cut);
        Check(!cut, "a 10-degree swing is not a cut");
        ps2r::gs::VertexRec v = a;
        FrameReview::ReviewVertex(K, off, off, 16777215.0, true, v);
        Check(Near(v.x, half.x, 0.05) && Near(v.y, half.y, 0.05), "slerp midway = the 5-degree camera's projection");
        Review(Va, Vr, P, 1.0, K, &cut);
        v = a;
        FrameReview::ReviewVertex(K, off, off, 16777215.0, true, v);
        Check(Near(v.x, full.x, 0.05) && Near(v.y, full.y, 0.05) && Near(v.q, full.q, 1e-6),
              "phase 1 of the swing = the rotated camera's projection, q by the new depth");
    }
    {
        double Vfar[16], C[16];
        const double far[3] = {5, 0, 0};
        Camera(0, far, Vfar);
        bool cut = false;
        FrameReview::Interpolate(Va, Vfar, 0.5, C, &cut);
        Check(cut, "a five-unit step is a cut");
        double Vspin[16];
        Camera(60.0 * M_PI / 180.0, pa, Vspin);
        FrameReview::Interpolate(Va, Vspin, 0.5, C, &cut);
        Check(cut, "a 60-degree swing is a cut");
    }
    {
        // A point behind the moved camera refuses (w' <= 0): camera jumps past it.
        double Vpast[16], K[16], C[16], inv_p[16], inv_v[16];
        const double past[3] = {0, 0, 20};
        Camera(0, past, Vpast);
        FrameReview::Interpolate(Va, Vpast, 1.0, C, nullptr);
        FrameReview::Invert4(P, inv_p);
        FrameReview::Invert4(Va, inv_v);
        FrameReview::Multiply4(inv_p, inv_v, K);
        FrameReview::Multiply4(K, C, K);
        FrameReview::Multiply4(K, P, K);
        ps2r::gs::VertexRec v = at_a;
        Check(!FrameReview::ReviewVertex(K, off, off, 16777215.0, true, v), "a vertex behind the re-viewed camera is refused");
    }
    std::printf("test_frame_review: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

// tests/render/test_vertex_convert.cpp -- Render_VertexConvert: the fused single-pass
// conversion equals the straightforward reference (a per-vertex loop with the three
// UV modes, then the two separate bbox loops) on random vertices, including the
// tail-vertex rule (n not a multiple of 3), the pad zeroing and NaN coordinates.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gs/hle/ir/vertex_rec.h"
#include "engine/render/backends/gl/vertex_convert.h"

using ps2r::gs::VertexRec;
using ps2r::render::GpuVertex;
using ps2r::render::VertexConvert;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static uint64_t g_seed = 0x5EEDBA5Eull;
static uint32_t Rnd() {
    g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
    return static_cast<uint32_t>(g_seed);
}
static float RndF(float lo, float hi) {
    return lo + (hi - lo) * (Rnd() & 0xFFFFFF) / 16777216.0f;
}

// Reference: the legacy per-vertex loop + the vertex-0-seeded strict-compare extent
// loop over [0, tri_n) and the same loop extended over the tail.
static void Reference(const VertexRec* src, size_t n, GpuVertex* dst, VertexConvert::UvMode mode,
                      int w, int h, float tri[4], float all[4]) {
    for (size_t i = 0; i < n; i++) {
        const VertexRec& v = src[i];
        GpuVertex& g = dst[i];
        std::memset(&g, 0, sizeof(g));
        g.x = v.x; g.y = v.y; g.z = v.z;
        if (mode == VertexConvert::UvMode::UvFixed) {
            g.u = static_cast<float>(v.uv_u) / 16.0f;
            g.v = static_cast<float>(v.uv_v) / 16.0f;
            g.q = 1.0f;
        } else if (mode == VertexConvert::UvMode::Stq) {
            g.u = v.s * static_cast<float>(w);
            g.v = v.t * static_cast<float>(h);
            g.q = v.q;
        } else {
            g.u = 0.f; g.v = 0.f; g.q = 1.f;
        }
        g.r = v.r; g.g = v.g; g.b = v.b; g.a = v.a; g.fog = v.fog;
        g.pad[0] = g.pad[1] = g.pad[2] = 0;
    }
    const size_t tri_n = (n / 3) * 3;
    float x0 = src[0].x, y0 = src[0].y, x1 = src[0].x, y1 = src[0].y;
    for (size_t i = 0; i < tri_n; i++) {
        if (src[i].x < x0) x0 = src[i].x;
        if (src[i].y < y0) y0 = src[i].y;
        if (src[i].x > x1) x1 = src[i].x;
        if (src[i].y > y1) y1 = src[i].y;
    }
    tri[0] = x0; tri[1] = y0; tri[2] = x1; tri[3] = y1;
    for (size_t i = tri_n; i < n; i++) {
        if (src[i].x < x0) x0 = src[i].x;
        if (src[i].y < y0) y0 = src[i].y;
        if (src[i].x > x1) x1 = src[i].x;
        if (src[i].y > y1) y1 = src[i].y;
    }
    all[0] = x0; all[1] = y0; all[2] = x1; all[3] = y1;
}

static bool SameBits(float a, float b) {
    uint32_t x, y;
    std::memcpy(&x, &a, 4); std::memcpy(&y, &b, 4);
    return x == y;
}

int main() {
    const VertexConvert::UvMode modes[3] = {VertexConvert::UvMode::Untextured,
                                            VertexConvert::UvMode::UvFixed,
                                            VertexConvert::UvMode::Stq};
    int mismatches = 0;
    for (int iter = 0; iter < 3000; iter++) {
        const size_t n = 1 + (Rnd() % 40);
        std::vector<VertexRec> src(n);
        for (VertexRec& v : src) {
            std::memset(&v, 0xA5, sizeof(v));   // poison the pad bytes on the source side
            v.x = RndF(-300.f, 900.f); v.y = RndF(-300.f, 700.f); v.z = RndF(0.f, 4.0e9f);
            v.s = RndF(-2.f, 3.f); v.t = RndF(-2.f, 3.f); v.q = RndF(0.001f, 8.f);
            v.uv_u = static_cast<uint16_t>(Rnd()); v.uv_v = static_cast<uint16_t>(Rnd());
            v.r = Rnd(); v.g = Rnd(); v.b = Rnd(); v.a = Rnd(); v.fog = Rnd();
        }
        if (iter % 97 == 0) src[Rnd() % n].x = std::nanf("");   // NaN keeps compares false
        const int w = 1 << (Rnd() % 11), h = 1 << (Rnd() % 11);
        const VertexConvert::UvMode mode = modes[iter % 3];
        std::vector<GpuVertex> got(n), want(n);
        std::memset(got.data(), 0xCC, n * sizeof(GpuVertex));
        float tri[4], all[4];
        Reference(src.data(), n, want.data(), mode, w, h, tri, all);
        const VertexConvert::FusedBboxes bb =
            VertexConvert::Convert(src.data(), n, got.data(), mode, w, h);
        bool ok = std::memcmp(got.data(), want.data(), n * sizeof(GpuVertex)) == 0;
        ok = ok && SameBits(bb.tri.x0, tri[0]) && SameBits(bb.tri.y0, tri[1]) &&
             SameBits(bb.tri.x1, tri[2]) && SameBits(bb.tri.y1, tri[3]);
        ok = ok && SameBits(bb.all.x0, all[0]) && SameBits(bb.all.y0, all[1]) &&
             SameBits(bb.all.x1, all[2]) && SameBits(bb.all.y1, all[3]);
        ok = ok && bb.valid == (n >= 3);
        if (!ok) mismatches++;
    }
    CHECK(mismatches == 0, "fused conversion == reference on 3000 random batches");
    // Tail rule: 5 vertices => tri covers the first 3 only, all covers 5.
    {
        VertexRec v[5] = {};
        for (int i = 0; i < 5; i++) { v[i].x = 10.f * i; v[i].y = 100.f - 10.f * i; }
        GpuVertex g[5];
        const VertexConvert::FusedBboxes bb =
            VertexConvert::Convert(v, 5, g, VertexConvert::UvMode::Untextured, 0, 0);
        CHECK(bb.valid, "n=5 valid");
        CHECK(bb.tri.x1 == 20.f && bb.tri.y0 == 80.f, "tri box stops at vertex 2");
        CHECK(bb.all.x1 == 40.f && bb.all.y0 == 60.f, "all box includes the tail");
        CHECK(g[0].q == 1.0f && g[0].u == 0.f, "untextured u=0 q=1");
        CHECK(g[4].pad[0] == 0 && g[4].pad[1] == 0 && g[4].pad[2] == 0, "pad zeroed");
    }
    // n=0 and n<3 are invalid boxes.
    {
        VertexRec v[2] = {};
        GpuVertex g[2];
        CHECK(!VertexConvert::Convert(v, 0, g, VertexConvert::UvMode::Stq, 8, 8).valid, "n=0 invalid");
        CHECK(!VertexConvert::Convert(v, 2, g, VertexConvert::UvMode::Stq, 8, 8).valid, "n=2 invalid");
    }
    // UvFixed: 12.4 fixed point => texel = raw / 16.
    {
        VertexRec v[3] = {};
        v[0].uv_u = 0x0100; v[0].uv_v = 0x0018;
        GpuVertex g[3];
        VertexConvert::Convert(v, 3, g, VertexConvert::UvMode::UvFixed, 0, 0);
        CHECK(g[0].u == 16.0f && g[0].v == 1.5f && g[0].q == 1.0f, "12.4 -> texel");
    }
    std::printf("test_vertex_convert: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

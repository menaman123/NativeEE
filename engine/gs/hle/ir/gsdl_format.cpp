// gs/hle/ir/gsdl_format.cpp -- GsdlFormat helpers: header validation and the
// hash primitives whose exact values are part of the v2 format.
#include "engine/gs/hle/ir/gsdl_format.h"

#include <cstring>

namespace ps2r {
namespace gs {

namespace {

constexpr uint64_t kOffset = 1469598103934665603ull;
constexpr uint64_t kPrime = 1099511628211ull;

}  // namespace

bool GsdlFormat::ValidateHeader(const GsdlFileHeader& h) {
    return h.magic == kMagic && h.version >= 1 && h.version <= kVersion;
}

size_t GsdlFormat::DrawRecSize(uint32_t version) {
    return version >= 2 ? sizeof(DrawRec) : kDrawRecSizeV1;
}

uint64_t GsdlFormat::Fnv1a(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    uint64_t h = kOffset;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= kPrime;
    }
    return h;
}

uint64_t GsdlFormat::Fnv1aFast(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    uint64_t h = kOffset ^ static_cast<uint64_t>(n);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, b + i, 8);
        h ^= w;
        h *= kPrime;
    }
    for (; i < n; i++) {
        h ^= b[i];
        h *= kPrime;
    }
    return h;
}

uint64_t GsdlFormat::Fnv1aSampled(const void* p, size_t n) {
    constexpr size_t kFull = 16 * 1024, kStripe = 4 * 1024, kStripes = 2;
    if (n <= kFull) return Fnv1aFast(p, n);
    const uint8_t* b = static_cast<const uint8_t*>(p);
    uint64_t h = kOffset ^ (static_cast<uint64_t>(n) * kPrime);
    const size_t step = (n - kStripe) / (kStripes - 1);
    for (size_t s = 0; s < kStripes; s++) {
        const size_t off = (s == kStripes - 1) ? n - kStripe : s * step;
        h = (h * kPrime) ^ Fnv1aFast(b + off, kStripe);
    }
    return h;
}

uint64_t GsdlFormat::VertexHash(const VertexRec* v, size_t n) {
    if (n == 0) return 0;
    uint64_t h[4] = {kOffset, 12638153115695167455ull, 14695981039346656037ull, kPrime};
    h[0] ^= static_cast<uint64_t>(n);
    for (size_t i = 0; i < n; i++) {
        uint64_t w0, w1, w2;
        std::memcpy(&w0, &v[i].x, 8);  // x, y
        std::memcpy(&w1, &v[i].z, 8);  // z, s
        std::memcpy(&w2, &v[i].t, 8);  // t, q
        uint32_t uv;
        std::memcpy(&uv, &v[i].uv_u, 4);
        const uint32_t rgba = static_cast<uint32_t>(v[i].r) | (static_cast<uint32_t>(v[i].g) << 8) |
                              (static_cast<uint32_t>(v[i].b) << 16) | (static_cast<uint32_t>(v[i].a) << 24);
        const uint64_t w4 = static_cast<uint64_t>(v[i].fog) |
                            (static_cast<uint64_t>(v[i].draw_kick ? 1 : 0) << 8);
        const uint64_t w3 = (static_cast<uint64_t>(uv) | (static_cast<uint64_t>(rgba) << 32)) ^
                            (w4 * 0x9E3779B97F4A7C15ull);
        h[0] = (h[0] ^ w0) * kPrime;
        h[1] = (h[1] ^ w1) * kPrime;
        h[2] = (h[2] ^ w2) * kPrime;
        h[3] = (h[3] ^ w3) * kPrime;
    }
    uint64_t r = h[0];
    r = (r * kPrime) ^ h[1];
    r = (r * kPrime) ^ h[2];
    r = (r * kPrime) ^ h[3];
    return r;
}

uint32_t GsdlFormat::HandlerId(const char* name) {
    uint32_t h = 2166136261u;
    for (const char* c = name; c && *c; c++) {
        h ^= static_cast<uint8_t>(*c);
        h *= 16777619u;
    }
    return h ? h : 1u;
}

void GsdlFormat::Bbox(const VertexRec* v, size_t n, float out[6]) {
    if (n == 0) {
        for (int i = 0; i < 6; i++) out[i] = 0.0f;
        return;
    }
    float bb[6] = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < n; i++) {
        if (v[i].x < bb[0]) bb[0] = v[i].x;
        if (v[i].y < bb[1]) bb[1] = v[i].y;
        if (v[i].z < bb[2]) bb[2] = v[i].z;
        if (v[i].x > bb[3]) bb[3] = v[i].x;
        if (v[i].y > bb[4]) bb[4] = v[i].y;
        if (v[i].z > bb[5]) bb[5] = v[i].z;
    }
    std::memcpy(out, bb, sizeof(bb));
}

}  // namespace gs
}  // namespace ps2r

// tests/gs/ir/test_gsdl_format.cpp -- GsdlFormat layout pins: every struct size and
// field offset of the .gsdl format asserted at run time (the header's static_asserts
// pin them at compile time), the hash primitives' fixed points, and -- when the
// frozen corpus is present -- the FileHeader decode of the first 4 KiB of one capture.
//
//   test_gsdl_format [<capture.gsdl>]     exit 0 pass, 1 fail, 77 skip (no capture)
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gs/hle/ir/gsdl_format.h"

using ps2r::gs::DrawRec;
using ps2r::gs::GsdlFileHeader;
using ps2r::gs::GsdlFormat;
using ps2r::gs::GsdlNameRec;
using ps2r::gs::GsdlRecordHeader;
using ps2r::gs::JobRec;
using ps2r::gs::VertexRec;

namespace {

int g_checks = 0, g_failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            std::printf("  FAIL line %d: ", __LINE__);                \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

#define OFF(T, f, want) CHECK(offsetof(T, f) == (want), #T "." #f " at %zu, want %d", offsetof(T, f), (want))

void CheckLayout() {
    CHECK(sizeof(GsdlFileHeader) == 24, "FileHeader %zu", sizeof(GsdlFileHeader));
    OFF(GsdlFileHeader, magic, 0); OFF(GsdlFileHeader, version, 4); OFF(GsdlFileHeader, flags, 8);
    OFF(GsdlFileHeader, reserved, 12); OFF(GsdlFileHeader, start_unix_ns, 16);
    CHECK(sizeof(GsdlRecordHeader) == 8, "RecordHeader %zu", sizeof(GsdlRecordHeader));
    OFF(GsdlRecordHeader, kind, 0); OFF(GsdlRecordHeader, size, 4);
    CHECK(sizeof(GsdlNameRec) == 8, "NameRec %zu", sizeof(GsdlNameRec));
    OFF(GsdlNameRec, handler, 0); OFF(GsdlNameRec, name_len, 4);

    CHECK(sizeof(JobRec) == 52, "JobRec %zu", sizeof(JobRec));
    OFF(JobRec, type, 0); OFF(JobRec, seq, 4); OFF(JobRec, src_frame, 8); OFF(JobRec, arg1, 12);
    OFF(JobRec, arg2, 16); OFF(JobRec, arg3, 20); OFF(JobRec, arg4, 24); OFF(JobRec, vram_epoch, 28);
    OFF(JobRec, payload_bytes, 36); OFF(JobRec, payload_incl, 40); OFF(JobRec, payload_hash, 44);

    CHECK(sizeof(VertexRec) == 36, "VertexRec %zu", sizeof(VertexRec));
    OFF(VertexRec, x, 0); OFF(VertexRec, y, 4); OFF(VertexRec, z, 8); OFF(VertexRec, s, 12);
    OFF(VertexRec, t, 16); OFF(VertexRec, q, 20); OFF(VertexRec, uv_u, 24); OFF(VertexRec, uv_v, 26);
    OFF(VertexRec, r, 28); OFF(VertexRec, g, 29); OFF(VertexRec, b, 30); OFF(VertexRec, a, 31);
    OFF(VertexRec, fog, 32); OFF(VertexRec, draw_kick, 33); OFF(VertexRec, pad, 34);

    CHECK(sizeof(DrawRec) == 220, "DrawRec %zu", sizeof(DrawRec));
    CHECK(GsdlFormat::kDrawRecSizeV1 == 188, "v1 size %zu", GsdlFormat::kDrawRecSizeV1);
    OFF(DrawRec, seq, 0); OFF(DrawRec, src_frame, 4); OFF(DrawRec, vram_epoch, 8);
    OFF(DrawRec, prim_type, 16); OFF(DrawRec, gouraud, 17); OFF(DrawRec, fog_enabled, 18);
    OFF(DrawRec, use_fst, 19); OFF(DrawRec, alpha_blend, 20); OFF(DrawRec, scanmsk, 21);
    OFF(DrawRec, shuffle_type, 22); OFF(DrawRec, pad0, 23);
    OFF(DrawRec, tex_enabled, 24); OFF(DrawRec, tex_fst, 25); OFF(DrawRec, tfx, 26); OFF(DrawRec, tcc, 27);
    OFF(DrawRec, cpsm, 28); OFF(DrawRec, csm, 29); OFF(DrawRec, csa, 30); OFF(DrawRec, cld, 31);
    OFF(DrawRec, tbp, 32); OFF(DrawRec, tbw, 36); OFF(DrawRec, tpsm, 40); OFF(DrawRec, tw, 44);
    OFF(DrawRec, th, 48); OFF(DrawRec, cbp, 52);
    OFF(DrawRec, mmag, 56); OFF(DrawRec, mmin, 57); OFF(DrawRec, wms, 58); OFF(DrawRec, wmt, 59);
    OFF(DrawRec, minu, 60); OFF(DrawRec, maxu, 62); OFF(DrawRec, minv, 64); OFF(DrawRec, maxv, 66);
    OFF(DrawRec, texa_ta0, 68); OFF(DrawRec, texa_ta1, 69); OFF(DrawRec, texa_aem, 70); OFF(DrawRec, pad1, 71);
    OFF(DrawRec, vram_snapshot_hash, 72); OFF(DrawRec, clut_snapshot_hash, 80);
    OFF(DrawRec, vram_snapshot_bytes, 88); OFF(DrawRec, clut_snapshot_bytes, 92);
    OFF(DrawRec, colclamp, 96); OFF(DrawRec, pabe, 97); OFF(DrawRec, fba, 98); OFF(DrawRec, alpha_a, 99);
    OFF(DrawRec, alpha_b, 100); OFF(DrawRec, alpha_c, 101); OFF(DrawRec, alpha_d, 102); OFF(DrawRec, fix_alpha, 103);
    OFF(DrawRec, fogcol_r, 104); OFF(DrawRec, fogcol_g, 105); OFF(DrawRec, fogcol_b, 106); OFF(DrawRec, pad2, 107);
    OFF(DrawRec, frame_fbp, 108); OFF(DrawRec, frame_fbw, 112); OFF(DrawRec, frame_fbmsk, 116);
    OFF(DrawRec, frame_psm, 120); OFF(DrawRec, zbuf_psm, 121); OFF(DrawRec, zbuf_zmsk, 122); OFF(DrawRec, pad3, 123);
    OFF(DrawRec, zbuf_zbp, 124);
    OFF(DrawRec, scissor_x0, 128); OFF(DrawRec, scissor_y0, 130); OFF(DrawRec, scissor_x1, 132); OFF(DrawRec, scissor_y1, 134);
    OFF(DrawRec, test_ate, 136); OFF(DrawRec, test_atst, 137); OFF(DrawRec, test_aref, 138); OFF(DrawRec, test_afail, 139);
    OFF(DrawRec, test_date, 140); OFF(DrawRec, test_datm, 141); OFF(DrawRec, test_zte, 142); OFF(DrawRec, test_ztst, 143);
    OFF(DrawRec, vertex_count, 144); OFF(DrawRec, verts_incl, 148); OFF(DrawRec, vertex_hash, 152);
    OFF(DrawRec, bbox, 160); OFF(DrawRec, shuffle_uv_scale_u, 184);
    OFF(DrawRec, hle_handler, 188); OFF(DrawRec, hle_frame_inst, 192); OFF(DrawRec, hle_image, 196);
    OFF(DrawRec, hle_kick_pc, 204); OFF(DrawRec, hle_kick_ord, 206); OFF(DrawRec, off_x, 208);
    OFF(DrawRec, off_y, 212); OFF(DrawRec, gs_context, 216); OFF(DrawRec, pad4, 217);

    CHECK(GsdlFormat::kMagic == 0x524C4432u, "magic");
    CHECK(GsdlFormat::kVersion == 2, "version");
    CHECK(GsdlFormat::kJob == 1 && GsdlFormat::kDraw == 2 && GsdlFormat::kFrame == 3 && GsdlFormat::kName == 4, "kinds");
    CHECK(GsdlFormat::DrawRecSize(1) == 188 && GsdlFormat::DrawRecSize(2) == 220, "DrawRecSize");
}

void CheckHashes() {
    // The format's basis is the pinned constant 1469598103934665603 (NOT the textbook
    // FNV-1a basis 14695981039346656037): every hash in the corpus was produced with
    // it, so it is part of the format. Prime 1099511628211.
    const uint64_t kBasis = 1469598103934665603ull, kPrime = 1099511628211ull;
    CHECK(GsdlFormat::Fnv1a("", 0) == kBasis, "fnv1a empty");
    CHECK(GsdlFormat::Fnv1a("a", 1) == (kBasis ^ 0x61u) * kPrime, "fnv1a a");
    // Fnv1aFast folds the length into the seed, so empty input is basis ^ 0 = basis.
    CHECK(GsdlFormat::Fnv1aFast("", 0) == kBasis, "fast empty");
    // 8-byte word path: one multiply over the little-endian word.
    const uint8_t w[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint64_t word = 0x0807060504030201ull;
    CHECK(GsdlFormat::Fnv1aFast(w, 8) == ((kBasis ^ 8u) ^ word) * kPrime, "fast word");
    // Sampled == Fast up to 16 KiB; differs above (size-seeded two-stripe fold).
    std::vector<uint8_t> big(16 * 1024 + 1, 0x5A);
    CHECK(GsdlFormat::Fnv1aSampled(big.data(), 16 * 1024) == GsdlFormat::Fnv1aFast(big.data(), 16 * 1024), "sampled<=16K");
    CHECK(GsdlFormat::Fnv1aSampled(big.data(), big.size()) != GsdlFormat::Fnv1aFast(big.data(), big.size()), "sampled>16K");
    // VertexHash: 0 for none; deterministic; sensitive to draw_kick alone.
    CHECK(GsdlFormat::VertexHash(nullptr, 0) == 0, "vh empty");
    VertexRec v[2]{};
    v[0].x = 1.5f; v[0].y = 2.5f; v[0].z = 3.f; v[0].s = 0.25f; v[0].t = 0.5f; v[0].q = 1.f;
    v[0].uv_u = 7; v[0].uv_v = 9; v[0].r = 1; v[0].g = 2; v[0].b = 3; v[0].a = 4; v[0].fog = 5; v[0].draw_kick = 1;
    v[1] = v[0]; v[1].draw_kick = 0;
    CHECK(GsdlFormat::VertexHash(v, 1) == GsdlFormat::VertexHash(v, 1), "vh deterministic");
    CHECK(GsdlFormat::VertexHash(v, 1) != GsdlFormat::VertexHash(v + 1, 1), "vh draw_kick sensitive");
    CHECK(GsdlFormat::VertexHash(v, 1) != GsdlFormat::VertexHash(v, 2), "vh count sensitive");
    // The pad bytes are not hashed.
    VertexRec vp = v[0]; vp.pad[0] = 0xFF;
    CHECK(GsdlFormat::VertexHash(&vp, 1) == GsdlFormat::VertexHash(v, 1), "vh ignores pad");
    CHECK(GsdlFormat::HandlerId("") == 2166136261u, "handler empty = offset");
    CHECK(GsdlFormat::HandlerId("a") == 0xe40c292cu, "handler a");
    float bb[6];
    GsdlFormat::Bbox(nullptr, 0, bb);
    CHECK(bb[0] == 0 && bb[3] == 0 && bb[5] == 0, "bbox empty");
    GsdlFormat::Bbox(v, 2, bb);
    CHECK(bb[0] == 1.5f && bb[1] == 2.5f && bb[2] == 3.f && bb[3] == 1.5f && bb[4] == 2.5f && bb[5] == 3.f, "bbox");
}

// Decode the FileHeader (and the first record header) of a real capture.
int CheckCapture(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::printf("SKIP: capture %s absent\n", path);
        return 77;
    }
    std::vector<uint8_t> head(4096, 0);
    const size_t got = std::fread(head.data(), 1, head.size(), f);
    std::fclose(f);
    CHECK(got >= sizeof(GsdlFileHeader) + sizeof(GsdlRecordHeader), "short capture %zu", got);
    GsdlFileHeader h;
    std::memcpy(&h, head.data(), sizeof(h));
    std::printf("  %s: magic=0x%08x version=%u flags=0x%x reserved=%u\n", path, h.magic, h.version, h.flags,
                h.reserved);
    CHECK(h.magic == GsdlFormat::kMagic, "magic 0x%08x", h.magic);
    CHECK(h.version == 2, "version %u", h.version);
    CHECK((h.flags & GsdlFormat::kFlagVerts) != 0, "verts flag");
    CHECK((h.flags & GsdlFormat::kFlagPixels) != 0, "pixels flag");
    CHECK(h.reserved == 0, "reserved");
    CHECK(GsdlFormat::ValidateHeader(h), "ValidateHeader");
    GsdlRecordHeader rh;
    std::memcpy(&rh, head.data() + sizeof(h), sizeof(rh));
    CHECK(rh.kind >= GsdlFormat::kJob && rh.kind <= GsdlFormat::kName, "first record kind %u", rh.kind);
    if (rh.kind == GsdlFormat::kDraw) {
        DrawRec d;
        std::memcpy(&d, head.data() + sizeof(h) + sizeof(rh), sizeof(d));
        CHECK(rh.size == sizeof(DrawRec) + (d.verts_incl ? d.vertex_count * sizeof(VertexRec) : 0),
              "kDraw size %u vs %u verts", rh.size, d.vertex_count);
    } else if (rh.kind == GsdlFormat::kJob || rh.kind == GsdlFormat::kFrame) {
        CHECK(rh.size >= sizeof(JobRec), "job size %u", rh.size);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    CheckLayout();
    CheckHashes();
    int rc = 0;
    if (argc > 1) rc = CheckCapture(argv[1]);
    std::printf("test_gsdl_format: %d checks, %d failures%s\n", g_checks, g_failures,
                rc == 77 ? " (capture SKIP)" : "");
    if (g_failures) return 1;
    return rc;
}

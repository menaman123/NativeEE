// tests/gs/ir/test_draw_ir.cpp -- DrawIR proofs.
//
//   test_draw_ir equivalence
//       Invariant 27: for every convertible GIF packet, DrawIrLift's stream and
//       DirectOps' op stream canonicalise to the same GS-observable event sequence
//       (DrawIrVerifier): handcrafted shapes + 20000 seeded random packets, and
//       decline parity on IMAGE tags / truncation / trailing bytes.
//   test_draw_ir roundtrip [<capture.gsdl>]
//       DrawRec -> DrawIr::FromDrawRec -> ToDrawRec is byte-identical: a synthetic
//       record with every field populated, and every draw of the capture when given
//       (exit 77 when the capture is absent).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gs/hle/ir/direct_ops.h"
#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/ir/draw_ir_lift.h"
#include "engine/gs/hle/ir/draw_ir_verifier.h"
#include "tests/lib/gsdl_reader.h"

using ps2r::gs::DirectOps;
using ps2r::gs::DrawIr;
using ps2r::gs::DrawIrLift;
using ps2r::gs::DrawIrVerifier;
using ps2r::gs::DrawRec;
using ps2r::gs::GsdlReader;
using ps2r::gs::VertexRec;
using Event = DrawIrVerifier::Event;

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

const char* EvName(Event::T t) {
    switch (t) {
        case Event::Kick: return "Kick";
        case Event::State: return "State";
        case Event::Prim: return "Prim";
        case Event::Flush: return "Flush";
        case Event::SrcId: return "SrcId";
        case Event::SrcName: return "SrcName";
        case Event::Final: return "Final";
    }
    return "?";
}

// ---- packet builder (a byte vector + free functions) --------------------------
using Bytes = std::vector<uint8_t>;
void Qw(Bytes& b, uint64_t lo, uint64_t hi) {
    const size_t o = b.size();
    b.resize(o + 16);
    std::memcpy(b.data() + o, &lo, 8);
    std::memcpy(b.data() + o + 8, &hi, 8);
}
void Dw(Bytes& b, uint64_t v) {
    const size_t o = b.size();
    b.resize(o + 8);
    std::memcpy(b.data() + o, &v, 8);
}
// GIFtag: NLOOP, EOP, PRE, PRIM, FLG (0 PACKED 1 REGLIST 2 IMAGE), regs
void Tag(Bytes& b, uint32_t nloop, bool eop, bool pre, uint32_t prim, uint32_t flg, const std::vector<uint8_t>& regs) {
    uint64_t lo = static_cast<uint64_t>(nloop & 0x7FFF);
    if (eop) lo |= 1ull << 15;
    if (pre) lo |= 1ull << 46;
    lo |= (static_cast<uint64_t>(prim & 0x7FF)) << 47;
    lo |= (static_cast<uint64_t>(flg & 3)) << 58;
    const uint32_t nreg = static_cast<uint32_t>(regs.size());
    lo |= (static_cast<uint64_t>(nreg & 0xF)) << 60;  // 16 encodes as 0
    uint64_t hi = 0;
    for (size_t i = 0; i < regs.size() && i < 16; i++) hi |= (static_cast<uint64_t>(regs[i] & 0xF)) << (i * 4);
    Qw(b, lo, hi);
}

uint64_t StQw(float s, float t) {
    return static_cast<uint64_t>(DrawIrVerifier::FloatBits(s)) |
           (static_cast<uint64_t>(DrawIrVerifier::FloatBits(t)) << 32);
}
uint64_t QHi(float q) { return static_cast<uint64_t>(DrawIrVerifier::FloatBits(q)); }
uint64_t RgbaLo(uint8_t r, uint8_t g) { return static_cast<uint64_t>(r) | (static_cast<uint64_t>(g) << 32); }
uint64_t RgbaHi(uint8_t b, uint8_t a) { return static_cast<uint64_t>(b) | (static_cast<uint64_t>(a) << 32); }
uint64_t XyLo(uint16_t x, uint16_t y) { return static_cast<uint64_t>(x) | (static_cast<uint64_t>(y) << 32); }
uint64_t XyzfHi(uint32_t z24, uint8_t f, bool adc) {
    return ((static_cast<uint64_t>(z24 & 0xFFFFFF)) << 4) | (static_cast<uint64_t>(f) << 36) | (adc ? (1ull << 47) : 0);
}
uint64_t XyzHi(uint32_t z32, bool adc) { return static_cast<uint64_t>(z32) | (adc ? (1ull << 47) : 0); }

// Convert both ways (with the identity head items a producer prepends), assert
// decline parity, and if convertible assert the event sequences match exactly.
void CheckPacket(const std::vector<uint8_t>& p, const char* name) {
    const uint64_t id_lo = 0x1122334455667788ull, id_hi = (0xABCDull << 32) | (7ull << 16) | 0x120;
    std::vector<DirectOps::Op> ops;
    ops.push_back({id_lo, id_hi, DirectOps::kOpSrcId});
    ops.push_back({0xDEADBEEFull, 0, DirectOps::kOpSrcName});
    const bool ok_ops = DirectOps::PacketToOps(p.data(), p.size(), ops);

    DrawIrLift::Stream ir;
    {
        DrawIrLift::Item id;
        id.kind = DrawIrLift::kSrcId; id.lo = id_lo; id.hi = id_hi;
        ir.items.push_back(id);
        DrawIrLift::Item nm;
        nm.kind = DrawIrLift::kSrcName; nm.lo = 0xDEADBEEFull;
        ir.items.push_back(nm);
    }
    const bool ok_ir = DrawIrLift::PacketToDrawIR(p.data(), p.size(), ir);
    CHECK(ok_ops == ok_ir, "%s: decline parity ops=%d ir=%d", name, (int)ok_ops, (int)ok_ir);
    // The one-call verifier agrees with the explicit two-path comparison.
    const int v = DrawIrVerifier::VerifyKickEquivalence(p.data(), p.size());
    if (!ok_ops || !ok_ir) {
        CHECK(v == 0 || ok_ops != ok_ir, "%s: verifier %d on declined packet", name, v);
        return;
    }
    CHECK(v == 1, "%s: verifier %d", name, v);
    const std::vector<Event> a = DrawIrVerifier::RunOps(ops);
    const std::vector<Event> b = DrawIrVerifier::RunIR(ir);
    if (a.size() != b.size()) {
        CHECK(false, "%s: event count ops=%zu ir=%zu", name, a.size(), b.size());
        return;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (!(a[i] == b[i])) {
            CHECK(false, "%s: event[%zu] mismatch ops=%s ir=%s", name, i, EvName(a[i].t), EvName(b[i].t));
            return;
        }
    }
}

// Seeded LCG; `s` is the state.
uint64_t Next(uint64_t& s) {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return s >> 17;
}
uint32_t Below(uint64_t& s, uint32_t n) { return static_cast<uint32_t>(Next(s) % n); }

std::vector<uint8_t> RandomPacket(uint64_t& rng) {
    Bytes p;
    const int ntags = 1 + Below(rng, 3);
    for (int tgi = 0; tgi < ntags; tgi++) {
        const bool last = (tgi == ntags - 1);
        const uint32_t flg = Below(rng, 16) == 0 ? 2 : Below(rng, 2);  // rare IMAGE
        const uint32_t nloop = Below(rng, 5);
        const bool pre = Below(rng, 2) != 0;
        const bool eop = last || Below(rng, 4) == 0;
        const uint32_t nregs = 1 + Below(rng, 4);
        std::vector<uint8_t> regs;
        for (uint32_t i = 0; i < nregs; i++) {
            static const uint8_t pool[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x0C, 0x0D, 0x0E, 0x0F,
                                           0x00, 0x0A, 0x06, 0x0B, 0x02, 0x01, 0x05, 0x04, 0x03};
            regs.push_back(pool[Below(rng, static_cast<uint32_t>(sizeof(pool)))]);
        }
        Tag(p, nloop, eop, pre, Below(rng, 0x800), flg, regs);
        if (flg == 2) break;  // IMAGE payload never written: decline path
        if (flg == 0) {
            for (uint32_t l = 0; l < nloop; l++)
                for (uint32_t r = 0; r < nregs; r++) Qw(p, Next(rng), Next(rng));
        } else {
            const size_t total = static_cast<size_t>(nloop) * nregs;
            for (size_t i = 0; i < total; i++) Dw(p, Next(rng));
            if (total & 1) Dw(p, 0);
        }
    }
    if (Below(rng, 8) == 0 && p.size() > 24) p.resize(p.size() - 8);  // occasional truncation
    return p;
}

void Handcrafted() {
    {   // The common triple {ST, RGBAQ, XYZF2}, PRE tristrip.
        Bytes p;
        Tag(p, 4, true, true, 0x18C, 0, {0x02, 0x01, 0x04});
        for (int i = 0; i < 4; i++) {
            Qw(p, StQw(0.25f * i, 0.5f), QHi(1.0f + i));
            Qw(p, RgbaLo(10 + i, 20), RgbaHi(30, 0x80));
            Qw(p, XyLo(static_cast<uint16_t>(1000 + 16 * i), 2000), XyzfHi(0xABCDE, 5, false));
        }
        CheckPacket(p, "triple_st_rgbaq_xyzf2");
    }
    {   // UV strip {UV, RGBAQ, XYZ2}; ADC on the two seed verts.
        Bytes p;
        Tag(p, 5, true, true, 0x1D7, 0, {0x03, 0x01, 0x05});
        for (int i = 0; i < 5; i++) {
            Qw(p, XyLo(static_cast<uint16_t>(i * 16), static_cast<uint16_t>(i * 32)), 0);
            Qw(p, RgbaLo(1, 2), RgbaHi(3, 4));
            Qw(p, XyLo(static_cast<uint16_t>(512 + i), 600), XyzHi(0xFFFFFFFFu, i < 2));
        }
        CheckPacket(p, "uv_strip_adc_seeds");
    }
    {   // A+D state block then vertices, plus A+D mid-strip.
        Bytes p;
        Tag(p, 3, false, false, 0, 0, {0x0E});
        Qw(p, 0x00000000A0000000ull, 0x06);
        Qw(p, 0x0000000000001234ull, 0x14);
        Qw(p, 0x0000000000000042ull, 0x3F);
        Tag(p, 4, true, true, 0x104, 0, {0x01, 0x05});
        Qw(p, RgbaLo(9, 9), RgbaHi(9, 9)); Qw(p, XyLo(100, 100), XyzHi(1, false));
        Qw(p, RgbaLo(8, 8), RgbaHi(8, 8)); Qw(p, XyLo(200, 100), XyzHi(2, false));
        Qw(p, RgbaLo(7, 7), RgbaHi(7, 7)); Qw(p, XyLo(200, 200), XyzHi(3, false));
        Qw(p, RgbaLo(6, 6), RgbaHi(6, 6)); Qw(p, XyLo(100, 200), XyzHi(4, false));
        CheckPacket(p, "adgif_then_strip");
    }
    {   // Attributes pending BEFORE an A+D boundary and trailing attributes after the last kick.
        Bytes p;
        Tag(p, 1, false, false, 0, 0, {0x01, 0x0E, 0x05});
        Qw(p, RgbaLo(11, 22), RgbaHi(33, 44));
        Qw(p, 0x0000000000000777ull, 0x47);
        Qw(p, XyLo(300, 300), XyzHi(9, false));
        Tag(p, 1, true, false, 0, 0, {0x02});
        Qw(p, StQw(2.5f, 3.5f), QHi(0.0f));  // q payload 0: q NOT written
        CheckPacket(p, "attr_before_ad_and_trailing_st_q0");
    }
    {   // Packed PRIM descriptor mid-stream + q=NaN edge + FOG packed.
        Bytes p;
        float nanf;
        const uint32_t nanbits = 0x7FC00000u;
        std::memcpy(&nanf, &nanbits, 4);
        Tag(p, 2, true, false, 0, 0, {0x00, 0x02, 0x0A, 0x04});
        Qw(p, 0x155, 0);
        Qw(p, StQw(1.0f, 2.0f), QHi(nanf));
        Qw(p, 0, static_cast<uint64_t>(0xE7) << 36);
        Qw(p, XyLo(50, 60), XyzfHi(0x111, 0x22, false));
        Qw(p, 0x1D7, 0);
        Qw(p, StQw(3.0f, 4.0f), QHi(0.0f));
        Qw(p, 0, static_cast<uint64_t>(0x00) << 36);
        Qw(p, XyLo(70, 80), XyzfHi(0x222, 0x33, true));
        CheckPacket(p, "prim_desc_nan_q_fog");
    }
    {   // REGLIST tag incl. vertex regs + odd count (QW pad) + 0x0E/0x0F skips.
        Bytes p;
        Tag(p, 3, true, false, 0, 1, {0x01, 0x05, 0x0E});
        for (int i = 0; i < 3; i++) {
            Dw(p, 0x11223344u + i);
            Dw(p, (static_cast<uint64_t>(0x1000) << 32) | static_cast<uint64_t>(400 + i));
            Dw(p, 0xAAAAAAAAAAAAAAAAull);
        }
        Dw(p, 0);
        CheckPacket(p, "reglist_with_vertices");
    }
    {   // NLOOP=0 tags with and without EOP; PRE on an NLOOP=0 tag.
        Bytes p;
        Tag(p, 0, false, true, 0x006, 0, {0x0F});
        Tag(p, 0, true, false, 0, 0, {0x0F});
        CheckPacket(p, "nloop0_pre_eop");
    }
    {   // Run continuation across a non-EOP PACKED tag boundary (PRE=0).
        Bytes p;
        Tag(p, 2, false, true, 0x18C, 0, {0x01, 0x04});
        Qw(p, RgbaLo(1, 1), RgbaHi(1, 1)); Qw(p, XyLo(10, 10), XyzfHi(1, 0, false));
        Qw(p, RgbaLo(2, 2), RgbaHi(2, 2)); Qw(p, XyLo(20, 20), XyzfHi(2, 0, false));
        Tag(p, 2, true, false, 0, 0, {0x01, 0x04});
        Qw(p, RgbaLo(3, 3), RgbaHi(3, 3)); Qw(p, XyLo(30, 30), XyzfHi(3, 0, false));
        Qw(p, RgbaLo(4, 4), RgbaHi(4, 4)); Qw(p, XyLo(40, 40), XyzfHi(4, 0, false));
        CheckPacket(p, "run_across_tag_boundary");
    }
    {   // Packed defaults 0x06/0x0B route to register writes; NOP 0x0F ignored.
        Bytes p;
        Tag(p, 1, true, false, 0, 0, {0x06, 0x0F, 0x0B, 0x05});
        Qw(p, 0x00000000000ABCDEull, 0xFFFFFFFFFFFFFFFFull);
        Qw(p, 0x1234, 0x5678);
        Qw(p, 0xBBBB, 0);
        Qw(p, XyLo(77, 88), XyzHi(99, false));
        CheckPacket(p, "packed_defaults");
    }
    {   // XYZF3/XYZ3 packed descriptors (0x0C/0x0D): kicks with draw=false.
        Bytes p;
        Tag(p, 2, true, true, 0x18C, 0, {0x02, 0x01, 0x0C});
        Qw(p, StQw(0.f, 0.f), QHi(2.f)); Qw(p, RgbaLo(5, 5), RgbaHi(5, 5)); Qw(p, XyLo(10, 20), XyzfHi(7, 3, false));
        Qw(p, StQw(1.f, 1.f), QHi(3.f)); Qw(p, RgbaLo(6, 6), RgbaHi(6, 6)); Qw(p, XyLo(30, 40), XyzfHi(8, 4, true));
        CheckPacket(p, "xyzf3_seeds");
    }
    {   // ST/UV recency flip inside one gap: UV then ST -> uvw=false final.
        Bytes p;
        Tag(p, 1, true, true, 0x1D6, 0, {0x03, 0x02, 0x05});
        Qw(p, XyLo(0x123, 0x456), 0);
        Qw(p, StQw(9.f, 8.f), QHi(7.f));
        Qw(p, XyLo(500, 500), XyzHi(1, false));
        CheckPacket(p, "uv_then_st_recency");
    }
    {   // IMAGE tag -> both converters decline.
        Bytes p;
        Tag(p, 2, true, false, 0, 2, {0x00});
        Qw(p, 0, 0); Qw(p, 0, 0);
        CheckPacket(p, "image_decline");
    }
    {   // Truncated PACKED payload -> both decline.
        Bytes p;
        Tag(p, 4, true, false, 0, 0, {0x01, 0x05});
        Qw(p, 0, 0);
        CheckPacket(p, "truncated_decline");
    }
    {   // Trailing partial tag -> both decline.
        Bytes p;
        Tag(p, 1, true, false, 0, 0, {0x05});
        Qw(p, XyLo(1, 1), XyzHi(1, false));
        p.resize(p.size() + 8);
        CheckPacket(p, "trailing_bytes_decline");
    }
}

int Equivalence() {
    Handcrafted();
    uint64_t rng = 0x5EEDBA5Eull;
    const int kIters = 20000;
    int convertible = 0, declined = 0;
    for (int i = 0; i < kIters; i++) {
        const std::vector<uint8_t> p = RandomPacket(rng);
        char nm[48];
        std::snprintf(nm, sizeof(nm), "random_%d", i);
        CheckPacket(p, nm);
        if (DrawIrVerifier::VerifyKickEquivalence(p.data(), p.size()) == 1) convertible++; else declined++;
        if (g_failures > 8) break;
    }
    std::printf("  random packets: %d convertible, %d declined (parity held)\n", convertible, declined);
    CHECK(convertible >= 1000, "at least 1000 convertible random packets exercised (%d)", convertible);
    return 0;
}

// ---- DrawRec <-> DrawIr round trip -------------------------------------------
bool RoundTripOne(const DrawRec& x, const VertexRec* v, size_t n, const char* what) {
    const DrawIr ir = DrawIr::FromDrawRec(x, v, n);
    const DrawRec y = ir.ToDrawRec();
    const bool same = std::memcmp(&x, &y, sizeof(DrawRec)) == 0;
    if (!same) {
        const uint8_t* a = reinterpret_cast<const uint8_t*>(&x);
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&y);
        for (size_t i = 0; i < sizeof(DrawRec); i++)
            if (a[i] != b[i]) { std::printf("  %s: first differing byte at offset %zu (%02x vs %02x)\n", what, i, a[i], b[i]); break; }
    }
    CHECK(ir.verts.size() == n, "%s: verts carried", what);
    return same;
}

int RoundTrip(const char* cap) {
    // Synthetic record with every field populated (bools as 0/1, pads zero).
    DrawRec x;
    std::memset(&x, 0, sizeof(x));
    uint8_t* p = reinterpret_cast<uint8_t*>(&x);
    for (size_t i = 0; i < sizeof(x); i++) p[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    x.gouraud = 1; x.fog_enabled = 0; x.use_fst = 1; x.alpha_blend = 1; x.pad0 = 0;
    x.tex_enabled = 1; x.tex_fst = 0; x.mmag = 1; x.texa_aem = 1; x.pad1 = 0; x.pad2 = 0;
    x.colclamp = 1; x.pabe = 0; x.fba = 1; x.zbuf_zmsk = 1; x.pad3 = 0;
    x.test_ate = 1; x.test_date = 0; x.test_datm = 1; x.test_zte = 1;
    x.pad4[0] = x.pad4[1] = x.pad4[2] = 0;
    x.shuffle_uv_scale_u = 0.5f; x.off_x = 2048.f; x.off_y = 2176.5f;
    for (int i = 0; i < 6; i++) x.bbox[i] = 10.f * i - 7.25f;
    x.vertex_count = 3;
    VertexRec v[3]{};
    for (int i = 0; i < 3; i++) { v[i].x = 1.f * i; v[i].y = 2.f * i; v[i].z = 3.f * i; v[i].draw_kick = i > 0; }
    CHECK(RoundTripOne(x, v, 3, "synthetic"), "synthetic DrawRec round trip byte-identical");
    // The DrawIr view decodes the fields it names.
    const DrawIr ir = DrawIr::FromDrawRec(x, v, 3);
    CHECK(ir.gouraud && ir.use_fst && ir.alpha_blend && !ir.fog, "prim bools");
    CHECK(ir.tex.tbp == x.tbp && ir.tex.psm == x.tpsm && ir.tex.mmag && ir.tex.texa_aem, "tex view");
    CHECK(ir.raster.frame.fbp == x.frame_fbp && ir.raster.zbuf.zmsk && ir.raster.test.datm, "raster");
    CHECK(ir.identity.hle_handler == x.hle_handler && ir.identity.image_hash == x.hle_image, "identity");
    CHECK(ir.raster.dthe == 0 && ir.raster.dimx == 0 && ir.tex.mxl == 0, "fields absent from the record are zero");

    if (!cap) return 0;
    GsdlReader::Recording r;
    if (std::FILE* f = std::fopen(cap, "rb")) {
        std::fclose(f);
    } else {
        std::printf("SKIP: capture %s absent\n", cap);
        return 77;
    }
    CHECK(GsdlReader::Load(cap, r), "load %s", cap);
    size_t draws = 0, bad = 0;
    for (const GsdlReader::Frame& f : r.frames)
        for (const GsdlReader::Draw& d : f.draws) {
            draws++;
            if (!RoundTripOne(d.d, d.verts.data(), d.verts.size(), "capture draw")) bad++;
            if (bad > 3) break;
        }
    CHECK(bad == 0, "%zu of %zu capture draws failed the round trip", bad, draws);
    std::printf("  capture draws round-tripped: %zu (bad %zu)\n", draws, bad);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    int rc = 1;
    if (argc >= 2 && std::strcmp(argv[1], "equivalence") == 0) rc = Equivalence();
    else if (argc >= 2 && std::strcmp(argv[1], "roundtrip") == 0) rc = RoundTrip(argc >= 3 ? argv[2] : nullptr);
    else {
        std::fprintf(stderr, "usage: test_draw_ir equivalence | roundtrip [<capture.gsdl>]\n");
        return 2;
    }
    std::printf("test_draw_ir %s: %d checks, %d failures\n", argv[1], g_checks, g_failures);
    if (g_failures) return 1;
    return rc;
}

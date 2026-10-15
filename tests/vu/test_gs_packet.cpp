// VuGsPacketCodec -- GifPacketBuilder -> GsPacketDiff::Decode -> Diff round
// trip: PACKED (A+D, ADC, XYZF2), REGLIST, PRE/PRIM, tolerance edges,
// normalisation, strict packets, truncation errors.
#include <cstdio>
#include <cstring>

#include "engine/vu/hle/gif_packet_builder.h"
#include "tools/vu1_replay/gs_packet_diff.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                                     \
        }                                                                 \
    } while (0)

using G = GsPacketDiff;

static std::vector<uint8_t> TriStrip(uint32_t x0, uint8_t r) {
    GifPacketBuilder b;
    b.BeginTag(-1, {G::REG_AD}, false);
    b.AD(G::REG_TEX0_1, 0x00000000A0002000ull);
    b.AD(G::REG_ALPHA_1, 0x0000008000000044ull);
    b.EndTag();
    b.BeginTag(/*tristrip+tme+abe*/ 0x54, {G::REG_ST, G::REG_RGBAQ, G::REG_XYZ2}, true);
    for (uint32_t i = 0; i < 4; i++) {
        b.ST(0.25f * i, 0.5f, 1.0f);
        b.RGBAQ(r, 64, 32, 128);
        b.XYZ2((x0 + i * 16) << 4, 100 << 4, 0x00FFFF00 - i, /*adc*/ i == 0);
    }
    b.EndTag();
    return b.Take();
}

int main() {
    // --- round trip ---------------------------------------------------------
    G::Stream s = G::Decode({TriStrip(10, 200)});
    CHECK(s.errors.empty());
    CHECK(s.packets == 1);
    CHECK(s.tags.size() == 2);
    CHECK(s.tags[0].flg == 0 && s.tags[0].nloop == 2 && s.tags[0].nreg == 1);
    CHECK(s.tags[1].pre && s.tags[1].prim == 0x54 && s.tags[1].nloop == 4 && s.tags[1].eop);
    // writes: TEX0_1, ALPHA_1, PRIM(pre), then 4 x (ST, RGBAQ, XYZ2/XYZ3)
    CHECK(s.writes.size() == 3 + 12);
    CHECK(s.writes[0].addr == G::REG_TEX0_1 && s.writes[0].value == 0x00000000A0002000ull);
    CHECK(s.writes[2].addr == G::REG_PRIM && s.writes[2].value == 0x54);
    CHECK(s.writes[5].addr == G::REG_XYZ3);  // ADC set on vertex 0
    CHECK(s.writes[8].addr == G::REG_XYZ2);
    {
        G::XYZ p = G::UnpackXYZ(s.writes[8].addr, s.writes[8].value);
        CHECK(p.x == (10 + 16) << 4 && p.y == 100 << 4 && p.z == 0x00FFFF00 - 1);
        G::RGBAQ c = G::UnpackRGBAQ(s.writes[4].value);
        CHECK(c.r == 200 && c.g == 64 && c.b == 32 && c.a == 128 && c.q == 1.0f);  // Q latched from ST
        float st, tt;
        G::UnpackST(s.writes[6].value, st, tt);
        CHECK(st == 0.25f && tt == 0.5f);
    }
    uint32_t kicks = 0, verts = 0;
    for (auto& w : s.writes) {
        if (G::IsKickReg(w.addr)) kicks++;
        if (G::IsVertexReg(w.addr)) verts++;
    }
    CHECK(kicks == 3 && verts == 4);

    // --- identical streams pass, exact and tolerant -------------------------
    G::Tolerance exact;
    exact.xy = exact.z = exact.rgba = 0;
    exact.st_rel = exact.st_abs = exact.q_rel = 0;
    exact.ignore_nop_state = false;
    CHECK(G::Diff(s, s, exact).pass);
    CHECK(G::Diff(s, s, G::Tolerance{}).pass);

    // --- one 12.4 unit off: passes default, fails exact ---------------------
    {
        G::Stream t = G::Decode({TriStrip(10, 200)});
        t.writes[8].value += 1;  // x += 1/16 px
        CHECK(G::Diff(s, t, G::Tolerance{}).pass);
        G::DiffResult d = G::Diff(s, t, exact);
        CHECK(!d.pass && d.mismatches == 1);
        t.writes[8].value += 1;  // now 2 units off
        CHECK(!G::Diff(s, t, G::Tolerance{}).pass);
    }
    // --- colour off by 1 passes, by 2 fails ---------------------------------
    {
        G::Stream t = G::Decode({TriStrip(10, 201)});
        CHECK(G::Diff(s, t, G::Tolerance{}).pass);
        G::Stream u = G::Decode({TriStrip(10, 202)});
        CHECK(!G::Diff(s, u, G::Tolerance{}).pass);
    }
    // --- state register value mismatch is always a failure ------------------
    {
        G::Stream t = G::Decode({TriStrip(10, 200)});
        t.writes[1].value ^= 0x10;
        CHECK(!G::Diff(s, t, G::Tolerance{}).pass);
    }
    // --- normalisation: redundant identical state rewrite is ignored by default
    {
        GifPacketBuilder b;
        b.BeginTag(-1, {G::REG_AD}, false);
        b.AD(G::REG_TEX0_1, 0x00000000A0002000ull);
        b.AD(G::REG_TEX0_1, 0x00000000A0002000ull);  // duplicate
        b.AD(G::REG_ALPHA_1, 0x0000008000000044ull);
        b.EndTag();
        b.BeginTag(0x54, {G::REG_ST, G::REG_RGBAQ, G::REG_XYZ2}, true);
        for (uint32_t i = 0; i < 4; i++) {
            b.ST(0.25f * i, 0.5f, 1.0f);
            b.RGBAQ(200, 64, 32, 128);
            b.XYZ2((10 + i * 16) << 4, 100 << 4, 0x00FFFF00 - i, i == 0);
        }
        b.EndTag();
        G::Stream t = G::Decode({b.Take()});
        CHECK(t.writes.size() == s.writes.size() + 1);
        CHECK(G::Diff(s, t, G::Tolerance{}).pass);
        CHECK(!G::Diff(s, t, exact).pass);
    }
    // --- two packets vs one: flat compare passes, strict fails --------------
    {
        GifPacketBuilder b;
        b.BeginTag(-1, {G::REG_AD}, true);
        b.AD(G::REG_TEX0_1, 0x00000000A0002000ull);
        b.AD(G::REG_ALPHA_1, 0x0000008000000044ull);
        b.EndTag();
        std::vector<uint8_t> p0 = b.Take();
        b.BeginTag(0x54, {G::REG_ST, G::REG_RGBAQ, G::REG_XYZ2}, true);
        for (uint32_t i = 0; i < 4; i++) {
            b.ST(0.25f * i, 0.5f, 1.0f);
            b.RGBAQ(200, 64, 32, 128);
            b.XYZ2((10 + i * 16) << 4, 100 << 4, 0x00FFFF00 - i, i == 0);
        }
        b.EndTag();
        G::Stream t = G::Decode({p0, b.Take()});
        CHECK(t.packets == 2);
        CHECK(G::Diff(s, t, G::Tolerance{}).pass);
        G::Tolerance strict;
        strict.strict_packets = true;
        CHECK(!G::Diff(s, t, strict).pass);
    }
    // --- REGLIST equivalence: same writes as PACKED -------------------------
    {
        std::vector<uint8_t> pkt(16, 0);
        uint64_t lo = 4 /*nloop*/ | (1ull << 15) | (1ull << 46) | (0x54ull << 47) | (1ull << 58) | (3ull << 60);
        uint64_t hi = G::REG_ST | (G::REG_RGBAQ << 4) | (G::REG_XYZ2 << 8);
        std::memcpy(&pkt[0], &lo, 8);
        std::memcpy(&pkt[8], &hi, 8);
        // REGLIST payload: 64-bit canonical register values, straight from the PACKED decode.
        for (size_t i = 3; i < s.writes.size(); i++) {
            const uint64_t v = s.writes[i].value;  // REGLIST XYZ2 cannot carry ADC
            size_t o = pkt.size();
            pkt.resize(o + 8);
            std::memcpy(&pkt[o], &v, 8);
        }
        G::Stream t;
        G::DecodePacket(pkt.data(), pkt.size(), t);
        CHECK(t.errors.empty());
        CHECK(t.writes.size() == 1 + 12);
        CHECK(t.writes[0].addr == G::REG_PRIM);
        CHECK(t.writes[3].addr == G::REG_XYZ2);  // ADC lost -> kick (the REGLIST limit)
        CHECK(t.writes[1].value == s.writes[3].value && t.writes[2].value == s.writes[4].value);
    }
    // --- truncated payload is an error --------------------------------------
    {
        std::vector<uint8_t> p = TriStrip(10, 200);
        p.resize(p.size() - 16);
        G::Stream t;
        G::DecodePacket(p.data(), p.size(), t);
        CHECK(t.errors.size() == 1);
        CHECK(!G::Diff(s, t, G::Tolerance{}).pass);
    }
    // --- XYZF2 packing ------------------------------------------------------
    {
        GifPacketBuilder b;
        b.BeginTag(0x1, {G::REG_XYZF2}, true);
        b.XYZF2(0x1234, 0x0ABC, 0x00ABCDEF, 0x7F);
        b.XYZF2(1, 2, 3, 4, /*adc*/ true);
        b.EndTag();
        G::Stream t = G::Decode({b.Take()});
        CHECK(t.writes.size() == 3);
        G::XYZ p = G::UnpackXYZ(t.writes[1].addr, t.writes[1].value);
        CHECK(t.writes[1].addr == G::REG_XYZF2 && p.x == 0x1234 && p.y == 0x0ABC && p.z == 0x00ABCDEF && p.f == 0x7F);
        CHECK(t.writes[2].addr == G::REG_XYZF3);
    }

    if (g_fail == 0) std::printf("GsPacketCodec PASS\n");
    return g_fail ? 1 : 0;
}

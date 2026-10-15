// GsPacketDiff -- decoder / normaliser / differ / dump (grammar in the header).
#include "tools/vu1_replay/gs_packet_diff.h"

#include <cmath>
#include <cstring>

namespace {

uint64_t Lo(const uint8_t* q) {
    uint64_t v;
    std::memcpy(&v, q, 8);
    return v;
}
uint64_t Hi(const uint8_t* q) {
    uint64_t v;
    std::memcpy(&v, q + 8, 8);
    return v;
}
uint32_t W(const uint8_t* q, int i) {
    uint32_t v;
    std::memcpy(&v, q + 4 * i, 4);
    return v;
}

bool FloatClose(float a, float b, float rel, float abs_) {
    const float d = std::fabs(a - b);
    return d <= abs_ || d <= rel * std::fmax(std::fabs(a), std::fabs(b));
}

}  // namespace

const char* GsPacketDiff::RegName(uint8_t a) {
    switch (a) {
        case REG_PRIM: return "PRIM";
        case REG_RGBAQ: return "RGBAQ";
        case REG_ST: return "ST";
        case REG_UV: return "UV";
        case REG_XYZF2: return "XYZF2";
        case REG_XYZ2: return "XYZ2";
        case REG_TEX0_1: return "TEX0_1";
        case REG_TEX0_2: return "TEX0_2";
        case REG_CLAMP_1: return "CLAMP_1";
        case REG_CLAMP_2: return "CLAMP_2";
        case REG_FOG: return "FOG";
        case REG_XYZF3: return "XYZF3";
        case REG_XYZ3: return "XYZ3";
        case REG_AD: return "A+D";
        case REG_NOP: return "NOP";
        case REG_TEX1_1: return "TEX1_1";
        case REG_TEX1_2: return "TEX1_2";
        case REG_TEX2_1: return "TEX2_1";
        case REG_TEX2_2: return "TEX2_2";
        case REG_XYOFFSET_1: return "XYOFFSET_1";
        case REG_XYOFFSET_2: return "XYOFFSET_2";
        case REG_PRMODECONT: return "PRMODECONT";
        case REG_PRMODE: return "PRMODE";
        case REG_TEXCLUT: return "TEXCLUT";
        case REG_SCANMSK: return "SCANMSK";
        case REG_MIPTBP1_1: return "MIPTBP1_1";
        case REG_MIPTBP1_2: return "MIPTBP1_2";
        case REG_MIPTBP2_1: return "MIPTBP2_1";
        case REG_MIPTBP2_2: return "MIPTBP2_2";
        case REG_TEXA: return "TEXA";
        case REG_FOGCOL: return "FOGCOL";
        case REG_TEXFLUSH: return "TEXFLUSH";
        case REG_SCISSOR_1: return "SCISSOR_1";
        case REG_SCISSOR_2: return "SCISSOR_2";
        case REG_ALPHA_1: return "ALPHA_1";
        case REG_ALPHA_2: return "ALPHA_2";
        case REG_DIMX: return "DIMX";
        case REG_DTHE: return "DTHE";
        case REG_COLCLAMP: return "COLCLAMP";
        case REG_TEST_1: return "TEST_1";
        case REG_TEST_2: return "TEST_2";
        case REG_PABE: return "PABE";
        case REG_FBA_1: return "FBA_1";
        case REG_FBA_2: return "FBA_2";
        case REG_FRAME_1: return "FRAME_1";
        case REG_FRAME_2: return "FRAME_2";
        case REG_ZBUF_1: return "ZBUF_1";
        case REG_ZBUF_2: return "ZBUF_2";
        case REG_BITBLTBUF: return "BITBLTBUF";
        case REG_TRXPOS: return "TRXPOS";
        case REG_TRXREG: return "TRXREG";
        case REG_TRXDIR: return "TRXDIR";
        case REG_HWREG: return "HWREG";
        case REG_SIGNAL: return "SIGNAL";
        case REG_FINISH: return "FINISH";
        case REG_LABEL: return "LABEL";
        default: return "?";
    }
}

bool GsPacketDiff::IsVertexReg(uint8_t a) {
    return a == REG_XYZ2 || a == REG_XYZ3 || a == REG_XYZF2 || a == REG_XYZF3;
}
bool GsPacketDiff::IsKickReg(uint8_t a) { return a == REG_XYZ2 || a == REG_XYZF2; }

bool GsPacketDiff::PackedToCanonical(uint8_t desc, const uint8_t* q, uint32_t& q_latch, uint8_t& out_addr,
                                     uint64_t& out_val) {
    switch (desc) {
        case REG_PRIM:
            out_addr = REG_PRIM;
            out_val = Lo(q) & 0x7FF;
            return true;
        case REG_RGBAQ: {
            const uint64_t r = W(q, 0) & 0xFF, g = W(q, 1) & 0xFF, b = W(q, 2) & 0xFF, a = W(q, 3) & 0xFF;
            out_addr = REG_RGBAQ;
            out_val = r | (g << 8) | (b << 16) | (a << 24) | ((uint64_t)q_latch << 32);
            return true;
        }
        case REG_ST:
            q_latch = W(q, 2);
            out_addr = REG_ST;
            out_val = Lo(q);
            return true;
        case REG_UV:
            out_addr = REG_UV;
            out_val = (uint64_t)(W(q, 0) & 0x3FFF) | ((uint64_t)(W(q, 1) & 0x3FFF) << 16);
            return true;
        case REG_XYZF2: {
            const uint64_t x = W(q, 0) & 0xFFFF, y = W(q, 1) & 0xFFFF;
            const uint64_t z = (W(q, 2) >> 4) & 0xFFFFFF;
            const uint64_t f = (W(q, 3) >> 4) & 0xFF;
            const bool adc = (W(q, 3) >> 15) & 1;
            out_addr = adc ? REG_XYZF3 : REG_XYZF2;
            out_val = x | (y << 16) | (z << 32) | (f << 56);
            return true;
        }
        case REG_XYZ2: {
            const uint64_t x = W(q, 0) & 0xFFFF, y = W(q, 1) & 0xFFFF;
            const uint64_t z = W(q, 2);
            const bool adc = (W(q, 3) >> 15) & 1;
            out_addr = adc ? REG_XYZ3 : REG_XYZ2;
            out_val = x | (y << 16) | (z << 32);
            return true;
        }
        case REG_FOG:
            out_addr = REG_FOG;
            out_val = (uint64_t)((W(q, 3) >> 4) & 0xFF) << 56;
            return true;
        case REG_AD:
            out_addr = (uint8_t)(Hi(q) & 0xFF);
            out_val = Lo(q);
            return true;
        case REG_NOP:
            return false;
        default:  // TEX0_1/2, CLAMP_1/2, XYZF3, XYZ3: raw 64-bit in the low half
            out_addr = desc;
            out_val = Lo(q);
            return true;
    }
}

void GsPacketDiff::DecodePacket(const uint8_t* p, size_t n, Stream& s) {
    const uint32_t pk = s.packets++;
    uint32_t off = 0, ti = 0;
    uint32_t q_latch = 0x3F800000;
    while (off + 16 <= n) {
        const uint8_t* t = p + off;
        TagInfo ti_info{};
        const uint64_t lo = Lo(t), hi = Hi(t);
        ti_info.packet_index = pk;
        ti_info.tag_index = ti;
        ti_info.byte_offset = off;
        ti_info.nloop = lo & 0x7FFF;
        ti_info.eop = (lo >> 15) & 1;
        ti_info.pre = (lo >> 46) & 1;
        ti_info.prim = (lo >> 47) & 0x7FF;
        ti_info.flg = (lo >> 58) & 3;
        ti_info.nreg = (lo >> 60) & 0xF;
        if (ti_info.nreg == 0) ti_info.nreg = 16;
        for (int i = 0; i < 16; i++) ti_info.regs[i] = (hi >> (i * 4)) & 0xF;
        off += 16;

        if (ti_info.pre && ti_info.flg != 2 && ti_info.flg != 3) s.writes.push_back({REG_PRIM, ti_info.prim, ti, pk});

        if (ti_info.flg == 0) {  // PACKED: one QW per register
            for (uint32_t l = 0; l < ti_info.nloop; l++) {
                for (uint32_t r = 0; r < ti_info.nreg; r++) {
                    if (off + 16 > n) {
                        char b[96];
                        std::snprintf(b, sizeof(b), "packet %u tag %u: truncated PACKED payload", pk, ti);
                        s.errors.push_back(b);
                        s.tags.push_back(ti_info);
                        return;
                    }
                    uint8_t a;
                    uint64_t v;
                    if (PackedToCanonical(ti_info.regs[r], p + off, q_latch, a, v)) s.writes.push_back({a, v, ti, pk});
                    off += 16;
                }
            }
        } else if (ti_info.flg == 1) {  // REGLIST: one 64-bit per register
            const uint32_t total = ti_info.nloop * ti_info.nreg;
            for (uint32_t i = 0; i < total; i++) {
                if (off + 8 > n) {
                    char b[96];
                    std::snprintf(b, sizeof(b), "packet %u tag %u: truncated REGLIST payload", pk, ti);
                    s.errors.push_back(b);
                    s.tags.push_back(ti_info);
                    return;
                }
                const uint8_t a = ti_info.regs[i % ti_info.nreg];
                if (a != REG_NOP && a != REG_AD) s.writes.push_back({a, Lo(p + off), ti, pk});
                off += 8;
            }
            if (total & 1) off += 8;  // REGLIST pads to a QW boundary
        } else {  // IMAGE
            ti_info.image_bytes = (uint32_t)ti_info.nloop * 16;
            s.image_bytes += ti_info.image_bytes;
            off += ti_info.image_bytes;
            if (off > n) {
                char b[96];
                std::snprintf(b, sizeof(b), "packet %u tag %u: truncated IMAGE payload", pk, ti);
                s.errors.push_back(b);
                s.tags.push_back(ti_info);
                return;
            }
        }
        s.tags.push_back(ti_info);
        ti++;
        if (ti_info.eop) break;
    }
}

GsPacketDiff::Stream GsPacketDiff::Decode(const std::vector<std::vector<uint8_t>>& packets) {
    Stream s;
    for (const auto& p : packets) DecodePacket(p.data(), p.size(), s);
    return s;
}

GsPacketDiff::XYZ GsPacketDiff::UnpackXYZ(uint8_t addr, uint64_t v) {
    XYZ r{(uint32_t)(v & 0xFFFF), (uint32_t)((v >> 16) & 0xFFFF), 0, 0};
    if (addr == REG_XYZF2 || addr == REG_XYZF3) {
        r.z = (uint32_t)((v >> 32) & 0xFFFFFF);
        r.f = (uint8_t)(v >> 56);
    } else {
        r.z = (uint32_t)(v >> 32);
    }
    return r;
}

GsPacketDiff::RGBAQ GsPacketDiff::UnpackRGBAQ(uint64_t v) {
    RGBAQ c{(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24), 0.f};
    uint32_t qb = (uint32_t)(v >> 32);
    std::memcpy(&c.q, &qb, 4);
    return c;
}

void GsPacketDiff::UnpackST(uint64_t v, float& s, float& t) {
    uint32_t a = (uint32_t)v, b = (uint32_t)(v >> 32);
    std::memcpy(&s, &a, 4);
    std::memcpy(&t, &b, 4);
}

std::string GsPacketDiff::Format(const Write& w) {
    char b[160];
    switch (w.addr) {
        case REG_XYZ2:
        case REG_XYZ3:
        case REG_XYZF2:
        case REG_XYZF3: {
            XYZ p = UnpackXYZ(w.addr, w.value);
            std::snprintf(b, sizeof(b), "%-6s x=%7.3f y=%7.3f z=0x%08x%s%s", RegName(w.addr), p.x / 16.0, p.y / 16.0,
                          p.z, (w.addr == REG_XYZF2 || w.addr == REG_XYZF3) ? " f=" : "",
                          (w.addr == REG_XYZF2 || w.addr == REG_XYZF3) ? std::to_string(p.f).c_str() : "");
            break;
        }
        case REG_RGBAQ: {
            RGBAQ c = UnpackRGBAQ(w.value);
            std::snprintf(b, sizeof(b), "RGBAQ  r=%3u g=%3u b=%3u a=%3u q=%g", c.r, c.g, c.b, c.a, c.q);
            break;
        }
        case REG_ST: {
            float s, t;
            UnpackST(w.value, s, t);
            std::snprintf(b, sizeof(b), "ST     s=%g t=%g", s, t);
            break;
        }
        case REG_UV:
            std::snprintf(b, sizeof(b), "UV     u=%7.3f v=%7.3f", (w.value & 0x3FFF) / 16.0,
                          ((w.value >> 16) & 0x3FFF) / 16.0);
            break;
        case REG_PRIM:
            std::snprintf(b, sizeof(b),
                          "PRIM   type=%llu iip=%llu tme=%llu fge=%llu abe=%llu aa1=%llu fst=%llu ctxt=%llu fix=%llu",
                          (unsigned long long)(w.value & 7), (unsigned long long)((w.value >> 3) & 1),
                          (unsigned long long)((w.value >> 4) & 1), (unsigned long long)((w.value >> 5) & 1),
                          (unsigned long long)((w.value >> 6) & 1), (unsigned long long)((w.value >> 7) & 1),
                          (unsigned long long)((w.value >> 8) & 1), (unsigned long long)((w.value >> 9) & 1),
                          (unsigned long long)((w.value >> 10) & 1));
            break;
        case REG_TEX0_1:
        case REG_TEX0_2:
            std::snprintf(b, sizeof(b),
                          "%s tbp=0x%llx tbw=%llu psm=%llu tw=%llu th=%llu tcc=%llu tfx=%llu cbp=0x%llx cpsm=%llu "
                          "csm=%llu csa=%llu cld=%llu",
                          RegName(w.addr), (unsigned long long)((w.value & 0x3FFF) * 64),
                          (unsigned long long)(((w.value >> 14) & 0x3F) * 64), (unsigned long long)((w.value >> 20) & 0x3F),
                          (unsigned long long)((w.value >> 26) & 0xF), (unsigned long long)((w.value >> 30) & 0xF),
                          (unsigned long long)((w.value >> 34) & 1), (unsigned long long)((w.value >> 35) & 3),
                          (unsigned long long)(((w.value >> 37) & 0x3FFF) * 64), (unsigned long long)((w.value >> 51) & 0xF),
                          (unsigned long long)((w.value >> 55) & 1), (unsigned long long)((w.value >> 56) & 0x1F),
                          (unsigned long long)((w.value >> 61) & 7));
            break;
        case REG_ALPHA_1:
        case REG_ALPHA_2:
            std::snprintf(b, sizeof(b), "%s a=%llu b=%llu c=%llu d=%llu fix=%llu", RegName(w.addr),
                          (unsigned long long)(w.value & 3), (unsigned long long)((w.value >> 2) & 3),
                          (unsigned long long)((w.value >> 4) & 3), (unsigned long long)((w.value >> 6) & 3),
                          (unsigned long long)((w.value >> 32) & 0xFF));
            break;
        case REG_TEST_1:
        case REG_TEST_2:
            std::snprintf(b, sizeof(b),
                          "%s ate=%llu atst=%llu aref=%llu afail=%llu date=%llu datm=%llu zte=%llu ztst=%llu",
                          RegName(w.addr), (unsigned long long)(w.value & 1), (unsigned long long)((w.value >> 1) & 7),
                          (unsigned long long)((w.value >> 4) & 0xFF), (unsigned long long)((w.value >> 12) & 3),
                          (unsigned long long)((w.value >> 14) & 1), (unsigned long long)((w.value >> 15) & 1),
                          (unsigned long long)((w.value >> 16) & 1), (unsigned long long)((w.value >> 17) & 3));
            break;
        case REG_FRAME_1:
        case REG_FRAME_2:
            std::snprintf(b, sizeof(b), "%s fbp=0x%llx fbw=%llu psm=%llu fbmsk=0x%08llx", RegName(w.addr),
                          (unsigned long long)((w.value & 0x1FF) * 8192), (unsigned long long)(((w.value >> 16) & 0x3F) * 64),
                          (unsigned long long)((w.value >> 24) & 0x3F), (unsigned long long)(w.value >> 32));
            break;
        default:
            std::snprintf(b, sizeof(b), "%-10s 0x%016llx", RegName(w.addr), (unsigned long long)w.value);
    }
    return b;
}

bool GsPacketDiff::WriteClose(const Write& a, const Write& b, const Tolerance& t, std::string* why) {
    if (a.addr != b.addr) {
        if (why) *why = std::string("register differs: ") + RegName(a.addr) + " vs " + RegName(b.addr);
        return false;
    }
    auto diff_u = [](uint32_t x, uint32_t y) { return x > y ? x - y : y - x; };
    switch (a.addr) {
        case REG_XYZ2:
        case REG_XYZ3:
        case REG_XYZF2:
        case REG_XYZF3: {
            XYZ p = UnpackXYZ(a.addr, a.value), q = UnpackXYZ(b.addr, b.value);
            bool ok = diff_u(p.x, q.x) <= t.xy && diff_u(p.y, q.y) <= t.xy && diff_u(p.z, q.z) <= t.z &&
                      diff_u(p.f, q.f) <= t.fog;
            if (!ok && why) {
                char m[160];
                std::snprintf(m, sizeof(m), "dx=%d dy=%d dz=%lld df=%d (12.4 units)", (int)p.x - (int)q.x,
                              (int)p.y - (int)q.y, (long long)p.z - (long long)q.z, (int)p.f - (int)q.f);
                *why = m;
            }
            return ok;
        }
        case REG_RGBAQ: {
            RGBAQ p = UnpackRGBAQ(a.value), q = UnpackRGBAQ(b.value);
            bool ok = diff_u(p.r, q.r) <= t.rgba && diff_u(p.g, q.g) <= t.rgba && diff_u(p.b, q.b) <= t.rgba &&
                      diff_u(p.a, q.a) <= t.rgba && FloatClose(p.q, q.q, t.q_rel, 0.f);
            if (!ok && why) *why = "rgba/q outside tolerance";
            return ok;
        }
        case REG_ST: {
            float s0, t0, s1, t1;
            UnpackST(a.value, s0, t0);
            UnpackST(b.value, s1, t1);
            bool ok = FloatClose(s0, s1, t.st_rel, t.st_abs) && FloatClose(t0, t1, t.st_rel, t.st_abs);
            if (!ok && why) *why = "st outside tolerance";
            return ok;
        }
        case REG_UV: {
            bool ok = diff_u(a.value & 0x3FFF, b.value & 0x3FFF) <= t.uv &&
                      diff_u((a.value >> 16) & 0x3FFF, (b.value >> 16) & 0x3FFF) <= t.uv;
            if (!ok && why) *why = "uv outside tolerance";
            return ok;
        }
        default:
            if (a.value != b.value) {
                if (why) {
                    char m[96];
                    std::snprintf(m, sizeof(m), "value 0x%016llx vs 0x%016llx", (unsigned long long)a.value,
                                  (unsigned long long)b.value);
                    *why = m;
                }
                return false;
            }
            return true;
    }
}

std::vector<GsPacketDiff::Write> GsPacketDiff::Normalize(const std::vector<Write>& in, const Tolerance& t) {
    if (!t.ignore_nop_state) return in;
    std::vector<Write> out;
    out.reserve(in.size());
    uint64_t last_val[256];
    bool have[256] = {false};
    for (const Write& w : in) {
        // PRIM restarts the strip queue, so a repeated PRIM is not a no-op.
        if (IsVertexReg(w.addr) || w.addr == REG_PRIM || w.addr == REG_RGBAQ || w.addr == REG_ST ||
            w.addr == REG_UV || w.addr == REG_FOG) {
            out.push_back(w);
            continue;
        }
        if (have[w.addr] && last_val[w.addr] == w.value) continue;
        have[w.addr] = true;
        last_val[w.addr] = w.value;
        out.push_back(w);
    }
    return out;
}

GsPacketDiff::DiffResult GsPacketDiff::Diff(const Stream& a, const Stream& b, const Tolerance& t, uint32_t max_lines) {
    DiffResult r;
    std::vector<Write> wa = Normalize(a.writes, t), wb = Normalize(b.writes, t);
    for (const Write& w : a.writes) {
        if (IsVertexReg(w.addr)) r.vertices_a++;
        if (IsKickReg(w.addr)) r.kicks_a++;
    }
    for (const Write& w : b.writes) {
        if (IsVertexReg(w.addr)) r.vertices_b++;
        if (IsKickReg(w.addr)) r.kicks_b++;
    }
    const size_t n = wa.size() < wb.size() ? wa.size() : wb.size();
    for (size_t i = 0; i < n; i++) {
        r.compared++;
        std::string why;
        if (!WriteClose(wa[i], wb[i], t, &why)) {
            r.mismatches++;
            if (r.lines.size() < max_lines) {
                char b0[64];
                std::snprintf(b0, sizeof(b0), "#%zu (pkt %u tag %u): ", i, wa[i].packet_index, wa[i].tag_index);
                r.lines.push_back(std::string(b0) + why + "\n    A: " + Format(wa[i]) + "\n    B: " + Format(wb[i]));
            }
        }
    }
    if (wa.size() != wb.size()) {
        r.mismatches++;
        char m[128];
        std::snprintf(m, sizeof(m), "write count differs: A=%zu B=%zu (after normalization)", wa.size(), wb.size());
        r.lines.push_back(m);
    }
    if (t.strict_packets && a.packets != b.packets) {
        r.mismatches++;
        char m[96];
        std::snprintf(m, sizeof(m), "packet count differs: A=%u B=%u", a.packets, b.packets);
        r.lines.push_back(m);
    }
    if (!a.errors.empty() || !b.errors.empty()) {
        r.mismatches++;
        for (const auto& e : a.errors) r.lines.push_back("A decode error: " + e);
        for (const auto& e : b.errors) r.lines.push_back("B decode error: " + e);
    }
    r.pass = r.mismatches == 0;
    char s[256];
    std::snprintf(s, sizeof(s),
                  "writes A=%zu B=%zu vertices A=%u B=%u kicks A=%u B=%u packets A=%u B=%u mismatches=%u", wa.size(),
                  wb.size(), r.vertices_a, r.vertices_b, r.kicks_a, r.kicks_b, a.packets, b.packets, r.mismatches);
    r.summary = s;
    return r;
}

void GsPacketDiff::Print(const Stream& s, std::FILE* f) {
    uint32_t vi = 0;
    for (const TagInfo& t : s.tags) {
        std::fprintf(f, "-- packet %u tag %u @+0x%x: nloop=%u eop=%d pre=%d prim=0x%03x flg=%s nreg=%u regs=",
                     t.packet_index, t.tag_index, t.byte_offset, t.nloop, t.eop, t.pre, t.prim,
                     t.flg == 0 ? "PACKED" : t.flg == 1 ? "REGLIST" : "IMAGE", t.nreg);
        for (uint32_t i = 0; i < t.nreg; i++) std::fprintf(f, "%s%s", i ? "," : "", RegName(t.regs[i]));
        if (t.flg >= 2) std::fprintf(f, " image_bytes=%u", t.image_bytes);
        std::fprintf(f, "\n");
        for (const Write& w : s.writes) {
            if (w.packet_index != t.packet_index || w.tag_index != t.tag_index) continue;
            if (IsVertexReg(w.addr))
                std::fprintf(f, "  v%-5u %s\n", vi++, Format(w).c_str());
            else
                std::fprintf(f, "         %s\n", Format(w).c_str());
        }
    }
    for (const auto& e : s.errors) std::fprintf(f, "ERROR: %s\n", e.c_str());
}

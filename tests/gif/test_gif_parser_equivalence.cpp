// GifParserEquivalence: the synthetic GIF packets of the OLD fast-path
// equivalence suite fed through two GifParser configurations (bulk lanes on /
// off) into two RegisterOpStreams; the recorded op vectors and hashes must be
// identical. Plus pinned rules of the canonical walk (op counts, decoded
// values, drop table, EOP handling, PATH3 stale reset, M3R hold, FinishPath3).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/gif_tag.h"
#include "engine/gs/hle/ir/register_op_stream.h"

namespace {

int g_fail = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                            \
        }                                                                        \
    } while (0)

constexpr uint8_t kPrim = static_cast<uint8_t>(GsReg::Prim);
constexpr uint8_t kRgbaq = static_cast<uint8_t>(GsReg::Rgbaq);
constexpr uint8_t kSt = static_cast<uint8_t>(GsReg::St);
constexpr uint8_t kUv = static_cast<uint8_t>(GsReg::Uv);
constexpr uint8_t kXyzf2 = static_cast<uint8_t>(GsReg::Xyzf2);
constexpr uint8_t kXyz2 = static_cast<uint8_t>(GsReg::Xyz2);
constexpr uint8_t kFog = static_cast<uint8_t>(GsReg::Fog);
constexpr uint8_t kXyzf3 = static_cast<uint8_t>(GsReg::Xyzf3);
constexpr uint8_t kXyz3 = static_cast<uint8_t>(GsReg::Xyz3);
constexpr uint8_t kAd = static_cast<uint8_t>(GsReg::Ad);
constexpr uint8_t kNop = static_cast<uint8_t>(GsReg::Nop);
constexpr uint8_t kTex0_1 = static_cast<uint8_t>(GsReg::Tex0_1);
constexpr uint8_t kTexclut = static_cast<uint8_t>(GsReg::Texclut);
constexpr uint8_t kTexflush = static_cast<uint8_t>(GsReg::Texflush);

constexpr int kPacked = 0;
constexpr int kReglist = 1;
constexpr int kImage = 2;
constexpr int kDisabled = 3;

// --- packet builders (byte layouts as the OLD suite) -------------------------

using Bytes = std::vector<uint8_t>;

void put_u64(Bytes& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

void put_tag(Bytes& v, uint16_t nloop, bool eop, bool pre, uint16_t prim, int flg, int nreg,
             const uint8_t* regs, int nregs_actual) {
    uint64_t lo = static_cast<uint64_t>(nloop & 0x7FFF) | (static_cast<uint64_t>(eop ? 1 : 0) << 15) |
                  (static_cast<uint64_t>(pre ? 1 : 0) << 46) | (static_cast<uint64_t>(prim & 0x7FF) << 47) |
                  (static_cast<uint64_t>(flg & 3) << 58) | (static_cast<uint64_t>(nreg & 0xF) << 60);
    uint64_t hi = 0;
    for (int i = 0; i < nregs_actual && i < 16; ++i) hi |= static_cast<uint64_t>(regs[i] & 0xF) << (4 * i);
    put_u64(v, lo);
    put_u64(v, hi);
}

uint32_t bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

void put_st(Bytes& v, float s, float t, float q) {
    put_u64(v, static_cast<uint64_t>(bits(s)) | (static_cast<uint64_t>(bits(t)) << 32));
    put_u64(v, bits(q));
}

void put_rgbaq(Bytes& v, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    put_u64(v, static_cast<uint64_t>(r) | (static_cast<uint64_t>(g) << 32));
    put_u64(v, static_cast<uint64_t>(b) | (static_cast<uint64_t>(a) << 32));
}

void put_xyz(Bytes& v, uint16_t x, uint16_t y, uint32_t z, bool adc) {
    put_u64(v, static_cast<uint64_t>(x) | (static_cast<uint64_t>(y) << 32));
    put_u64(v, static_cast<uint64_t>(z) | (static_cast<uint64_t>(adc ? 1 : 0) << 47));
}

void put_xyzf(Bytes& v, uint16_t x, uint16_t y, uint32_t z24, uint8_t f, bool adc) {
    put_u64(v, static_cast<uint64_t>(x) | (static_cast<uint64_t>(y) << 32));
    put_u64(v, (static_cast<uint64_t>(z24) << 4) | (static_cast<uint64_t>(f) << 36) |
                   (static_cast<uint64_t>(adc ? 1 : 0) << 47));
}

void put_ad(Bytes& v, uint8_t addr, uint64_t data) {
    put_u64(v, data);
    put_u64(v, addr);
}

const uint8_t kStrip3[3] = {kSt, kRgbaq, kXyz2};
const uint8_t kStrip3F[3] = {kSt, kRgbaq, kXyzf2};

Bytes strip_packet(uint16_t nloop, bool eop, const uint8_t* regs3, bool xyzf) {
    Bytes v;
    put_tag(v, nloop, eop, true, 0x004 | 0x8, kPacked, 3, regs3, 3);
    for (uint16_t i = 0; i < nloop; ++i) {
        put_st(v, 0.25f * i, 0.5f + i, 1.0f);
        put_rgbaq(v, static_cast<uint8_t>(10 + i), static_cast<uint8_t>(20 + i), static_cast<uint8_t>(30 + i),
                  static_cast<uint8_t>(0x60 + i));
        if (xyzf) {
            put_xyzf(v, static_cast<uint16_t>(100 * 16 + i), static_cast<uint16_t>(200 * 16 + i), 0x123400u + i,
                     static_cast<uint8_t>(0x40 + (i & 7)), false);
        } else {
            put_xyz(v, static_cast<uint16_t>(1792 * 16 + 37 * i), static_cast<uint16_t>(1824 * 16 + 11 * i),
                    0x00A000u + i, false);
        }
    }
    return v;
}

Bytes slice(const Bytes& b, size_t from, size_t to) {
    return Bytes(b.begin() + static_cast<std::ptrdiff_t>(from), b.begin() + static_cast<std::ptrdiff_t>(to));
}

// --- the rig: one parser with its register file and recording sink -----------

struct Sub {
    GifPath path;
    Bytes bytes;
};

struct TestGifParser {
    GifRegs regs;
    RegisterOpStream ops;
    GifParser parser;
    explicit TestGifParser(bool lanes) : parser(ops, regs, GifParserSettings{lanes}) {}
    void feed(const std::vector<Sub>& subs) {
        for (const Sub& s : subs) parser.ReceiveData(s.path, s.bytes.data(), s.bytes.size());
    }
};

bool same_op(const RegisterOp& a, const RegisterOp& b) { return std::memcmp(&a, &b, sizeof(RegisterOp)) == 0; }

// Feed `subs` to a lanes-on and a lanes-off parser; the recorded streams and
// the final parser state must agree. Returns the lanes-on world's op count.
size_t ab_case(const char* name, const std::vector<Sub>& subs, TestGifParser& on, TestGifParser& off) {
    on.feed(subs);
    off.feed(subs);
    const std::vector<RegisterOp>& a = on.ops.Ops();
    const std::vector<RegisterOp>& b = off.ops.Ops();
    size_t first_diff = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < first_diff; ++i) {
        if (!same_op(a[i], b[i])) {
            first_diff = i;
            break;
        }
    }
    const bool ok = a.size() == b.size() && first_diff == a.size() && on.ops.Hash() == off.ops.Hash() &&
                    on.parser.Transfer().in_transfer == off.parser.Transfer().in_transfer &&
                    on.parser.Transfer().loop == off.parser.Transfer().loop &&
                    on.parser.Transfer().reg == off.parser.Transfer().reg &&
                    on.parser.BufferedBytes() == off.parser.BufferedBytes();
    if (!ok) {
        std::fprintf(stderr, "FAIL %s: ops on=%zu off=%zu first_diff=%zu hash on=%016llx off=%016llx\n", name,
                     a.size(), b.size(), first_diff, static_cast<unsigned long long>(on.ops.Hash()),
                     static_cast<unsigned long long>(off.ops.Hash()));
        ++g_fail;
    }
    // The lanes-off leg never enters a lane; the lanes-on leg's census is the
    // must-execute evidence checked per case by the caller.
    CHECK(off.parser.Strip3Triples() == 0 && off.parser.RowableRows() == 0 && off.parser.BulkQuads() == 0);
    std::fprintf(stderr, "case %-28s ops=%zu hash=%016llx triples=%llu rows=%llu bulkqw=%llu %s\n", name, a.size(),
                 static_cast<unsigned long long>(on.ops.Hash()),
                 static_cast<unsigned long long>(on.parser.Strip3Triples()),
                 static_cast<unsigned long long>(on.parser.RowableRows()),
                 static_cast<unsigned long long>(on.parser.BulkQuads()), ok ? "PASS" : "FAIL");
    return a.size();
}

size_t count_kind(const RegisterOpStream& s, RegisterOpKind k) {
    size_t n = 0;
    for (const RegisterOp& op : s.Ops()) n += op.kind == static_cast<uint8_t>(k) ? 1 : 0;
    return n;
}

bool kick_draw(const RegisterOp& op) { return ((op.hi >> 40) & 1u) != 0; }
uint32_t kick_z_bits(const RegisterOp& op) { return static_cast<uint32_t>(op.hi & 0xFFFFFFFFu); }
uint8_t kick_fog(const RegisterOp& op) { return static_cast<uint8_t>((op.hi >> 32) & 0xFFu); }
bool is_kind(const RegisterOp& op, RegisterOpKind k) { return op.kind == static_cast<uint8_t>(k); }

// --- OLD cases ----------------------------------------------------------------

void old_cases() {
    {
        TestGifParser on(true), off(false);
        const size_t n = ab_case("1_strip3_xyz2", {{GifPath::Path2, strip_packet(7, false, kStrip3, false)}}, on, off);
        CHECK(n == 1 + 1 + 7 * 3);
        CHECK(on.parser.Strip3Triples() == 7 && on.parser.BulkQuads() == 21);
        CHECK(count_kind(on.ops, RegisterOpKind::Eop) == 0);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(is_kind(ops[0], RegisterOpKind::Tag) && ops[0].path == 2);
        CHECK(is_kind(ops[1], RegisterOpKind::Reg) && ops[1].reg == kPrim && ops[1].lo == 0x00C);
        CHECK(is_kind(ops[4], RegisterOpKind::Kick) && kick_draw(ops[4]));
        CHECK(static_cast<uint32_t>(ops[4].lo) == bits(1792.0f) && static_cast<uint32_t>(ops[4].lo >> 32) == bits(1824.0f));
        CHECK(kick_z_bits(ops[4]) == bits(static_cast<float>(0x00A000u)) && kick_fog(ops[4]) == 0);
        CHECK(!on.parser.Transfer().in_transfer && on.parser.BufferedBytes() == 0);
    }
    {
        TestGifParser on(true), off(false);
        const size_t n = ab_case("1b_strip3_xyzf2", {{GifPath::Path2, strip_packet(6, false, kStrip3F, true)}}, on, off);
        CHECK(n == 1 + 1 + 6 * 3 && on.parser.Strip3Triples() == 6);
        const RegisterOp& k = on.ops.Ops()[4];
        CHECK(is_kind(k, RegisterOpKind::Kick) && kick_draw(k) && kick_fog(k) == 0x40);
        CHECK(kick_z_bits(k) == bits(static_cast<float>(0x123400u)));
        CHECK(static_cast<uint32_t>(k.lo) == bits(100.0f) && static_cast<uint32_t>(k.lo >> 32) == bits(200.0f));
    }
    {
        const Bytes whole = strip_packet(7, false, kStrip3, false);
        TestGifParser on(true), off(false);
        ab_case("2_split_non16", {{GifPath::Path2, slice(whole, 0, 37)}, {GifPath::Path2, slice(whole, 37, whole.size())}},
                on, off);
        CHECK(on.ops.Count() == 23 && on.parser.BufferedBytes() == 0);
        TestGifParser on2(true), off2(false);
        const size_t cut = 16 + 4 * 16;
        ab_case("2b_split_midvertex",
                {{GifPath::Path2, slice(whole, 0, cut)}, {GifPath::Path2, slice(whole, cut, whole.size())}}, on2, off2);
        CHECK(on2.ops.Count() == 23);
        // span 1: one lane triple + one Tier-A quadword; span 2: unaligned, all Tier A.
        CHECK(on2.parser.Strip3Triples() == 1 && on2.parser.BulkQuads() == 21);
        CHECK(on.ops.Hash() == on2.ops.Hash());
    }
    {
        Bytes v;
        const uint8_t regs[2] = {kAd, kXyz2};
        put_tag(v, 3, true, true, 0x004, kPacked, 2, regs, 2);
        for (int i = 0; i < 3; ++i) {
            put_ad(v, kRgbaq, (static_cast<uint64_t>(0x40 + i) << 24) | 0x00c0c0c0ull);
            put_xyz(v, static_cast<uint16_t>(500 * 16 + i), static_cast<uint16_t>(600 * 16 + i), 0x1000u + i, false);
        }
        TestGifParser on(true), off(false);
        const size_t n = ab_case("3_ad_fallback", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 + 1 + 3 * 2 + 1 && on.parser.BulkQuads() == 0);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(is_kind(ops[2], RegisterOpKind::Reg) && ops[2].reg == kRgbaq && ops[2].lo == 0x40c0c0c0ull);
        CHECK(is_kind(ops[3], RegisterOpKind::Kick) && kick_draw(ops[3]));
        CHECK(is_kind(ops[8], RegisterOpKind::Eop));
    }
    {
        Bytes v;
        const uint8_t regs[3] = {kPrim, kRgbaq, kXyz2};
        put_tag(v, 3, true, false, 0, kReglist, 3, regs, 3);
        uint64_t dws[9];
        for (int i = 0; i < 9; ++i) {
            const int slot = i % 3;
            if (slot == 0) {
                dws[i] = 0x004;
            } else if (slot == 1) {
                dws[i] = static_cast<uint64_t>(0x30 + i) | (static_cast<uint64_t>(0x50 + i) << 8) |
                         (static_cast<uint64_t>(0x70 + i) << 16) | (static_cast<uint64_t>(0x80) << 24);
            } else {
                dws[i] = static_cast<uint64_t>(static_cast<uint16_t>(700 * 16 + i)) |
                         (static_cast<uint64_t>(static_cast<uint16_t>(800 * 16 + i)) << 16) |
                         (static_cast<uint64_t>(0x2000 + i) << 32);
            }
        }
        for (int q = 0; q < 5; ++q) {
            put_u64(v, dws[q * 2]);
            put_u64(v, (q * 2 + 1 < 9) ? dws[q * 2 + 1] : 0xDEADBEEFull);
        }
        TestGifParser on(true), off(false);
        const size_t n = ab_case("4_reglist", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 + 3 * 3 + 1);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(is_kind(ops[1], RegisterOpKind::Reg) && ops[1].reg == kPrim && ops[1].lo == 0x004);
        CHECK(is_kind(ops[2], RegisterOpKind::Reg) && ops[2].reg == kRgbaq && ops[2].lo == dws[1]);
        CHECK(is_kind(ops[3], RegisterOpKind::Kick) && kick_draw(ops[3]) &&
              static_cast<uint32_t>(ops[3].lo) == bits(700.0f + 2.0f / 16.0f) &&
              kick_z_bits(ops[3]) == bits(static_cast<float>(0x2002u)));
        CHECK(is_kind(ops[10], RegisterOpKind::Eop));
        // the 0xDEADBEEF padding half was never dispatched
        for (const RegisterOp& op : ops) CHECK(op.lo != 0xDEADBEEFull);
        CHECK(!on.parser.Transfer().in_transfer);
    }
    {
        Bytes setup;
        const uint8_t regs[1] = {kAd};
        put_tag(setup, 4, false, false, 0, kPacked, 1, regs, 1);
        put_ad(setup, 0x50, static_cast<uint64_t>(1) << 48);
        put_ad(setup, 0x51, 0);
        put_ad(setup, 0x52, 8ull | (4ull << 32));
        put_ad(setup, 0x53, 0);
        Bytes img;
        put_tag(img, 8, true, false, 0, kImage, 0, nullptr, 0);
        for (int i = 0; i < 16; ++i) put_u64(img, 0x1111111100000000ull * static_cast<uint64_t>((i % 4) + 1) + i);
        TestGifParser on(true), off(false);
        const size_t n = ab_case("5_image_split",
                                 {{GifPath::Path2, setup},
                                  {GifPath::Path2, slice(img, 0, 16 + 3 * 16)},
                                  {GifPath::Path2, slice(img, 16 + 3 * 16, img.size())}},
                                 on, off);
        CHECK(n == 1 + 4 + 1 + 8 + 1);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(ops[1].reg == 0x50 && ops[1].lo == (1ull << 48) && ops[3].reg == 0x52 && ops[4].reg == 0x53);
        CHECK(count_kind(on.ops, RegisterOpKind::Image) == 8);
        CHECK(is_kind(ops[6], RegisterOpKind::Image) && ops[6].lo == 0x1111111100000000ull && ops[6].hi == 0x2222222200000001ull);
        CHECK(is_kind(ops[13], RegisterOpKind::Image) && ops[13].lo == (0x1111111100000000ull * 3 + 14) &&
              ops[13].hi == (0x1111111100000000ull * 4 + 15));
        CHECK(is_kind(ops[14], RegisterOpKind::Eop) && !on.parser.Transfer().in_transfer);
    }
    {
        Bytes fl;
        put_tag(fl, 0, true, false, 0, kPacked, 0, nullptr, 0);
        TestGifParser on(true), off(false);
        const size_t n = ab_case("6_nloop0_eop",
                                 {{GifPath::Path2, strip_packet(4, false, kStrip3, false)}, {GifPath::Path2, fl}}, on, off);
        CHECK(n == (1 + 1 + 12) + 2);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(is_kind(ops[14], RegisterOpKind::Tag) && is_kind(ops[15], RegisterOpKind::Eop));
        CHECK(!on.parser.Transfer().in_transfer && on.parser.BufferedBytes() == 0);
    }
    {
        Bytes v;
        const uint8_t regs[3] = {kSt, kRgbaq, kXyz3};
        put_tag(v, 4, false, true, 0x004, kPacked, 3, regs, 3);
        for (int i = 0; i < 4; ++i) {
            put_st(v, 1.0f + i, 2.0f + i, 1.0f);
            put_rgbaq(v, static_cast<uint8_t>(1 + i), static_cast<uint8_t>(2 + i), static_cast<uint8_t>(3 + i),
                      static_cast<uint8_t>(4 + i));
            put_xyz(v, static_cast<uint16_t>(300 * 16 + i), static_cast<uint16_t>(400 * 16 + i), 0x3000u + i, false);
        }
        const uint8_t regs2[3] = {kSt, kRgbaq, kXyz2};
        put_tag(v, 4, false, true, 0x004, kPacked, 3, regs2, 3);
        for (int i = 0; i < 4; ++i) {
            put_st(v, 5.0f + i, 6.0f + i, 1.0f);
            put_rgbaq(v, static_cast<uint8_t>(9 + i), static_cast<uint8_t>(8 + i), static_cast<uint8_t>(7 + i),
                      static_cast<uint8_t>(6 + i));
            put_xyz(v, static_cast<uint16_t>(310 * 16 + i), static_cast<uint16_t>(410 * 16 + i), 0x4000u + i, (i & 1) != 0);
        }
        TestGifParser on(true), off(false);
        const size_t n = ab_case("7_xyz3_adc", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 2 * (1 + 1 + 12));
        CHECK(on.parser.RowableRows() == 4 && on.parser.Strip3Triples() == 4);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        size_t draws = 0, kicks = 0;
        for (const RegisterOp& op : ops) {
            if (!is_kind(op, RegisterOpKind::Kick)) continue;
            ++kicks;
            draws += kick_draw(op) ? 1 : 0;
        }
        CHECK(kicks == 8 && draws == 2);  // XYZ3 never draws; strip3 alternates ADC
        CHECK(!kick_draw(ops[4]) && kick_draw(ops[18]) && !kick_draw(ops[21]));
    }
    {
        Bytes v;
        uint8_t regs[16];
        for (int i = 0; i < 16; ++i) {
            const int m = i % 4;
            regs[i] = static_cast<uint8_t>((m == 0) ? kSt : (m == 1) ? kRgbaq : (m == 2) ? kXyz2 : kNop);
        }
        put_tag(v, 2, true, true, 0x004, kPacked, 0, regs, 16);
        for (int loop = 0; loop < 2; ++loop) {
            for (int i = 0; i < 16; ++i) {
                const int m = i % 4;
                if (m == 0) {
                    put_st(v, 0.5f * i + loop, 0.25f * i, 1.0f);
                } else if (m == 1) {
                    put_rgbaq(v, static_cast<uint8_t>(40 + i), static_cast<uint8_t>(50 + i), static_cast<uint8_t>(60 + i),
                              static_cast<uint8_t>(70 + loop));
                } else if (m == 2) {
                    put_xyz(v, static_cast<uint16_t>(320 * 16 + i + loop), static_cast<uint16_t>(420 * 16 + i), 0x5000u + i,
                            false);
                } else {
                    put_u64(v, 0xAAAAAAAAAAAAAAAAull);
                    put_u64(v, 0x5555555555555555ull);
                }
            }
        }
        TestGifParser on(true), off(false);
        const size_t n = ab_case("8_nregs16", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 + 1 + 2 * 12 + 1);  // NOP quadwords record nothing
        CHECK(on.parser.RowableRows() == 2 && on.parser.BulkQuads() == 32);
    }
    {
        Bytes v;
        const uint8_t regs[4] = {kSt, kRgbaq, kUv, kXyz2};
        put_tag(v, 6, true, true, 0x004, kPacked, 4, regs, 4);
        for (int i = 0; i < 6; ++i) {
            put_st(v, 0.125f * i, 0.375f * i, 1.0f);
            put_rgbaq(v, static_cast<uint8_t>(100 + i), static_cast<uint8_t>(110 + i), static_cast<uint8_t>(120 + i),
                      static_cast<uint8_t>(0x40 + i));
            put_u64(v, static_cast<uint64_t>((330 + i) & 0x3FFF) | (static_cast<uint64_t>((430 + i) & 0x3FFF) << 32));
            put_u64(v, 0);
            put_xyz(v, static_cast<uint16_t>(340 * 16 + 3 * i), static_cast<uint16_t>(440 * 16 + 5 * i), 0x6000u + i, false);
        }
        TestGifParser on(true), off(false);
        const size_t n = ab_case("9_row4_whole", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 + 1 + 6 * 4 + 1 && on.parser.RowableRows() == 6);
        const RegisterOp& uv = on.ops.Ops()[4];
        CHECK(is_kind(uv, RegisterOpKind::Uv) && uv.reg == kUv && uv.lo == (330ull | (430ull << 16)));
        TestGifParser on2(true), off2(false);
        const size_t cut = 16 + 4 * 16 + 2 * 16;
        ab_case("9b_row4_midrow_split", {{GifPath::Path2, slice(v, 0, cut)}, {GifPath::Path2, slice(v, cut, v.size())}},
                on2, off2);
        CHECK(on2.ops.Hash() == on.ops.Hash());
        // row 0 through the lane, then Tier A for the partial row; the second
        // span enters unaligned and rides Tier A to the end.
        CHECK(on2.parser.RowableRows() == 1 && on2.parser.BulkQuads() == 24);
    }
    {
        TestGifParser on(true), off(false);
        ab_case("10c_strip3_n256", {{GifPath::Path2, strip_packet(256, false, kStrip3, false)}}, on, off);
        CHECK(on.ops.Count() == 2 + 256 * 3 && on.parser.Strip3Triples() == 256);
    }
}

// --- pinned rules of the canonical walk ---------------------------------------

void pinned_cases() {
    {
        // A+D to TEXCLUT, the reserved descriptor, A+D itself, NOP and an
        // undefined number are dropped; TEXFLUSH is forwarded.
        Bytes v;
        const uint8_t regs[1] = {kAd};
        put_tag(v, 6, true, false, 0, kPacked, 1, regs, 1);
        put_ad(v, kTexclut, 0x1234);
        put_ad(v, 0x0B, 0x1);
        put_ad(v, kAd, 0x2);
        put_ad(v, kNop, 0x3);
        put_ad(v, 0x10, 0x4);
        put_ad(v, kTexflush, 0x5);
        TestGifParser on(true), off(false);
        const size_t n = ab_case("texclut_dropped", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 3);
        CHECK(on.ops.Ops()[1].reg == kTexflush && on.ops.Ops()[1].lo == 0x5 && is_kind(on.ops.Ops()[2], RegisterOpKind::Eop));
    }
    {
        // REGLIST XYZ2/XYZF2 always draw (the 64-bit form has no ADC bit);
        // XYZ3/XYZF3 never draw; XYZF keeps 24-bit Z and the fog byte.
        Bytes v;
        const uint8_t regs[4] = {kXyz2, kXyzf2, kXyz3, kXyzf3};
        put_tag(v, 1, true, false, 0, kReglist, 4, regs, 4);
        const uint64_t xy = static_cast<uint64_t>(640 * 16) | (static_cast<uint64_t>(480 * 16 + 8) << 16);
        put_u64(v, xy | (0x00ABCDEFull << 32) | (1ull << 47));
        put_u64(v, xy | (0x00ABCDEFull << 32) | (0x7Full << 56) | (1ull << 47));
        put_u64(v, xy | (0xFFFFFFFFull << 32));
        put_u64(v, xy | (0x00123456ull << 32) | (0x33ull << 56));
        TestGifParser on(true), off(false);
        const size_t n = ab_case("reglist_xyz_adc_ignored", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 + 4 + 1);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(kick_draw(ops[1]) && kick_fog(ops[1]) == 0 && kick_z_bits(ops[1]) == bits(static_cast<float>(0x00ABCDEFu)));
        CHECK(static_cast<uint32_t>(ops[1].lo) == bits(640.0f) && static_cast<uint32_t>(ops[1].lo >> 32) == bits(480.5f));
        CHECK(kick_draw(ops[2]) && kick_fog(ops[2]) == 0x7F && kick_z_bits(ops[2]) == bits(static_cast<float>(0x00ABCDEFu)));
        CHECK(!kick_draw(ops[3]) && kick_z_bits(ops[3]) == bits(static_cast<float>(0xFFFFFFFFu)));
        CHECK(!kick_draw(ops[4]) && kick_fog(ops[4]) == 0x33 && kick_z_bits(ops[4]) == bits(static_cast<float>(0x00123456u)));
    }
    {
        // PRE=1 writes PRIM (11 bits) before the payload; PRE=0 does not.
        // PACKED PRIM descriptor and REGLIST PRIM are masked to 11 bits too.
        Bytes v;
        const uint8_t regs[1] = {kPrim};
        put_tag(v, 1, false, true, 0x7FF, kPacked, 1, regs, 1);
        put_u64(v, 0xFFFFFFFFFFFFFFFFull);
        put_u64(v, 0);
        put_tag(v, 1, true, false, 0x123, kReglist, 1, regs, 1);
        put_u64(v, 0xFFFFull);
        put_u64(v, 0xDEADBEEFull);
        TestGifParser on(true), off(false);
        const size_t n = ab_case("pre_prim", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 + 1 + 1 + 1 + 1 + 1);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(is_kind(ops[1], RegisterOpKind::Reg) && ops[1].reg == kPrim && ops[1].lo == 0x7FF);
        CHECK(is_kind(ops[2], RegisterOpKind::Reg) && ops[2].reg == kPrim && ops[2].lo == 0x7FF);
        CHECK(is_kind(ops[3], RegisterOpKind::Tag) && is_kind(ops[4], RegisterOpKind::Reg) && ops[4].lo == 0x7FF);
        CHECK(is_kind(ops[5], RegisterOpKind::Eop));
    }
    {
        // PACKED decoders: ST passes raw q (also 0), UV, FOG byte, XYZF2 with
        // ADC set does not draw, an unknown PACKED descriptor carries a 64-bit
        // register value in the low half.
        Bytes v;
        const uint8_t regs[5] = {kSt, kUv, kFog, kXyzf2, kTex0_1};
        put_tag(v, 1, true, false, 0, kPacked, 5, regs, 5);
        put_st(v, 0.5f, -2.0f, 0.0f);
        put_u64(v, 0xFFFFull | (0x1234ull << 32));
        put_u64(v, 0);
        put_u64(v, 0);
        put_u64(v, 0xA5ull << 36);
        put_xyzf(v, 4095 * 16 + 15, 1, 0xFFFFFFu, 0x11, true);
        put_u64(v, 0x0123456789ABCDEFull);
        put_u64(v, 0xFFFFFFFFFFFFFFFFull);
        TestGifParser on(true), off(false);
        const size_t n = ab_case("packed_decoders", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 + 5 + 1 && on.parser.BulkQuads() == 0);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(is_kind(ops[1], RegisterOpKind::St) && static_cast<uint32_t>(ops[1].lo) == bits(0.5f) &&
              static_cast<uint32_t>(ops[1].lo >> 32) == bits(-2.0f) && ops[1].hi == 0);
        CHECK(is_kind(ops[2], RegisterOpKind::Uv) && ops[2].lo == (0x3FFFull | (0x1234ull << 16)));
        CHECK(is_kind(ops[3], RegisterOpKind::Fog) && ops[3].lo == 0xA5);
        CHECK(is_kind(ops[4], RegisterOpKind::Kick) && !kick_draw(ops[4]) && kick_fog(ops[4]) == 0x11 &&
              static_cast<uint32_t>(ops[4].lo) == bits(4095.9375f) && static_cast<uint32_t>(ops[4].lo >> 32) == bits(0.0625f) &&
              kick_z_bits(ops[4]) == bits(static_cast<float>(0xFFFFFFu)));
        CHECK(is_kind(ops[5], RegisterOpKind::Reg) && ops[5].reg == kTex0_1 && ops[5].lo == 0x0123456789ABCDEFull);
    }
    {
        // NLOOP=0 EOP=0 under a non-bulk descriptor set: the tag stays in
        // transfer and the next quadword is consumed as one register write
        // (both configurations, as OLD).
        Bytes v;
        const uint8_t regs[1] = {kAd};
        put_tag(v, 0, false, false, 0, kPacked, 1, regs, 1);
        put_ad(v, kTexflush, 0x77);
        Bytes rest = strip_packet(2, true, kStrip3, false);
        v.insert(v.end(), rest.begin(), rest.end());
        TestGifParser on(true), off(false);
        const size_t n = ab_case("nloop0_noeop_generic", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 + 1 + (1 + 1 + 6 + 1));
        CHECK(on.ops.Ops()[1].reg == kTexflush && on.ops.Ops()[1].lo == 0x77);
    }
    {
        // NLOOP=0 EOP=0 under an all-known descriptor set: the bulk branch
        // has nothing to consume, so the following tag is parsed intact. The
        // lanes-off leg eats one quadword instead; the bulk arm is the norm.
        Bytes v;
        put_tag(v, 0, false, false, 0, kPacked, 3, kStrip3, 3);
        Bytes rest = strip_packet(2, true, kStrip3, false);
        v.insert(v.end(), rest.begin(), rest.end());
        TestGifParser on(true);
        on.feed({{GifPath::Path2, v}});
        CHECK(on.ops.Count() == 1 + (1 + 1 + 6 + 1));
        CHECK(is_kind(on.ops.Ops()[1], RegisterOpKind::Tag) && !on.parser.Transfer().in_transfer);
        TestGifParser off(false);
        off.feed({{GifPath::Path2, v}});
        CHECK(off.ops.Count() != on.ops.Count());
        std::fprintf(stderr, "case %-28s ops on=%zu off=%zu (bulk arm is the norm)\n", "nloop0_noeop_allknown",
                     on.ops.Count(), off.ops.Count());
    }
    {
        // FLG=3 (DISABLED): quadwords are consumed with no counter advance; the
        // tag never completes.
        Bytes v;
        put_tag(v, 1, true, false, 0, kDisabled, 1, nullptr, 0);
        put_u64(v, 1);
        put_u64(v, 2);
        put_u64(v, 3);
        put_u64(v, 4);
        TestGifParser on(true), off(false);
        const size_t n = ab_case("flg3_disabled", {{GifPath::Path2, v}}, on, off);
        CHECK(n == 1 && on.parser.Transfer().in_transfer && on.parser.BufferedBytes() == 0);
    }
    {
        // A PATH3 packet arriving mid-transfer abandons the PATH2 tag and its
        // buffered tail; a PATH2 continuation does not.
        const Bytes strip = strip_packet(3, true, kStrip3, false);
        TestGifParser on(true), off(false);
        ab_case("path3_stale_reset",
                {{GifPath::Path2, slice(strip, 0, 16 + 16 + 4)},
                 {GifPath::Path3, strip}},
                on, off);
        const std::vector<RegisterOp>& ops = on.ops.Ops();
        CHECK(ops.size() == 1 + 1 + 1 + (1 + 1 + 9 + 1));
        CHECK(is_kind(ops[2], RegisterOpKind::St) && ops[2].path == 2);
        CHECK(is_kind(ops[3], RegisterOpKind::Tag) && ops[3].path == 3 && is_kind(ops[14], RegisterOpKind::Eop));
        CHECK(!on.parser.Transfer().in_transfer && on.parser.BufferedBytes() == 0);
        TestGifParser on2(true), off2(false);
        ab_case("path2_continuation",
                {{GifPath::Path2, slice(strip, 0, 16 + 16 + 4)}, {GifPath::Path2, slice(strip, 16 + 16 + 4, strip.size())}},
                on2, off2);
        CHECK(on2.ops.Count() == 1 + 1 + 9 + 1 && on2.parser.BufferedBytes() == 0);
    }
    {
        // MODE.M3R holds received data of every path unparsed until the mask
        // lifts; the next delivery then parses the whole tail.
        const Bytes strip = strip_packet(2, true, kStrip3, false);
        TestGifParser on(true), off(false);
        on.regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 1);
        off.regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 1);
        ab_case("m3r_hold", {{GifPath::Path1, strip}}, on, off);
        CHECK(on.ops.Count() == 0 && on.parser.BufferedBytes() == strip.size());
        on.regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 0);
        off.regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 0);
        ab_case("m3r_release", {{GifPath::Path1, strip}}, on, off);
        CHECK(on.ops.Count() == 2 * (1 + 1 + 6 + 1) && on.parser.BufferedBytes() == 0);
        CHECK(on.ops.Ops()[0].path == 1);
    }
    {
        // FinishPath3: a mid-IMAGE transfer is kept (no sink call, tail kept);
        // any other incomplete tag is abandoned and the GS told to finish.
        Bytes img;
        put_tag(img, 4, true, false, 0, kImage, 0, nullptr, 0);
        for (int i = 0; i < 8; ++i) put_u64(img, 0x100 + i);
        TestGifParser w(true);
        w.feed({{GifPath::Path3, slice(img, 0, 16 + 2 * 16 + 4)}});
        CHECK(w.parser.Transfer().in_transfer && w.parser.Transfer().loop == 2 && w.parser.BufferedBytes() == 4);
        w.parser.FinishPath3();
        CHECK(w.parser.Transfer().in_transfer && w.parser.BufferedBytes() == 4);
        CHECK(count_kind(w.ops, RegisterOpKind::Finish) == 0);
        w.feed({{GifPath::Path1, slice(img, 16 + 2 * 16 + 4, img.size())}});
        CHECK(!w.parser.Transfer().in_transfer && count_kind(w.ops, RegisterOpKind::Image) == 4);
        w.parser.FinishPath3();
        CHECK(count_kind(w.ops, RegisterOpKind::Finish) == 1);
        const Bytes strip = strip_packet(3, true, kStrip3, false);
        w.feed({{GifPath::Path3, slice(strip, 0, 16 + 16 + 4)}});
        CHECK(w.parser.Transfer().in_transfer && w.parser.BufferedBytes() == 4);
        w.parser.FinishPath3();
        CHECK(!w.parser.Transfer().in_transfer && w.parser.BufferedBytes() == 0);
        CHECK(count_kind(w.ops, RegisterOpKind::Finish) == 2);
        // the register file reads the transfer the parser advanced
        w.feed({{GifPath::Path3, slice(strip, 0, 16 + 16)}});
        CHECK((w.regs.Read(GifRegs::kBase + GifRegs::kStat, 4) & 0xE00u) == 0xE00u);
        CHECK(w.regs.Read(GifRegs::kBase + GifRegs::kCnt, 4) == (3u | (1u << 16)));
    }
    {
        // Hash determinism: the same bytes twice give the same hash, and one
        // flipped ADC bit changes it.
        const Bytes a = strip_packet(5, true, kStrip3, false);
        Bytes b = a;
        b[16 + 32 + 8 + 5] ^= 0x80;  // ADC bit 47 of the first vertex
        TestGifParser w1(true), w2(true), w3(true);
        w1.feed({{GifPath::Path2, a}});
        w2.feed({{GifPath::Path2, a}});
        w3.feed({{GifPath::Path2, b}});
        CHECK(w1.ops.Hash() == w2.ops.Hash() && w1.ops.Hash() != w3.ops.Hash() && w1.ops.Count() == w3.ops.Count());
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "gif parser equivalence: bulk lanes on vs off\n");
    old_cases();
    pinned_cases();
    if (g_fail != 0) {
        std::fprintf(stderr, "GIF PARSER EQUIVALENCE FAIL: %d\n", g_fail);
        return 1;
    }
    std::fprintf(stderr, "GIF PARSER EQUIVALENCE PASS\n");
    return 0;
}

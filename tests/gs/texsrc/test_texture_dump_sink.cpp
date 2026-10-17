// tests/gs/texsrc/test_texture_dump_sink.cpp -- Texsrc_DumpSinkOracleB / Texsrc_DumpSinkFiles:
// the texture-oracle observer (TextureDumpSink).
//
// `oracle_b`: the fast tier the resolver binds (TextureDecoder::Decode / DecodeRect over
// BlockDecode::Decode -- the page LUT walk + NEON expansion) equals the per-pixel
// reference (BlockDecode::DecodeGeneric over PsmLayout::PixelAddress) byte for byte,
// over the matrix EVERY PSM the GS has (CT32 CT24 CT16 CT16S T8 T4 T8H T4HL T4HH Z32 Z24
// Z16 Z16S) x CLUT (CPSM CT32 / CT16 / CT16S, CSM1 / CSM2, CSA 0..31) x TBW != TW/8
// (narrower and wider than the texture, TBW 0) x non-page-aligned bases (block-granular
// TBP0 / CBP) x TEXA x full / UV sub-rect decodes, on random VRAM. A deliberately
// corrupted texel buffer must be reported with the right count and first (x, y).
// `files`: armed, the sink writes each distinct texture ONCE (.ppm / .alpha.pgm / .json
// / .win / .clut), dedups a repeat, records a served tuple once, and Disarm writes
// serves.tsv.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/gs/format/block_decode.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/texsrc/texture_decoder.h"
#include "engine/gs/hle/texsrc/texture_dump_sink.h"

using namespace ps2r;
using namespace ps2r::gs;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c, ...)                                             \
    do {                                                          \
        ++g_checks;                                               \
        if (!(c)) {                                               \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);      \
            std::printf(__VA_ARGS__);                             \
            std::printf("\n");                                    \
            ++g_fail;                                             \
        }                                                         \
    } while (0)

uint64_t g_rng = 0x9E3779B97F4A7C15ull;
uint64_t Rnd64() {
    uint64_t x = g_rng;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    g_rng = x;
    return x * 0x2545F4914F6CDD1Dull;
}
uint32_t Rnd(uint32_t lo, uint32_t hi) { return lo + uint32_t(Rnd64() % (uint64_t(hi) - lo + 1u)); }

constexpr uint32_t kPsmFields[13] = {0x00, 0x01, 0x02, 0x0A, 0x13, 0x14, 0x1B, 0x24, 0x2C, 0x30, 0x31, 0x32, 0x3A};
constexpr uint32_t kCpsmFields[3] = {0x00, 0x02, 0x0A};
bool Indexed(uint32_t psm) { return psm == 0x13 || psm == 0x14 || psm == 0x1b || psm == 0x24 || psm == 0x2c; }

// One case: the resolver's decode of `v` (full or the sub-rect) and the sink's verdict.
struct Case {
    TexReadView v;
    bool sub = false;
    int x0 = 0, y0 = 0, w = 0, h = 0;
};

TextureDumpSink::OracleB RunCase(const std::vector<uint8_t>& vram, const Case& c, std::vector<uint32_t>* texels,
                                 std::vector<uint32_t>* ref, TextureDumpSink::Decode* out) {
    const int tw = 1 << c.v.tw, th = 1 << c.v.th;
    const int dw = c.sub ? c.w : tw, dh = c.sub ? c.h : th;
    if (c.sub) TextureDecoder::DecodeRect(vram.data(), c.v, c.x0, c.y0, dw, dh, nullptr, nullptr, texels);
    else TextureDecoder::Decode(vram.data(), c.v, nullptr, nullptr, texels);
    TextureDumpSink::Decode d;
    d.tex = &c.v;
    d.view = vram.data();
    d.texels = texels->data();
    d.dw = dw;
    d.dh = dh;
    d.sub = c.sub;
    d.x0 = c.x0;
    d.y0 = c.y0;
    BlockDecode::Texture t;
    Psm psm;
    PsmLayout::FromField(c.v.psm, &psm);
    t.psm = psm;
    t.tbp = c.v.tbp / 256;
    t.tbw = c.v.tbw / 64;
    t.w = uint32_t(tw);
    t.h = uint32_t(th);
    const BlockDecode::Footprint fp = BlockDecode::DecodeFootprint(t);
    d.fp_start = fp.start;
    d.fp_size = fp.size;
    d.frame_count = 7;
    d.draw_seq = 11;
    if (out) *out = d;
    return TextureDumpSink::CompareWithReference(d, ref);
}

const char* PsmName(uint32_t f) {
    Psm p;
    return PsmLayout::FromField(f, &p) ? PsmLayout::Describe(p).name : "?";
}

int OracleBMatrix() {
    std::vector<uint8_t> vram(4u * 1024u * 1024u);
    for (size_t i = 0; i < vram.size(); i += 8) { const uint64_t r = Rnd64(); std::memcpy(&vram[i], &r, 8); }
    std::vector<uint32_t> texels, ref;
    uint64_t cases = 0, texel_words = 0;
    // The TBW choices per log2 width: equal to the texture width, narrower (the manual's
    // "a width narrower than one page still addresses one page per row"), wider, zero.
    for (uint32_t psm : kPsmFields) {
        const uint32_t cpsm_n = Indexed(psm) ? 3u : 1u;
        for (uint32_t ci = 0; ci < cpsm_n; ++ci) {
            for (uint32_t csm = 0; csm < (Indexed(psm) ? 2u : 1u); ++csm) {
                for (int rep = 0; rep < 6; ++rep) {
                    Case c;
                    c.v.psm = psm;
                    c.v.tw = Rnd(0, 8);
                    c.v.th = Rnd(0, 8);
                    const uint32_t tw_px = 1u << c.v.tw;
                    const uint32_t tbw_choice = Rnd(0, 3);
                    c.v.tbw = tbw_choice == 0 ? (tw_px < 64 ? 64 : tw_px)        // = the texture width
                              : tbw_choice == 1 ? 64                             // narrower (one 64 px unit)
                              : tbw_choice == 2 ? Rnd(1, 32) * 64                // arbitrary
                              : 0;                                               // TBW 0 (behaves as 1)
                    c.v.tbp = Rnd(0, 16383) * 256;                               // block-granular, not page-aligned
                    c.v.cbp = Rnd(0, 16383) * 256;
                    c.v.cpsm = kCpsmFields[ci];
                    c.v.csm = csm;
                    c.v.csa = Rnd(0, 31);
                    c.v.texa_ta0 = uint8_t(Rnd(0, 255));
                    c.v.texa_ta1 = uint8_t(Rnd(0, 255));
                    c.v.texa_aem = Rnd(0, 1) != 0;
                    if (rep >= 3) {   // a UV sub-rect (what UvRegion derives), inside the level
                        const int tw = 1 << c.v.tw, th = 1 << c.v.th;
                        c.sub = true;
                        c.x0 = int(Rnd(0, uint32_t(tw - 1)));
                        c.y0 = int(Rnd(0, uint32_t(th - 1)));
                        c.w = int(Rnd(1, uint32_t(tw - c.x0)));
                        c.h = int(Rnd(1, uint32_t(th - c.y0)));
                    }
                    const TextureDumpSink::OracleB ob = RunCase(vram, c, &texels, &ref, nullptr);
                    CHECK(ob.equal, "%s cpsm=%02x csm=%u csa=%u tbp=%06x tbw=%u tw=%u th=%u sub=%d: fast != reference (%u diffs, first %d,%d)",
                          PsmName(psm), c.v.cpsm, c.v.csm, c.v.csa, c.v.tbp, c.v.tbw, c.v.tw, c.v.th, c.sub ? 1 : 0, ob.diffs,
                          ob.first_x, ob.first_y);
                    ++cases;
                    texel_words += texels.size();
                }
            }
        }
    }
    // Every CSA value on a T4 / T8 read: the engine's decode does not consult CSA (the CLUT
    // rectangle is read at CBP), so the fast tier and the reference agree for all 32 --
    // pinned here so a CSA model added later lands on both sides at once.
    for (uint32_t csa = 0; csa < 32; ++csa) {
        Case c;
        c.v.psm = (csa & 1) ? 0x14 : 0x13;
        c.v.tw = 6; c.v.th = 6; c.v.tbw = 64; c.v.tbp = 0x100000; c.v.cbp = 0x180000 + csa * 256;
        c.v.cpsm = kCpsmFields[csa % 3]; c.v.csm = (csa >> 1) & 1; c.v.csa = csa;
        c.v.texa_ta0 = 0x80; c.v.texa_ta1 = 0x80;
        const TextureDumpSink::OracleB ob = RunCase(vram, c, &texels, &ref, nullptr);
        CHECK(ob.equal, "CSA %u: fast != reference (%u diffs)", csa, ob.diffs);
        ++cases;
    }
    // The 4 MB wrap: a base in the last page with a texture that runs past the top.
    {
        Case c;
        c.v.psm = 0x00; c.v.tw = 7; c.v.th = 7; c.v.tbw = 128; c.v.tbp = 511 * 8192 + 3 * 256;
        c.v.texa_ta1 = 0x80;
        const TextureDumpSink::OracleB ob = RunCase(vram, c, &texels, &ref, nullptr);
        CHECK(ob.equal, "4 MB wrap: fast != reference (%u diffs)", ob.diffs);
        ++cases;
    }
    // Negative control: a corrupted fast result is reported with its count and first (x, y).
    {
        Case c;
        c.v.psm = 0x13; c.v.tw = 5; c.v.th = 4; c.v.tbw = 64; c.v.tbp = 0x200000; c.v.cbp = 0x200400;
        c.v.texa_ta1 = 0x80;
        TextureDumpSink::Decode d;
        TextureDumpSink::OracleB ob = RunCase(vram, c, &texels, &ref, &d);
        CHECK(ob.equal, "control base case differs");
        texels[3 * 32 + 5] ^= 0x00FF00FFu;
        texels[9 * 32 + 31] ^= 0x01000000u;
        d.texels = texels.data();
        ob = TextureDumpSink::CompareWithReference(d, &ref);
        CHECK(!ob.equal && ob.diffs == 2 && ob.first_x == 5 && ob.first_y == 3, "control: diffs=%u first=%d,%d", ob.diffs,
              ob.first_x, ob.first_y);
    }
    std::printf("oracle_b matrix: %llu cases, %llu texel words, %d checks, %d failures\n", (unsigned long long)cases,
                (unsigned long long)texel_words, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

bool Exists(const std::string& p) { return std::filesystem::exists(p); }
size_t FileSize(const std::string& p) { return std::filesystem::exists(p) ? size_t(std::filesystem::file_size(p)) : 0; }
std::string Slurp(const std::string& p) {
    std::string s;
    if (std::FILE* f = std::fopen(p.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

int Files() {
    std::vector<uint8_t> vram(4u * 1024u * 1024u);
    for (size_t i = 0; i < vram.size(); i += 8) { const uint64_t r = Rnd64(); std::memcpy(&vram[i], &r, 8); }
    const std::string dir = (std::filesystem::temp_directory_path() / ("ps2r_texdump_" + std::to_string(uint32_t(Rnd64())))).string();
    std::filesystem::remove_all(dir);
    TextureDumpSink sink;
    CHECK(!sink.Armed(), "disarmed at construction");
    // Disarmed: a report is a no-op.
    Case c;
    c.v.psm = 0x13; c.v.tw = 6; c.v.th = 5; c.v.tbw = 64; c.v.tbp = 0x300000 + 256; c.v.cbp = 0x310000; c.v.texa_ta1 = 0x80;
    std::vector<uint32_t> texels, ref;
    TextureDumpSink::Decode d;
    RunCase(vram, c, &texels, &ref, &d);
    sink.OnDecode(d);
    CHECK(sink.GetStats().decodes == 0, "disarmed sink counted a decode");
    CHECK(sink.Arm(dir), "Arm creates %s", dir.c_str());
    sink.OnDecode(d);
    sink.OnDecode(d);   // the same bytes again: one distinct texture
    const uint64_t h = TextureDumpSink::ContentHash(d);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx", (unsigned long long)h);
    const std::string base = dir + "/" + name;
    CHECK(sink.GetStats().decodes == 2 && sink.GetStats().distinct == 1, "dedup: decodes=%llu distinct=%llu",
          (unsigned long long)sink.GetStats().decodes, (unsigned long long)sink.GetStats().distinct);
    CHECK(Exists(base + ".ppm") && FileSize(base + ".ppm") > 64u * 32u * 3u, "ppm written");
    CHECK(Exists(base + ".alpha.pgm") && FileSize(base + ".alpha.pgm") > 64u * 32u, "alpha pgm written");
    CHECK(Exists(base + ".json"), "json written");
    CHECK(Exists(base + ".win") && FileSize(base + ".win") == d.fp_size, "win written (%zu vs %u)", FileSize(base + ".win"), d.fp_size);
    CHECK(Exists(base + ".clut") && FileSize(base + ".clut") == 8192u, "clut page written");
    CHECK(!Exists(base + ".ref.ppm"), "no reference image when equal");
    const std::string js = Slurp(base + ".json");
    CHECK(js.find("\"oracle_b\": \"equal\"") != std::string::npos, "json oracle_b equal: %s", js.c_str());
    CHECK(js.find("\"psm\": 19") != std::string::npos && js.find("\"first_frame\": 7, \"first_draw\": 11") != std::string::npos,
          "json tuple / first use");
    CHECK(js.find("\"palette\": [") != std::string::npos, "json palette of an indexed read");
    // A different CLUT page = a different texture (the hash covers the palette bytes).
    Case c2 = c;
    c2.v.cbp = 0x320000;
    TextureDumpSink::Decode d2;
    std::vector<uint32_t> texels2;
    RunCase(vram, c2, &texels2, &ref, &d2);
    sink.OnDecode(d2);
    CHECK(sink.GetStats().distinct == 2, "a different CLUT is a distinct texture");
    // A corrupted fast result: oracle_b differs, the reference image is written.
    Case c3 = c;
    c3.v.tbp = 0x340000;
    TextureDumpSink::Decode d3;
    std::vector<uint32_t> texels3;
    RunCase(vram, c3, &texels3, &ref, &d3);
    texels3[0] ^= 0xFFu;
    d3.texels = texels3.data();
    sink.OnDecode(d3);
    std::snprintf(name, sizeof(name), "%016llx", (unsigned long long)TextureDumpSink::ContentHash(d3));
    const std::string base3 = dir + "/" + name;
    CHECK(sink.GetStats().oracle_b_bad == 1, "oracle_b_bad counted");
    CHECK(Exists(base3 + ".ref.ppm"), "reference image written when differing");
    CHECK(Slurp(base3 + ".json").find("\"oracle_b\": \"differs\", \"oracle_b_diffs\": 1, \"oracle_b_first_xy\": [0, 0]") !=
              std::string::npos, "json oracle_b differs");
    // Served tuples: once per (tuple, tier).
    sink.NoteServe(c.v, "TargetServe", 7, 12);
    sink.NoteServe(c.v, "TargetServe", 8, 13);
    sink.NoteServe(c.v, "DepthServe", 8, 14);
    CHECK(sink.GetStats().serves == 2, "serves=%llu", (unsigned long long)sink.GetStats().serves);
    sink.Disarm();
    CHECK(!sink.Armed(), "disarmed");
    const std::string tsv = Slurp(dir + "/serves.tsv");
    CHECK(tsv.find("TargetServe\t300100\t64\t13\t6\t5\t310000") != std::string::npos && tsv.find("DepthServe\t") != std::string::npos,
          "serves.tsv: %s", tsv.c_str());
    // Disarmed again: no-op, no files.
    sink.OnDecode(d);
    CHECK(sink.GetStats().decodes == 4, "post-disarm decode not counted (%llu)", (unsigned long long)sink.GetStats().decodes);
    std::filesystem::remove_all(dir);
    std::printf("files: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "oracle_b";
    if (which == "oracle_b") return OracleBMatrix();
    if (which == "files") return Files();
    std::fprintf(stderr, "usage: %s oracle_b|files\n", argv[0]);
    return 2;
}

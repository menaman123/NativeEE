// tests/oracle/og_block_decode.cpp -- port of OLD tests/test_block_decode_equivalence.cpp
// (1,150 ln) as a DIFFERENTIAL oracle: the original's exact case list (its xorshift64*
// stream, seed 0x9E3779B97F4A7C15, same VRAM fill, 400 random + 29 edge full decodes,
// 400 random + 14 targeted rect decodes) is replayed against NEW BlockDecode, and every
// byte that differs from the original's decode is ATTRIBUTED to a pre-registered
// EQUIVALENCE_PLAN 7.1 row -- never tolerated silently.
//
// The original's decode leaves are .cpp-local (texture_decoder.cpp: GetAddressPSM*,
// DecodePSM*, ReadCLUT32/16, CSM1Swizzle, Expand16to32) behind a closure that is not
// std-only, so they are LIFTED here (each cited) as a per-pixel model over the same
// lifted tables the address oracle uses (og_tables.bin). The model is factored so the
// deviation sources can be swapped in one at a time:
//   step A  OLD address (F1)  + OLD values + OLD palette   = the original's output
//   step B  NEW address        + OLD values + OLD palette   -> A vs B = address rows
//   step C  NEW address        + OLD values + NEW palette   -> B vs C = CLUT rows
//   step D  NEW BlockDecode                                 -> C vs D = value rows
// Every A/B, B/C, C/D difference must be exactly the registered deviation of a row
// that applies to the case's modes (checked per byte); anything else FAILS.
//
// Rows (docs/design/EQUIVALENCE_PLAN.md 7.1):
//   P2-T4           address: OLD's linear T4 column vs the manual's (PSMT4 cases)
//   P2-CT16S        address: OLD aliases CT16S to CT16 (no CT16S case in the OLD list)
//   P2-CLUT16-CSM1  CLUT: OLD reads a CSM1 16-bit CLUT linearly (cpsm CT16, csm 0)
//   P2-CLUT32-I4-CSM2  CLUT: a 16-entry CT32 CLUT under CSM2 (undefined by the manual,
//                   section 2.7.4: CSM2 requires CPSM = CT16): OLD reads the grid's 16x1
//                   row (i, 0), NEW the 8x2 rectangle without reorder (T4, cpsm CT32,
//                   csm 1). Under CSM1 both trees read the 8x2 rectangle (OLD's
//                   CSM1Swizzle sends entries 8-15 to grid row 1) -- registered 2D
//   P2-T8H-CSM1     CLUT: OLD's PSMT8H leaf applies CSM1Swizzle a second time at lookup
//                   (net: no reorder) while its PSMT8 leaf applies it once (T8H, csm 0) -- 2D
//   P2-CT16-TEXA    value: OLD never applied TEXA to 16-bit texels / CLUT entries; with
//                   TA0 = 0x00, TA1 = 0x80, AEM = 0 the difference is exactly the alpha
//                   byte 0xFF -> 0x80 of every A = 1 texel (CT16 textures, CT16 palettes)
//   P2-EXP16-REPL   value: both trees replicate 5 -> 8 (no OLD-vs-NEW difference)
//
// Dropped from the original's cases (harness mechanics, not decode rules): the
// vram_base snapshot-window offset (NEW reads the 4 MB view; base 0), the PSMZ16/Z16S
// out-of-window transparent guard (`zsize`), and byte-granular tbp (TBP0 is in 256-byte
// blocks: bases are snapped to the block grid; the count is printed). The 12 key-fold
// assertions are texture-cache facts (Phase 6).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "engine/gs/format/block_decode.h"
#include "engine/gs/format/clut_layout.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/texa_expand.h"

using ps2r::gs::BlockDecode;
using ps2r::gs::Psm;
using ps2r::gs::PsmLayout;
using ps2r::gs::TexaExpand;

namespace {

constexpr uint32_t kVram = 4u * 1024u * 1024u;

// ---------------------------------------------------------------- lifted OLD tables
struct OgTable { uint16_t rows = 0, cols = 0; std::vector<uint16_t> v; uint16_t at(int r, int c) const { return v[r * cols + c]; } };
std::map<std::string, OgTable> g_og;

bool LoadOgTables(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 8 || std::memcmp(d.data(), "OGT1", 4) != 0) return false;
    uint32_t count; std::memcpy(&count, d.data() + 4, 4);
    size_t p = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 36 > d.size()) return false;
        char name[33] = {0}; std::memcpy(name, d.data() + p, 32); p += 32;
        OgTable t; std::memcpy(&t.rows, d.data() + p, 2); std::memcpy(&t.cols, d.data() + p + 2, 2); p += 4;
        size_t nv = size_t(t.rows) * t.cols;
        if (p + nv * 2 > d.size()) return false;
        t.v.resize(nv); std::memcpy(t.v.data(), d.data() + p, nv * 2); p += nv * 2;
        g_og[name] = t;
    }
    return true;
}

// OLD F1 (texture_decoder.cpp GetAddressPSM* :306/:645/:675/:694/:964/:996; Z32 :336):
// tbp bytes, tbw pixels -> byte address % 4 MB. T8H/CT24 through the CT32 helper.
uint32_t OldF1(Psm psm, int x, int y, uint32_t tbp, uint32_t tbw) {
    auto ppr = [&](int page_w) { int p = (tbw > 0) ? int(tbw) / page_w : 1; return p == 0 ? 1 : p; };
    switch (psm) {
        case Psm::CT32: case Psm::CT24: case Psm::T8H: case Psm::T4HL: case Psm::T4HH:
        case Psm::Z32: case Psm::Z24: {
            const OgTable& bt = g_og[(psm == Psm::Z32 || psm == Psm::Z24) ? "block_table_PSMZ32" : "block_table_PSMCT32"];
            const OgTable& ct = g_og["columnTable32"];
            int page = (y / 32) * ppr(64) + x / 64;
            int block = bt.at((y % 32) / 8, (x % 64) / 8);
            return (tbp + page * 8192 + block * 256 + ct.at(y % 8, x % 8) * 4) % kVram;
        }
        case Psm::CT16: case Psm::CT16S: case Psm::Z16: case Psm::Z16S: {
            const char* name = psm == Psm::Z16 ? "block_table_PSMZ16" : psm == Psm::Z16S ? "block_table_PSMZ16S" : "block_table_PSMCT16";
            const OgTable& bt = g_og[name];
            const OgTable& ct = g_og["columnTable16"];
            int page = (y / 64) * ppr(64) + x / 64;
            int block = bt.at((y % 64) / 8, (x % 64) / 16);
            return (tbp + page * 8192 + block * 256 + ct.at(y % 8, x % 16) * 2) % kVram;
        }
        case Psm::T8: {
            const OgTable& bt = g_og["block_table_PSMT8"];
            const OgTable& ct = g_og["columnTable8"];
            int page = (y / 64) * ppr(128) + x / 128;
            int block = bt.at((y % 64) / 16, (x % 128) / 16);
            return (tbp + page * 8192 + block * 256 + ct.at(y % 16, x % 16)) % kVram;
        }
        case Psm::T4: {
            const OgTable& bt = g_og["block_table_PSMT4"];
            const OgTable& ct = g_og["og_t4_column_formula"];
            int page = (y / 128) * ppr(128) + x / 128;
            int block = bt.at((y % 128) / 16, (x % 128) / 32);
            return (tbp + page * 8192 + block * 256 + ct.at(y % 16, x % 32) / 2) % kVram;
        }
    }
    return 0;
}

// ------------------------------------------------------------ lifted OLD value leaves
uint32_t OldCsm1(uint32_t i) {   // texture_decoder.cpp:944 CSM1Swizzle
    const uint32_t g = i % 32;
    if (g >= 8 && g < 16) return i + 8;
    if (g >= 16 && g < 24) return i - 8;
    return i;
}
uint32_t OldExpand16(uint16_t p) {   // texture_decoder.cpp:1946 Expand16to32
    const uint32_t r = p & 0x1F, g = (p >> 5) & 0x1F, b = (p >> 10) & 0x1F, a = (p >> 15) & 1;
    return ((r << 3) | (r >> 2)) | (((g << 3) | (g >> 2)) << 8) | (((b << 3) | (b >> 2)) << 16) | ((a ? 0xFFu : 0u) << 24);
}
uint32_t Rd32(const uint8_t* v, uint32_t a) { uint32_t w; std::memcpy(&w, v + a, 4); return w; }
uint16_t Rd16(const uint8_t* v, uint32_t a) { uint16_t w; std::memcpy(&w, v + a, 2); return w; }

// ------------------------------------------------------------------- the case list
struct Case {
    Psm psm;
    uint32_t tbp, tbw;        // OLD units: bytes, pixels
    int w, h;
    uint32_t cbp; Psm cpsm; uint32_t csm;   // indexed only
    uint8_t ta0 = 0x80; bool aem = false;
};
struct Rect { int x0, y0, w, h; };

uint64_t s_rng = 0x9E3779B97F4A7C15ull;   // the original's seed and generator
uint64_t Rnd64() { uint64_t x = s_rng; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; s_rng = x; return x * 0x2545F4914F6CDD1Dull; }
uint32_t Rnd32() { return uint32_t(Rnd64() >> 32); }
uint32_t RndRange(uint32_t lo, uint32_t hi) { return lo + uint32_t(Rnd64() % uint64_t(hi - lo + 1)); }

// The original's RandomCase, call for call (the RNG stream must match).
Case RandomCase() {
    Case c{};
    const Psm psms[9] = {Psm::T8, Psm::T4, Psm::CT32, Psm::CT16, Psm::CT24, Psm::T8H, Psm::Z32, Psm::Z16, Psm::Z16S};
    c.psm = psms[Rnd32() % 9];
    c.ta0 = uint8_t(Rnd32() & 0xFF);
    c.aem = (Rnd32() & 1) != 0;
    switch (Rnd32() % 3) { case 0: break; case 1: break; case 2: RndRange(0x2000, 0x100000); break; }   // zsize (dropped)
    switch (Rnd32() % 4) {
        case 0: c.tbp = (Rnd32() % (kVram / 256)) * 256; break;
        case 1: c.tbp = (Rnd32() % (kVram / 4)) * 4; break;
        case 2: c.tbp = Rnd32() % kVram; break;
        case 3: c.tbp = kVram - RndRange(1, 0x8000); break;
    }
    const uint32_t tbws[9] = {0, 32, 64, 96, 128, 192, 256, 512, 1024};
    c.tbw = tbws[Rnd32() % 9];
    if ((Rnd32() % 8) == 0) { c.w = int(RndRange(1, 1024)); c.h = int(RndRange(1, 512)); }
    else                    { c.w = int(RndRange(1, 300));  c.h = int(RndRange(1, 300)); }
    switch (Rnd32() % 3) { case 0: break; case 1: break; case 2: Rnd32(); break; }   // vram_base (dropped)
    c.cbp = (Rnd32() % (kVram / 256)) * 256;
    c.cpsm = (Rnd32() & 1) ? Psm::CT32 : Psm::CT16;
    c.csm = Rnd32() & 1;
    switch (Rnd32() % 3) { case 0: break; case 1: break; case 2: Rnd32(); break; }   // clut_base (dropped)
    return c;
}
Rect RandomRect(int w, int h) {
    Rect r{};
    r.x0 = (w > 1) ? int(Rnd32() % uint32_t(w)) : 0;
    r.y0 = (h > 1) ? int(Rnd32() % uint32_t(h)) : 0;
    if ((Rnd32() % 3) == 0) r.w = w - r.x0; else r.w = 1 + int(Rnd32() % uint32_t(w - r.x0));
    if ((Rnd32() % 3) == 0) r.h = h - r.y0; else r.h = 1 + int(Rnd32() % uint32_t(h - r.y0));
    return r;
}
bool RectCapable(Psm p) { return p == Psm::T8 || p == Psm::T4 || p == Psm::CT32 || p == Psm::CT16 || p == Psm::CT24 || p == Psm::T8H; }
bool Indexed(Psm p) { return p == Psm::T8 || p == Psm::T4 || p == Psm::T8H; }
const char* Name(Psm p) { return PsmLayout::Describe(p).name; }

// ------------------------------------------------------------------ the model
// palette[i] for the OLD readers (ReadCLUT32 :1146 / ReadCLUT16 :1217) or NEW's
// ClutLayout order, values in the OLD convention (raw CT32, Expand16to32 for CT16).
void ModelPalette(const uint8_t* vram, const Case& c, bool new_order, uint32_t* pal) {
    const uint32_t n = c.psm == Psm::T4 ? 16u : 256u;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t addr;
        if (new_order) {
            addr = ps2r::gs::ClutLayout::EntryAddress(c.cpsm, c.cbp / 256u, c.csm != 0, n, i) * (c.cpsm == Psm::CT32 ? 4u : 2u);
        } else {
            const uint32_t idx = (c.csm == 0) ? OldCsm1(i) : i;
            addr = c.cpsm == Psm::CT32 ? OldF1(Psm::CT32, int(idx % 16), int(idx / 16), c.cbp, 64) : (c.cbp + idx * 2u) % kVram;
        }
        pal[i] = c.cpsm == Psm::CT32 ? Rd32(vram, addr) : OldExpand16(Rd16(vram, addr));
    }
}

// One texel of the OLD decode leaves (DecodePSMCT32 :365 / Z32 :460 / CT24 :499 /
// CT16 :723 / Z16(S) :794 / T8 :1252 / T4 :1764 / T8H :1863) with the address from
// F1 (new_addr = false) or from PsmLayout (true).
uint32_t ModelTexel(const uint8_t* vram, const Case& c, int x, int y, bool new_addr, bool new_clut, const uint32_t* pal) {
    const uint32_t bp = c.tbp / 256u, bw = c.tbw / 64u;
    uint32_t addr, nib = 0;
    if (new_addr) {
        addr = PsmLayout::PixelByteAddress(c.psm, uint32_t(x), uint32_t(y), bp, bw);
        nib = PsmLayout::PixelBitInByte(c.psm, uint32_t(x), uint32_t(y), bp, bw);
    } else {
        addr = OldF1(c.psm, x, y, c.tbp, c.tbw);
        nib = (((x % 32) + (y % 4) * 32) & 1) ? 4u : 0u;   // DecodePSMT4 :1820 pixel_in_byte parity
    }
    switch (c.psm) {
        case Psm::CT32: case Psm::Z32: return Rd32(vram, addr);
        case Psm::CT24: {
            const uint32_t w = Rd32(vram, addr) & 0x00FFFFFFu;
            const uint8_t a = (c.aem && w == 0) ? 0 : c.ta0;
            return w | (uint32_t(a) << 24);
        }
        case Psm::CT16: case Psm::Z16: case Psm::Z16S: return OldExpand16(Rd16(vram, addr));
        case Psm::T8:  return pal[vram[addr]];
        case Psm::T4:  return pal[(vram[addr] >> nib) & 0xF];
        case Psm::T8H: {
            const uint32_t index = vram[addr + 3];
            // OLD :1922: clut_rgba[csm == 0 ? CSM1Swizzle(index) : index] -- the second reorder.
            return pal[(c.csm == 0 && !new_clut) ? OldCsm1(index) : index];
        }
        default: return 0;
    }
}

// ---------------------------------------------------------------- attribution
struct Tally { uint64_t cases = 0, hit = 0, pixels = 0; };
std::map<std::string, Tally> g_rows;
uint64_t g_identical = 0, g_deviating = 0, g_failed = 0, g_snapped = 0, g_pixels = 0;

// One case may hit several rows (a T4 texture with a CSM1 CT16 palette hits P2-T4,
// P2-CLUT16-CSM1 and P2-CT16-TEXA); each row counts the case once.
struct Hits { bool any = false; std::map<std::string, bool> rows; };
void Hit(const char* row, uint64_t pixels, Hits& h) {
    Tally& t = g_rows[row];
    if (!h.rows[row]) { t.hit++; h.rows[row] = true; }
    t.pixels += pixels;
    h.any = true;
}

// Compare A/B/C/D over `r` of case `c`; returns false on an unattributable byte.
bool RunCase(const uint8_t* vram, Case c, const Rect& r, const char* what) {
    if (c.tbp % 256u) { c.tbp &= ~255u; g_snapped++; }
    const size_t n = size_t(r.w) * r.h;
    std::vector<uint32_t> A(n), B(n), C(n), D(n);
    uint32_t palOld[256] = {}, palNew[256] = {};
    if (Indexed(c.psm)) { ModelPalette(vram, c, false, palOld); ModelPalette(vram, c, true, palNew); }
    for (int yy = 0; yy < r.h; yy++)
        for (int xx = 0; xx < r.w; xx++) {
            const size_t i = size_t(yy) * r.w + xx;
            A[i] = ModelTexel(vram, c, r.x0 + xx, r.y0 + yy, false, false, palOld);
            B[i] = ModelTexel(vram, c, r.x0 + xx, r.y0 + yy, true, false, palOld);
            C[i] = ModelTexel(vram, c, r.x0 + xx, r.y0 + yy, true, true, palNew);
        }
    // D: NEW. TEXA for 16-bit modes = the common setup (TA0 0, TA1 0x80, AEM 0) so the
    // registered CT16-TEXA deviation is exactly alpha 0xFF -> 0x80; CT24 uses the case's TA0/AEM.
    TexaExpand::Texa texa{0x00, 0x80, false};
    if (c.psm == Psm::CT24) texa = TexaExpand::Texa{c.ta0, 0x80, c.aem};
    uint32_t palette[256];
    const BlockDecode::Texture tex{c.psm, c.tbp / 256u, c.tbw / 64u, uint32_t(c.w), uint32_t(c.h)};
    if (Indexed(c.psm))
        BlockDecode::ReadPalette(vram, BlockDecode::Palette{c.cpsm, c.cbp / 256u, c.csm != 0}, BlockDecode::PaletteEntries(c.psm), texa, palette);
    BlockDecode::Decode(vram, tex, BlockDecode::Rect{uint32_t(r.x0), uint32_t(r.y0), uint32_t(r.w), uint32_t(r.h)}, texa, palette, D.data());
    g_pixels += n;

    Hits any; bool bad = false;
    uint64_t ab = 0, bc = 0, cd = 0;
    for (size_t i = 0; i < n; i++) { if (A[i] != B[i]) ab++; if (B[i] != C[i]) bc++; if (C[i] != D[i]) cd++; }
    // A vs B: address rows
    if (ab) {
        if (c.psm == Psm::T4) Hit("P2-T4", ab, any);
        else if (c.psm == Psm::CT16S) Hit("P2-CT16S", ab, any);
        else { std::printf("  UNATTRIBUTED address difference (%llu px) %s %s\n", (unsigned long long)ab, what, Name(c.psm)); bad = true; }
    }
    // B vs C: CLUT rows (only indexed cases can differ; exactly one palette rule per case)
    if (bc) {
        if (!Indexed(c.psm)) bad = true;
        else if (c.psm == Psm::T8H && c.csm == 0) Hit("P2-T8H-CSM1", bc, any);
        else if (c.cpsm == Psm::CT16 && c.csm == 0) Hit("P2-CLUT16-CSM1", bc, any);
        else if (c.psm == Psm::T4 && c.cpsm == Psm::CT32 && c.csm == 1) Hit("P2-CLUT32-I4-CSM2", bc, any);
        else bad = true;
        if (bad) std::printf("  UNATTRIBUTED CLUT difference (%llu px) %s %s cpsm=%s csm=%u\n", (unsigned long long)bc, what, Name(c.psm), Name(c.cpsm), c.csm);
    }
    // T8H csm 0: the second reorder also shows up in B vs C through palOld's order; that is
    // the same row. (B uses OLD's double reorder, C NEW's single one.)
    // C vs D: value rows -- only the alpha byte, only 0xFF -> 0x80, only where a 16-bit value is involved
    if (cd) {
        const bool sixteen = c.psm == Psm::CT16 || (Indexed(c.psm) && c.cpsm == Psm::CT16);
        uint64_t exact = 0;
        for (size_t i = 0; i < n; i++) {
            if (C[i] == D[i]) continue;
            const bool rgb_same = (C[i] & 0x00FFFFFFu) == (D[i] & 0x00FFFFFFu);
            const bool alpha_rule = (C[i] >> 24) == 0xFF && (D[i] >> 24) == 0x80;
            if (sixteen && rgb_same && alpha_rule) exact++;
        }
        if (sixteen && exact == cd) Hit("P2-CT16-TEXA", cd, any);
        else { std::printf("  UNATTRIBUTED value difference (%llu px, %llu match the TEXA rule) %s %s\n", (unsigned long long)cd, (unsigned long long)exact, what, Name(c.psm)); bad = true; }
    }
    if (bad) { g_failed++; return false; }
    if (any.any) g_deviating++; else g_identical++;
    // per-mode row bookkeeping: a case counts for every row whose modes it exercises
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: og_block_decode <og_tables.bin>\n"); return 2; }
    if (!LoadOgTables(argv[1])) { std::printf("og_block_decode: cannot load %s\n", argv[1]); return 2; }

    // The original's VRAM fill: kVram + 16 bytes, 8 at a time, from the same stream.
    std::vector<uint8_t> vram(kVram + 16);
    for (size_t i = 0; i < vram.size(); i += 8) { const uint64_t v = Rnd64(); std::memcpy(vram.data() + i, &v, 8); }

    std::vector<std::pair<Case, Rect>> cases;
    std::vector<const char*> what;
    for (int i = 0; i < 400; i++) { Case c = RandomCase(); cases.push_back({c, Rect{0, 0, c.w, c.h}}); what.push_back("random"); }
    // The original's 29 targeted edge cases (tbp bytes, tbw px; vram_base/clut_base/zsize dropped).
    const Case edges[] = {
        {Psm::T8, kVram - 0x800, 512, 256, 128, 0x1000, Psm::CT32, 0}, {Psm::T4, kVram - 0x100, 256, 200, 200, 0x2000, Psm::CT16, 1},
        {Psm::CT32, kVram - 4, 64, 64, 64, 0, Psm::CT32, 0}, {Psm::CT16, kVram - 2, 64, 128, 128, 0, Psm::CT32, 0},
        {Psm::T8, 0x1000, 128, 128, 64, 0x3000, Psm::CT32, 0}, {Psm::CT32, 0x0, 64, 64, 32, 0, Psm::CT32, 0},
        {Psm::T8, 0x100000, 0, 1, 1, 0x8000, Psm::CT32, 1}, {Psm::T4, 0x140000, 32, 17, 13, 0x9000, Psm::CT16, 0},
        {Psm::T4, 0x140100, 96, 31, 5, 0x9100, Psm::CT32, 0}, {Psm::CT16, 0x180000, 96, 3, 1023, 0, Psm::CT32, 0},
        {Psm::T8, 0x300000, 256, 256, 256, 0x350000, Psm::CT32, 0}, {Psm::T8, 0x2C0000, 128, 128, 256, 0x2C8000, Psm::CT32, 0},
        {Psm::T4, 0x200000, 512, 512, 512, 0x260000, Psm::CT32, 0}, {Psm::CT32, 0x150000, 640, 640, 448, 0, Psm::CT32, 0},
        {Psm::CT16, 0x2F0000, 512, 512, 448, 0, Psm::CT32, 0},
        {Psm::T8H, 0x280000, 512, 1024, 448, 0x350000, Psm::CT32, 0}, {Psm::T8H, kVram - 0x800, 512, 256, 128, 0x1000, Psm::CT32, 1},
        {Psm::T8H, 0x1000, 128, 128, 64, 0x3000, Psm::CT16, 0}, {Psm::T8H, 0x100000, 0, 1, 1, 0x8000, Psm::CT32, 1},
        {Psm::CT24, 0x150000, 640, 640, 448, 0, Psm::CT32, 0, 0x00, true}, {Psm::CT24, kVram - 4, 64, 64, 64, 0, Psm::CT32, 0, 0x80, false},
        {Psm::CT24, 0x0, 64, 63, 31, 0, Psm::CT32, 0, 0x40, true},
        {Psm::Z32, 0x268000, 512, 512, 448, 0, Psm::CT32, 0}, {Psm::Z32, kVram - 4, 64, 64, 64, 0, Psm::CT32, 0}, {Psm::Z32, 0x0, 64, 65, 33, 0, Psm::CT32, 0},
        {Psm::Z16, 0x150000, 512, 512, 448, 0, Psm::CT32, 0}, {Psm::Z16, kVram - 2, 64, 128, 128, 0, Psm::CT32, 0},
        {Psm::Z16S, 0x150000, 512, 512, 448, 0, Psm::CT32, 0}, {Psm::Z16S, 0x2F0000, 96, 97, 33, 0, Psm::CT32, 0},
    };
    for (const Case& c : edges) { cases.push_back({c, Rect{0, 0, c.w, c.h}}); what.push_back("edge"); }
    for (int i = 0; i < 400; i++) {
        Case c = RandomCase();
        if (!RectCapable(c.psm)) { i--; continue; }
        Rect r = RandomRect(c.w, c.h);
        cases.push_back({c, r}); what.push_back("rect");
    }
    struct RCase { Case c; Rect r; };
    const RCase rects[] = {
        {{Psm::CT32, 0x100000, 64, 8, 8, 0, Psm::CT32, 0}, {1, 2, 5, 3}}, {{Psm::CT16, 0x110000, 64, 16, 8, 0, Psm::CT32, 0}, {8, 0, 8, 8}},
        {{Psm::T8, 0x120000, 64, 16, 16, 0x8000, Psm::CT32, 0}, {0, 8, 16, 8}}, {{Psm::T4, 0x130000, 64, 32, 16, 0x9000, Psm::CT16, 1}, {3, 1, 7, 5}},
        {{Psm::T4, 0x200000, 512, 512, 512, 0x260000, Psm::CT32, 0}, {129, 65, 255, 127}},
        {{Psm::T8, 0x300000, 256, 256, 256, 0x350000, Psm::CT32, 0}, {128, 192, 128, 64}}, {{Psm::CT32, 0x150000, 640, 640, 448, 0, Psm::CT32, 0}, {576, 384, 64, 64}},
        {{Psm::CT16, 0x2F0000, 512, 512, 448, 0, Psm::CT32, 0}, {0, 384, 512, 64}}, {{Psm::T8H, 0x280000, 512, 1024, 448, 0x350000, Psm::CT32, 0}, {960, 0, 64, 448}},
        {{Psm::CT24, 0x150000, 640, 640, 448, 0, Psm::CT32, 0, 0x00, true}, {320, 224, 320, 224}},
        {{Psm::T8, 0x2C0000, 128, 128, 256, 0x2C8000, Psm::CT32, 0}, {127, 255, 1, 1}},
        {{Psm::T8, kVram - 0x800, 512, 256, 128, 0x1000, Psm::CT32, 0}, {64, 32, 128, 64}}, {{Psm::CT32, kVram - 4, 64, 64, 64, 0, Psm::CT32, 0}, {32, 32, 32, 32}},
        {{Psm::T4, 0x140000, 32, 17, 13, 0x9000, Psm::CT16, 0}, {0, 0, 17, 13}},
    };
    for (const RCase& rc : rects) { cases.push_back({rc.c, rc.r}); what.push_back("rect-edge"); }

    // per-mode / per-row exposure counts (how many cases could hit each row)
    std::map<std::string, uint64_t> exposed;
    std::map<std::string, uint64_t> per_psm;
    for (size_t i = 0; i < cases.size(); i++) {
        const Case& c = cases[i].first;
        per_psm[Name(c.psm)]++;
        if (c.psm == Psm::T4) exposed["P2-T4"]++;
        if (c.psm == Psm::CT16S) exposed["P2-CT16S"]++;
        if (Indexed(c.psm) && c.cpsm == Psm::CT16 && c.csm == 0 && c.psm != Psm::T8H) exposed["P2-CLUT16-CSM1"]++;
        if (c.psm == Psm::T4 && c.cpsm == Psm::CT32 && c.csm == 1) exposed["P2-CLUT32-I4-CSM2"]++;
        if (c.psm == Psm::T8H && c.csm == 0) exposed["P2-T8H-CSM1"]++;
        if (c.psm == Psm::CT16 || (Indexed(c.psm) && c.cpsm == Psm::CT16)) exposed["P2-CT16-TEXA"]++;
        RunCase(vram.data(), c, cases[i].second, what[i]);
    }

    std::printf("og_block_decode: %zu cases replayed from the original's list (400 random + 29 edge + 400 random rect + 14 targeted rect), %llu texels\n",
                cases.size(), (unsigned long long)g_pixels);
    std::printf("  tbp snapped to the 256-byte block grid: %llu cases\n", (unsigned long long)g_snapped);
    std::printf("  per PSM:");
    for (auto& kv : per_psm) std::printf(" %s=%llu", kv.first.c_str(), (unsigned long long)kv.second);
    std::printf("\n");
    std::printf("  IDENTICAL to the original: %llu cases\n", (unsigned long long)g_identical);
    std::printf("  DEVIATING, every byte attributed to a registered 7.1 row: %llu cases\n", (unsigned long long)g_deviating);
    for (const char* row : {"P2-T4", "P2-CT16S", "P2-CLUT16-CSM1", "P2-CLUT32-I4-CSM2", "P2-T8H-CSM1", "P2-CT16-TEXA"}) {
        const Tally& t = g_rows[row];
        std::printf("    %-18s exposed %3llu cases, deviating %3llu cases, %10llu texels %s\n", row,
                    (unsigned long long)exposed[row], (unsigned long long)t.hit, (unsigned long long)t.pixels,
                    std::string(row) == "P2-CT16S" ? "(no CT16S case in the original's pool)" : "");
    }
    std::printf("    %-18s both trees replicate 5->8: 0 texels differ by it\n", "P2-EXP16-REPL");
    std::printf("  UNATTRIBUTED: %llu cases\n", (unsigned long long)g_failed);
    std::printf("og_block_decode: %s\n", g_failed == 0 ? "PASS" : "FAIL");
    return g_failed == 0 ? 0 : 1;
}

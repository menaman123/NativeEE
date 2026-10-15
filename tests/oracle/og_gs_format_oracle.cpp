// tests/oracle/og_gs_format_oracle.cpp -- differential oracle: PsmLayout vs the
// frozen ORIGINAL's address families, per PSM (PHASE2_PLAN section 4).
//
//   F1: OLD texture_decoder.cpp GetAddressPSM* -- re-evaluated here from the LIFTED
//       tables (og_tables.bin, lift_og_tables.py) with OLD's own index expressions
//       (tbp bytes, tbw pixels, byte result % 4 MB). OLD aliases: CT24 -> CT32,
//       Z24 -> Z32, CT16S -> CT16 (gs_state.cpp:4534/:5110/:5362), T8H/T4HL/T4HH -> CT32.
//   F3/F4: the transfer write side and the RT encode side, loop structure lifted
//       (see RunRectFamily); CSM1 x3 + csm1_unswizzle + ReadCLUT32/16 (RunClut).
//   F2: OLD host_app/gs_deswizzle.h addr_psmct32/addr_psmct16/addr_psmt8, included
//       READ-ONLY by absolute path when the OLD tree is present (bw pixels, no base).
//   M : the GS manual's six-step formula (GS_SKILL section 8.4) spelled out here
//       independently of PsmLayout over re-typed section 8.3 tables. NEW must equal M
//       for every sample (a construction bug otherwise); OLD deviations from M/NEW are
//       REPORTED, never hidden, and never fail the test (accepted-difference candidates).
//
// Usage: og_gs_format_oracle <og_tables.bin> <PSM name | all>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "engine/gs/format/clut_layout.h"
#include "engine/gs/format/pixel_convert.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/swizzle.h"
#include "engine/gs/format/texa_expand.h"

#if defined(PS2R_OG_DESWIZZLE_H)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include PS2R_OG_DESWIZZLE_H
#pragma GCC diagnostic pop
#define OG_HAVE_F2 1
#else
#define OG_HAVE_F2 0
#endif

using ps2r::gs::Psm;
using ps2r::gs::PsmClass;
using ps2r::gs::PsmDescriptor;
using ps2r::gs::PsmLayout;

namespace {

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

// OLD F1 (texture_decoder.cpp GetAddressPSM*): tbp bytes, tbw pixels -> byte address.
// Returns false when OLD has no F1 variant for the mode (never: every mode aliases).
uint32_t OldF1(Psm psm, int x, int y, uint32_t tbp, uint32_t tbw) {
    const uint32_t kVram = 4u * 1024u * 1024u;
    auto ppr = [&](int page_w) { int p = (tbw > 0) ? int(tbw) / page_w : 1; return p == 0 ? 1 : p; };
    switch (psm) {
        case Psm::CT32: case Psm::CT24: case Psm::T8H: case Psm::T4HL: case Psm::T4HH:
        case Psm::Z32: case Psm::Z24: {
            const OgTable& bt = g_og[(psm == Psm::Z32 || psm == Psm::Z24) ? "block_table_PSMZ32" : "block_table_PSMCT32"];
            const OgTable& ct = g_og["columnTable32"];
            int page = (y / 32) * ppr(64) + x / 64;
            int block = bt.at((y % 32) / 8, (x % 64) / 8);
            uint32_t addr = page * 8192 + block * 256 + ct.at(y % 8, x % 8) * 4;
            return (tbp + addr) % kVram;
        }
        case Psm::CT16: case Psm::CT16S: case Psm::Z16: case Psm::Z16S: {
            const char* name = psm == Psm::Z16 ? "block_table_PSMZ16" : psm == Psm::Z16S ? "block_table_PSMZ16S" : "block_table_PSMCT16";
            const OgTable& bt = g_og[name];
            const OgTable& ct = g_og["columnTable16"];
            int page = (y / 64) * ppr(64) + x / 64;
            int block = bt.at((y % 64) / 8, (x % 64) / 16);
            uint32_t addr = page * 8192 + block * 256 + ct.at(y % 8, x % 16) * 2;
            return (tbp + addr) % kVram;
        }
        case Psm::T8: {
            const OgTable& bt = g_og["block_table_PSMT8"];
            const OgTable& ct = g_og["columnTable8"];
            int page = (y / 64) * ppr(128) + x / 128;
            int block = bt.at((y % 64) / 16, (x % 128) / 16);
            uint32_t addr = page * 8192 + block * 256 + ct.at(y % 16, x % 16);
            return (tbp + addr) % kVram;
        }
        case Psm::T4: {
            const OgTable& bt = g_og["block_table_PSMT4"];
            const OgTable& ct = g_og["og_t4_column_formula"];  // nibble units in block
            int page = (y / 128) * ppr(128) + x / 128;
            int block = bt.at((y % 128) / 16, (x % 128) / 32);
            uint32_t addr = page * 8192 + block * 256 + ct.at(y % 16, x % 32) / 2;
            return (tbp + addr) % kVram;
        }
    }
    return 0;
}

// OLD F2 (gs_deswizzle.h): bw pixels, page-relative byte offset; CT32/CT16/T8 only.
bool OldF2(Psm psm, int x, int y, int bw_px, uint32_t* out) {
#if OG_HAVE_F2
    switch (psm) {
        case Psm::CT32: *out = gs_deswizzle::addr_psmct32(x, y, bw_px); return true;
        case Psm::CT16: *out = gs_deswizzle::addr_psmct16(x, y, bw_px); return true;
        case Psm::T8:   *out = gs_deswizzle::addr_psmt8(x, y, bw_px);   return true;
        default: return false;
    }
#else
    (void)psm; (void)x; (void)y; (void)bw_px; (void)out;
    return false;
#endif
}

// ------------------------------------------------- M: the manual, spelled out again
// Section 8.3 block tables, re-typed from the GS_SKILL figures (independent copy).
const uint8_t kM_B32[4][8]  = {{0,1,4,5,16,17,20,21},{2,3,6,7,18,19,22,23},{8,9,12,13,24,25,28,29},{10,11,14,15,26,27,30,31}};
const uint8_t kM_BZ32[4][8] = {{24,25,28,29,8,9,12,13},{26,27,30,31,10,11,14,15},{16,17,20,21,0,1,4,5},{18,19,22,23,2,3,6,7}};
const uint8_t kM_B16[8][4]  = {{0,2,8,10},{1,3,9,11},{4,6,12,14},{5,7,13,15},{16,18,24,26},{17,19,25,27},{20,22,28,30},{21,23,29,31}};
const uint8_t kM_BZ16[8][4] = {{24,26,16,18},{25,27,17,19},{28,30,20,22},{29,31,21,23},{8,10,0,2},{9,11,1,3},{12,14,4,6},{13,15,5,7}};
const uint8_t kM_B16S[8][4] = {{0,2,16,18},{1,3,17,19},{8,10,24,26},{9,11,25,27},{4,6,20,22},{5,7,21,23},{12,14,28,30},{13,15,29,31}};
const uint8_t kM_BZ16S[8][4]= {{24,26,8,10},{25,27,9,11},{16,18,0,2},{17,19,1,3},{28,30,12,14},{29,31,13,15},{20,22,4,6},{21,23,5,7}};
// Section 8.3.1 column word order, and the section 8.3.3/8.3.4 rotated order for rows 2-3.
const uint8_t kM_W[2][8]    = {{0,1,4,5,8,9,12,13},{2,3,6,7,10,11,14,15}};
const uint8_t kM_WR[2][8]   = {{8,9,12,13,0,1,4,5},{10,11,14,15,2,3,6,7}};

int ManualBlock(Psm psm, int bx, int by) {
    switch (psm) {
        case Psm::CT32: case Psm::CT24: case Psm::T8H: case Psm::T4HL: case Psm::T4HH: case Psm::T8: return kM_B32[by][bx];
        case Psm::Z32: case Psm::Z24: return kM_BZ32[by][bx];
        case Psm::CT16: case Psm::T4: return kM_B16[by][bx];
        case Psm::CT16S: return kM_B16S[by][bx];
        case Psm::Z16: return kM_BZ16[by][bx];
        case Psm::Z16S: return kM_BZ16S[by][bx];
    }
    return 0;
}

// Section 8.4 steps 1-6 -> BIT address (so every class compares in one unit), then
// the 4 MB wrap (bits). bp in blocks, bw in 64-pixel units.
uint64_t ManualBitAddress(const PsmDescriptor& d, int x, int y, uint32_t bp, uint32_t bw) {
    int ppr = int(bw * 64) / d.page_w; if (ppr == 0) ppr = 1;
    uint64_t page = uint64_t(y / d.page_h) * ppr + x / d.page_w;
    int lx = x % d.page_w, ly = y % d.page_h;
    int block = ManualBlock(d.psm, lx / d.block_w, ly / d.block_h);
    int blx = lx % d.block_w, bly = ly % d.block_h;
    int column = bly / d.column_h, cy = bly % d.column_h;
    uint64_t bit_in_column;
    switch (d.cls) {
        case PsmClass::Bits32: bit_in_column = uint64_t(kM_W[cy][blx]) * 32; break;
        case PsmClass::Bits16: bit_in_column = uint64_t(kM_W[cy][blx % 8]) * 32 + (blx / 8) * 16; break;
        case PsmClass::Bits8: {
            bool rot = (cy >= 2) != ((column & 1) != 0);
            int w = rot ? kM_WR[cy & 1][blx % 8] : kM_W[cy & 1][blx % 8];
            int byte = (cy >= 2 ? 1 : 0) + 2 * (blx / 8);
            bit_in_column = uint64_t(w) * 32 + byte * 8; break;
        }
        default: {  // Bits4
            bool rot = (cy >= 2) != ((column & 1) != 0);
            int w = rot ? kM_WR[cy & 1][blx % 8] : kM_W[cy & 1][blx % 8];
            int nib = (cy >= 2 ? 1 : 0) + 2 * (blx / 8);
            bit_in_column = uint64_t(w) * 32 + nib * 4; break;
        }
    }
    uint64_t bits = (uint64_t(bp) * 64 + page * 2048 + uint64_t(block) * 64 + uint64_t(column) * 16) * 32 + bit_in_column;
    return bits % (uint64_t(4u * 1024u * 1024u) * 8u);
}

// ------------------------------------------------------------------ the grid run
struct Dev { uint64_t count = 0; bool have_first = false; int x = 0, y = 0; uint32_t bp = 0, bw = 0; uint32_t got = 0, want = 0;
             void hit(int px, int py, uint32_t pbp, uint32_t pbw, uint32_t g, uint32_t w) { if (!have_first) { have_first = true; x = px; y = py; bp = pbp; bw = pbw; got = g; want = w; } count++; } };

const uint32_t kBw[] = {1, 2, 3, 4, 5, 7, 8, 10, 16, 20, 32};
// bp samples (blocks): 0, block 1, block 9 (section 8.5 figures), mid, the two OLD
// test_page_invariance bases (0x330000 / 0x3c0000 bytes), and the top three pages.
const uint32_t kBp[] = {0, 1, 9, 256 * 32, 0x330000 / 256, 0x3c0000 / 256, 509 * 32, 510 * 32 + 5, 511 * 32};

int RunPsm(const PsmDescriptor& d) {
    Dev vs_manual, vs_f1, vs_f2;
    uint64_t samples = 0;
    bool f2_covers = false;
    for (uint32_t bw : kBw) {
        for (uint32_t bp : kBp) {
            const uint32_t tbp = bp * 256, tbw = bw * 64;
            for (int y = 0; y < 2 * d.page_h; y++) {
                for (int x = 0; x < int(tbw); x++) {
                    samples++;
                    const uint32_t nb = PsmLayout::PixelByteAddress(d.psm, x, y, bp, bw);
                    const uint32_t nbit = PsmLayout::PixelBitInByte(d.psm, x, y, bp, bw);
                    const uint64_t mbits = ManualBitAddress(d, x, y, bp, bw);
                    if (uint64_t(nb) * 8 + nbit != mbits) vs_manual.hit(x, y, bp, bw, nb, uint32_t(mbits / 8));
                    const uint32_t f1 = OldF1(d.psm, x, y, tbp, tbw);
                    if (f1 != nb) vs_f1.hit(x, y, bp, bw, nb, f1);
                    uint32_t f2;
                    if (bp == 0 && OldF2(d.psm, x, y, int(tbw), &f2)) {
                        f2_covers = true;
                        if (f2 != nb) vs_f2.hit(x, y, bp, bw, nb, f2);
                    }
                }
            }
        }
    }
    auto report = [&](const char* fam, const Dev& dv, bool covers) {
        if (!covers) { std::printf("  %-8s %-3s n/a (OLD family does not cover this PSM)\n", d.name, fam); return; }
        if (dv.count == 0) std::printf("  %-8s %-3s IDENTICAL\n", d.name, fam);
        else std::printf("  %-8s %-3s DEVIATION(count=%llu, first (x=%d,y=%d,bp=%u,bw=%u) NEW=0x%06x OLD=0x%06x)\n",
                         d.name, fam, (unsigned long long)dv.count, dv.x, dv.y, dv.bp, dv.bw, dv.got, dv.want);
    };
    std::printf("%s: %llu samples (bw x bp x 2 pages)\n", d.name, (unsigned long long)samples);
    if (vs_manual.count == 0) std::printf("  %-8s M   NEW == manual\n", d.name);
    else std::printf("  %-8s M   FAIL NEW != manual (count=%llu, first (x=%d,y=%d,bp=%u,bw=%u) NEW=0x%06x M=0x%06x)\n",
                     d.name, (unsigned long long)vs_manual.count, vs_manual.x, vs_manual.y, vs_manual.bp, vs_manual.bw, vs_manual.got, vs_manual.want);
    report("F1", vs_f1, true);
    report("F2", vs_f2, f2_covers);
    if (!OG_HAVE_F2 && (d.psm == Psm::CT32 || d.psm == Psm::CT16 || d.psm == Psm::T8))
        std::printf("  %-8s F2  SKIP (OLD gs_deswizzle.h not available at compile time)\n", d.name);
    if (vs_f1.count || vs_f2.count)
        std::printf("  %-8s accepted-difference CANDIDATE: OLD deviates from the GS manual here (EQUIVALENCE_PLAN 7.1; Phase 6 frame lane arbitrates)\n", d.name);
    return vs_manual.count == 0 ? 0 : 1;
}

// ------------------------------------------------- F3 / F4: the rect families
// OLD F3 (gs_state.cpp WriteHWReg, legacy loop + PS2_XFER_FAST word path) and OLD
// F4 (opengl_backend.cpp EncodeRTToVRAM / SyncZToVRAM) are member functions with
// GL / GSState closures, so their loop structure is LIFTED here: one OLD F1 address
// per pixel of the rect through each family's own PSM dispatch, then bpp byte
// writes at the byte address (bounds `addr + bpp <= 4 MB`, OLD's checks).
//   F3 dispatch (gs_state.cpp:5108-5124 / :5362-5372): CT32,CT24 -> CT32 helper;
//   CT16,CT16S -> CT16 helper; T8 -> T8; T4 -> address computed but NO write case
//   (:5160-5169 / :5436-5445 `default: break`) -- reported, never modeled as a write.
//   F4 dispatch (:15996 CT32/CT24, :16017 CT16, :18396 Z32/Z24, :18385 Z16/Z16S).
// CT24/Z24 write 3 bytes (F4 "byte 3 untouched"; F3's 0x80 quirk is a value-level
// candidate, EQUIVALENCE_PLAN 7.1 P2-CT24-HWREG-A, not an address fact).
//   Family 'L' = the F3 local->local copy (gs_state.cpp PerformLocalToLocalTransfer,
//   TRXDIR=2): its readPixel32/writePixel closures (:4527-4545 / :4586-4599) route
//   Z32/Z24 through the CT32 helper (:4539-4541 / :4591-4593) and Z16/Z16S through
//   the CT16 helper (:4543-4545 / :4594-4596) -- the Z block tables (F4 SyncZToVRAM
//   uses GetAddressPSMZ32/Z16/Z16S) are never consulted; CT16S -> CT16 as in F3.
//   OLD's own census note at :4504 records that the game never issues TRXDIR=2, so
//   the deviation is counted here (EQUIVALENCE_PLAN 7.1 P2-Z-L2L), not observable.
// NEW: Swizzle::LinearToVram over the same rect with the same linear source.
struct RectCase { Psm psm; uint32_t bp, bw, x, y, w, h; const char* what; };

bool OldFamilyCovers(char fam, Psm psm) {
    switch (psm) {
        case Psm::CT32: case Psm::CT24: case Psm::CT16: return fam != 'L';
        case Psm::CT16S: return fam == '3' || fam == 'L';
        case Psm::T8: return fam == '3';
        case Psm::Z32: case Psm::Z24: case Psm::Z16: case Psm::Z16S: return fam == '4' || fam == 'L';
        default: return false;
    }
}

// Byte address OLD's family computes for the pixel (its own aliasing).
uint32_t OldRectAddr(char fam, Psm psm, int x, int y, uint32_t tbp, uint32_t tbw) {
    Psm alias = psm;
    if (fam == '3' && psm == Psm::CT16S) alias = Psm::CT16;  // gs_state.cpp:5110 "use same swizzle for now"
    if (fam == 'L') {
        switch (psm) {                                          // PerformLocalToLocalTransfer writePixel :4586-4599
            case Psm::CT16S: case Psm::Z16: case Psm::Z16S: alias = Psm::CT16; break;
            case Psm::Z32: case Psm::Z24: alias = Psm::CT32; break;
            default: break;
        }
    }
    return OldF1(alias, x, y, tbp, tbw);
}

uint32_t BytesPerPixel(Psm psm) {
    switch (psm) {
        case Psm::CT32: case Psm::Z32: return 4;
        case Psm::CT24: case Psm::Z24: return 3;   // upper byte untouched
        case Psm::CT16: case Psm::CT16S: case Psm::Z16: case Psm::Z16S: return 2;
        default: return 1;
    }
}

int RunRectFamily(char fam) {
    const RectCase cases[] = {
        {Psm::CT32,  0,      1, 0,   0,   64,  32,  "one CT32 page (HWREG upload / RT page)"},
        {Psm::CT32,  0x3300, 1, 0,   0,   16,  16,  "16x16 CT32 at the page-invariance base"},
        {Psm::CT32,  0x3308, 1, 0,   0,   16,  16,  "16x16 CT32 CLUT rectangle at cbp"},
        {Psm::CT32,  0,      1, 5,   3,   7,   1,   "1-row segment at an odd offset (per-qword F3 role)"},
        {Psm::CT32,  0x400,  8, 0,   0,   512, 448, "512x448 CT32 frame buffer, fbw 8"},
        {Psm::CT24,  0x400,  8, 0,   0,   512, 448, "512x448 CT24 frame buffer"},
        {Psm::CT24,  9,      2, 3,   1,   61,  9,   "CT24 window straddling blocks, base block 9"},
        {Psm::CT16,  0x2f00, 10, 0,  0,   640, 224, "640x224 CT16 buffer at 0x2f0000"},
        {Psm::CT16,  0,      1, 17,  33,  30,  2,   "CT16 2-row window"},
        {Psm::CT16S, 0x2f00, 10, 0,  0,   640, 64,  "CT16S buffer (OLD aliases to CT16)"},
        {Psm::T8,    0x3300, 1, 0,   0,   16,  16,  "16x16 T8 tile (page-invariance)"},
        {Psm::T8,    0x3302, 1, 0,   0,   8,   8,   "8x8 T8 L1 at +2 blocks"},
        {Psm::T8,    0,      4, 0,   0,   256, 256, "256x256 T8 atlas, tbw 256"},
        {Psm::T8,    0,      2, 9,   5,   100, 1,   "T8 1-row segment crossing a page"},
        {Psm::Z32,   0x1000, 8, 0,   0,   512, 448, "512x448 Z32 at zbp"},
        {Psm::Z24,   0x1000, 8, 0,   0,   512, 448, "512x448 Z24"},
        {Psm::Z16,   0x1000, 8, 0,   0,   512, 448, "512x448 Z16"},
        {Psm::Z16S,  0x1000, 8, 0,   0,   512, 448, "512x448 Z16S"},
        {Psm::CT32,  511 * 32 + 3, 4, 0, 0, 256, 33, "CT32 rect running past the 4 MB (wrap)"},
        {Psm::CT16,  511 * 32, 1, 0, 0,   64,  64,  "CT16 last page"},
    };
    int rc = 0;
    std::vector<uint8_t> vn(4u * 1024u * 1024u), vo(4u * 1024u * 1024u);
    std::printf("F%c rect family (OLD loop lifted, per-pixel F1 dispatch):\n", fam);
    for (const RectCase& c : cases) {
        if (!OldFamilyCovers(fam, c.psm)) continue;
        const uint32_t bpp = BytesPerPixel(c.psm);
        const uint32_t lin_bpp = ps2r::gs::Swizzle::LinearBitsPerPixel(c.psm) / 8u;
        const size_t pitch = size_t(c.w) * lin_bpp;
        std::vector<uint8_t> src(pitch * c.h);
        uint32_t s = 0x12345u;
        for (auto& b : src) { s = s * 1664525u + 1013904223u; b = uint8_t(s >> 24); }
        std::fill(vn.begin(), vn.end(), 0); std::fill(vo.begin(), vo.end(), 0);
        ps2r::gs::Swizzle::LinearToVram(vn.data(), {c.psm, c.bp, c.bw}, {c.x, c.y, c.w, c.h}, src.data(), pitch);
        const uint32_t tbp = c.bp * 256u, tbw = c.bw * 64u;
        for (uint32_t j = 0; j < c.h; j++)
            for (uint32_t i = 0; i < c.w; i++) {
                const uint32_t addr = OldRectAddr(fam, c.psm, int(c.x + i), int(c.y + j), tbp, tbw);
                if (addr + bpp > vo.size()) continue;  // OLD's bounds check (never true: % VRAM)
                const uint8_t* p = src.data() + j * pitch + size_t(i) * lin_bpp;
                for (uint32_t k = 0; k < bpp; k++) vo[addr + k] = p[k];
            }
        uint64_t diff = 0; uint32_t first = 0; bool have = false;
        for (size_t a = 0; a < vn.size(); a++) if (vn[a] != vo[a]) { if (!have) { have = true; first = uint32_t(a); } diff++; }
        const PsmDescriptor& d = PsmLayout::Describe(c.psm);
        if (diff == 0) std::printf("  %-8s F%c IDENTICAL   %s\n", d.name, fam, c.what);
        else std::printf("  %-8s F%c DEVIATION(bytes=%llu, first addr=0x%06x NEW=%02x OLD=%02x)   %s%s\n", d.name, fam,
                         (unsigned long long)diff, first, vn[first], vo[first], c.what,
                         c.psm == Psm::CT16S ? "  [P2-CT16S, by design]"
                         : (fam == 'L' && d.is_z) ? "  [P2-Z-L2L: OLD local->local routes Z through the colour helper; never issued by the game]"
                         : "");
    }
    if (fam == 'L')
        std::printf("  (F%c = PerformLocalToLocalTransfer writePixel dispatch, gs_state.cpp:4586-4599; census :4504: TRXDIR=2 never issued)\n", fam);
    if (fam == '3')
        std::printf("  PSMT4    F3 NO-WRITE: OLD WriteHWReg computes the T4 address but has no 0x14 store case "
                    "(gs_state.cpp:5160-5169, :5436-5445) -- T4 uploads never reach OLD's VRAM; candidate P2-T4-HWREG\n");
    return rc;
}

// ------------------------------------------------------------- CSM1 x3 + CLUT
// The three OLD copies, lifted verbatim (each cited), plus gs_deswizzle.h's
// row-order variant called through the read-only include when present.
uint32_t OldCsm1_TextureDecoder(uint32_t index) {   // texture_decoder.cpp:944 CSM1Swizzle
    uint32_t within_group = index % 32;
    if (within_group >= 8 && within_group < 16) return index + 8;
    else if (within_group >= 16 && within_group < 24) return index - 8;
    return index;
}
uint32_t OldCsm1_OpenGlBackend(uint32_t index) {    // opengl_backend.cpp:21667 R164Csm1Swizzle
    uint32_t within_group = index % 32;
    if (within_group >= 8 && within_group < 16) return index + 8;
    if (within_group >= 16 && within_group < 24) return index - 8;
    return index;
}
uint32_t OldCsm1_S9Composite(uint32_t index) {      // s9_composite.h:322 Csm1Swizzle
    uint32_t within_group = index % 32;
    if (within_group >= 8 && within_group < 16) return index + 8;
    if (within_group >= 16 && within_group < 24) return index - 8;
    return index;
}

int RunClut() {
    int rc = 0;
    uint64_t d1 = 0, d2 = 0, d3 = 0, d4 = 0, cross = 0;
    for (uint32_t i = 0; i < 256; i++) {
        const uint32_t n = ps2r::gs::ClutLayout::Csm1Index(i);
        const uint32_t a = OldCsm1_TextureDecoder(i), b = OldCsm1_OpenGlBackend(i), c = OldCsm1_S9Composite(i);
        if (n != a) d1++;
        if (n != b) d2++;
        if (n != c) d3++;
        if (a != b || b != c) cross++;
#if OG_HAVE_F2
        if (int(n) != gs_deswizzle::csm1_unswizzle(int(i))) d4++;
        if (int(a) != gs_deswizzle::csm1_unswizzle(int(i))) cross++;
#endif
    }
    std::printf("CSM1 index reorder (256 entries):\n");
    std::printf("  CSM1 vs texture_decoder.cpp:944 CSM1Swizzle        %s\n", d1 ? "DEVIATION" : "IDENTICAL");
    std::printf("  CSM1 vs opengl_backend.cpp:21667 R164Csm1Swizzle   %s\n", d2 ? "DEVIATION" : "IDENTICAL");
    std::printf("  CSM1 vs s9_composite.h:322 Csm1Swizzle             %s\n", d3 ? "DEVIATION" : "IDENTICAL");
#if OG_HAVE_F2
    std::printf("  CSM1 vs gs_deswizzle.h:140 csm1_unswizzle          %s (read-only include)\n", d4 ? "DEVIATION" : "IDENTICAL");
#else
    std::printf("  CSM1 vs gs_deswizzle.h:140 csm1_unswizzle          SKIP (OLD header not available)\n");
#endif
    std::printf("  OLD copies vs each other                           %s (%llu disagreements)\n", cross ? "DISAGREE" : "AGREE", (unsigned long long)cross);
    if (d1 || d2 || d3 || d4) { std::printf("  CSM1 FAIL: NEW differs from an OLD copy that matches the manual figure\n"); rc = 1; }

    // ReadCLUT32 (texture_decoder.cpp:1146): idx = CSM1Swizzle(i); addr =
    // GetAddressPSMCT32(idx % 16, idx / 16, cbp_bytes, 64) -- byte address.
    // ReadCLUT16 (:1217): addr = (cbp_bytes + CSM1Swizzle(i) * 2) % 4 MB -- LINEAR.
    const uint32_t cbps[] = {0, 0x3308, 0x3c08, 16383};
    uint64_t dev32 = 0, dev16 = 0, dev16_4 = 0; uint32_t first16 = 0; bool have16 = false;
    for (uint32_t cbp : cbps)
        for (uint32_t i = 0; i < 256; i++) {
            const uint32_t idx = OldCsm1_TextureDecoder(i);
            const uint32_t old32 = OldF1(Psm::CT32, int(idx % 16), int(idx / 16), cbp * 256u, 64);
            const uint32_t new32 = ps2r::gs::ClutLayout::EntryAddress(Psm::CT32, cbp, false, 256, i) * 4u;
            if (old32 != new32) dev32++;
            const uint32_t old16 = (cbp * 256u + idx * 2u) % (4u * 1024u * 1024u);
            const uint32_t new16 = ps2r::gs::ClutLayout::EntryAddress(Psm::CT16, cbp, false, 256, i) * 2u;
            if (old16 != new16) { if (!have16) { have16 = true; first16 = i; } dev16++; }
            if (i < 16) {
                const uint32_t new16_4 = ps2r::gs::ClutLayout::EntryAddress(Psm::CT16, cbp, false, 16, i) * 2u;
                const uint32_t old16_4 = (cbp * 256u + OldCsm1_TextureDecoder(i) * 2u) % (4u * 1024u * 1024u);
                if (new16_4 != old16_4) dev16_4++;
            }
        }
    std::printf("CLUT reads (4 cbp x 256 entries):\n");
    std::printf("  ReadClut32 CSM1/CT32 vs texture_decoder.cpp:1146 ReadCLUT32   %s\n", dev32 ? "DEVIATION" : "IDENTICAL");
    if (dev32) { std::printf("  FAIL: the CT32 grid walk is the manual's rectangle; NEW must match it\n"); rc = 1; }
    if (dev16 == 0) std::printf("  ReadClut16 CSM1/CT16 vs texture_decoder.cpp:1217 ReadCLUT16   IDENTICAL\n");
    else std::printf("  ReadClut16 CSM1/CT16 vs texture_decoder.cpp:1217 ReadCLUT16   DEVIATION(count=%llu of 1024, first entry %u; IDTEX4 %llu of 64) "
                     "-- OLD reads a linear halfword row, the manual stores the rectangle through the PSMCT16 layout; EQUIVALENCE_PLAN 7.1 P2-CLUT16-CSM1\n",
                     (unsigned long long)dev16, first16, (unsigned long long)dev16_4);
    // The OLD test_page_invariance.cpp:118 authoring formula is a fourth expression of the grid.
    uint64_t dev_pi = 0;
    for (uint32_t i = 0; i < 256; i++) {
        const uint32_t cx = (i & 7) + ((i >> 4) & 1) * 8, cy = ((i >> 3) & 1) + ((i >> 5) << 1);
        const ps2r::gs::ClutLayout::Position p = ps2r::gs::ClutLayout::Csm1Position(i, 256);
        if (p.x != cx || p.y != cy) dev_pi++;
    }
    std::printf("  Csm1Position vs tests/test_page_invariance.cpp:118 authoring formula   %s\n", dev_pi ? "DEVIATION" : "IDENTICAL");
    if (dev_pi) rc = 1;
    return rc;
}

// ------------------------------------------------------- TEXA / value leaves
// The OLD value leaves are .cpp-local (texture_decoder.cpp) or inline in a header
// whose closure is not std-only (texture_decoder.h includes render.h), so they are
// LIFTED here, each cited:
//   Expand16to32            texture_decoder.cpp:1946  replicate 5->8, a = msb ? 255 : 0
//   DecodePSMCT24 alpha     texture_decoder.cpp:502-553 (:540-546)  aem && r==g==b==0 ? 0 : ta0
//   DecodePSMCT16 alpha     texture_decoder.cpp:740-780  Expand16to32 only -- TEXA never applied
//                           (opengl_backend.cpp:1395 "no TEXA"; A3 section 2 inconsistency 1)
//   ScaleAlphaPS2ToGPU      texture_decoder.h:263  a >= 128 ? 255 : a * 2
//   EncodeRTToVRAM inverse  opengl_backend.cpp:15992  a >= 255 ? 128 : a / 2
//   GetPixelSize            gs_state.cpp:4731  4/4/2/2/1/1/4/4/2/2 (T4 rounded up to 1 byte)
//   pixels-per-64-bit       gs_state.cpp:5066 / :5332  CT32,CT24 -> 2; CT16,CT16S -> 4; T8 -> 8; T4 -> 16
uint32_t OldExpand16to32(uint16_t pixel) {
    uint8_t r = uint8_t(pixel & 0x1F), g = uint8_t((pixel >> 5) & 0x1F), b = uint8_t((pixel >> 10) & 0x1F);
    uint8_t a = uint8_t((pixel >> 15) & 1);
    r = uint8_t((r << 3) | (r >> 2)); g = uint8_t((g << 3) | (g >> 2)); b = uint8_t((b << 3) | (b >> 2));
    a = a ? 255 : 0;
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}
uint8_t OldCt24Alpha(uint8_t r, uint8_t g, uint8_t b, uint8_t texa_ta0, bool texa_aem) {
    if (texa_aem && r == 0 && g == 0 && b == 0) return 0;
    return texa_ta0;
}
uint8_t OldScaleAlphaPS2ToGPU(uint8_t a) { return uint8_t((a >= 128) ? 255 : (a * 2)); }
uint8_t OldEncodeAlphaGpuToPs2(uint8_t a) { return uint8_t((a >= 255) ? 128 : (a / 2)); }
int OldGetPixelSize(Psm psm) {
    switch (psm) {
        case Psm::CT32: case Psm::CT24: case Psm::Z32: case Psm::Z24: return 4;
        case Psm::CT16: case Psm::CT16S: case Psm::Z16: case Psm::Z16S: return 2;
        case Psm::T8: case Psm::T4: return 1;
        default: return 4;
    }
}
int OldPixelsPerWrite(Psm psm) {
    switch (psm) {
        case Psm::CT32: case Psm::CT24: return 2;
        case Psm::CT16: case Psm::CT16S: return 4;
        case Psm::T8: return 8;
        case Psm::T4: return 16;
        default: return 2;
    }
}

int RunTexa() {
    int rc = 0;
    std::printf("TEXA / value leaves (OLD lifted, cited in the source):\n");
    // Expand16To32 vs Expand16to32, all 65536 halfwords.
    uint64_t dev = 0; uint32_t first = 0;
    for (uint32_t p = 0; p < 65536; p++) {
        if (ps2r::gs::TexaExpand::Expand16To32(uint16_t(p)) != OldExpand16to32(uint16_t(p))) { if (!dev) first = p; dev++; }
    }
    std::printf("  Expand16To32 vs texture_decoder.cpp:1946 Expand16to32 (65536)        %s", dev ? "DEVIATION" : "IDENTICAL");
    if (dev) { std::printf("(count=%llu, first 0x%04x) FAIL: both trees replicate", (unsigned long long)dev, first); rc = 1; }
    std::printf("\n");
    // The manual's shift formula (GS_SKILL 3.4.6, out = in5 << 3), counted for the record:
    // both trees deviate from it identically -- EQUIVALENCE_PLAN 7.1 P2-EXP16-REPL.
    uint64_t vs_manual = 0;
    for (uint32_t p = 0; p < 65536; p++) {
        const uint32_t m = (p & 0x1F) << 3 | (((p >> 5) & 0x1F) << 3) << 8 | (((p >> 10) & 0x1F) << 3) << 16 | ((p >> 15) ? 0xFFu : 0u) << 24;
        if (ps2r::gs::TexaExpand::Expand16To32(uint16_t(p)) != m) vs_manual++;
    }
    std::printf("  Expand16To32 vs the manual's shift (in5 << 3)                      DEVIATION(count=%llu of 65536) -- adjudicated replicate (BUG-020), P2-EXP16-REPL; OLD deviates identically\n",
                (unsigned long long)vs_manual);

    // ApplyTexa24 vs DecodePSMCT24's alpha rule, every (r,g,b in {0, 1, 0x7F, 0xFF}) x ta0 x aem.
    uint64_t dev24 = 0; uint32_t n24 = 0;
    const uint8_t vals[] = {0, 1, 0x7F, 0xFF};
    for (uint8_t r : vals) for (uint8_t g : vals) for (uint8_t b : vals)
        for (uint32_t ta0 = 0; ta0 < 256; ta0 += 5) for (int aem = 0; aem < 2; aem++) {
            ps2r::gs::TexaExpand::Texa t; t.ta0 = uint8_t(ta0); t.ta1 = 0x33; t.aem = aem != 0;
            const uint32_t rgb = uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16);
            const uint32_t want = rgb | (uint32_t(OldCt24Alpha(r, g, b, uint8_t(ta0), aem != 0)) << 24);
            if (ps2r::gs::TexaExpand::ApplyTexa24(rgb | 0x80000000u, t) != want) dev24++;
            n24++;
        }
    std::printf("  ApplyTexa24 vs texture_decoder.cpp:540-546 DecodePSMCT24 alpha (%u)   %s\n", n24, dev24 ? "DEVIATION FAIL" : "IDENTICAL");
    if (dev24) rc = 1;

    // ApplyTexa16 vs OLD's CT16 texel value (Expand16to32, TEXA never applied): the
    // pre-registered rule-2b deviation, counted -- EQUIVALENCE_PLAN 7.1 P2-CT16-TEXA.
    {
        ps2r::gs::TexaExpand::Texa common; common.ta0 = 0x00; common.ta1 = 0x80; common.aem = false;
        uint64_t d16 = 0, d16_rgb = 0;
        for (uint32_t p = 0; p < 65536; p++) {
            const uint32_t n = ps2r::gs::TexaExpand::ApplyTexa16(uint16_t(p), common), o = OldExpand16to32(uint16_t(p));
            if (n != o) d16++;
            if ((n & 0x00FFFFFFu) != (o & 0x00FFFFFFu)) d16_rgb++;
        }
        std::printf("  ApplyTexa16 vs texture_decoder.cpp:740-780 DecodePSMCT16 (TEXA omitted) DEVIATION(count=%llu of 65536 at TA0=0x00/TA1=0x80/AEM=0; RGB differs in %llu)"
                    " -- OLD never applies TEXA to CT16 texels (opengl_backend.cpp:1395); NEW = manual 3.4.6; P2-CT16-TEXA, Phase 6 arbitrates\n",
                    (unsigned long long)d16, (unsigned long long)d16_rgb);
        if (d16_rgb) { std::printf("  FAIL: RGB must agree (both replicate)\n"); rc = 1; }
    }

    // Alpha domain, both directions, every byte.
    uint64_t da = 0, di = 0;
    for (uint32_t a = 0; a < 256; a++) {
        if (ps2r::gs::PixelConvert::ScaleAlphaPs2ToGpu(uint8_t(a)) != OldScaleAlphaPS2ToGPU(uint8_t(a))) da++;
        if (ps2r::gs::PixelConvert::ScaleAlphaGpuToPs2(uint8_t(a)) != OldEncodeAlphaGpuToPs2(uint8_t(a))) di++;
    }
    std::printf("  ScaleAlphaPs2ToGpu vs texture_decoder.h:263 ScaleAlphaPS2ToGPU (256)    %s\n", da ? "DEVIATION FAIL" : "IDENTICAL");
    std::printf("  ScaleAlphaGpuToPs2 vs opengl_backend.cpp:15992 EncodeRTToVRAM (256)     %s\n", di ? "DEVIATION FAIL" : "IDENTICAL");
    if (da || di) rc = 1;

    // Widths: BytesPerPixel vs GetPixelSize (10 modes OLD lists); OLD rounds T4 up to 1.
    // Pixels per 64-bit HWREG write: OLD's table vs 64 / StreamBitsPerPixel -- CT24 is the
    // registered difference (OLD 2 = 32-bit unpack; the stream packs 24 bits: P2-CT24-HWREG-A).
    uint32_t dsz = 0, dppw = 0;
    for (int i = 0; i < ps2r::gs::kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        if (d.is_indexed && d.psm != Psm::T8 && d.psm != Psm::T4) continue;   // OLD lists no T8H/T4HL/T4HH size
        const uint32_t nb = d.storage_bits >= 8 ? ps2r::gs::PixelConvert::BytesPerPixel(d.psm) : 0u;
        if (d.psm == Psm::T4) { if (OldGetPixelSize(d.psm) != 1) dsz++; }   // OLD: 1 (rounded); NEW: no whole byte
        else if (int(nb) != OldGetPixelSize(d.psm)) dsz++;
        if (!d.is_z) {
            const uint32_t stream = ps2r::gs::PixelConvert::StreamBitsPerPixel(d.psm);
            const uint32_t ppw = 64u / stream;   // 64/24 = 2 by integer division; the stream is not word-aligned per pixel
            if (int(ppw) != OldPixelsPerWrite(d.psm)) dppw++;
            if (d.psm == Psm::CT24 && stream != 24) rc = 1;
        }
    }
    std::printf("  BytesPerPixel vs gs_state.cpp:4731 GetPixelSize (byte-or-wider modes)   %s (T4: OLD rounds to 1 byte, NEW exposes 4 bits)\n", dsz ? "DEVIATION FAIL" : "IDENTICAL");
    std::printf("  64 / StreamBitsPerPixel vs gs_state.cpp:5066/:5332 pixels-per-write       %s (CT24: OLD unpacks the stream at 32 bpp -> P2-CT24-HWREG-A; NEW StreamBitsPerPixel(CT24) = 24)\n",
                dppw ? "DEVIATION" : "IDENTICAL-BY-DIVISION");
    if (dsz) rc = 1;
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <og_tables.bin> <PSM name|F3F4|CLUT|TEXA|all>\n", argv[0]); return 2; }
    if (!LoadOgTables(argv[1])) { std::printf("og_gs_format_oracle: FAIL cannot load %s\n", argv[1]); return 1; }
    const std::string want = argv[2];
    int rc = 0; bool any = false;
    for (int i = 0; i < ps2r::gs::kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        if (want != "all" && want != d.name) continue;
        any = true;
        rc |= RunPsm(d);
    }
    if (want == "all" || want == "F3F4") { any = true; rc |= RunRectFamily('3'); rc |= RunRectFamily('4'); rc |= RunRectFamily('L'); }
    if (want == "all" || want == "CLUT") { any = true; rc |= RunClut(); }
    if (want == "all" || want == "TEXA") { any = true; rc |= RunTexa(); }
    if (!any) { std::printf("og_gs_format_oracle: unknown PSM '%s'\n", want.c_str()); return 2; }
    std::printf(rc == 0 ? "og_gs_format_oracle: PASS (NEW == manual)\n" : "og_gs_format_oracle: FAIL\n");
    return rc;
}

// tests/gs/texsrc/test_title_atlas_decode.cpp -- Texsrc_TitleAtlasDecode: the title
// screen's logo atlas, as the PCSX2 GS dump uploads it (one host->local IMAGE
// transfer of a 64x32 PSMCT32 page into 0x3c0000, the pre-swizzled PSMT8 32x32 texels
// in blocks 0..3 and the 256-entry CSM1 CLUT at CBP 0x3c0400), written through the
// TransferEngine and read back through TextureDecoder with the draw's TEX0
// (T8 32x32, CLUT CT32 CSM1 CSA 0, TCC 1, MODULATE) and TEXA 0x80 -- the same entry
// point the resolver's decode path uses. The pin: the palette index the letters use
// decodes to the yellow the console shows (the dump's screenshot letter pixel /2:
// the vertex colour 0xFF modulates 2x, GS User's Manual 3.4.4 "0x80 = 1.0"), the
// decoded atlas carries that yellow and the texel-alpha lane is the stored 0x7f
// (the stored-alpha convention: no GPU rescale).
// `--dump <ppm>` writes the decoded level for inspection.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/texsrc/texture_decoder.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/gs/xfer/transfer_engine.h"

using namespace ps2r;
using namespace ps2r::gs;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, msg)                                                                   \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); ++g_fail; } \
    } while (0)

namespace {

bool ReadFile(const char* path, std::vector<uint8_t>* out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out->resize(static_cast<size_t>(n));
    const size_t got = std::fread(out->data(), 1, out->size(), f);
    std::fclose(f);
    return got == out->size();
}

// TEX0 / TEX1 / CLAMP bit layouts (GS User's Manual 3.4.4 / 3.4.5 / 3.4.7).
TexReadView ViewFromRegs(uint64_t tex0, uint64_t tex1, uint64_t clamp, uint64_t texa) {
    TexReadView v;
    v.tbp = static_cast<uint32_t>(tex0 & 0x3FFF) * 256u;
    v.tbw = static_cast<uint32_t>((tex0 >> 14) & 0x3F) * 64u;
    v.psm = static_cast<uint32_t>((tex0 >> 20) & 0x3F);
    v.tw = static_cast<uint32_t>((tex0 >> 26) & 0xF);
    v.th = static_cast<uint32_t>((tex0 >> 30) & 0xF);
    v.tcc = static_cast<uint32_t>((tex0 >> 34) & 1);
    v.tfx = static_cast<uint32_t>((tex0 >> 35) & 3);
    v.cbp = static_cast<uint32_t>((tex0 >> 37) & 0x3FFF) * 256u;
    v.cpsm = static_cast<uint32_t>((tex0 >> 51) & 0xF);
    v.csm = static_cast<uint32_t>((tex0 >> 55) & 1);
    v.csa = static_cast<uint32_t>((tex0 >> 56) & 0x1F);
    v.cld = static_cast<uint32_t>((tex0 >> 61) & 7);
    v.lcm = tex1 & 1;
    v.mxl = static_cast<uint8_t>((tex1 >> 2) & 7);
    v.mmag = (tex1 >> 5) & 1;
    v.mmin = static_cast<uint8_t>((tex1 >> 6) & 7);
    v.l = static_cast<uint8_t>((tex1 >> 19) & 3);
    const uint16_t k12 = static_cast<uint16_t>((tex1 >> 32) & 0xFFF);
    v.k = static_cast<int16_t>(k12 & 0x800 ? static_cast<int>(k12) - 0x1000 : static_cast<int>(k12));
    v.wms = static_cast<uint8_t>(clamp & 3);
    v.wmt = static_cast<uint8_t>((clamp >> 2) & 3);
    v.texa_ta0 = static_cast<uint8_t>(texa & 0xFF);
    v.texa_aem = (texa >> 15) & 1;
    v.texa_ta1 = static_cast<uint8_t>((texa >> 32) & 0xFF);
    return v;
}

uint64_t Bitblt(uint32_t dbp_blk, uint32_t dbw64, uint32_t dpsm) {
    return ((uint64_t)(dbp_blk & 0x3FFF) << 32) | ((uint64_t)(dbw64 & 0x3F) << 48) | ((uint64_t)(dpsm & 0x3F) << 56);
}

void WritePpm(const char* path, int w, int h, const std::vector<uint32_t>& px) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (uint32_t p : px) {
        const uint8_t rgb[3] = {static_cast<uint8_t>(p), static_cast<uint8_t>(p >> 8), static_cast<uint8_t>(p >> 16)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    const char* page_path = nullptr;
    const char* dump = nullptr;
    const char* vram_out = nullptr;   // --vram-out <file>: the 8 KiB page as the GS stores it (swizzled)
    bool vram_in = false;             // --vram-in: <page.bin> IS the stored page (a trace's window bytes), not an IMAGE payload
    const char* dump_levels = nullptr;   // --dump-levels <prefix>: the authored mip levels 1-2 as <prefix>_l1.ppm / _l2.ppm
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!std::strcmp(argv[i], "--dump-levels") && i + 1 < argc) dump_levels = argv[++i];
        else if (!std::strcmp(argv[i], "--vram-out") && i + 1 < argc) vram_out = argv[++i];
        else if (!std::strcmp(argv[i], "--vram-in")) vram_in = true;
        else page_path = argv[i];
    }
    if (!page_path) { std::printf("usage: test_title_atlas_decode <page.bin> [--vram-in] [--dump out.ppm] [--vram-out page.bin]\n"); return 2; }
    std::vector<uint8_t> page;
    if (!ReadFile(page_path, &page) || page.size() != 8192) {
        std::printf("test_title_atlas_decode: %s missing or not 8192 bytes -> SKIP (77)\n", page_path);
        return 77;
    }

    // The dump's transfer before draw [167] (frame 1): BITBLTBUF dbp 0x3c0000 dbw 64 dpsm CT32,
    // TRXPOS (0,0), TRXREG 64x32, TRXDIR host->local.
    VramImage vram;
    if (vram_in) {
        // The page as stored (a TexresTrace window): no transfer, the bytes land as they are.
        vram.WriteStream(0x3c0000, page.data(), page.size());
    } else {
        TransferEngine xfer(&vram);
        xfer.WriteRegister(0x50, Bitblt(0x3c0000 / 256, 1, 0x00));
        xfer.WriteRegister(0x51, 0);
        xfer.WriteRegister(0x52, 64ull | (32ull << 32));
        xfer.WriteRegister(0x53, 0);
        xfer.WriteImage(page.data(), page.size());
        CHECK(xfer.Stats().completed == 1, "the 64x32 CT32 page upload completes");
    }

    // Draw [167]'s registers from the dump.
    const TexReadView v = ViewFromRegs(0x2007808555307c00ull, 0xffffffbb00000128ull, 0ull, 0x8000000000ull);
    std::printf("TEX0: tbp=%#x tbw=%u psm=%#x tw=%u th=%u tcc=%u tfx=%u cbp=%#x cpsm=%u csm=%u csa=%u\n",
                v.tbp, v.tbw, v.psm, v.tw, v.th, v.tcc, v.tfx, v.cbp, v.cpsm, v.csm, v.csa);
    std::printf("TEX1: mmag=%d mmin=%u mxl=%u lcm=%d l=%u k=%d (%.4f)  CLAMP wms=%u wmt=%u  TEXA ta0=%#x ta1=%#x aem=%d\n",
                v.mmag, v.mmin, v.mxl, v.lcm, v.l, v.k, v.k / 16.0, v.wms, v.wmt, v.texa_ta0, v.texa_ta1, v.texa_aem);
    CHECK(v.tbp == 0x3c0000 && v.psm == 0x13 && v.tw == 5 && v.th == 5 && v.cbp == 0x3c0400 && v.cpsm == 0 &&
          v.csm == 0 && v.csa == 0 && v.tcc == 1 && v.tfx == 0, "TEX0 decodes to T8 32x32 / CLUT CT32 CSM1 at 0x3c0400");

    uint32_t palette[256];
    CHECK(TextureDecoder::ReadPalette(vram.Bytes(), v, nullptr, nullptr, palette), "palette read");
    std::vector<uint32_t> texels;
    TextureDecoder::Decode(vram.Bytes(), v, nullptr, nullptr, &texels);
    CHECK(texels.size() == 32 * 32, "32x32 texels");

    // Histogram of the decoded texels (the stored alpha byte in bits 24-31).
    std::map<uint32_t, int> hist;
    for (uint32_t p : texels) hist[p]++;
    std::printf("decoded texel histogram (%zu distinct):\n", hist.size());
    std::vector<std::pair<int, uint32_t>> top;
    for (auto& kv : hist) top.push_back({kv.second, kv.first});
    std::sort(top.begin(), top.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < top.size() && i < 12; i++)
        std::printf("  %08x (r=%3u g=%3u b=%3u a=%3u) x%d\n", top[i].second, top[i].second & 0xFF,
                    (top[i].second >> 8) & 0xFF, (top[i].second >> 16) & 0xFF, top[i].second >> 24, top[i].first);
    std::printf("palette (PS2 alpha) first 16: ");
    for (int i = 0; i < 16; i++) std::printf("%08x ", palette[i]);
    std::printf("\n");

    // The console's letter yellow: the dump screenshot's letter pixel (238,216,16) is the
    // texel modulated by the 0xFF vertex colour (2x, clamped) -> the texel is within one
    // GS step of (119,108,8). The pin: some decoded texel (and some palette entry) is that
    // yellow with the atlas's PS2 alpha 0x7f, stored as 0x7f.
    auto near = [](uint32_t p, int r, int g, int b, int tol) {
        return std::abs(int(p & 0xFF) - r) <= tol && std::abs(int((p >> 8) & 0xFF) - g) <= tol &&
               std::abs(int((p >> 16) & 0xFF) - b) <= tol;
    };
    // The atlas's palette carries PS2 alpha 0x7f on its colour entries; the decoder
    // stores the byte as is (the stored-alpha convention).
    int yellow_texels = 0, yellow_opaque = 0;
    for (uint32_t p : texels)
        if (near(p, 119, 108, 8, 3)) { yellow_texels++; if ((p >> 24) == 0x7F) yellow_opaque++; }
    std::printf("yellow (119,108,8)+-3 texels: %d (alpha 0x7f: %d)\n", yellow_texels, yellow_opaque);
    int pal_yellow = -1;
    for (int i = 0; i < 256; i++)
        if (near(palette[i], 119, 108, 8, 3)) { pal_yellow = i; break; }
    std::printf("palette yellow index: %d (entry %08x)\n", pal_yellow,
                pal_yellow >= 0 ? palette[pal_yellow] : 0u);
    CHECK(pal_yellow >= 0, "the CLUT carries the letters' yellow (119,108,8)+-3");
    CHECK(yellow_texels > 0, "the decoded 32x32 atlas carries the letters' yellow");
    CHECK(yellow_texels == yellow_opaque, "the yellow texels decode with the stored PS2 alpha 0x7f");

    if (dump) { WritePpm(dump, 32, 32, texels); std::printf("decoded atlas -> %s\n", dump); }
    if (dump_levels) {
        // The dump's MIPTBP1 for [167]: level 1 at 0x3c0800 (16x16), level 2 at 0x3c0a00 (8x8), TBW 64 --
        // inside the same uploaded page. Each level's mean is printed (the colour a sampler clamped to the
        // chain's tail would return).
        const uint32_t lv_tbp[2] = {0x3c0800u, 0x3c0a00u};
        for (int lv = 0; lv < 2; ++lv) {
            const uint32_t lw = 32u >> (lv + 1), lh = 32u >> (lv + 1);
            std::vector<uint32_t> lt;
            TextureDecoder::DecodeLevel(vram.Bytes(), v, lv_tbp[lv], 64, lw, lh, nullptr, nullptr, &lt);
            double m[3] = {0, 0, 0};
            for (uint32_t p : lt) { m[0] += p & 0xFF; m[1] += (p >> 8) & 0xFF; m[2] += (p >> 16) & 0xFF; }
            const double n = lt.empty() ? 1.0 : double(lt.size());
            std::printf("level %d (%ux%u at %#x): %zu texels, mean=(%.1f,%.1f,%.1f)\n", lv + 1, lw, lh, lv_tbp[lv],
                        lt.size(), m[0] / n, m[1] / n, m[2] / n);
            const std::string path = std::string(dump_levels) + "_l" + std::to_string(lv + 1) + ".ppm";
            WritePpm(path.c_str(), int(lw), int(lh), lt);
        }
    }
    if (vram_out) {
        if (FILE* f = std::fopen(vram_out, "wb")) {
            std::fwrite(vram.Bytes() + 0x3c0000, 1, 8192, f);
            std::fclose(f);
            std::printf("VRAM page 0x3c0000 -> %s\n", vram_out);
        }
    }
    std::printf("test_title_atlas_decode: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

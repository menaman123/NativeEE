// gs/hle/texsrc/texture_dump_sink.cpp -- TextureDumpSink (texture_dump_sink.h).
#include "engine/gs/hle/texsrc/texture_dump_sink.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "engine/gs/format/block_decode.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/texsrc/texture_decoder.h"

namespace ps2r {
namespace gs {

namespace {

constexpr uint32_t kVramBytes = 4u * 1024u * 1024u;
constexpr uint32_t kPageBytes = 8192;

bool Indexed(uint32_t psm) { return psm == 0x13 || psm == 0x14 || psm == 0x1b || psm == 0x24 || psm == 0x2c; }

// The tuple fields a decode depends on, in a fixed byte order (TBP / CBP excluded, as
// ContentKey excludes them: the texels do not depend on where the bytes sit).
void TupleBytes(const TexReadView& v, uint32_t out[12]) {
    out[0] = v.tbw; out[1] = v.psm; out[2] = v.tw; out[3] = v.th; out[4] = v.cpsm; out[5] = v.csm;
    out[6] = v.csa; out[7] = v.texa_ta0; out[8] = v.texa_ta1; out[9] = v.texa_aem ? 1u : 0u;
    out[10] = v.cbp % kVramBytes; out[11] = v.tbp % kVramBytes;
}

// Reads n bytes at `start` of the 4 MiB view, wrapping, into the hash.
uint64_t HashSpan(uint64_t h, const uint8_t* view, uint32_t start, uint32_t n) {
    if (n > kVramBytes) n = kVramBytes;
    const uint32_t a = start % kVramBytes;
    const uint32_t first = n < kVramBytes - a ? n : kVramBytes - a;
    h = TextureDumpSink::HashBytes(h, view + a, first);
    if (first < n) h = TextureDumpSink::HashBytes(h, view, n - first);
    return h;
}

bool WriteSpan(std::FILE* f, const uint8_t* view, uint32_t start, uint32_t n) {
    if (n > kVramBytes) n = kVramBytes;
    const uint32_t a = start % kVramBytes;
    const uint32_t first = n < kVramBytes - a ? n : kVramBytes - a;
    bool ok = std::fwrite(view + a, 1, first, f) == first;
    if (first < n) ok = ok && std::fwrite(view, 1, n - first, f) == n - first;
    return ok;
}

}  // namespace

uint64_t TextureDumpSink::HashBytes(uint64_t h, const uint8_t* p, size_t n) {
    // FNV-1a over 8-byte lanes (the content, not a cryptographic identity).
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, p + i, 8);
        h ^= w;
        h *= 1099511628211ull;
    }
    for (; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t TextureDumpSink::ContentHash(const Decode& d) {
    uint32_t tup[12];
    TupleBytes(*d.tex, tup);
    uint64_t h = HashBytes(1469598103934665603ull, reinterpret_cast<const uint8_t*>(tup), sizeof(tup));
    h = HashSpan(h, d.view, d.fp_start, d.fp_size);
    if (Indexed(d.tex->psm)) h = HashSpan(h, d.view, d.tex->cbp, kPageBytes);
    return h;
}

TextureDumpSink::OracleB TextureDumpSink::CompareWithReference(const Decode& d, std::vector<uint32_t>* reference) {
    OracleB ob;
    const TexReadView& v = *d.tex;
    const size_t n = static_cast<size_t>(d.dw) * d.dh;
    reference->assign(n, 0u);
    Psm psm;
    if (!PsmLayout::FromField(v.psm, &psm)) return ob;
    BlockDecode::Texture t;
    t.psm = psm;
    t.tbp = (v.tbp % kVramBytes) / PsmLayout::kBlockBytes;
    t.tbw = v.tbw / 64;
    t.w = 1u << v.tw;
    t.h = 1u << v.th;
    uint32_t palette[256];
    // The palette as the manual reads it (CLUT rectangle at CBP in CPSM / CSM), without
    // the resolver's heal workaround: a healed palette shows up as a palette-class
    // difference, which is what the oracle is for.
    const bool indexed = TextureDecoder::ReadPalette(d.view, v, nullptr, nullptr, palette);
    TexaExpand::Texa texa;
    texa.ta0 = v.texa_ta0;
    texa.ta1 = v.texa_ta1;
    texa.aem = v.texa_aem;
    BlockDecode::Rect r;
    r.x = static_cast<uint32_t>(d.sub ? d.x0 : 0);
    r.y = static_cast<uint32_t>(d.sub ? d.y0 : 0);
    r.w = static_cast<uint32_t>(d.dw);
    r.h = static_cast<uint32_t>(d.dh);
    BlockDecode::DecodeGeneric(d.view, t, r, texa, indexed ? palette : nullptr, reference->data());
    for (size_t i = 0; i < n; ++i) {
        if ((*reference)[i] == d.texels[i]) continue;
        if (ob.diffs == 0) {
            ob.first_x = static_cast<int>(i % static_cast<size_t>(d.dw));
            ob.first_y = static_cast<int>(i / static_cast<size_t>(d.dw));
        }
        ++ob.diffs;
    }
    ob.equal = ob.diffs == 0;
    return ob;
}

bool TextureDumpSink::Arm(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec && !std::filesystem::is_directory(dir)) return false;
    dir_ = dir;
    seen_.clear();
    served_.clear();
    serve_lines_.clear();
    stats_ = Stats{};
    armed_.store(true, std::memory_order_release);
    return true;
}

void TextureDumpSink::Disarm() {
    if (!armed_.exchange(false, std::memory_order_acq_rel)) return;
    if (std::FILE* f = std::fopen((dir_ + "/serves.tsv").c_str(), "w")) {
        std::fprintf(f, "tier\ttbp\ttbw\tpsm\ttw\tth\tcbp\tcpsm\tcsm\tcsa\tframe\tdraw\n");
        for (const std::string& l : serve_lines_) std::fputs(l.c_str(), f);
        std::fclose(f);
        ++stats_.written;
    } else {
        ++stats_.failed;
    }
    serve_lines_.clear();
}

std::string TextureDumpSink::Path(uint64_t hash, const char* ext) const {
    char name[64];
    std::snprintf(name, sizeof(name), "/%016llx%s", static_cast<unsigned long long>(hash), ext);
    return dir_ + name;
}

void TextureDumpSink::NoteServe(const TexReadView& v, const char* tier, uint32_t frame_count, uint32_t draw_seq) {
    if (!Armed()) return;
    uint32_t tup[12];
    TupleBytes(v, tup);
    uint64_t h = HashBytes(1469598103934665603ull, reinterpret_cast<const uint8_t*>(tup), sizeof(tup));
    h = HashBytes(h, reinterpret_cast<const uint8_t*>(tier), std::strlen(tier));
    if (!served_.insert(h).second) return;
    ++stats_.serves;
    char line[256];
    std::snprintf(line, sizeof(line), "%s\t%06x\t%u\t%02x\t%u\t%u\t%06x\t%02x\t%u\t%u\t%u\t%u\n", tier, v.tbp % kVramBytes,
                  v.tbw, v.psm, v.tw, v.th, v.cbp % kVramBytes, v.cpsm, v.csm, v.csa, frame_count, draw_seq);
    serve_lines_.push_back(line);
}

void TextureDumpSink::OnDecode(const Decode& d) {
    if (!Armed() || !d.tex || !d.view || !d.texels || d.dw <= 0 || d.dh <= 0) return;
    ++stats_.decodes;
    const uint64_t hash = ContentHash(d);
    if (!seen_.insert(hash).second) return;
    ++stats_.distinct;
    const OracleB ob = CompareWithReference(d, &reference_);
    if (!ob.equal) ++stats_.oracle_b_bad;
    if (!WriteTexture(d, hash, ob, reference_)) ++stats_.failed;
}

bool TextureDumpSink::WriteTexture(const Decode& d, uint64_t hash, const OracleB& ob, const std::vector<uint32_t>& reference) {
    const TexReadView& v = *d.tex;
    const size_t n = static_cast<size_t>(d.dw) * d.dh;
    bool ok = true;
    // P6 (RGB) + P5 (the alpha byte, PS2 domain) of the fast tier's texels.
    if (std::FILE* f = std::fopen(Path(hash, ".ppm").c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", d.dw, d.dh);
        std::vector<uint8_t> rgb(n * 3);
        for (size_t i = 0; i < n; ++i) {
            rgb[i * 3 + 0] = static_cast<uint8_t>(d.texels[i]);
            rgb[i * 3 + 1] = static_cast<uint8_t>(d.texels[i] >> 8);
            rgb[i * 3 + 2] = static_cast<uint8_t>(d.texels[i] >> 16);
        }
        ok = std::fwrite(rgb.data(), 1, rgb.size(), f) == rgb.size() && ok;
        std::fclose(f);
        ++stats_.written;
    } else {
        ok = false;
    }
    if (std::FILE* f = std::fopen(Path(hash, ".alpha.pgm").c_str(), "wb")) {
        std::fprintf(f, "P5\n%d %d\n255\n", d.dw, d.dh);
        std::vector<uint8_t> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = static_cast<uint8_t>(d.texels[i] >> 24);
        ok = std::fwrite(a.data(), 1, a.size(), f) == a.size() && ok;
        std::fclose(f);
        ++stats_.written;
    } else {
        ok = false;
    }
    // The reference's texels only when they differ (the judge's contact sheet).
    if (!ob.equal) {
        if (std::FILE* f = std::fopen(Path(hash, ".ref.ppm").c_str(), "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", d.dw, d.dh);
            std::vector<uint8_t> rgb(n * 3);
            for (size_t i = 0; i < n; ++i) {
                rgb[i * 3 + 0] = static_cast<uint8_t>(reference[i]);
                rgb[i * 3 + 1] = static_cast<uint8_t>(reference[i] >> 8);
                rgb[i * 3 + 2] = static_cast<uint8_t>(reference[i] >> 16);
            }
            ok = std::fwrite(rgb.data(), 1, rgb.size(), f) == rgb.size() && ok;
            std::fclose(f);
            ++stats_.written;
        }
    }
    // The raw bytes: the level-0 footprint (bounded) and the CLUT page of an indexed read.
    uint64_t win_hash = 0, clut_hash = 0;
    win_hash = HashSpan(1469598103934665603ull, d.view, d.fp_start, d.fp_size);
    if (d.fp_size <= kMaxRawBytes) {
        if (std::FILE* f = std::fopen(Path(hash, ".win").c_str(), "wb")) {
            ok = WriteSpan(f, d.view, d.fp_start, d.fp_size) && ok;
            std::fclose(f);
            ++stats_.written;
        }
    }
    if (Indexed(v.psm)) {
        clut_hash = HashSpan(1469598103934665603ull, d.view, v.cbp, kPageBytes);
        if (std::FILE* f = std::fopen(Path(hash, ".clut").c_str(), "wb")) {
            ok = WriteSpan(f, d.view, v.cbp, kPageBytes) && ok;
            std::fclose(f);
            ++stats_.written;
        }
    }
    return WriteJson(d, hash, ob, win_hash, clut_hash) && ok;
}

bool TextureDumpSink::WriteJson(const Decode& d, uint64_t hash, const OracleB& ob, uint64_t win_hash, uint64_t clut_hash) {
    const TexReadView& v = *d.tex;
    std::FILE* f = std::fopen(Path(hash, ".json").c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"hash\": \"%016llx\",\n", static_cast<unsigned long long>(hash));
    std::fprintf(f, "  \"tbp0\": %u, \"tbw\": %u, \"psm\": %u, \"tw\": %u, \"th\": %u, \"tcc\": %u, \"tfx\": %u,\n",
                 v.tbp % kVramBytes, v.tbw, v.psm, v.tw, v.th, v.tcc, v.tfx);
    std::fprintf(f, "  \"cbp\": %u, \"cpsm\": %u, \"csm\": %u, \"csa\": %u, \"cld\": %u,\n", v.cbp % kVramBytes, v.cpsm, v.csm,
                 v.csa, v.cld);
    std::fprintf(f, "  \"texa\": {\"ta0\": %u, \"ta1\": %u, \"aem\": %d},\n", v.texa_ta0, v.texa_ta1, v.texa_aem ? 1 : 0);
    std::fprintf(f, "  \"clamp\": {\"wms\": %u, \"wmt\": %u, \"minu\": %u, \"maxu\": %u, \"minv\": %u, \"maxv\": %u},\n", v.wms,
                 v.wmt, v.minu, v.maxu, v.minv, v.maxv);
    std::fprintf(f, "  \"tex1\": {\"mmag\": %d, \"mmin\": %u, \"mxl\": %u, \"lcm\": %d, \"l\": %u, \"k\": %d},\n", v.mmag ? 1 : 0,
                 v.mmin, v.mxl, v.lcm ? 1 : 0, v.l, static_cast<int>(v.k));
    std::fprintf(f, "  \"miptbp\": [");
    for (int i = 0; i < 6; ++i) std::fprintf(f, "%s[%u, %u]", i ? ", " : "", v.mip_tbp[i] % kVramBytes, v.mip_tbw[i]);
    std::fprintf(f, "],\n");
    std::fprintf(f, "  \"w\": %d, \"h\": %d, \"sub\": %d, \"x0\": %d, \"y0\": %d,\n", d.dw, d.dh, d.sub ? 1 : 0, d.sub ? d.x0 : 0,
                 d.sub ? d.y0 : 0);
    std::fprintf(f, "  \"window_start\": %u, \"window_size\": %u, \"window_hash\": \"%016llx\", \"clut_hash\": \"%016llx\",\n",
                 d.fp_start % kVramBytes, d.fp_size, static_cast<unsigned long long>(win_hash),
                 static_cast<unsigned long long>(clut_hash));
    std::fprintf(f, "  \"first_frame\": %u, \"first_draw\": %u,\n", d.frame_count, d.draw_seq);
    std::fprintf(f, "  \"oracle_b\": \"%s\", \"oracle_b_diffs\": %u, \"oracle_b_first_xy\": [%d, %d],\n",
                 ob.equal ? "equal" : "differs", ob.diffs, ob.first_x, ob.first_y);
    if (Indexed(v.psm)) {
        uint32_t palette[256];
        const uint32_t entries = (v.psm == 0x13 || v.psm == 0x1b) ? 256u : 16u;
        if (TextureDecoder::ReadPalette(d.view, v, nullptr, nullptr, palette)) {
            std::fprintf(f, "  \"palette\": [");
            for (uint32_t i = 0; i < entries; ++i) std::fprintf(f, "%s\"%08x\"", i ? ", " : "", palette[i]);
            std::fprintf(f, "],\n");
        }
    }
    std::fprintf(f, "  \"files\": {\"ppm\": \"%016llx.ppm\", \"alpha\": \"%016llx.alpha.pgm\", \"win\": %s, \"clut\": %s%s}\n",
                 static_cast<unsigned long long>(hash), static_cast<unsigned long long>(hash),
                 d.fp_size <= kMaxRawBytes ? "true" : "false", Indexed(v.psm) ? "true" : "false",
                 ob.equal ? "" : ", \"ref\": true");
    std::fprintf(f, "}\n");
    std::fclose(f);
    ++stats_.written;
    return true;
}

}  // namespace gs
}  // namespace ps2r

// Vu1LayoutKey -- the loaded-program layout key of the VU1 HLE registry: on a
// synthetic family and a two-program catalog, (1) the analyzer identifies the
// programs loaded back to back from variant_base and hashes exactly those
// bytes (layout_hash), so two images with the same programs under DIFFERENT
// stale tails share the layout_hash while their variant_hash differs;
// (2) a stale tail that is not a catalog program ends the walk; (3) with no
// catalog program at variant_base layout_hash is 0 and no layout row matches;
// (4) Lookup precedence image -> variant -> layout -> wildcard, with the
// start_pc filter applying to layout rows; (5) the runtime hands the analysis
// to the program (Vu1HleInput::info) and a layout-keyed row RUNS the program
// where the variant key alone would MISS.
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/vu/hle/vu1_hle_runtime.h"
#include "engine/vu/hle/vu1_image_analyzer.h"
#include "engine/vu/hle/vu1_registry.h"
#include "engine/vu/vu_image_hash.h"
#include "engine/vu/vu_unit.h"
#include "tools/vu1_replay/xgk_capture_sink.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                                     \
        }                                                                 \
    } while (0)

namespace {

constexpr uint32_t kBase = 0x100;  // the synthetic variant base
constexpr uint32_t kSizeA = 64, kSizeB = 32;

void Fill(uint8_t* p, uint32_t n, uint8_t seed) {
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + i * 7 + 1);  // never a zero bundle
}

// Two synthetic programs and their catalog rows (a POD; MakeImages fills it).
struct Images {
    uint8_t a[kSizeA], b[kSizeB];
    Vu1LoadedProgram catalog[2];
};

Images MakeImages() {
    Images im{};
    Fill(im.a, kSizeA, 0x11);
    Fill(im.b, kSizeB, 0x55);
    im.catalog[0] = {VuImageHash::Fast(im.a, kSizeA), kSizeA, "prog_a"};
    im.catalog[1] = {VuImageHash::Fast(im.b, kSizeB), kSizeB, "prog_b"};
    return im;
}

// micro = prefix (non-zero) | programs at kBase | tail (stale bytes, seed-dependent)
std::vector<uint8_t> Micro(const Images& im, bool with_b, uint32_t tail_bytes, uint8_t tail_seed) {
    std::vector<uint8_t> m(kVu1HleMicroSize, 0);
    Fill(m.data(), kBase, 0x01);
    uint32_t pos = kBase;
    std::memcpy(m.data() + pos, im.a, kSizeA);
    pos += kSizeA;
    if (with_b) {
        std::memcpy(m.data() + pos, im.b, kSizeB);
        pos += kSizeB;
    }
    Fill(m.data() + pos, tail_bytes, tail_seed);
    return m;
}

const Vu1ImageInfo* g_seen_info = nullptr;
bool SeesInfo(const Vu1HleInput& in, Vu1HleOutput& out) {
    g_seen_info = in.info;
    std::vector<uint8_t> t(16, 0);
    t[1] = 0x80;  // EOP, NLOOP 0
    out.kicks.push_back(t);
    return true;
}
bool Never(const Vu1HleInput&, Vu1HleOutput&) { return false; }

void Analyzer() {
    const Images im = MakeImages();
    const Vu1FamilySignature fam = {kBase, 0xDEADBEEF, {0x70, 0x90}, 0xA0};
    const Vu1ProgramLibrary lib = {fam, nullptr, 0, im.catalog, 2};
    Vu1ImageAnalyzer an(lib);

    const auto m1 = Micro(im, true, 48, 0x80);   // A, B, stale tail 1
    const auto m2 = Micro(im, true, 200, 0xC0);  // A, B, a different stale tail
    const auto m3 = Micro(im, false, 48, 0x80);  // A alone, then stale bytes (not B)
    const Vu1ImageInfo i1 = an.Analyze(m1.data(), nullptr), i2 = an.Analyze(m2.data(), nullptr),
                       i3 = an.Analyze(m3.data(), nullptr);
    CHECK(i1.layout.size() == 2 && i1.layout[0].pc == kBase && i1.layout[0].program == 0 &&
          i1.layout[1].pc == kBase + kSizeA && i1.layout[1].program == 1);
    CHECK(i1.loaded_end == kBase + kSizeA + kSizeB);
    CHECK(i1.layout_hash == VuImageHash::Fast(m1.data() + kBase, kSizeA + kSizeB));
    CHECK(i1.layout_hash == i2.layout_hash);      // the stale tail does not move the key
    CHECK(i1.variant_hash != i2.variant_hash);    // ... while the variant key follows it
    CHECK(i1.image_hash != i2.image_hash);
    CHECK(i3.layout.size() == 1 && i3.loaded_end == kBase + kSizeA);
    CHECK(i3.layout_hash == im.catalog[0].code_hash);  // one program: its own code hash
    CHECK(i3.layout_hash != i1.layout_hash);

    // No catalog program at the base: no layout.
    std::vector<uint8_t> m4(kVu1HleMicroSize, 0);
    Fill(m4.data(), kBase + 80, 0x33);
    const Vu1ImageInfo i4 = an.Analyze(m4.data(), nullptr);
    CHECK(i4.layout.empty() && i4.layout_hash == 0 && i4.loaded_end == kBase);

    // Without a catalog the analysis carries no layout at all.
    Vu1ImageAnalyzer plain(fam);
    const Vu1ImageInfo p1 = plain.Analyze(m1.data(), nullptr);
    CHECK(p1.layout.empty() && p1.layout_hash == 0 && p1.variant_hash == i1.variant_hash);
    std::printf("analyzer: layout A+B=%016llx A=%016llx variant1=%016llx variant2=%016llx\n",
                (unsigned long long)i1.layout_hash, (unsigned long long)i3.layout_hash,
                (unsigned long long)i1.variant_hash, (unsigned long long)i2.variant_hash);
}

void Precedence() {
    const Vu1FamilySignature fam = {kBase, 0xDEADBEEF, {0x70, 0x90}, 0xA0};
    static const Vu1LibraryEntry kRows[] = {
        {0x1111, 0, "by_image", 0x0, Never, nullptr, 0},
        {0, 0x2222, "by_variant", 0x0, Never, nullptr, 0},
        {0, 0, "by_layout_pc0", 0x0, Never, nullptr, 0, 0x3333},
        {0, 0, "by_layout_pc8", 0x8, Never, nullptr, 0, 0x3333},
        {0, 0, "by_layout_any", kVu1AnyEntry, Never, nullptr, 0, 0x4444},
        {0, 0, "wildcard", kVu1AnyEntry, Never, nullptr, 0},
    };
    const Vu1ProgramLibrary lib = {fam, kRows, 6};
    Vu1Registry reg(lib);
    CHECK(reg.LayoutEntries() == 3);
    CHECK(reg.DistinctVariantHashes() == 1);  // the layout / wildcard rows carry none
    auto name = [&](uint64_t img, uint64_t var, uint64_t lay, uint32_t pc) {
        const Vu1LibraryEntry* e = reg.Lookup(img, var, lay, pc);
        return e ? std::string(e->name) : std::string("none");
    };
    CHECK(name(0x1111, 0x2222, 0x3333, 0) == "by_image");
    CHECK(name(0x9999, 0x2222, 0x3333, 0) == "by_variant");
    CHECK(name(0x9999, 0x8888, 0x3333, 0) == "by_layout_pc0");
    CHECK(name(0x9999, 0x8888, 0x3333, 8) == "by_layout_pc8");
    CHECK(name(0x9999, 0x8888, 0x3333, 16) == "wildcard");  // pc filter applies to layout rows
    CHECK(name(0x9999, 0x8888, 0x4444, 16) == "by_layout_any");
    CHECK(name(0x9999, 0x8888, 0, 0) == "wildcard");         // no layout: never a layout row
    CHECK(name(0x9999, 0x8888, 0x7777, 0) == "wildcard");
    // The 3-argument form is the layout-less lookup.
    const Vu1LibraryEntry* e = reg.Lookup(0x9999, 0x8888, 0);
    CHECK(e && std::string(e->name) == "wildcard");
}

void RuntimeRunsLayoutRow() {
    const Images im = MakeImages();
    const Vu1FamilySignature fam = {kBase, 0xDEADBEEF, {0x70, 0x90}, 0xA0};
    const auto m1 = Micro(im, true, 48, 0x80);
    const auto m2 = Micro(im, true, 200, 0xC0);
    const uint64_t layout = VuImageHash::Fast(m1.data() + kBase, kSizeA + kSizeB);
    // A variant row for image 1's tail only, and a layout row for the programs.
    std::vector<uint8_t> ext1 = m1;
    uint32_t extent = kVu1HleMicroSize;
    while (extent > 0 && std::memcmp(ext1.data() + extent - 8, "\0\0\0\0\0\0\0\0", 8) == 0) extent -= 8;
    const uint64_t variant1 = VuImageHash::Fast(m1.data() + kBase, extent - kBase);
    const Vu1LibraryEntry rows[] = {
        {0, variant1, "variant_row", 0x0, SeesInfo, nullptr, 0},
        {0, 0, "layout_row", 0x0, SeesInfo, nullptr, 0, layout},
    };
    const Vu1ProgramLibrary lib = {fam, rows, 2, im.catalog, 2};

    XgkCaptureSink sink;
    GifRegs regs;
    GifParser parser(sink, regs);
    GifArbiter arbiter(parser);
    sink.Attach(arbiter);
    Vu1HleRuntime rt(lib, arbiter);
    VuUnit vu(1, &arbiter);

    // Image 1: the variant row fires; the program receives the analysis.
    std::memcpy(vu.state().micro, m1.data(), kVu1HleMicroSize);
    g_seen_info = nullptr;
    CHECK(rt.TryExecute(vu, 0, 1));
    CHECK(g_seen_info != nullptr && g_seen_info->layout.size() == 2 && g_seen_info->layout_hash == layout);
    // Image 2 (another stale tail): no variant row -- the layout row runs it.
    std::memcpy(vu.state().micro, m2.data(), kVu1HleMicroSize);
    g_seen_info = nullptr;
    CHECK(rt.TryExecute(vu, 0, 2));
    CHECK(g_seen_info != nullptr && g_seen_info->layout_hash == layout);
    CHECK(rt.census().runs == 2 && rt.census().misses == 0);
    // Without the catalog the same image MISSES (the variant key alone).
    const Vu1ProgramLibrary no_catalog = {fam, rows, 2};
    Vu1HleRuntime rt2(no_catalog, arbiter);
    CHECK(!rt2.TryExecute(vu, 0, 3));
    CHECK(rt2.census().misses == 1 && rt2.census().miss_kinds.size() == 1);
    std::printf("runtime: variant row + layout row runs=%llu misses=%llu; no catalog misses=%llu\n",
                (unsigned long long)rt.census().runs, (unsigned long long)rt.census().misses,
                (unsigned long long)rt2.census().misses);
}

}  // namespace

int main() {
    Analyzer();
    Precedence();
    RuntimeRunsLayoutRow();
    std::printf("Vu1LayoutKey: %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}

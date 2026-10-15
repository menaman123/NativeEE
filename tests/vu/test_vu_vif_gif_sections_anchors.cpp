// VuVifGifSectionsAnchors: for each of the 11 frozen anchor states (OLD
// save_states, read-only; the rows of restore_mhash_expected.tsv) find the
// eight blobs VU0C VU0D VU1C VU1D DMAC VIFS GIFS VU1R, check every one is
// marked REQUIRED and has the exact reference length, validate + restore each
// through this tree's codecs into a fresh rig (the registry, in the root's
// registration order), then re-serialize and require byte identity with the
// file's payload -- except VU1R byte 0, the reference's MTVU-worker running
// flag (1 in every anchor), which this tree writes as 0; the other 1604 bytes
// must be identical. argv: <expected.tsv> <anchor_dir>. Exit 77 when the anchor
// directory is absent.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "engine/core/savestate/savestate_format.h"
#include "engine/core/savestate/section_registry.h"
#include "engine/dmac/dmac.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/gif/savestate/gifs_section.h"
#include "engine/vif/savestate/vifs_section.h"
#include "engine/vif/vif.h"
#include "engine/vu/savestate/vu1_register_section.h"
#include "engine/vu/savestate/vu_memory_section.h"
#include "engine/vu/vu_unit.h"

namespace {

using ps2r::savestate::Blob;

struct TestVuVifGifSectionsAnchors {
    MainMemory ram;
    Scratchpad spad;
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0;
    VuUnit vu1;
    Vif vif;
    Dmac dmac;
    VuMemorySection vu_mem;
    VifsSection vifs;
    GifsSection gifs;
    Vu1RegisterSection vu1r;
    ps2r::savestate::SectionRegistry registry;
    TestVuVifGifSectionsAnchors()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter), vu_mem(vu0, vu1), vifs(vif), gifs(gif_regs), vu1r(vu1) {
        // The root's order for the tags this rig owns.
        vu_mem.Register(registry);
        registry.Register(dmac.SavestateSection());
        vifs.Register(registry);
        gifs.Register(registry);
        vu1r.Register(registry);
    }
};
using Rig = TestVuVifGifSectionsAnchors;

struct Expect {
    uint32_t tag;
    const char* name;
    size_t bytes;
};
const Expect kExpect[] = {
    {ps2r::savestate::TAG_VU0C, "VU0C", 4096},  {ps2r::savestate::TAG_VU0D, "VU0D", 4096},
    {ps2r::savestate::TAG_VU1C, "VU1C", 16384}, {ps2r::savestate::TAG_VU1D, "VU1D", 16384},
    {ps2r::savestate::TAG_DMAC, "DMAC", 324},   {ps2r::savestate::TAG_VIFS, "VIFS", 184},
    {ps2r::savestate::TAG_GIFS, "GIFS", 13},    {ps2r::savestate::TAG_VU1R, "VU1R", 1605},
};

std::vector<std::string> ReadAnchorNames(const char* tsv) {
    std::vector<std::string> names;
    std::ifstream in(tsv);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        names.push_back(tab == std::string::npos ? line : line.substr(0, tab));
    }
    return names;
}

const Blob* Find(const std::vector<Blob>& v, uint32_t tag) {
    for (const Blob& b : v)
        if (b.tag == tag) return &b;
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_vu_vif_gif_sections_anchors <expected.tsv> <anchor_dir>\n");
        return 2;
    }
    const std::vector<std::string> names = ReadAnchorNames(argv[1]);
    if (names.empty()) {
        std::fprintf(stderr, "no rows in %s\n", argv[1]);
        return 1;
    }
    {
        FILE* probe = std::fopen((std::string(argv[2]) + "/" + names[0]).c_str(), "rb");
        if (!probe) {
            std::printf("SKIP: anchor dir %s absent\n", argv[2]);
            return 77;
        }
        std::fclose(probe);
    }

    int failures = 0;
    int decoded = 0;
    size_t anchors = 0;
    std::printf("| anchor | 8 blobs present+REQUIRED | exact lengths | validate+restore | re-serialize |\n|---|---|---|---|---|\n");
    for (const std::string& name : names) {
        std::string err;
        ps2r::savestate::FileMeta meta;
        std::vector<Blob> sections;
        const std::string path = std::string(argv[2]) + "/" + name;
        // Rows approving boot imports outside the anchor directory (the coldboot
        // probe under verify/baseline/coldboot/probe) are not this test's anchors.
        if (FILE* present = std::fopen(path.c_str(), "rb")) std::fclose(present);
        else { std::printf("| %s | not in anchor dir, skipped | | | |\n", name.c_str()); continue; }
        ++anchors;
        if (!ps2r::savestate::ReadStateFile(path, &meta, &sections, &err)) {
            std::printf("| %s | PARSE FAILED: %s | | | |\n", name.c_str(), err.c_str());
            ++failures;
            continue;
        }
        bool present = true, lengths = true;
        std::vector<Blob> owned;
        for (const Expect& e : kExpect) {
            const Blob* b = Find(sections, e.tag);
            if (!b || (b->flags & ps2r::savestate::kSectionRequired) == 0) {
                present = false;
                continue;
            }
            if (b->payload.size() != e.bytes) {
                lengths = false;
                std::printf("  %s: %s is %zu bytes, expected %zu\n", name.c_str(), e.name, b->payload.size(), e.bytes);
            }
            owned.push_back(*b);
        }
        Rig rig;
        const bool restored = present && lengths && rig.registry.Restore(owned, &err);
        bool identical = restored;
        int anchor_running = -1;
        if (restored) {
            const std::vector<Blob> again = rig.registry.Capture();
            for (const Expect& e : kExpect) {
                const Blob* a = Find(owned, e.tag);
                const Blob* b = Find(again, e.tag);
                if (!a || !b) {
                    identical = false;
                    continue;
                }
                if (e.tag == ps2r::savestate::TAG_VU1R) {
                    // The reference captured these with its MTVU worker running
                    // (byte 0 = 1); this tree writes running = 0 under the
                    // deterministic profile. Every other byte -- the size word,
                    // the EE-context padding, the VUregs block -- must be identical.
                    anchor_running = a->payload[0];
                    const bool rest_same = a->payload.size() == b->payload.size() &&
                                           std::memcmp(a->payload.data() + 1, b->payload.data() + 1, a->payload.size() - 1) == 0;
                    if (!rest_same || b->payload[0] != 0) {
                        identical = false;
                        std::printf("  %s: VU1R re-serialize differs beyond the running byte\n", name.c_str());
                    }
                    continue;
                }
                if (a->payload != b->payload) {
                    identical = false;
                    std::printf("  %s: %s re-serialize differs\n", name.c_str(), e.name);
                }
            }
            // The VU1R image's VUregs block landed in the unit; the memories landed raw.
            const Blob* v = Find(owned, ps2r::savestate::TAG_VU1R);
            if (std::memcmp(v->payload.data() + 5 + Vu1RegisterSection::kVuRegsOffset, &rig.vu1.regs(), Vu1RegisterSection::kVuRegsBytes) != 0 ||
                std::memcmp(Find(owned, ps2r::savestate::TAG_VU1C)->payload.data(), rig.vu1.state().micro, 16384) != 0 ||
                std::memcmp(Find(owned, ps2r::savestate::TAG_VU0D)->payload.data(), rig.vu0.state().data, 4096) != 0) {
                identical = false;
                std::printf("  %s: restored state does not match the payloads\n", name.c_str());
            }
            if (!rig.vif.IsIdle(0) || !rig.vif.IsIdle(1) || rig.gif_regs.Transfer().in_transfer) {
                identical = false;
                std::printf("  %s: VIF / GIF not idle after restore\n", name.c_str());
            }
        }
        const bool ok = present && lengths && restored && identical;
        if (ok) ++decoded;
        else ++failures;
        std::printf("| %s | %s | %s | %s%s%s | %s (VU1R running: anchor %d -> 0) |\n", name.c_str(), present ? "yes" : "NO",
                    lengths ? "yes" : "NO", restored ? "ok" : "FAILED", restored ? "" : ": ", restored ? "" : err.c_str(),
                    identical ? "identical" : "DIFFERS", anchor_running);
    }
    std::printf("anchors decode: %d/%zu (vu memories %u, vifs %u, gifs %u, vu1r %u per anchor rig)\n", decoded, anchors, 4u, 1u, 1u, 1u);
    if (anchors == 0) ++failures;
    std::printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

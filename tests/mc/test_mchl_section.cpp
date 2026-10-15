// MchlSection: the MCHL savestate section over McCommandModel.
//   roundtrip  -- serialize a model with a pending pair and seen flags through
//                 the SectionRegistry, restore into a fresh model, every field
//                 equal; the payload is exactly 12 bytes in the wire order; a
//                 wrong-length blob is refused; the absent path leaves boot
//                 state; the restored pair is delivered by the next Sync.
//   anchors <dir> <expected.tsv> -- every frozen anchor listed in the table
//                 carries a 12-byte MCHL blob the codec validates and decodes
//                 (skip 77 when the anchor directory is absent).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "engine/core/savestate/savestate_format.h"
#include "engine/core/savestate/section_registry.h"
#include "engine/mc/hle/mc_command_model.h"
#include "engine/mc/savestate/mchl_section.h"

using ps2r::savestate::Blob;
using ps2r::savestate::SectionRegistry;

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static int RunRoundTrip() {
    McCommandModel src(0);
    src.SetBound(true);
    src.Issue(McCommandModel::kCmdOpen, 3);   // an undelivered Open completion, fd 3
    (void)src.GetInfo(0, 0);                  // port 0 slot 0 has answered a probe
    (void)src.GetInfo(1, 1);
    MchlSection src_section(src);
    SectionRegistry src_reg;
    src_section.Register(src_reg);
    CHECK(src_reg.Count() == 1);
    CHECK(src_reg.Specs()[0].tag == ps2r::savestate::TAG_MCHL);
    CHECK(!src_reg.Specs()[0].required);

    const std::vector<Blob> sections = src_reg.Capture();
    CHECK(sections.size() == 1);
    const std::vector<uint8_t>& p = sections[0].payload;
    CHECK(p.size() == MchlSection::kPayloadBytes);
    CHECK(p.size() == 12);
    // Wire order: i32 cmd, i32 result, u8 seen[0][0], [0][1], [1][0], [1][1].
    const uint8_t expect[12] = {0x02, 0, 0, 0, 0x03, 0, 0, 0, 1, 0, 0, 1};
    CHECK(std::memcmp(p.data(), expect, 12) == 0);

    McCommandModel dst(0);
    MchlSection dst_section(dst);
    SectionRegistry dst_reg;
    dst_section.Register(dst_reg);
    std::string err;
    CHECK(dst_reg.Validate(sections, &err));
    CHECK(dst_reg.Restore(sections, &err));
    CHECK(err.empty());
    CHECK(dst.PendingCmd() == McCommandModel::kCmdOpen);
    CHECK(dst.PendingResult() == 3);
    CHECK(dst.Seen(0, 0) == true);
    CHECK(dst.Seen(0, 1) == false);
    CHECK(dst.Seen(1, 0) == false);
    CHECK(dst.Seen(1, 1) == true);
    // Re-serialize the restored model: identical bytes.
    const std::vector<Blob> again = dst_reg.Capture();
    CHECK(again.size() == 1 && again[0].payload == p);
    // The restored pair is what the next sceMcSync delivers.
    const McCommandModel::SyncResult s = dst.Sync(1);
    CHECK(s.ret == 1);
    CHECK(s.cmd == McCommandModel::kCmdOpen);
    CHECK(s.result == 3);
    // A seen slot reports "same card"; an unseen one the detection edge.
    CHECK(dst.GetInfo(0, 0).result == McCommandModel::kResSameCard);
    CHECK(dst.GetInfo(0, 1).result == McCommandModel::kResChangedFormatted);

    // A wrong-length blob is refused by validate.
    std::vector<Blob> bad = sections;
    bad[0].payload.pop_back();
    CHECK(!dst_reg.Validate(bad, &err));
    CHECK(err.find("MCHL") != std::string::npos);

    // Absent: a file without MCHL leaves a fresh model in boot state.
    McCommandModel fresh(0);
    MchlSection fresh_section(fresh);
    SectionRegistry fresh_reg;
    fresh_section.Register(fresh_reg);
    const std::vector<Blob> none;
    CHECK(fresh_reg.Restore(none, &err));
    CHECK(fresh.PendingCmd() == McCommandModel::kCmdNone);
    CHECK(fresh.PendingResult() == 0);
    CHECK(!fresh.Seen(0, 0) && !fresh.Seen(0, 1) && !fresh.Seen(1, 0) && !fresh.Seen(1, 1));
    return 0;
}

static std::vector<std::string> AnchorNames(const std::string& tsv) {
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

static int RunAnchors(const std::string& dir, const std::string& tsv) {
    const std::vector<std::string> names = AnchorNames(tsv);
    if (names.empty()) {
        std::printf("SKIP: no anchors listed in %s\n", tsv.c_str());
        return 77;
    }
    int decoded = 0;
    size_t anchors = 0;
    for (const std::string& n : names) {
        const std::string path = dir + "/" + n;
        ps2r::savestate::FileMeta meta;
        std::vector<Blob> sections;
        std::string err;
        // Rows approving boot imports outside the anchor directory (the coldboot
        // probe under verify/baseline/coldboot/probe) are not this test's anchors.
        if (FILE* present = std::fopen(path.c_str(), "rb")) std::fclose(present);
        else { std::printf("%s: not in anchor dir, skipped\n", n.c_str()); continue; }
        ++anchors;
        if (!ps2r::savestate::ReadStateFile(path, &meta, &sections, &err)) {
            std::printf("SKIP: cannot read anchor %s (%s)\n", path.c_str(), err.c_str());
            return 77;
        }
        const Blob* mchl = nullptr;
        for (const Blob& b : sections)
            if (b.tag == ps2r::savestate::TAG_MCHL) mchl = &b;
        CHECK(mchl != nullptr);
        if (!mchl) continue;
        CHECK(mchl->payload.size() == MchlSection::kPayloadBytes);
        CHECK(MchlSection::Validate(mchl->payload, &err));
        McCommandModel model(0);
        MchlSection section(model);
        CHECK(section.Restore(mchl->payload, &err));
        // Every frozen anchor was taken with the boot-time GetInfo kick still
        // pending (cmd 1, result 0) and only port 0 slot 0 probed.
        CHECK(model.PendingCmd() == McCommandModel::kCmdGetInfo);
        CHECK(model.PendingResult() == 0);
        CHECK(model.Seen(0, 0));
        CHECK(!model.Seen(0, 1) && !model.Seen(1, 0) && !model.Seen(1, 1));
        // Re-serializing the decoded state reproduces the anchor's bytes.
        std::vector<uint8_t> again;
        section.Serialize(again);
        CHECK(again == mchl->payload);
        decoded++;
    }
    if (anchors == 0) {
        std::printf("SKIP: no anchors of %s present in %s\n", tsv.c_str(), dir.c_str());
        return 77;
    }
    std::printf("anchors decoded: %d of %zu\n", decoded, anchors);
    CHECK(decoded == static_cast<int>(anchors));
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s roundtrip | anchors <dir> <expected.tsv>\n", argv[0]);
        return 2;
    }
    const std::string mode = argv[1];
    int rc = 2;
    if (mode == "roundtrip") {
        rc = RunRoundTrip();
    } else if (mode == "anchors" && argc >= 4) {
        rc = RunAnchors(argv[2], argv[3]);
    }
    if (rc == 77) return 77;
    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS MchlSection %s\n", mode.c_str());
    return 0;
}

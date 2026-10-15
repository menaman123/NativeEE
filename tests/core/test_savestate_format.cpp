// tests/core/test_savestate_format.cpp -- PS2SAVE1 envelope: synthetic
// round-trip + every validation failure; SectionRegistry restore policy;
// SavestateWriter async + atomic; and the FROZEN-format proof: every anchor
// .ps2st under <og-root>/save_states (read-only) parses and re-serializes
// byte-identical through core/savestate (SKIPs when the OLD tree is absent).
//
// argv[1] = scratch dir, argv[2] = OLD root (optional).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/savestate/payload_reader.h"
#include "engine/core/savestate/savestate_format.h"
#include "engine/core/savestate/savestate_writer.h"
#include "engine/core/savestate/section_registry.h"

using namespace ps2r::savestate;

static int g_fail = 0;
#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); \
            g_fail++;                                                             \
        }                                                                         \
    } while (0)

static std::string g_scratch;

static bool ReadAll(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t buf[1 << 16];
    for (;;) {
        const size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n) out->insert(out->end(), buf, buf + n);
        if (n < sizeof(buf)) break;
    }
    std::fclose(f);
    return true;
}

static bool Exists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

static std::vector<Blob> Sample() {
    std::vector<Blob> s;
    Blob a;
    a.tag = TAG_RAM;
    a.payload.assign(4096, 0);
    for (size_t i = 0; i < a.payload.size(); i++) a.payload[i] = (uint8_t)(i * 7);
    s.push_back(a);
    Blob b;
    b.tag = TAG_CNTR;
    b.version = 3;
    PutPod(b.payload, (uint32_t)1234);
    PutPod(b.payload, (uint64_t)0x1122334455667788ull);
    s.push_back(b);
    Blob c;  // empty optional section
    c.tag = Tag('O', 'P', 'T', '1');
    c.flags = 0;
    s.push_back(c);
    return s;
}

static void TestCrcAndLayout() {
    // CRC32 IEEE check value for "123456789".
    CHECK(Crc32("123456789", 9) == 0xCBF43926u);
    CHECK(Crc32("", 0) == 0);
    // Chaining over two buffers equals one pass.
    const char* s = "hello world";
    const uint32_t one = Crc32(s, 11);
    const uint32_t two = Crc32(s + 5, 6, Crc32(s, 5));
    CHECK(one == two);
    CHECK(sizeof(FileHeader) == 32 && sizeof(SectionHeader) == 24);
    CHECK(TagName(TAG_RAM) == "RAM0" && TagName(TAG_SNDC) == "SNDC");
    CHECK(TAG_RAM == 0x304D4152u);   // 'R' low byte
}

static void TestRoundTripAndValidation() {
    FileMeta m;
    m.vsync_frame = 777;
    m.flags = 5;
    m.wall_ms = 123456789ull;
    m.file_version = kFileVersionSnd;
    const std::vector<Blob> secs = Sample();
    const std::vector<uint8_t> img = SerializeStateFile(m, secs);
    CHECK(img.size() == 32 + 3 * 24 + 4096 + 12 + 4);

    FileMeta rm;
    std::vector<Blob> rs;
    std::string err;
    CHECK(ParseStateFile(img.data(), img.size(), &rm, &rs, &err));
    CHECK(rm.vsync_frame == 777 && rm.flags == 5 && rm.wall_ms == 123456789ull && rm.file_version == 4);
    CHECK(rs.size() == 3 && rs[0].payload == secs[0].payload && rs[1].version == 3 && rs[2].flags == 0);
    CHECK(SerializeStateFile(rm, rs) == img);   // re-serialization is exact

    // Corrupt payload byte -> section CRC.
    std::vector<uint8_t> bad = img;
    bad[32 + 24 + 10] ^= 1;
    CHECK(!ParseStateFile(bad.data(), bad.size(), nullptr, nullptr, &err) && err.find("CRC mismatch in section 'RAM0'") == 0);
    // Corrupt a section header byte -> trailer CRC (payload crc still matches).
    bad = img;
    bad[32 + 4] ^= 1;   // SectionHeader.version of RAM0
    CHECK(!ParseStateFile(bad.data(), bad.size(), nullptr, nullptr, &err) && err.find("trailer CRC") != std::string::npos);
    // Truncations.
    CHECK(!ParseStateFile(img.data(), 10, nullptr, nullptr, &err) && err == "truncated: header");
    CHECK(!ParseStateFile(img.data(), 40, nullptr, nullptr, &err) && err == "truncated: section header 0");
    CHECK(!ParseStateFile(img.data(), 100, nullptr, nullptr, &err) && err == "truncated: section payload 0");
    CHECK(!ParseStateFile(img.data(), img.size() - 1, nullptr, nullptr, &err) && err == "truncated: trailer");
    // Magic / version.
    bad = img;
    bad[0] = 'X';
    CHECK(!ParseStateFile(bad.data(), bad.size(), nullptr, nullptr, &err) && err.find("bad magic") == 0);
    FileMeta m9 = m;
    m9.file_version = 9;
    const std::vector<uint8_t> v9 = SerializeStateFile(m9, secs);
    CHECK(!ParseStateFile(v9.data(), v9.size(), nullptr, nullptr, &err) && err.find("unsupported version 9") == 0);
    // Unknown REQUIRED tag policy.
    const uint32_t known[] = {TAG_RAM};
    CHECK(!ParseStateFile(img.data(), img.size(), nullptr, nullptr, &err, known, 1) &&
          err.find("unknown REQUIRED section 'CNTR'") == 0);
    const uint32_t known2[] = {TAG_RAM, TAG_CNTR};   // OPT1 is not required -> accepted
    CHECK(ParseStateFile(img.data(), img.size(), nullptr, nullptr, &err, known2, 2));

    // File path write (atomic) + read.
    const std::string path = g_scratch + "/rt.ps2st";
    std::remove(path.c_str());
    CHECK(WriteStateFile(path, m, secs, &err));
    CHECK(!Exists(path + ".tmp"));
    std::vector<uint8_t> disk;
    CHECK(ReadAll(path, &disk) && disk == img);
    CHECK(ReadStateFile(path, &rm, &rs, &err) && rs.size() == 3);
    CHECK(!WriteStateFile(g_scratch + "/no/such/dir/x.ps2st", m, secs, &err) && err.find("cannot open") == 0);
    CHECK(!ReadStateFile(g_scratch + "/missing.ps2st", nullptr, nullptr, &err) && err.find("cannot open") == 0);

    // PayloadReader.
    PayloadReader r(rs[1].payload);
    uint32_t u = 0;
    uint64_t w = 0;
    CHECK(r.Get(&u) && u == 1234 && r.Get(&w) && w == 0x1122334455667788ull && r.Remaining() == 0);
    CHECK(!r.Get(&u) && !r.Skip(1) && r.Offset() == 12);
}

static void TestRegistry() {
    SectionRegistry reg;
    int ram_restored = 0, cntr_restored = 0;
    std::vector<uint8_t> ram_state(16, 0xAB);
    SectionSpec ram;
    ram.tag = TAG_RAM;
    ram.owner = "ee/mem";
    ram.serialize = [&](std::vector<uint8_t>& out) { PutBytes(out, ram_state.data(), ram_state.size()); };
    ram.validate = [&](const Blob& b, std::string* e) {
        if (b.payload.size() != 16) { *e = "size"; return false; }
        return true;
    };
    ram.restore = [&](const Blob& b, std::string*) { ram_state = b.payload; ram_restored++; return true; };
    SectionSpec cntr;
    cntr.tag = TAG_CNTR;
    cntr.version = 2;
    cntr.required = false;
    cntr.owner = "ee/timing";
    cntr.serialize = [&](std::vector<uint8_t>& out) { PutPod(out, (uint32_t)42); };
    cntr.restore = [&](const Blob&, std::string*) { cntr_restored++; return true; };
    CHECK(reg.Register(ram) && reg.Register(cntr) && !reg.Register(ram) && reg.Count() == 2);
    CHECK(reg.Has(TAG_RAM) && !reg.Has(TAG_SNDC) && reg.KnownTags().size() == 2);

    std::vector<Blob> cap = reg.Capture();
    CHECK(cap.size() == 2 && cap[0].tag == TAG_RAM && cap[0].flags == kSectionRequired && cap[0].payload.size() == 16);
    CHECK(cap[1].tag == TAG_CNTR && cap[1].version == 2 && cap[1].flags == 0 && cap[1].payload.size() == 4);
    CHECK(SectionRegistry::FileVersionFor(cap) == kFileVersion);
    Blob timr;
    timr.tag = TAG_TIMR;
    Blob sndc;
    sndc.tag = TAG_SNDC;
    CHECK(SectionRegistry::FileVersionFor({timr}) == kFileVersionTimr);
    CHECK(SectionRegistry::FileVersionFor({sndc, timr}) == kFileVersionSnd);

    std::string err;
    ram_state.assign(16, 0);
    CHECK(reg.Restore(cap, &err) && ram_restored == 1 && cntr_restored == 1 && ram_state[0] == 0xAB);
    // Unknown non-required skipped; unknown required refused.
    std::vector<Blob> extra = cap;
    Blob opt;
    opt.tag = Tag('Z', 'Z', 'Z', 'Z');
    opt.flags = 0;
    extra.push_back(opt);
    CHECK(reg.Restore(extra, &err));
    extra.back().flags = kSectionRequired;
    CHECK(!reg.Restore(extra, &err) && err.find("unknown REQUIRED section 'ZZZZ'") == 0);
    // Missing required refused; missing optional fine.
    CHECK(!reg.Restore({cap[1]}, &err) && err.find("missing required section 'RAM0'") == 0);
    CHECK(reg.Restore({cap[0]}, &err));
    // Validate failure names owner and section; nothing restored.
    const int before = ram_restored;
    std::vector<Blob> badsz = cap;
    badsz[0].payload.resize(8);
    CHECK(!reg.Restore(badsz, &err) && err.find("section 'RAM0' rejected by ee/mem: size") == 0 && ram_restored == before);
}

static void TestOwnerValidation() {
    SectionRegistry registry;
    unsigned applied = 0;
    SectionSpec source;
    source.tag = Tag('F','I','L','S');
    source.owner = "File fixture";
    source.serialize = [](auto& out) { out = {7}; };
    source.validate = [](const Blob& blob, std::string*) { return blob.payload.size() == 1; };
    source.restore = [&](const Blob&, std::string*) { ++applied; return true; };
    CHECK(registry.Register(source));
    SectionSpec consumer = source;
    consumer.tag = Tag('L','O','A','N');
    consumer.owner = "Loan fixture";
    consumer.required = false;
    consumer.capture_required = true;
    CHECK(registry.Register(consumer));
    const uint64_t revision = registry.Revision();
    auto match = [&](const std::vector<Blob>& blobs, std::string* error) {
        const Blob *file = nullptr, *loan = nullptr;
        for (const auto& blob : blobs) {
            if (blob.tag == source.tag) file = &blob;
            if (blob.tag == consumer.tag) loan = &blob;
        }
        if (!loan || (file && loan->payload == file->payload)) return true;
        if (error) *error = "Loan belongs to a different file generation.";
        return false;
    };
    CHECK(registry.RegisterValidation("file/loan ownership", match));
    CHECK(registry.Revision() == revision + 1);
    CHECK(!registry.RegisterValidation("file/loan ownership", match));
    CHECK(!registry.RegisterValidation("", match));
    const auto good = registry.Capture();
    CHECK(good[1].flags == kSectionRequired);
    std::string error;
    auto bad = good; bad[1].payload[0] = 8;
    CHECK(!registry.Restore(bad, &error) && applied == 0 && error.find("different file generation") != std::string::npos);
    bad = good; bad.push_back(good[0]);
    CHECK(!registry.Restore(bad, &error) && applied == 0);
    CHECK(registry.Restore(good, &error) && applied == 2);
    CHECK(registry.Restore({good[0]}, &error) && applied == 3); // Explicit legacy owner policy.
}

static void TestWriter() {
    const std::string p1 = g_scratch + "/async1.ps2st";
    const std::string p2 = g_scratch + "/async2.ps2st";
    std::remove(p1.c_str());
    std::remove(p2.c_str());
    FileMeta m;
    m.vsync_frame = 99;
    const std::vector<Blob> secs = Sample();
    const std::vector<uint8_t> expect = SerializeStateFile(m, secs);

    SavestateWriter w;
    CHECK(!w.Busy() && w.CompletedCount() == 0);
    w.Flush();   // never started: no-op
    WriteJob j;
    j.path = p1;
    j.meta = m;
    j.sections = secs;
    CHECK(w.TryEnqueue(std::move(j)));
    // Slot semantics: a second job while the first may still be in flight is
    // either accepted (slot already drained) or refused -- never queued twice.
    WriteJob j2;
    j2.path = p2;
    j2.meta = m;
    j2.sections = secs;
    const bool second = w.TryEnqueue(std::move(j2));
    w.Flush();
    CHECK(!w.Busy());
    std::vector<uint8_t> disk;
    CHECK(ReadAll(p1, &disk) && disk == expect);   // async == sync bytes
    CHECK(!Exists(p1 + ".tmp"));
    if (second) {
        CHECK(ReadAll(p2, &disk) && disk == expect && w.CompletedCount() == 2);
    } else {
        CHECK(!Exists(p2) && w.CompletedCount() == 1);
    }
    WriteResult r = w.LastResult();
    CHECK(r.ok && r.bytes == expect.size());
    // Failure is reported, not thrown.
    WriteJob j3;
    j3.path = g_scratch + "/no/such/dir/x.ps2st";
    j3.meta = m;
    j3.sections = secs;
    CHECK(w.TryEnqueue(std::move(j3)));
    w.JoinForExit();
    r = w.LastResult();
    CHECK(!r.ok && r.error.find("cannot open") == 0);
    w.JoinForExit();   // idempotent
}

// The 12 anchor states: the 11 leg anchors of verify/baseline plus the live
// slot file, all written by the frozen OLD writer.
static const char* const kAnchors[] = {
    "artdet_f37152.ps2st",
    "ps2state_slot0_f36935.ps2st",
    "ps2state_slot0_f39807.ps2st",
    "ps2state_slot0_f40391.ps2st",
    "ps2state_slot0_f44111.ps2st",
    "ps2state_slot0_f46203_red_crystal.ps2st",
    "ps2state_slot0_f46949_yellow_crystal.ps2st",
    "ps2state_slot0_f57292.ps2st",
    "ps2state_slot0_f59991.ps2st",
    "slowspot_f37092_s1789428268.ps2st",
    "slowspot_f40298_s1789693059.ps2st",
    "ps2state_slot0.ps2st",
};

static void TestAnchorsByteIdentical(const std::string& og_root) {
    const std::string dir = og_root + "/save_states/";
    if (og_root.empty() || !Exists(dir + kAnchors[0])) {
        std::printf("anchor_roundtrip: SKIP (OLD save_states not found under '%s')\n", og_root.c_str());
        return;
    }
    int identical = 0;
    const int total = (int)(sizeof(kAnchors) / sizeof(kAnchors[0]));
    for (const char* name : kAnchors) {
        std::vector<uint8_t> bytes;
        if (!ReadAll(dir + name, &bytes)) {
            std::fprintf(stderr, "anchor %s: cannot read\n", name);
            g_fail++;
            continue;
        }
        FileMeta m;
        std::vector<Blob> secs;
        std::string err;
        if (!ParseStateFile(bytes.data(), bytes.size(), &m, &secs, &err)) {
            std::fprintf(stderr, "anchor %s: parse failed: %s\n", name, err.c_str());
            g_fail++;
            continue;
        }
        const std::vector<uint8_t> again = SerializeStateFile(m, secs);
        const bool same = again == bytes;
        if (same) identical++;
        else g_fail++;
        std::printf("anchor %-44s v%u sections=%zu vsync=%u bytes=%zu -> %s\n", name, m.file_version, secs.size(),
                    m.vsync_frame, bytes.size(), same ? "IDENTICAL" : "DIFFERENT");
    }
    std::printf("anchor_roundtrip: %d/%d byte-identical through core/savestate\n", identical, total);
}

int main(int argc, char** argv) {
    // A scratch directory is mandatory: this test writes state files, and writing
    // into the caller's working directory leaves artifacts in the repo root.
    if (argc < 2 || argv[1][0] == '\0' || std::string(argv[1]) == ".") {
        std::fprintf(stderr, "usage: test_savestate_format <scratch-dir> [og-root]\n"
                             "refusing to run without an explicit scratch directory\n");
        return 2;
    }
    g_scratch = argv[1];
    const std::string og_root = argc > 2 ? argv[2] : "";
    TestCrcAndLayout();
    TestRoundTripAndValidation();
    TestRegistry();
    TestOwnerValidation();
    TestWriter();
    TestAnchorsByteIdentical(og_root);
    std::printf("test_savestate_format: %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}

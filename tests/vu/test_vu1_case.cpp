// Vu1CaseRoundTrip: the frozen VUC1 record -- layout/offset pins, WriteCase
// -> ReadCase identity on a synthetic record, magic/version rejection, and one
// real corpus case from the frozen OLD tree (argv[1]; SKIP = exit 77 when the
// file is absent) with its micro image hash pinned to the OLD catalog row.
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/vu/vu1_case.h"
#include "engine/vu/vu1_state.h"
#include "engine/vu/vu_image_hash.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

// vutrace_cases2/vu1_case_0000_1748.bin: the catalog row of its image (the
// `image_hash` column of the OLD --catalog output) -- pinned after the first
// run against /tmp/p5/5P/old_catalog.log.
static constexpr uint64_t kCase0000ImageHash = 0xa11b3516d81359c3ull;
static constexpr uint32_t kCase0000StartPc = 0x1748;
static constexpr uint64_t kEmptyImageHash = 0xfdec5776828957c1ull;

static void TestLayout() {
    CHECK(sizeof(Vu1Case) == 33456);
    CHECK(sizeof(Vu1Case) % 16 == 0);
    CHECK(Vu1Case::kMagic == 0x31435556u);
    const char magic_bytes[4] = {'V', 'U', 'C', '1'};
    uint32_t m;
    std::memcpy(&m, magic_bytes, 4);
    CHECK(m == Vu1Case::kMagic);
    CHECK(offsetof(Vu1Case, regs) == 16 && offsetof(Vu1Case, micro_mem) == 624 && offsetof(Vu1Case, data_mem) == 17008);
    CHECK(offsetof(Vu1Case, vif1_itops) == 33404 && offsetof(Vu1Case, p_pending_valid) == 33409);
    CHECK(offsetof(Vu1Case, q_pending_value_bits) == 33424 && offsetof(Vu1Case, q_pending_status) == 33428);
    CHECK(offsetof(Vu1Case, p_pending_value_bits) == 33440 && offsetof(Vu1Case, reserved2) == 33444);
    CHECK(offsetof(Vu1Case, vu_cycle) == 33448);
    CHECK(Vu1Case::kMicroSize == Vu1State::kMemSize && Vu1Case::kDataSize == Vu1State::kMemSize);
}

static void FillSynthetic(Vu1Case& c) {
    std::memset(&c, 0, sizeof(c));
    c.magic = Vu1Case::kMagic;
    c.version = Vu1Case::kVersion;
    c.start_pc = 0x0F98;
    c.regs.Reset();
    for (int i = 0; i < 32; ++i) {
        c.regs.VF[i].I[0] = 0x3F800000u + i;
        c.regs.VF[i].I[1] = 0x40000000u ^ i;
        c.regs.VF[i].I[2] = i * 0x01010101u;
        c.regs.VF[i].I[3] = ~static_cast<uint32_t>(i);
    }
    for (int i = 0; i < 16; ++i) c.regs.VI[i] = static_cast<u16>(0x1234 * (i + 1));
    c.regs.status_flag = 0x0C30;
    c.regs.mac_flag = 0xBEEF;
    c.regs.clip_flag = 0x00ABCDEF;
    c.regs.TPC = 0x0F98;
    for (uint32_t i = 0; i < Vu1Case::kMicroSize; ++i) c.micro_mem[i] = static_cast<uint8_t>(i * 7 + 1);
    for (uint32_t i = 0; i < Vu1Case::kDataSize; ++i) c.data_mem[i] = static_cast<uint8_t>(i * 13 + 5);
    c.vif1_top = 0x10;
    c.vif1_tops = 0x20;
    c.vif1_itop = 0x30;
    c.vif1_itops = 0x40;
    c.q_pending_valid = 1;
    c.p_pending_valid = 0;
    c.q_pending_retire = 0x123456789ull;
    c.q_pending_value_bits = 0x3F000000u;
    c.q_pending_status = 0x0020;
    c.p_pending_retire = 0xABCDEFull;
    c.p_pending_value_bits = 0x40490FDBu;
    c.vu_cycle = 0x1122334455ull;
}

static void TestRoundTrip() {
    const std::string tmp = "/tmp/ps2r_vu1_case_roundtrip.bin";
    Vu1Case a;
    FillSynthetic(a);
    CHECK(vu1_case::WriteCase(tmp.c_str(), a));
    Vu1Case b;
    std::memset(&b, 0xEE, sizeof(b));
    CHECK(vu1_case::ReadCase(tmp.c_str(), b));
    CHECK(std::memcmp(&a, &b, sizeof(Vu1Case)) == 0);
    CHECK(b.start_pc == 0x0F98 && b.regs.VF[31].I[3] == ~31u && b.regs.VI[15] == static_cast<u16>(0x1234 * 16));
    CHECK(b.micro_mem[16383] == static_cast<uint8_t>(16383 * 7 + 1) && b.data_mem[0] == 5);
    CHECK(b.q_pending_retire == 0x123456789ull && b.vu_cycle == 0x1122334455ull && b.p_pending_value_bits == 0x40490FDBu);

    // Wrong magic / version / short file are rejected.
    a.magic ^= 1;
    CHECK(vu1_case::WriteCase(tmp.c_str(), a));
    CHECK(!vu1_case::ReadCase(tmp.c_str(), b));
    a.magic ^= 1;
    a.version = 2;
    CHECK(vu1_case::WriteCase(tmp.c_str(), a));
    CHECK(!vu1_case::ReadCase(tmp.c_str(), b));
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    CHECK(f != nullptr);
    if (f) {
        std::fwrite(&a, 1, 1000, f);
        std::fclose(f);
    }
    CHECK(!vu1_case::ReadCase(tmp.c_str(), b));
    CHECK(!vu1_case::ReadCase("/nonexistent/dir/no_such_case.bin", b));
    std::remove(tmp.c_str());

    // The image hash is a pure function of the micro bytes.
    Vu1Case c;
    FillSynthetic(c);
    const uint64_t h = VuImageHash::Fast(c.micro_mem, Vu1Case::kMicroSize);
    CHECK(h == VuImageHash::Fast(a.micro_mem, Vu1Case::kMicroSize));
    c.micro_mem[100] ^= 1;
    CHECK(h != VuImageHash::Fast(c.micro_mem, Vu1Case::kMicroSize));
    // Tail rule: whole 8-byte words feed lane 0, a partial word is ignored.
    uint8_t buf[48];
    for (int i = 0; i < 48; ++i) buf[i] = static_cast<uint8_t>(i + 1);
    CHECK(VuImageHash::Fast(buf, 40) == VuImageHash::Fast(buf, 47));
    CHECK(VuImageHash::Fast(buf, 40) != VuImageHash::Fast(buf, 48));
    CHECK(VuImageHash::Fast(buf, 32) != VuImageHash::Fast(buf, 40));
    CHECK(VuImageHash::Fast(buf, 0) == VuImageHash::Fast(buf, 7));
    // Empty input: the fold of the four seeds (a fixed constant).
    CHECK(VuImageHash::Fast(buf, 0) == kEmptyImageHash);
}

static int TestRealCase(const char* path) {
    Vu1Case c;
    if (!vu1_case::ReadCase(path, c)) {
        std::printf("SKIP: corpus case not readable: %s\n", path);
        return 77;
    }
    CHECK(c.magic == Vu1Case::kMagic && c.version == Vu1Case::kVersion);
    CHECK(c.start_pc == kCase0000StartPc);
    CHECK(c.regs.VF[0].I[0] == 0 && c.regs.VF[0].I[3] == 0x3F800000u);  // VF0 hard-wired {0,0,0,1}
    CHECK(c.regs.VI[0] == 0);
    CHECK((c.start_pc & 7u) == 0 && c.start_pc < Vu1Case::kMicroSize);
    const uint64_t h = VuImageHash::Fast(c.micro_mem, Vu1Case::kMicroSize);
    std::printf("case %s: start_pc 0x%04x image_hash %016llx image %08x vu_cycle %llu q_pending %u p_pending %u\n", path,
                c.start_pc, static_cast<unsigned long long>(h), static_cast<unsigned>(h >> 32),
                static_cast<unsigned long long>(c.vu_cycle), c.q_pending_valid, c.p_pending_valid);
    CHECK(h == kCase0000ImageHash);
    return 0;
}

int main(int argc, char** argv) {
    TestLayout();
    TestRoundTrip();
    int rc = 0;
    if (argc > 1) rc = TestRealCase(argv[1]);
    else std::printf("SKIP: no corpus case path given\n");
    if (g_fail) {
        std::printf("Vu1CaseRoundTrip: %d FAILED\n", g_fail);
        return 1;
    }
    if (rc == 77) {
        std::printf("Vu1CaseRoundTrip: synthetic PASS; real case SKIPPED\n");
        return 77;
    }
    std::printf("Vu1CaseRoundTrip: PASS (sizeof(Vu1Case)=%zu)\n", sizeof(Vu1Case));
    return 0;
}

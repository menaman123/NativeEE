// DmacSectionRoundTrip: the TAG_DMAC savestate section -- byte layout pinned
// to the frozen format (10 x 28-byte channel records, 9 x u32 globals, i32
// next handler id, u32 count, 25-byte records cause-then-id; 324 + 25 * n
// bytes), Encode -> Decode -> Encode identity for the empty, populated and
// handler-heavy states, the validate rejections, and what a restore resets.
#include <cstdint>
#include <cstdio>
#include <cstring>
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
#include "engine/vif/vif.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

struct TestDmacSection {
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
    TestDmacSection()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter) {}
};
using Rig = TestDmacSection;

constexpr size_t kFixed = 324;
constexpr size_t kRecord = 25;
constexpr size_t kChannelsBytes = 10 * 28;
constexpr size_t kGlobalsOffset = kChannelsBytes;       // 280
constexpr size_t kNextIdOffset = kGlobalsOffset + 9 * 4;  // 316
constexpr size_t kCountOffset = kNextIdOffset + 4;        // 320

template <typename T>
T Rd(const std::vector<uint8_t>& v, size_t off) {
    T x;
    std::memcpy(&x, v.data() + off, sizeof(T));
    return x;
}

std::vector<uint8_t> Encode(Rig& rig) {
    std::vector<uint8_t> out;
    rig.dmac.SerializeSection(out);
    return out;
}
bool Decode(Rig& rig, const std::vector<uint8_t>& payload) {
    std::string err;
    const bool ok = rig.dmac.RestoreSection(payload, &err);
    if (!ok) std::printf("  restore: %s\n", err.c_str());
    return ok;
}

void Populate(Rig& rig) {
    for (int ch = 0; ch < 10; ++ch) {
        rig.dmac.channel(ch) = DmacChannelRegs{0x01000000u * static_cast<uint32_t>(ch) + 0x105u,
                                               0x00100000u + 0x10u * static_cast<uint32_t>(ch),
                                               static_cast<uint32_t>(ch) * 3u,
                                               0x00200000u + 0x20u * static_cast<uint32_t>(ch),
                                               0x00300000u + static_cast<uint32_t>(ch),
                                               0x00400000u + static_cast<uint32_t>(ch),
                                               0x1000u * static_cast<uint32_t>(ch)};
    }
    rig.dmac.globals() = DmacGlobalRegs{0x1u, 0x00400040u, 0x0000AAAAu, 0x4, 0x5, 0x6, 0x7, 0x1201u, 0x9};
    // Causes out of order and interleaved, ids ascending: the table groups by cause.
    rig.dmac.handlers().Add(6, 0x00201000u, 0x00301000u, 0x11u, 1, -1);   // id 1
    rig.dmac.handlers().Add(2, 0x00202000u, 0x00302000u, 0x22u, 2, -1);   // id 2
    rig.dmac.handlers().Add(6, 0x00203000u, 0x00303000u, 0x33u, 3, 0);    // id 3, head of cause 6
    rig.dmac.handlers().Add(15, 0x00204000u, 0x00304000u, 0x44u, 4, -1);  // id 4 (cause 15 allowed)
    rig.dmac.handlers().SetActive(2, false);
}

void TestEmptyState() {
    Rig rig;
    const std::vector<uint8_t> p = Encode(rig);
    CHECK(p.size() == kFixed);
    // Everything zero except the next handler id (1).
    bool zero = true;
    for (size_t i = 0; i < kNextIdOffset; ++i) zero = zero && p[i] == 0;
    CHECK(zero);
    CHECK(Rd<int32_t>(p, kNextIdOffset) == 1 && Rd<uint32_t>(p, kCountOffset) == 0);
    Rig other;
    CHECK(Decode(other, p) && Encode(other) == p);
}

void TestLayout() {
    Rig rig;
    Populate(rig);
    const std::vector<uint8_t> p = Encode(rig);
    CHECK(p.size() == kFixed + kRecord * 4);
    // Channel records at 28-byte stride, field order chcr madr qwc tadr asr0 asr1 sadr.
    for (int ch = 0; ch < 10; ++ch) {
        const size_t o = 28u * static_cast<size_t>(ch);
        const DmacChannelRegs& r = rig.dmac.channel(ch);
        CHECK(Rd<uint32_t>(p, o + 0) == r.chcr && Rd<uint32_t>(p, o + 4) == r.madr && Rd<uint32_t>(p, o + 8) == r.qwc);
        CHECK(Rd<uint32_t>(p, o + 12) == r.tadr && Rd<uint32_t>(p, o + 16) == r.asr0 &&
              Rd<uint32_t>(p, o + 20) == r.asr1 && Rd<uint32_t>(p, o + 24) == r.sadr);
    }
    // Globals in order ctrl stat pcr sqwc rbsr rbor stadr enabler enablew.
    const uint32_t want[9] = {0x1u, 0x00400040u, 0x0000AAAAu, 0x4, 0x5, 0x6, 0x7, 0x1201u, 0x9};
    for (size_t i = 0; i < 9; ++i) CHECK(Rd<uint32_t>(p, kGlobalsOffset + 4 * i) == want[i]);
    CHECK(Rd<int32_t>(p, kNextIdOffset) == 5 && Rd<uint32_t>(p, kCountOffset) == 4);
    // Records: cause ascending (2, 6, 6, 15); within cause 6 the head insert (id 3) precedes id 1.
    struct Rec {
        int32_t cause, id;
        uint32_t pc, gp, arg;
        int32_t flag;
        uint8_t active;
    };
    const Rec want_recs[4] = {{2, 2, 0x00202000u, 0x00302000u, 0x22u, 2, 0},
                              {6, 3, 0x00203000u, 0x00303000u, 0x33u, 3, 1},
                              {6, 1, 0x00201000u, 0x00301000u, 0x11u, 1, 1},
                              {15, 4, 0x00204000u, 0x00304000u, 0x44u, 4, 1}};
    for (size_t i = 0; i < 4; ++i) {
        const size_t o = kFixed + kRecord * i;
        CHECK(Rd<int32_t>(p, o + 0) == want_recs[i].cause);
        CHECK(Rd<int32_t>(p, o + 4) == want_recs[i].id);
        CHECK(Rd<uint32_t>(p, o + 8) == want_recs[i].pc);
        CHECK(Rd<uint32_t>(p, o + 12) == want_recs[i].gp);
        CHECK(Rd<uint32_t>(p, o + 16) == want_recs[i].arg);
        CHECK(Rd<int32_t>(p, o + 20) == want_recs[i].flag);
        CHECK(p[o + 24] == want_recs[i].active);
    }
}

void TestRoundTripIdentity() {
    Rig rig;
    Populate(rig);
    const std::vector<uint8_t> p = Encode(rig);
    // Decode into a fresh DMAC, re-encode: byte identical; decode again into a third.
    Rig b;
    CHECK(Decode(b, p));
    const std::vector<uint8_t> p2 = Encode(b);
    CHECK(p2 == p);
    Rig c;
    CHECK(Decode(c, p2) && Encode(c) == p);
    // Decoded state matches field for field.
    for (int ch = 0; ch < 10; ++ch) {
        CHECK(std::memcmp(&b.dmac.channel(ch), &rig.dmac.channel(ch), sizeof(DmacChannelRegs)) == 0);
    }
    CHECK(std::memcmp(&b.dmac.globals(), &rig.dmac.globals(), sizeof(DmacGlobalRegs)) == 0);
    CHECK(b.dmac.handlers().Count() == 4 && b.dmac.handlers().NextId() == 5);
    CHECK(b.dmac.handlers().ForCause(6).size() == 2 && b.dmac.handlers().ForCause(6)[0].id == 3);
    CHECK(b.dmac.handlers().ForCause(15).size() == 1 && !b.dmac.handlers().ForCause(2)[0].active);
    // The restored mask makes CheckInterrupt true (0x00400040: CIS(6) & CIM(6)).
    CHECK(b.dmac.CheckInterrupt());
    // Decoding over a populated, different DMAC replaces everything (handlers included).
    Rig d;
    d.dmac.handlers().Add(3, 0x00205000u, 0, 0, 0, -1);
    d.dmac.handlers().Add(3, 0x00206000u, 0, 0, 0, -1);
    d.dmac.globals().stat = 0xFFFFFFFFu;
    CHECK(Decode(d, p) && Encode(d) == p);
    // Through the SectionSpec closures (what the registry would call).
    ps2r::savestate::SectionSpec spec = rig.dmac.SavestateSection();
    CHECK(spec.tag == ps2r::savestate::TAG_DMAC && spec.version == 1 && spec.required);
    CHECK(std::string(spec.owner) == "dmac" && spec.serialize && spec.validate && spec.restore && !spec.absent);
    std::vector<uint8_t> via_spec;
    spec.serialize(via_spec);
    CHECK(via_spec == p);
    ps2r::savestate::Blob blob;
    blob.tag = spec.tag;
    blob.version = spec.version;
    blob.payload = p;
    std::string err;
    Rig e;
    ps2r::savestate::SectionSpec espec = e.dmac.SavestateSection();
    CHECK(espec.validate(blob, &err) && espec.restore(blob, &err) && Encode(e) == p);
}

void TestManyHandlers() {
    // 64 handlers across every dispatch cause: size 324 + 25 * 64, identity.
    Rig rig;
    for (int i = 0; i < 64; ++i) rig.dmac.handlers().Add(i % 10, 0x00210000u + 8u * static_cast<uint32_t>(i), 0x100u, 0, i, -1);
    const std::vector<uint8_t> p = Encode(rig);
    CHECK(p.size() == kFixed + kRecord * 64 && Rd<uint32_t>(p, kCountOffset) == 64 && Rd<int32_t>(p, kNextIdOffset) == 65);
    // Grouped by cause ascending, insertion order within the cause.
    int32_t prev_cause = -1;
    bool grouped = true;
    for (size_t i = 0; i < 64; ++i) {
        const int32_t cause = Rd<int32_t>(p, kFixed + kRecord * i);
        grouped = grouped && cause >= prev_cause;
        prev_cause = cause;
    }
    CHECK(grouped && Rd<int32_t>(p, kFixed + 4) == 1 && Rd<int32_t>(p, kFixed + kRecord + 4) == 11);
    Rig b;
    CHECK(Decode(b, p) && Encode(b) == p);
}

void TestValidate() {
    Rig rig;
    Populate(rig);
    const std::vector<uint8_t> p = Encode(rig);
    std::string err;
    CHECK(Dmac::ValidateSection(p, &err));
    std::vector<uint8_t> bad(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(kFixed - 1));
    CHECK(!Dmac::ValidateSection(bad, &err) && !err.empty());
    bad = p;
    bad.resize(p.size() + 1);
    CHECK(!Dmac::ValidateSection(bad, &err));
    bad = p;
    bad.resize(p.size() - kRecord);  // count says 4, payload carries 3
    CHECK(!Dmac::ValidateSection(bad, &err));
    bad = p;
    std::memset(bad.data() + kCountOffset, 0, 4);
    CHECK(!Dmac::ValidateSection(bad, &err));
    // Empty payload rejected; the empty-state payload accepted.
    CHECK(!Dmac::ValidateSection({}, &err));
    Rig empty;
    CHECK(Dmac::ValidateSection(Encode(empty), &err));
    // A rejected payload leaves the DMAC untouched.
    Rig b;
    Populate(b);
    const std::vector<uint8_t> before = Encode(b);
    std::string rerr;
    CHECK(!b.dmac.RestoreSection(bad, &rerr) && Encode(b) == before);
    // Restore resets the transient state (parked kick + tail records).
    Rig t;
    uint8_t* tag = t.ram.Pointer(0x4000, 16);
    const uint64_t end_lo = 7ull << 28;
    std::memcpy(tag, &end_lo, 8);
    std::memset(tag + 8, 0, 8);
    t.dmac.channel(1).tadr = 0x4000;
    t.dmac.Write(DmacAddr::kVif1, 4, DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift));
    t.dmac.Read(DmacAddr::kVif1, 4);
    CHECK(t.dmac.deferred().TailCount() == 1);
    t.dmac.Write(DmacAddr::kVif1, 4, DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift));
    CHECK(t.dmac.HasDeferredVif1());
    CHECK(Decode(t, p) && !t.dmac.HasDeferredVif1() && t.dmac.deferred().TailCount() == 0);
    CHECK(t.dmac.channel(1).chcr == rig.dmac.channel(1).chcr);
}

}  // namespace

int main() {
    std::printf("TAG_DMAC fixed = %zu, record = %zu\n", kFixed, kRecord);
    CHECK(Dmac::kSectionFixedBytes == kFixed && Dmac::kHandlerRecordBytes == kRecord);
    TestEmptyState();
    TestLayout();
    TestRoundTripIdentity();
    TestManyHandlers();
    TestValidate();
    if (g_fail) {
        std::printf("DmacSectionRoundTrip: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("DmacSectionRoundTrip: PASS\n");
    return 0;
}

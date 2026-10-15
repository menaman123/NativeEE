// DmacGifPath3: a synthetic GIF chain in fake RAM -> Dmac CHCR kick ->
// ChainWalker -> ONE GifArbiter PATH3 submit -> drain -> GifParser ->
// RegisterOpStream. Pins the op sequence + hash against the same bytes fed
// directly, the IMAGE packet path (image ops, FinishPath3 keeps a mid-IMAGE
// transfer), and the M3R-masked case (the DMAC completes regardless; the
// parser holds the data while masked).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/dmac/dmac.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/gif_tag.h"
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

struct TestDmacGifPath3 {
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
    TestDmacGifPath3()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter) {}
};
using Rig = TestDmacGifPath3;

uint8_t* At(Rig& rig, uint32_t addr) { return rig.ram.Pointer(addr, 16); }
void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

uint64_t TagLo(uint32_t qwc, uint32_t id, bool irq, uint32_t addr, bool spr) {
    return static_cast<uint64_t>(qwc & 0xFFFFu) | (static_cast<uint64_t>(id & 7u) << 28) |
           (irq ? (1ull << 31) : 0ull) | (static_cast<uint64_t>(addr) << 32) | (spr ? (1ull << 63) : 0ull);
}
void PutTag(Rig& rig, uint32_t at, uint32_t qwc, uint32_t id, uint32_t addr = 0, bool spr = false) {
    uint8_t* p = At(rig, at);
    Put64(p, TagLo(qwc, id, false, addr, spr));
    Put64(p + 8, 0);
}
// GIFtag: NLOOP, EOP, FLG (0 PACKED / 1 REGLIST / 2 IMAGE), NREG, regs nibbles.
void PutGifTag(uint8_t* p, uint32_t nloop, bool eop, uint32_t flg, uint32_t nreg, uint64_t regs) {
    const uint64_t lo = (static_cast<uint64_t>(nloop) & 0x7FFFu) | (eop ? (1ull << 15) : 0ull) |
                        (static_cast<uint64_t>(flg & 3u) << 58) | (static_cast<uint64_t>(nreg & 0xFu) << 60);
    Put64(p, lo);
    Put64(p + 8, regs);
}
void PutAdTag(uint8_t* p, uint32_t nloop) { PutGifTag(p, nloop, true, 0, 1, 0xEull); }
void PutAd(uint8_t* p, uint64_t value, uint32_t reg) {
    Put64(p, value);
    Put64(p + 8, reg);
}
void PutImageTag(uint8_t* p, uint32_t nloop) { PutGifTag(p, nloop, true, 2, 0, 0); }

size_t CountKind(const RegisterOpStream& ops, RegisterOpKind k) {
    size_t n = 0;
    for (const RegisterOp& op : ops.Ops()) n += op.kind == static_cast<uint8_t>(k);
    return n;
}

struct SubmitLog {
    std::vector<uint8_t> paths;
    std::vector<size_t> sizes;
    std::vector<bool> image_first;  // the arbiter's is_path3_image rule: first GIFtag FLG == IMAGE
};
void ObserveSubmit(void* user, GifPath path, const uint8_t* data, size_t size) {
    SubmitLog* log = static_cast<SubmitLog*>(user);
    log->paths.push_back(static_cast<uint8_t>(path));
    log->sizes.push_back(size);
    uint64_t lo = 0;
    if (size >= 8) std::memcpy(&lo, data, 8);
    log->image_first.push_back(path == GifPath::Path3 && ((lo >> 58) & 3u) == 2u);
}

constexpr uint32_t kChainKick = DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift);

void KickGif(Rig& rig, uint32_t tadr) {
    rig.dmac.channel(2).tadr = tadr;
    rig.dmac.Write(DmacAddr::kGif, 4, kChainKick);
}

// Build the reference stream: the concatenated payload fed as one PATH3 packet.
void Reference(Rig& ref, const std::vector<uint8_t>& bytes) {
    ref.parser.ReceiveData(GifPath::Path3, bytes.data(), bytes.size());
    ref.parser.FinishPath3();
}

// The canonical chain: CNT(3) [A+D tag NLOOP 4 + 2 A+D] , REF(1) -> 1 A+D, NEXT(0) -> END(1) [1 A+D].
// The single PACKED A+D packet spans four DMA segments.
std::vector<uint8_t> BuildCanonicalChain(Rig& rig) {
    PutTag(rig, 0x4000, 3, DmacTagId::kCnt);
    PutAdTag(At(rig, 0x4010), 4);
    PutAd(At(rig, 0x4020), 0x0000000000123456ull, 0x47);  // TEST_1
    PutAd(At(rig, 0x4030), 0x00000000ABCDEF01ull, 0x48);  // PABE / next reg
    PutTag(rig, 0x4040, 1, DmacTagId::kRef, 0x8000);
    PutAd(At(rig, 0x8000), 0x0000000000000042ull, 0x49);
    PutTag(rig, 0x4050, 0, DmacTagId::kNext, 0x9000);
    PutTag(rig, 0x9000, 1, DmacTagId::kEnd);
    PutAd(At(rig, 0x9010), 0x0000000000000077ull, 0x4A);
    std::vector<uint8_t> bytes(80);
    std::memcpy(bytes.data(), At(rig, 0x4010), 48);
    std::memcpy(bytes.data() + 48, At(rig, 0x8000), 16);
    std::memcpy(bytes.data() + 64, At(rig, 0x9010), 16);
    return bytes;
}

void TestOpSequenceAndHash() {
    Rig rig;
    SubmitLog log;
    rig.arbiter.SetSubmitObserver(&ObserveSubmit, &log);
    const std::vector<uint8_t> bytes = BuildCanonicalChain(rig);
    KickGif(rig, 0x4000);
    // The DMAC completes the channel and queues exactly one PATH3 packet; nothing parsed yet.
    CHECK(log.paths.size() == 1 && log.paths[0] == 3 && log.sizes[0] == 80 && !log.image_first[0]);
    CHECK(rig.arbiter.hasPending() && rig.arbiter.PendingBytes() == 80 && rig.ops.Count() == 0);
    const DmacChannelRegs& r = rig.dmac.channel(2);
    CHECK((r.chcr & DmacChcr::kStr) == 0 && r.qwc == 0 && r.tadr == 0x9020u && r.madr == 0x9020u);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(2)) != 0);
    rig.arbiter.drain();
    CHECK(rig.arbiter.DrainedPackets() == 1 && !rig.arbiter.hasPending());
    // Op sequence: Tag, 4 Reg, Eop, Finish -- all on path 3.
    CHECK(rig.ops.Count() == 7);
    if (rig.ops.Count() == 7) {
        const std::vector<RegisterOp>& o = rig.ops.Ops();
        CHECK(o[0].kind == static_cast<uint8_t>(RegisterOpKind::Tag) && o[0].path == 3);
        CHECK(o[1].kind == static_cast<uint8_t>(RegisterOpKind::Reg) && o[1].reg == 0x47 && o[1].lo == 0x123456ull);
        CHECK(o[2].kind == static_cast<uint8_t>(RegisterOpKind::Reg) && o[2].reg == 0x48 && o[2].lo == 0xABCDEF01ull);
        CHECK(o[3].kind == static_cast<uint8_t>(RegisterOpKind::Reg) && o[3].reg == 0x49 && o[3].lo == 0x42ull);
        CHECK(o[4].kind == static_cast<uint8_t>(RegisterOpKind::Reg) && o[4].reg == 0x4A && o[4].lo == 0x77ull);
        CHECK(o[5].kind == static_cast<uint8_t>(RegisterOpKind::Eop) && o[5].path == 3);
        CHECK(o[6].kind == static_cast<uint8_t>(RegisterOpKind::Finish));
    }
    Rig ref;
    Reference(ref, bytes);
    CHECK(ref.ops.Count() == rig.ops.Count() && ref.ops.Hash() == rig.ops.Hash());
    // Two chains kicked back to back = two packets, drained in kick order.
    Rig two;
    BuildCanonicalChain(two);
    PutTag(two, 0x5000, 2, DmacTagId::kEnd);
    PutAdTag(At(two, 0x5010), 1);
    PutAd(At(two, 0x5020), 0x99ull, 0x4B);
    KickGif(two, 0x4000);
    KickGif(two, 0x5000);
    CHECK(two.arbiter.hasPending() && two.arbiter.PendingBytes() == 80 + 32);
    two.arbiter.drain();
    // One drain: chain 1 (Tag, 4 Reg, Eop), chain 2 (Tag, Reg, Eop), then ONE Finish.
    CHECK(two.arbiter.DrainedPackets() == 2 && two.ops.Count() == 6 + 3 + 1);
    CHECK(two.ops.Ops()[6].kind == static_cast<uint8_t>(RegisterOpKind::Tag) && two.ops.Ops()[7].reg == 0x4B &&
          two.ops.Ops()[9].kind == static_cast<uint8_t>(RegisterOpKind::Finish) &&
          CountKind(two.ops, RegisterOpKind::Finish) == 1);
}

void TestImagePacket() {
    // IMAGE packet: GIFtag FLG=IMAGE NLOOP 2 + 2 quadwords of texels in a CNT chain.
    Rig rig;
    SubmitLog log;
    rig.arbiter.SetSubmitObserver(&ObserveSubmit, &log);
    PutTag(rig, 0x4000, 3, DmacTagId::kEnd);
    PutImageTag(At(rig, 0x4010), 2);
    Put64(At(rig, 0x4020), 0x1111111111111111ull);
    Put64(At(rig, 0x4020) + 8, 0x2222222222222222ull);
    Put64(At(rig, 0x4030), 0x3333333333333333ull);
    Put64(At(rig, 0x4030) + 8, 0x4444444444444444ull);
    KickGif(rig, 0x4000);
    CHECK(log.image_first.size() == 1 && log.image_first[0]);  // the arbiter tags this packet is_path3_image
    rig.arbiter.drain();
    CHECK(CountKind(rig.ops, RegisterOpKind::Image) == 2);
    CHECK(CountKind(rig.ops, RegisterOpKind::Eop) == 1 && CountKind(rig.ops, RegisterOpKind::Finish) == 1);
    const std::vector<RegisterOp>& o = rig.ops.Ops();
    CHECK(o.size() == 5 && o[1].lo == 0x1111111111111111ull && o[1].hi == 0x2222222222222222ull &&
          o[2].lo == 0x3333333333333333ull && o[2].hi == 0x4444444444444444ull);
    CHECK(!rig.parser.Transfer().in_transfer);

    // A chain that delivers only part of an IMAGE (NLOOP 4, 2 quadwords): the
    // transfer stays open across the drain's FinishPath3 (no Finish op); the
    // next PATH3 chain abandons the stale transfer and parses on its own.
    Rig part;
    PutTag(part, 0x4000, 3, DmacTagId::kEnd);
    PutImageTag(At(part, 0x4010), 4);
    Put64(At(part, 0x4020), 0xA1ull);
    Put64(At(part, 0x4020) + 8, 0xA2ull);
    Put64(At(part, 0x4030), 0xA3ull);
    Put64(At(part, 0x4030) + 8, 0xA4ull);
    KickGif(part, 0x4000);
    part.arbiter.drain();
    CHECK(CountKind(part.ops, RegisterOpKind::Image) == 2 && part.parser.Transfer().in_transfer);
    CHECK(part.parser.Transfer().loop == 2 && CountKind(part.ops, RegisterOpKind::Finish) == 0);
    PutTag(part, 0x5000, 2, DmacTagId::kEnd);
    PutAdTag(At(part, 0x5010), 1);
    PutAd(At(part, 0x5020), 0xB1ull, 0x4B);
    KickGif(part, 0x5000);
    part.arbiter.drain();
    CHECK(CountKind(part.ops, RegisterOpKind::Image) == 2 && !part.parser.Transfer().in_transfer);
    CHECK(CountKind(part.ops, RegisterOpKind::Tag) == 2 && CountKind(part.ops, RegisterOpKind::Reg) == 1);
    CHECK(CountKind(part.ops, RegisterOpKind::Eop) == 1 && CountKind(part.ops, RegisterOpKind::Finish) == 1);
    CHECK(part.ops.Ops()[4].reg == 0x4B && part.ops.Ops()[4].lo == 0xB1ull);
}

void TestM3rMasked() {
    // MODE.M3R set: the DMAC still walks and completes (no mask test in the
    // DMAC), the arbiter still queues; the parser holds the PATH3 data unparsed.
    Rig rig;
    const std::vector<uint8_t> bytes = BuildCanonicalChain(rig);
    rig.gif_regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 1);
    CHECK(rig.gif_regs.Path3Masked());
    KickGif(rig, 0x4000);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(2)) != 0 && (rig.dmac.channel(2).chcr & DmacChcr::kStr) == 0);
    CHECK(rig.arbiter.hasPending() && rig.arbiter.PendingBytes() == 80);
    // Case A: the mask lifts before the drain -> everything parses (mask is tested at delivery).
    rig.gif_regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 0);
    rig.arbiter.drain();
    Rig ref;
    Reference(ref, bytes);
    CHECK(rig.ops.Count() == 7 && rig.ops.Hash() == ref.ops.Hash());
    // Case B: still masked at the drain -> the packet is held in the parser
    // tail (no register op) until the drain's FinishPath3 abandons the pass.
    Rig held;
    BuildCanonicalChain(held);
    held.gif_regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 1);
    KickGif(held, 0x4000);
    const uint8_t* payload = At(held, 0x4010);
    held.parser.ReceiveData(GifPath::Path3, payload, 48);  // what the drain delivers first: held, not parsed
    CHECK(held.ops.Count() == 0 && held.parser.BufferedBytes() == 48);
    held.parser.FinishPath3();
    CHECK(held.parser.BufferedBytes() == 0 && CountKind(held.ops, RegisterOpKind::Finish) == 1);
    held.ops.Clear();
    held.arbiter.drain();
    CHECK(CountKind(held.ops, RegisterOpKind::Tag) == 0 && CountKind(held.ops, RegisterOpKind::Reg) == 0);
    CHECK(CountKind(held.ops, RegisterOpKind::Finish) == 1 && held.parser.BufferedBytes() == 0);
    CHECK(!held.arbiter.hasPending() && held.arbiter.DrainedPackets() == 1);
    // The VIF MSKPATH3 state is a separate observable and does not hold data here.
    Rig vifmask;
    BuildCanonicalChain(vifmask);
    vifmask.gif_regs.SetPath3MaskedByVif(true);
    KickGif(vifmask, 0x4000);
    vifmask.arbiter.drain();
    CHECK(vifmask.ops.Count() == 7);
}

void TestPath2BeforePath3() {
    // A parked VIF1 chain with a DIRECT packet flushes on the GIF kick: its
    // PATH2 ops precede the PATH3 chain's after one drain.
    Rig rig;
    // VIF1 chain: CNT(3) -> DIRECT 2 qw (code qw + GIFtag + one A+D), END(0).
    PutTag(rig, 0x2000, 3, DmacTagId::kCnt);
    uint8_t* code = At(rig, 0x2010);
    const uint32_t direct = (0x50u << 24) | 2u;
    std::memcpy(code, &direct, 4);
    std::memset(code + 4, 0, 12);
    PutAdTag(At(rig, 0x2020), 1);
    PutAd(At(rig, 0x2030), 0x0000000000000AAAull, 0x47);
    PutTag(rig, 0x2040, 0, DmacTagId::kEnd);
    rig.dmac.channel(1).tadr = 0x2000;
    rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
    CHECK(rig.dmac.HasDeferredVif1() && !rig.arbiter.hasPending() && rig.ops.Count() == 0);
    // GIF chain: END(2) -> one A+D.
    PutTag(rig, 0x4000, 2, DmacTagId::kEnd);
    PutAdTag(At(rig, 0x4010), 1);
    PutAd(At(rig, 0x4020), 0x0000000000000BBBull, 0x48);
    KickGif(rig, 0x4000);
    // The flush drained the PATH2 packet; the PATH3 packet is still queued.
    CHECK(!rig.dmac.HasDeferredVif1());
    CHECK(rig.ops.Count() == 3 && rig.ops.Ops()[0].path == 2 && rig.ops.Ops()[1].lo == 0xAAAull);
    CHECK(rig.arbiter.hasPending() && rig.arbiter.PendingBytes() == 32);
    rig.arbiter.drain();
    CHECK(rig.ops.Count() == 3 + 4);  // Tag, Reg, Eop, Finish on path 3
    CHECK(rig.ops.Ops()[3].path == 3 && rig.ops.Ops()[4].lo == 0xBBBull &&
          rig.ops.Ops()[6].kind == static_cast<uint8_t>(RegisterOpKind::Finish));
    CHECK((rig.dmac.globals().stat & (DmacStat::Cis(1) | DmacStat::Cis(2))) == (DmacStat::Cis(1) | DmacStat::Cis(2)));
}

void TestShortAndEmptyChains() {
    // A chain whose whole payload is under one GIFtag is dropped by the arbiter; the channel still completes.
    Rig rig;
    PutTag(rig, 0x4000, 0, DmacTagId::kEnd);
    KickGif(rig, 0x4000);
    CHECK(!rig.arbiter.hasPending() && (rig.dmac.globals().stat & DmacStat::Cis(2)) != 0);
    // GIF terminator (all-ones low word) at the head: nothing decoded, channel completes, TADR unchanged.
    Rig term;
    Put64(At(term, 0x4000), ~0ull);
    Put64(At(term, 0x4000) + 8, 0);
    KickGif(term, 0x4000);
    CHECK(!term.arbiter.hasPending() && term.dmac.channel(2).tadr == 0x4000u);
    CHECK((term.dmac.globals().stat & DmacStat::Cis(2)) != 0 && (term.dmac.channel(2).chcr & DmacChcr::kStr) == 0);
}

}  // namespace

int main() {
    TestOpSequenceAndHash();
    TestImagePacket();
    TestM3rMasked();
    TestPath2BeforePath3();
    TestShortAndEmptyChains();
    if (g_fail) {
        std::printf("DmacGifPath3: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("DmacGifPath3: PASS\n");
    return 0;
}

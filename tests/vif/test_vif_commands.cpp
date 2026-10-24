// VifCommands: the command processor on synthetic VIF1/VIF0 streams -- register
// effects, MSCAL/MSCALF/MSCNT latching TOP/ITOPS with the DBF swap and running
// a real micro through VuUnit (XTOP/XITOP), MPG, DIRECT/DIRECTHL into the GIF
// arbiter -> parser -> RegisterOpStream, MSKPATH3, the i-bit rule, the MMIO
// register file + FBRST + the VIF1 FIFO window, the TTE pad skip, and the
// whole-span parser vs the word latch on every quadword split.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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

constexpr uint32_t U_NOP = 0x000002FFu;
constexpr uint32_t L_NOP = 0x8000033Cu;
constexpr uint32_t U_EBIT = 0x40000000u;
uint32_t EncIaddiu(uint32_t it, uint32_t is, uint32_t imm15) {
    return (0x08u << 25) | (((imm15 >> 11) & 0xFu) << 21) | (it << 16) | (is << 11) | (imm15 & 0x7FFu);
}
uint32_t EncXtop(uint32_t it) { return 0x80000000u | (it << 16) | (0x1Au << 6) | 0x3Cu; }
uint32_t EncXitop(uint32_t it) { return 0x80000000u | (it << 16) | (0x1Au << 6) | 0x3Du; }

void PutBundle(VuUnit& vu, uint32_t pc, uint32_t lower, uint32_t upper) {
    uint8_t b[8];
    std::memcpy(b, &lower, 4);
    std::memcpy(b + 4, &upper, 4);
    vu.WriteMicroMem(pc, b, 8);
}

// A VIF stream built word by word (POD + free functions).
struct Stream {
    std::vector<uint32_t> w;
};
Stream& Code(Stream& s, uint32_t cmd, uint32_t num, uint32_t imm, bool irq = false) {
    s.w.push_back((irq ? 0x80000000u : 0u) | (cmd << 24) | (num << 16) | imm);
    return s;
}
Stream& Word(Stream& s, uint32_t x) {
    s.w.push_back(x);
    return s;
}
void PadToQw(Stream& s) {
    while (s.w.size() % 4) s.w.push_back(0);
}
const uint8_t* Bytes(const Stream& s) { return reinterpret_cast<const uint8_t*>(s.w.data()); }
size_t Size(const Stream& s) { return s.w.size() * 4; }

// One complete VIF rig: GS recording sink behind the parser and arbiter, both VU units, the VIF.
struct TestVifCommands {
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0;
    VuUnit vu1;
    Vif vif;
    explicit TestVifCommands(bool fast_span = true)
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter),
          vif(vu0, vu1, arbiter, gif_regs, VifSettings{fast_span, VifUnpackSettings{}}) {}
};
using Rig = TestVifCommands;

// Every DMA feed is whole quadwords: pad a copy like the DMAC's qwc*16 bytes.
void Feed(Rig& rig, int unit, const Stream& s) {
    Stream p = s;
    PadToQw(p);
    rig.vif.ProcessData(unit, Bytes(p), Size(p));
}

bool QwIs(const VuUnit& vu, uint32_t qw, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t got[4];
    vu.ReadDataMem((qw * 16) & vu.mem_mask(), reinterpret_cast<uint8_t*>(got), 16);
    const bool ok = got[0] == a && got[1] == b && got[2] == c && got[3] == d;
    if (!ok)
        std::printf("  qw %u = %08x %08x %08x %08x, want %08x %08x %08x %08x\n", qw, got[0], got[1], got[2],
                    got[3], a, b, c, d);
    return ok;
}

// PACKED GIFtag: NLOOP 1, EOP, NREGS 1, REGS[0] = RGBAQ; then one RGBAQ quadword.
void PutGifPacket(Stream& s, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    Word(s, 0x00008001u);
    Word(s, 0x10000000u);
    Word(s, 0x00000001u);
    Word(s, 0);
    Word(s, r);
    Word(s, g);
    Word(s, b);
    Word(s, a);
}

void TestRegisterEffects() {
    Rig rig;
    Stream s;
    Code(s, 0x01, 0, 0x0204);           // STCYCL CL=4 WL=2
    Code(s, 0x03, 0, 20);               // BASE
    Code(s, 0x02, 0, 8);                // OFFSET
    Code(s, 0x04, 0, 7);                // ITOP
    Code(s, 0x05, 0, 2);                // STMOD
    Code(s, 0x07, 0, 0x1234);           // MARK
    Code(s, 0x20, 0, 0); Word(s, 0x55AAFFE4u);                        // STMASK
    Code(s, 0x30, 0, 0); Word(s, 0x10); Word(s, 0x11); Word(s, 0x12); Word(s, 0x13);  // STROW
    Code(s, 0x31, 0, 0); Word(s, 0x20); Word(s, 0x21); Word(s, 0x22); Word(s, 0x23);  // STCOL
    Code(s, 0x00, 0, 0);
    PadToQw(s);
    Feed(rig, 1, s);
    const VifRegs& r = rig.vif.regs(1);
    CHECK(r.cycle == 0x0204u && VifRegsCl(r) == 4 && VifRegsWl(r) == 2);
    CHECK(r.base == 20 && r.ofst == 8 && r.tops == 20 && (r.stat & VifStat::kDbf) == 0);
    CHECK(r.itop == 7 && r.mode == 2);
    CHECK(r.mark == 0x1234 && (r.stat & VifStat::kMrk) != 0);
    CHECK(r.mask == 0x55AAFFE4u);
    CHECK(r.row[0] == 0x10 && r.row[1] == 0x11 && r.row[2] == 0x12 && r.row[3] == 0x13);
    CHECK(r.col[0] == 0x20 && r.col[1] == 0x21 && r.col[2] == 0x22 && r.col[3] == 0x23);
    CHECK((r.stat & VifStat::kVpsMask) == VifStat::kVpsIdle);
    CHECK(r.code == 0);  // the last decoded code (a NOP pad)
    CHECK(rig.vif.IsIdle(1) && rig.vif.IsIdle(0));
    // Field widths: BASE/OFFSET/ITOP are 10 bits, STMOD 2 bits.
    Stream s2;
    Code(s2, 0x03, 0, 0xFFFF); Code(s2, 0x02, 0, 0xFFFF); Code(s2, 0x04, 0, 0xFFFF); Code(s2, 0x05, 0, 0x7);
    Feed(rig, 1, s2);
    CHECK(r.base == 0x3FF && r.ofst == 0x3FF && r.itop == 0x3FF && r.mode == 3);
    // VIF0 has no BASE/OFFSET/MSKPATH3.
    Stream s0;
    Code(s0, 0x03, 0, 20); Code(s0, 0x02, 0, 8); Code(s0, 0x06, 0, 0x8000); Code(s0, 0x04, 0, 5);
    Feed(rig, 0, s0);
    CHECK(rig.vif.regs(0).base == 0 && rig.vif.regs(0).ofst == 0 && rig.vif.regs(0).tops == 0);
    CHECK(rig.vif.regs(0).itop == 5);
    CHECK(!rig.gif_regs.Path3MaskedByVif());
    // Reset: CYCLE back to 1/1, everything else zero.
    rig.vif.Reset();
    CHECK(r.cycle == 0x0101u && r.base == 0 && r.mask == 0 && r.row[3] == 0 && r.stat == 0);
}

struct TopsLog {
    std::vector<uint32_t> v;
};
void ObserveTops(void* user, uint16_t tops, uint16_t itops) {
    static_cast<TopsLog*>(user)->v.push_back((static_cast<uint32_t>(tops) << 16) | itops);
}

void TestMscal() {
    Rig rig;
    TopsLog log;
    rig.vif.SetTopsObserver(&ObserveTops, &log);
    // Micro: 0x00 vi1 = XTOP ; 0x08 vi2 = XITOP [E] ; 0x10 nop (delay) ; 0x18 vi3 = 9 [E] ; 0x20 nop.
    PutBundle(rig.vu1, 0x00, EncXtop(1), U_NOP);
    PutBundle(rig.vu1, 0x08, EncXitop(2), U_NOP | U_EBIT);
    PutBundle(rig.vu1, 0x10, L_NOP, U_NOP);
    PutBundle(rig.vu1, 0x18, EncIaddiu(3, 0, 9), U_NOP | U_EBIT);
    PutBundle(rig.vu1, 0x20, L_NOP, U_NOP);

    Stream s;
    Code(s, 0x03, 0, 20);   // BASE 20
    Code(s, 0x02, 0, 8);    // OFFSET 8 -> TOPS = 20, DBF = 0
    Code(s, 0x04, 0, 7);    // ITOP 7
    Code(s, 0x14, 0, 0);    // MSCAL 0
    Feed(rig, 1, s);
    const VifRegs& r = rig.vif.regs(1);
    // First kick: TOP := TOPS (20), ITOPS := ITOP (7), TOPS -> BASE+OFST (28), DBF set.
    CHECK(r.top == 20 && r.itops == 7 && r.tops == 28 && (r.stat & VifStat::kDbf) != 0);
    CHECK(rig.vu1.regs().VI[1] == 20 && rig.vu1.regs().VI[2] == 7);
    CHECK(rig.vu1.vif_top() == 20 && rig.vu1.vif_itop() == 7);
    CHECK(rig.vu1.regs().TPC == 0x18u);
    CHECK(log.v.size() == 1 && log.v[0] == ((28u << 16) | 7u));

    // Second kick (MSCALF): TOP := 28, TOPS -> BASE (20), DBF cleared.
    Stream s2;
    Code(s2, 0x04, 0, 3);   // ITOP 3
    Code(s2, 0x15, 0, 0);   // MSCALF 0
    Feed(rig, 1, s2);
    CHECK(r.top == 28 && r.itops == 3 && r.tops == 20 && (r.stat & VifStat::kDbf) == 0);
    CHECK(rig.vu1.regs().VI[1] == 28 && rig.vu1.regs().VI[2] == 3);
    CHECK(log.v.size() == 2 && log.v[1] == ((20u << 16) | 3u));

    // MSCNT resumes at TPC (0x18): vi3 = 9; the swap happens again (TOP := 20).
    Stream s3;
    Code(s3, 0x17, 0, 0x7FFF);  // imm ignored
    Feed(rig, 1, s3);
    CHECK(rig.vu1.regs().VI[3] == 9);
    CHECK(r.top == 20 && r.tops == 28 && (r.stat & VifStat::kDbf) != 0);
    CHECK(rig.vu1.regs().TPC == 0x28u);

    // OFST 0: TOPS stays put, DBF still toggles.
    Stream s4;
    Code(s4, 0x02, 0, 0);  // OFFSET 0 -> DBF 0, TOPS = BASE
    Code(s4, 0x14, 0, 0);
    Feed(rig, 1, s4);
    CHECK(r.tops == 20 && r.top == 20 && (r.stat & VifStat::kDbf) != 0);

    // VIF0: MSCAL runs VU0 without any swap; MSCALF / MSCNT are no-ops.
    PutBundle(rig.vu0, 0x00, EncIaddiu(1, 0, 5), U_NOP | U_EBIT);
    PutBundle(rig.vu0, 0x08, L_NOP, U_NOP);
    PutBundle(rig.vu0, 0x10, EncIaddiu(4, 0, 3), U_NOP | U_EBIT);
    PutBundle(rig.vu0, 0x18, L_NOP, U_NOP);
    Stream s0;
    Code(s0, 0x14, 0, 0);
    Feed(rig, 0, s0);
    CHECK(rig.vu0.regs().VI[1] == 5);
    CHECK(rig.vif.regs(0).top == 0 && rig.vif.regs(0).tops == 0 && (rig.vif.regs(0).stat & VifStat::kDbf) == 0);
    Stream s0b;
    Code(s0b, 0x15, 0, 2);
    Code(s0b, 0x17, 0, 0);
    Feed(rig, 0, s0b);
    CHECK(rig.vu0.regs().VI[4] == 0);
    CHECK(log.v.size() == 4);  // VIF0 kicks never touch TOPS/ITOPS

    // BindVu1: VIF1 programs run on the bound unit.
    VuUnit other(1, &rig.arbiter);
    PutBundle(other, 0x00, EncXtop(5), U_NOP | U_EBIT);
    PutBundle(other, 0x08, L_NOP, U_NOP);
    rig.vif.BindVu1(&other);
    CHECK(&rig.vif.CtxFor(1) == &other && &rig.vif.CtxFor(0) == &rig.vu0);
    Stream s5;
    Code(s5, 0x14, 0, 0);
    Feed(rig, 1, s5);
    CHECK(other.regs().VI[5] == r.top);
    rig.vif.BindVu1(nullptr);
    CHECK(&rig.vif.CtxFor(1) == &rig.vu1);
}

void TestMpg() {
    Rig rig;
    const uint64_t gen0 = rig.vu1.micro_generation();
    Stream s;
    Code(s, 0x4A, 2, 0x10);  // MPG 2 bundles at 0x10*8 = 0x80
    Word(s, 0x11111111u); Word(s, 0x22222222u); Word(s, 0x33333333u); Word(s, 0x44444444u);
    PadToQw(s);
    Feed(rig, 1, s);
    uint32_t got[4];
    std::memcpy(got, rig.vu1.state().micro + 0x80, 16);
    CHECK(got[0] == 0x11111111u && got[1] == 0x22222222u && got[2] == 0x33333333u && got[3] == 0x44444444u);
    CHECK(rig.vu1.micro_generation() == gen0 + 1);
    CHECK(rig.vu0.micro_generation() == 0);
    // Micro wrap: address 0x7FF*8 = 0x3FF8, the second bundle lands at 0.
    Stream s2;
    Code(s2, 0x4A, 2, 0x7FF);
    Word(s2, 0xA); Word(s2, 0xB); Word(s2, 0xC); Word(s2, 0xD);
    PadToQw(s2);
    Feed(rig, 1, s2);
    std::memcpy(got, rig.vu1.state().micro + 0x3FF8, 8);
    CHECK(got[0] == 0xA && got[1] == 0xB);
    std::memcpy(got, rig.vu1.state().micro, 8);
    CHECK(got[0] == 0xC && got[1] == 0xD);
    CHECK(rig.vu1.micro_generation() == gen0 + 2);
    // VIF0 MPG writes the VU0 micro memory (4 KB mask).
    Stream s0;
    Code(s0, 0x4A, 1, 0x200);  // 0x200*8 = 0x1000 -> 0 through the VU0 mask
    Word(s0, 0x55); Word(s0, 0x66);
    PadToQw(s0);
    Feed(rig, 0, s0);
    std::memcpy(got, rig.vu0.state().micro, 8);
    CHECK(got[0] == 0x55 && got[1] == 0x66);
    CHECK(rig.vu0.micro_generation() == 1);
    // MPG num 0 = 256 bundles (2048 bytes).
    Stream s3;
    Code(s3, 0x4A, 0, 0);
    for (uint32_t i = 0; i < 512; i++) Word(s3, 0x1000 + i);
    PadToQw(s3);
    Feed(rig, 1, s3);
    std::memcpy(got, rig.vu1.state().micro + 2040, 8);
    CHECK(got[0] == 0x1000 + 510 && got[1] == 0x1000 + 511);
    CHECK(rig.vif.IsIdle(1));
}

void TestDirect() {
    for (int fast = 0; fast < 2; fast++) {
        Rig rig(fast != 0);
        Stream s;
        Code(s, 0x50, 0, 2);  // DIRECT 2 quadwords; payload starts on the next quadword
        PadToQw(s);
        PutGifPacket(s, 0x10, 0x20, 0x30, 0x40);
        Feed(rig, 1, s);
        CHECK(rig.arbiter.hasPending());
        CHECK(rig.arbiter.PendingBytes() == 32);
        rig.arbiter.drain();
        CHECK(rig.ops.Count() == 3);
        if (rig.ops.Count() == 3) {
            CHECK(rig.ops.Ops()[0].kind == static_cast<uint8_t>(RegisterOpKind::Tag));
            CHECK(rig.ops.Ops()[0].path == static_cast<uint8_t>(GifPath::Path2));
            CHECK(rig.ops.Ops()[1].kind == static_cast<uint8_t>(RegisterOpKind::Rgbaq));
            CHECK(rig.ops.Ops()[1].lo == (0x10u | (0x20u << 8) | (0x30u << 16) | (0x40u << 24)));
            CHECK(rig.ops.Ops()[2].kind == static_cast<uint8_t>(RegisterOpKind::Eop));
        }
        CHECK(rig.vif.unit(1).state == VifState::Idle && rig.vif.unit(1).pending_bytes == 0);
        CHECK((rig.vif.regs(1).stat & VifStat::kInt) == 0);

        // DIRECTHL with the i-bit: the interrupt flag lands when the transfer completes;
        // a transfer split across two feeds streams through DirectStream.
        Stream s2;
        Code(s2, 0x51, 0, 2, true);
        PadToQw(s2);
        Feed(rig, 1, s2);
        CHECK(rig.vif.unit(1).state == VifState::DirectStream && rig.vif.unit(1).pending_bytes == 32);
        CHECK((rig.vif.regs(1).stat & VifStat::kVpsMask) == VifStat::kVpsTransfer ||
              (rig.vif.regs(1).stat & VifStat::kVpsMask) == VifStat::kVpsIdle);
        CHECK((rig.vif.regs(1).stat & VifStat::kInt) == 0);
        Stream s3;
        PutGifPacket(s3, 1, 2, 3, 4);
        Feed(rig, 1, s3);
        CHECK(rig.vif.unit(1).state == VifState::Idle);
        CHECK((rig.vif.regs(1).stat & VifStat::kInt) != 0);
        rig.arbiter.drain();
        CHECK(rig.ops.Count() == 6);
        if (rig.ops.Count() == 6) CHECK(rig.ops.Ops()[4].lo == (1u | (2u << 8) | (3u << 16) | (4u << 24)));

        // ERR.MII masks the i-bit; commands after the DIRECT payload still run.
        rig.vif.regs(1).stat &= ~VifStat::kInt;
        rig.vif.regs(1).err = VifErr::kMii;
        Stream s4;
        Code(s4, 0x50, 0, 2, true);
        PadToQw(s4);
        PutGifPacket(s4, 9, 9, 9, 9);
        Code(s4, 0x07, 0, 0xBEEF);      // MARK after the transfer
        PadToQw(s4);
        Feed(rig, 1, s4);
        CHECK((rig.vif.regs(1).stat & VifStat::kInt) == 0);
        CHECK(rig.vif.regs(1).mark == 0xBEEF);
        CHECK(rig.vif.unit(1).state == VifState::Idle);
        // i-bit on an ordinary command sets INT immediately unless masked.
        rig.vif.regs(1).err = 0;
        Stream s5;
        Code(s5, 0x00, 0, 0, true);
        Feed(rig, 1, s5);
        CHECK((rig.vif.regs(1).stat & VifStat::kInt) != 0);
    }
}

// The i-bit stall hooks (SetIrqHooks): INT | VIS after the command that carries the i bit --
// the hook runs AFTER the command's effect (an UNPACK's data is in VU memory, a MSCAL's
// program has run), once per i bit, on both feed paths, never under ERR.MII; FBRST.STC
// clears INT | VIS and runs the STC hook; a DIRECT's i bit fires at its completion.
struct IrqLog {
    int irq = 0, stc = 0, vif_seen = -1;
    uint32_t qw2_at_irq = 0;
    Rig* rig = nullptr;
};
void OnIrqLog(void* user, int vif) {
    auto* l = static_cast<IrqLog*>(user);
    ++l->irq;
    l->vif_seen = vif;
    l->rig->vu1.ReadDataMem(2 * 16, reinterpret_cast<uint8_t*>(&l->qw2_at_irq), 4);
}
void OnStcLog(void* user, int vif) {
    auto* l = static_cast<IrqLog*>(user);
    if (vif == 1) ++l->stc;
}
void TestIrqHooks() {
    for (int fast = 0; fast < 2; fast++) {
        Rig rig(fast != 0);
        IrqLog log;
        log.rig = &rig;
        rig.vif.SetIrqHooks(&OnIrqLog, &OnStcLog, &log);
        Stream s;
        Code(s, 0x01, 0, 0x0101);
        Code(s, 0x6C, 1, 2, true);   // UNPACK V4-32 x1 -> qw2, i bit
        s.w.push_back(0xAAAA0001u); s.w.push_back(2); s.w.push_back(3); s.w.push_back(4);
        Feed(rig, 1, s);
        CHECK(log.irq == 1 && log.vif_seen == 1);
        CHECK(log.qw2_at_irq == 0xAAAA0001u);   // the hook saw the unpack's data: after the command
        CHECK((rig.vif.regs(1).stat & (VifStat::kInt | VifStat::kVis)) == (VifStat::kInt | VifStat::kVis));
        // FLUSHE with the i bit (the title's lighting packet tail) -- no payload.
        Stream s2;
        Code(s2, 0x10, 0, 0, true);
        Feed(rig, 1, s2);
        CHECK(log.irq == 2);
        // STC: INT | VIS cleared, the STC hook ran once; an FBRST without STC runs nothing.
        rig.vif.Write(Vif::kVif1Base + VifRegOffset::kFbrst, 4, VifFbrst::kStc);
        CHECK((rig.vif.regs(1).stat & (VifStat::kInt | VifStat::kVis)) == 0);
        CHECK(log.stc == 1);
        rig.vif.Write(Vif::kVif1Base + VifRegOffset::kFbrst, 4, 0);
        CHECK(log.stc == 1);
        // ERR.MII: no INT, no hook.
        rig.vif.regs(1).err = VifErr::kMii;
        Feed(rig, 1, s2);
        CHECK(log.irq == 2 && (rig.vif.regs(1).stat & VifStat::kInt) == 0);
        rig.vif.regs(1).err = 0;
        // A DIRECT's i bit fires once, at completion.
        Stream s3;
        Code(s3, 0x50, 0, 2, true);
        PadToQw(s3);
        PutGifPacket(s3, 1, 2, 3, 4);
        Feed(rig, 1, s3);
        CHECK(log.irq == 3);
        rig.arbiter.drain();
        // VIF0 reports its own unit.
        Stream s4;
        Code(s4, 0x00, 0, 0, true);
        Feed(rig, 0, s4);
        CHECK(log.irq == 4 && log.vif_seen == 0);
        CHECK((rig.vif.regs(0).stat & (VifStat::kInt | VifStat::kVis)) == (VifStat::kInt | VifStat::kVis));
        rig.vif.SetIrqHooks(nullptr, nullptr, nullptr);
        Feed(rig, 1, s2);
        CHECK(log.irq == 4);   // unhooked: the bits alone
    }
}

void TestMskpath3() {
    Rig rig;
    Stream s;
    Code(s, 0x06, 0, 0x8000);
    Feed(rig, 1, s);
    CHECK(rig.gif_regs.Path3MaskedByVif());
    Stream s2;
    Code(s2, 0x06, 0, 0x0000);
    Feed(rig, 1, s2);
    CHECK(!rig.gif_regs.Path3MaskedByVif());
    Stream s3;
    Code(s3, 0x06, 0, 0x8000);
    Feed(rig, 0, s3);  // VIF0 has no PATH3 mask
    CHECK(!rig.gif_regs.Path3MaskedByVif());
}

void TestMmio() {
    Rig rig;
    Vif& vif = rig.vif;
    using namespace VifRegOffset;
    vif.Write(Vif::kVif1Base + kCycle, 4, 0x0303);
    vif.Write(Vif::kVif1Base + kMode, 4, 0xFFFF);
    vif.Write(Vif::kVif1Base + kMask, 4, 0x12345678);
    vif.Write(Vif::kVif1Base + kErr, 4, 0xFF);
    vif.Write(Vif::kVif1Base + kRow0 + 8, 4, 0xAA);
    vif.Write(Vif::kVif1Base + kCol0 + 12, 4, 0xBB);
    CHECK(vif.regs(1).cycle == 0x0303 && vif.regs(1).mode == 3 && vif.regs(1).mask == 0x12345678u);
    CHECK(vif.regs(1).err == 7 && vif.regs(1).row[2] == 0xAA && vif.regs(1).col[3] == 0xBB);
    CHECK(vif.Read(Vif::kVif1Base + kCycle, 4) == 0x0303 && vif.Read(Vif::kVif1Base + kRow0 + 8, 4) == 0xAA);
    CHECK(vif.Read(Vif::kVif1Base + kCol0 + 12, 4) == 0xBB && vif.Read(Vif::kVif1Base + kErr, 4) == 7);
    // Read-only registers drop writes.
    vif.Write(Vif::kVif1Base + kTops, 4, 99);
    vif.Write(Vif::kVif1Base + kNum, 4, 99);
    vif.Write(Vif::kVif1Base + kCode, 4, 99);
    CHECK(vif.regs(1).tops == 0 && vif.regs(1).num == 0 && vif.regs(1).code == 0);
    // VIF0 has no BASE/OFST/TOPS/TOP view.
    vif.regs(0).base = 5; vif.regs(0).tops = 6; vif.regs(0).top = 7; vif.regs(0).ofst = 8; vif.regs(0).itop = 9;
    CHECK(vif.Read(Vif::kVif0Base + kBase, 4) == 0 && vif.Read(Vif::kVif0Base + kTops, 4) == 0);
    CHECK(vif.Read(Vif::kVif0Base + kTop, 4) == 0 && vif.Read(Vif::kVif0Base + kOfst, 4) == 0);
    CHECK(vif.Read(Vif::kVif0Base + kItop, 4) == 9);
    // MARK write clears STAT.MRK; STAT write-1-to-clear.
    Stream s;
    Code(s, 0x07, 0, 0x1111);
    Feed(rig, 1, s);
    CHECK((vif.regs(1).stat & VifStat::kMrk) != 0);
    vif.Write(Vif::kVif1Base + kMark, 4, 0x2222);
    CHECK(vif.regs(1).mark == 0x2222 && (vif.regs(1).stat & VifStat::kMrk) == 0);
    vif.regs(1).stat |= VifStat::kInt | VifStat::kMrk | VifStat::kEr1;
    vif.Write(Vif::kVif1Base + kStat, 4, VifStat::kInt | VifStat::kEr1);
    CHECK((vif.regs(1).stat & (VifStat::kInt | VifStat::kEr1)) == 0 && (vif.regs(1).stat & VifStat::kMrk) != 0);
    // FBRST: STP -> VSS, FBK -> VFS, STC clears them, RST drops a pending command.
    vif.Write(Vif::kVif1Base + kFbrst, 4, VifFbrst::kStp | VifFbrst::kFbk);
    CHECK((vif.regs(1).stat & (VifStat::kVss | VifStat::kVfs)) == (VifStat::kVss | VifStat::kVfs));
    CHECK(vif.regs(1).fbrst == (VifFbrst::kStp | VifFbrst::kFbk));
    vif.Write(Vif::kVif1Base + kFbrst, 4, VifFbrst::kStc);
    CHECK((vif.regs(1).stat & (VifStat::kVss | VifStat::kVfs)) == 0);
    vif.regs(1).cycle = 0x0101;
    Stream s2;
    Code(s2, 0x6C, 4, 0);  // UNPACK V4-32 x4 needs 16 words; only 3 follow
    Word(s2, 1); Word(s2, 2); Word(s2, 3);
    Feed(rig, 1, s2);
    CHECK(vif.unit(1).state == VifState::WaitData && vif.unit(1).pending_bytes == 64);
    CHECK((vif.regs(1).stat & VifStat::kVpsMask) == VifStat::kVpsWait);
    vif.Write(Vif::kVif1Base + kFbrst, 4, VifFbrst::kRst);
    CHECK(vif.unit(1).state == VifState::Idle && vif.unit(1).pending_bytes == 0 && vif.unit(1).fifo.empty());
    // The VIF1 FIFO window: four words, the quadword runs on the last slot.
    vif.Write(Vif::kVif1FifoBase + 0x0, 4, (0x01u << 24) | 0x0808u);  // STCYCL 8/8
    vif.Write(Vif::kVif1FifoBase + 0x4, 4, (0x04u << 24) | 0x0011u);  // ITOP 0x11
    CHECK(vif.regs(1).cycle == 0x0101u);  // nothing yet
    vif.Write(Vif::kVif1FifoBase + 0x8, 4, 0);
    vif.Write(Vif::kVif1FifoBase + 0xC, 4, 0);
    CHECK(vif.regs(1).cycle == 0x0808u && vif.regs(1).itop == 0x11);
    CHECK(vif.Read(Vif::kVif1FifoBase, 4) == 0 && vif.Read(0x10004000u, 4) == 0);
}

void TestTteFeed() {
    // A DMAtag's TTE quadword: {code, code, pad, pad}. A payload that does not
    // fit in the quadword's remaining words starts on the NEXT quadword when the
    // feed is bracketed; without the bracket the pads are consumed as data.
    for (int bracket = 0; bracket < 2; bracket++) {
        Rig rig;
        Stream tte;
        Code(tte, 0x00, 0, 0);
        Code(tte, 0x6C, 1, 5);  // UNPACK V4-32 x1 -> qw 5, needs 4 words
        PadToQw(tte);
        rig.vif.SetTteFeed(1, bracket != 0);
        Feed(rig, 1, tte);
        rig.vif.SetTteFeed(1, false);
        Stream data;
        Word(data, 1); Word(data, 2); Word(data, 3); Word(data, 4);
        Word(data, 0x60 << 24 | 1 << 16 | 6);  // S-32 x1 -> qw 6 follows in the next span
        Word(data, 0x77);
        PadToQw(data);
        Feed(rig, 1, data);
        if (bracket) {
            CHECK(QwIs(rig.vu1, 5, 1, 2, 3, 4));
            CHECK(QwIs(rig.vu1, 6, 0x77, 0x77, 0x77, 0x77));
        } else {
            CHECK(QwIs(rig.vu1, 5, 0, 0, 1, 2));  // pads consumed; 3, 4 decode as NOPs
        }
        CHECK(rig.vif.IsIdle(1));
    }
    // ResetLatch drops a partial latch (word feed) and IsIdle reports it.
    Rig rig(false);
    uint32_t words[2] = {0, 0};
    rig.vif.ProcessData(1, reinterpret_cast<const uint8_t*>(words), 8);
    CHECK(!rig.vif.IsIdle(1) && rig.vif.unit(1).latch_fill == 2);
    rig.vif.ResetLatch(1);
    CHECK(rig.vif.IsIdle(1) && rig.vif.unit(1).latch_fill == 0);
}

// A mixed stream fed (a) whole through the span parser, (b) whole through the
// word latch, (c)/(d) split at every quadword boundary on both feeds: identical
// registers, VU memories and GS ops.
Stream MixedStream() {
    Stream s;
    Code(s, 0x01, 0, 0x0101);
    Code(s, 0x20, 0, 0); Word(s, 0x000000FFu);                                   // STMASK (row 0 all write-protect)
    Code(s, 0x30, 0, 0); Word(s, 0x10); Word(s, 0x11); Word(s, 0x12); Word(s, 0x13);  // STROW
    Code(s, 0x6C, 3, 40); for (uint32_t i = 1; i <= 12; i++) Word(s, i * 0x01010101u);  // V4-32 x3 (payload at odd word)
    Code(s, 0x07, 0, 0x4242);                                                    // MARK
    Code(s, 0x05, 0, 1);                                                          // STMOD 1
    Code(s, 0x68, 5, 300); for (uint32_t i = 1; i <= 15; i++) Word(s, 0xA0000000u + i);  // V3-32 x5, ROW add
    Code(s, 0x05, 0, 0);
    Code(s, 0x4A, 2, 4); Word(s, EncIaddiu(1, 0, 3)); Word(s, U_NOP | U_EBIT); Word(s, L_NOP); Word(s, U_NOP);  // MPG
    Code(s, 0x7C, 2, 700); for (uint32_t i = 1; i <= 8; i++) Word(s, 0xB0 + i);   // masked V4-32 x2
    Code(s, 0x50, 0, 2); PadToQw(s); PutGifPacket(s, 5, 6, 7, 8);             // DIRECT 2 QW
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x68, 3, 600); for (uint32_t i = 1; i <= 9; i++) Word(s, 0xC0 + i);   // V3-32 skip lane, odd start
    Code(s, 0x6E, 5, 0x4000 | 610); Word(s, 0x04030201u); Word(s, 0x08070605u); Word(s, 0x0C0B0A09u);
    Word(s, 0x100F0E0Du); Word(s, 0x14131211u);                                   // V4-8 x5 skip lane
    Code(s, 0x14, 0, 4);                                                          // MSCAL 0x20
    Code(s, 0x00, 0, 0);
    PadToQw(s);
    return s;
}

struct Snapshot {
    VifRegs regs1, regs0;
    std::vector<uint8_t> data, micro;
    uint64_t ops_hash;
    size_t ops_count;
    uint16_t vi1;
    bool operator==(const Snapshot& o) const {
        return std::memcmp(&regs1, &o.regs1, sizeof(VifRegs)) == 0 &&
               std::memcmp(&regs0, &o.regs0, sizeof(VifRegs)) == 0 && data == o.data && micro == o.micro &&
               ops_hash == o.ops_hash && ops_count == o.ops_count && vi1 == o.vi1;
    }
};

Snapshot Take(Rig& rig) {
    rig.arbiter.drain();
    Snapshot s;
    s.regs1 = rig.vif.regs(1);
    s.regs0 = rig.vif.regs(0);
    s.data.assign(rig.vu1.state().data, rig.vu1.state().data + Vu1State::kMemSize);
    s.micro.assign(rig.vu1.state().micro, rig.vu1.state().micro + Vu1State::kMemSize);
    s.ops_hash = rig.ops.Hash();
    s.ops_count = rig.ops.Count();
    s.vi1 = rig.vu1.regs().VI[1];
    return s;
}

void TestFeedEquivalence() {
    const Stream s = MixedStream();
    const size_t nqw = Size(s) / 16;
    Rig whole_fast(true), whole_word(false);
    Feed(whole_fast, 1, s);
    Feed(whole_word, 1, s);
    const Snapshot ref = Take(whole_fast);
    CHECK(ref == Take(whole_word));
    CHECK(ref.ops_count == 3 && ref.vi1 == 3 && ref.regs1.mark == 0x4242 && ref.regs1.top == 0);
    CHECK(QwIs(whole_fast.vu1, 40, 0x01010101u, 0x02020202u, 0x03030303u, 0x04040404u));
    CHECK(QwIs(whole_fast.vu1, 300, 0xA0000011u, 0xA0000013u, 0xA0000015u, 0xA0000017u));  // + ROW {10,11,12,13}, W = peek + row[3]
    CHECK(QwIs(whole_fast.vu1, 301, 0xA0000014u, 0xA0000016u, 0xA0000018u, 0x13));         // parity 1 != phase 0: W = 0 + row[3]
    CHECK(QwIs(whole_fast.vu1, 700, 0, 0, 0, 0) && QwIs(whole_fast.vu1, 701, 0, 0, 0, 0));  // mask row 0 all NoWrite
    CHECK(QwIs(whole_fast.vu1, 600, 0xC1, 0xC2, 0xC3, 0xC4));               // skip lane, parity 0 == phase 0
    CHECK(QwIs(whole_fast.vu1, 604, 0xC4, 0xC5, 0xC6, 0));                  // parity 1: W = 0
    CHECK(QwIs(whole_fast.vu1, 608, 0xC7, 0xC8, 0xC9, 0x6E054262u));        // W = the lookahead word (the next code)
    CHECK(QwIs(whole_fast.vu1, 610, 1, 2, 3, 4) && QwIs(whole_fast.vu1, 614, 5, 6, 7, 8) &&
          QwIs(whole_fast.vu1, 626, 17, 18, 19, 20));
    int compared = 0;
    for (size_t cut = 1; cut < nqw; cut++) {
        for (int fast = 0; fast < 2; fast++) {
            Rig rig(fast != 0);
            rig.vif.ProcessData(1, Bytes(s), cut * 16);
            rig.vif.ProcessData(1, Bytes(s) + cut * 16, Size(s) - cut * 16);
            const Snapshot got = Take(rig);
            if (!(got == ref)) {
                std::printf("FAIL feed split at qw %zu fast=%d diverges (ops %zu vs %zu, mark %x)\n", cut, fast,
                            got.ops_count, ref.ops_count, got.regs1.mark);
                g_fail++;
            }
            compared++;
        }
    }
    // Three-way splits through the word path as well (every pair of cuts on a stride).
    for (size_t c1 = 1; c1 + 1 < nqw; c1 += 2) {
        for (size_t c2 = c1 + 1; c2 < nqw; c2 += 3) {
            Rig rig(true);
            rig.vif.ProcessData(1, Bytes(s), c1 * 16);
            rig.vif.ProcessData(1, Bytes(s) + c1 * 16, (c2 - c1) * 16);
            rig.vif.ProcessData(1, Bytes(s) + c2 * 16, Size(s) - c2 * 16);
            if (!(Take(rig) == ref)) {
                std::printf("FAIL feed splits at qw %zu/%zu diverge\n", c1, c2);
                g_fail++;
            }
            compared++;
        }
    }
    CHECK(compared > 40);
}

}  // namespace

int main() {
    TestRegisterEffects();
    TestMscal();
    TestMpg();
    TestDirect();
    TestIrqHooks();
    TestMskpath3();
    TestMmio();
    TestTteFeed();
    TestFeedEquivalence();
    if (g_fail) {
        std::printf("VifCommands: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("VifCommands: PASS\n");
    return 0;
}

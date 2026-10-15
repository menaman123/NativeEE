// VifMmioGate: the router-facing VIF surface over a full Vif rig.
// Pins the reference's routed observable (P5-VIF-MMIO-OBSERVABLE): reads of
// FBRST/ERR on either unit forward to the unit, every other VIF register read
// is 0 (while the unit's own Read of the same address is non-zero); writes of
// FBRST/ERR forward, a VIF1 FIFO-window write reaches the unit exactly as a
// direct Vif::Write does, every other VIF register write leaves the register
// file untouched; the gated-access counters are exact.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_mmio_gate.h"
#include "engine/vif/vif_regs.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// One complete VIF rig: GS recording sink behind the parser and arbiter, both VU units, the VIF.
struct TestVifMmioGate {
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0;
    VuUnit vu1;
    Vif vif;
    TestVifMmioGate() : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs) {}
};
using Rig = TestVifMmioGate;

bool SameUnitFeedState(const VifUnitState& a, const VifUnitState& b) {
    return a.latch == b.latch && a.latch_fill == b.latch_fill && a.fifo == b.fifo && a.state == b.state &&
           a.word_index == b.word_index && a.current_qw == b.current_qw && a.pending_bytes == b.pending_bytes;
}

}  // namespace

int main() {
    using namespace VifRegOffset;
    std::printf("VifMmioGate\n");

    // --- FBRST / ERR forward on both units (write through the gate, read back through it) ---
    {
        Rig rig;
        VifMmioGate gate(rig.vif);
        gate.Write(Vif::kVif0Base + kErr, 4, 0x5u);
        gate.Write(Vif::kVif1Base + kErr, 4, 0x3u);
        Check(gate.Read(Vif::kVif0Base + kErr, 4) == 0x5u && rig.vif.regs(0).err == 0x5u, "VIF0_ERR write+read forward");
        Check(gate.Read(Vif::kVif1Base + kErr, 4) == 0x3u && rig.vif.regs(1).err == 0x3u, "VIF1_ERR write+read forward");
        gate.Write(Vif::kVif0Base + kFbrst, 4, VifFbrst::kStp);
        gate.Write(Vif::kVif1Base + kFbrst, 4, VifFbrst::kFbk);
        Check(gate.Read(Vif::kVif0Base + kFbrst, 4) == VifFbrst::kStp && (rig.vif.regs(0).stat & VifStat::kVss),
              "VIF0_FBRST write forwards (STP -> STAT.VSS) and reads back");
        Check(gate.Read(Vif::kVif1Base + kFbrst, 4) == VifFbrst::kFbk && (rig.vif.regs(1).stat & VifStat::kVfs),
              "VIF1_FBRST write forwards (FBK -> STAT.VFS) and reads back");
        Check(gate.ReadsZeroed() == 0 && gate.WritesDropped() == 0, "forwarded accesses are not counted");
    }

    // --- every other register reads 0 through the gate while the unit's own Read is non-zero ---
    {
        Rig rig;
        VifMmioGate gate(rig.vif);
        // Make the unit's registers observably non-zero: STAT through FBRST.STP
        // (a routed write), the rest through the register file directly.
        rig.vif.Write(Vif::kVif1Base + kFbrst, 4, VifFbrst::kStp);
        for (int u = 0; u < 2; ++u) {
            VifRegs& r = rig.vif.regs(u);
            r.mark = 0x1234u + static_cast<uint32_t>(u);
            r.num = 0x55u;
            r.code = 0x6c000010u;
            for (int i = 0; i < 4; ++i) { r.row[i] = 0x100u + static_cast<uint32_t>(i); r.col[i] = 0x200u + static_cast<uint32_t>(i); }
        }
        const uint32_t gated[] = {kStat, kMark, kCycle, kNum, kCode, kRow0, kRow0 + 4, kRow0 + 8, kRow0 + 12,
                                  kCol0, kCol0 + 4, kCol0 + 8, kCol0 + 12};
        uint64_t expected_zeroed = 0;
        bool all_zero = true, unit_nonzero = true;
        for (int u = 0; u < 2; ++u) {
            const uint32_t base = u == 0 ? Vif::kVif0Base : Vif::kVif1Base;
            for (uint32_t off : gated) {
                if (u == 0 && off == kStat) continue;  // VIF0 STAT was left 0 on purpose: skip the non-zero proof there
                const uint64_t through_gate = gate.Read(base + off, 4);
                const uint64_t direct = rig.vif.Read(base + off, 4);
                ++expected_zeroed;
                if (through_gate != 0) { all_zero = false; std::printf("    unit %d +0x%03x read 0x%llx through the gate\n", u, off, (unsigned long long)through_gate); }
                if (direct == 0) { unit_nonzero = false; std::printf("    unit %d +0x%03x reads 0 from Vif itself (proof vacuous)\n", u, off); }
            }
        }
        Check(all_zero, "STAT/MARK/CYCLE/NUM/CODE/ROW/COL read 0 through the gate");
        Check(unit_nonzero, "... while Vif::Read of the same addresses is non-zero");
        Check(gate.ReadsZeroed() == expected_zeroed, "ReadsZeroed counts exactly the zeroed reads");
        std::printf("    reads_zeroed=%llu\n", (unsigned long long)gate.ReadsZeroed());
        // FBRST/ERR still forward on the same gate, uncounted.
        Check(gate.Read(Vif::kVif1Base + kFbrst, 4) == VifFbrst::kStp && gate.ReadsZeroed() == expected_zeroed,
              "FBRST read forwards and does not count");
    }

    // --- non-FBRST/ERR writes leave the register file unchanged and count ---
    {
        Rig rig;
        VifMmioGate gate(rig.vif);
        VifRegs before0, before1;
        std::memcpy(&before0, &rig.vif.regs(0), sizeof(VifRegs));
        std::memcpy(&before1, &rig.vif.regs(1), sizeof(VifRegs));
        const uint32_t dropped[] = {kStat, kMark, kCycle, kMode, kMask, kRow0, kRow0 + 12, kCol0, kCol0 + 12};
        uint64_t expected_dropped = 0;
        for (int u = 0; u < 2; ++u) {
            const uint32_t base = u == 0 ? Vif::kVif0Base : Vif::kVif1Base;
            for (uint32_t off : dropped) { gate.Write(base + off, 4, 0xDEADBEEFu); ++expected_dropped; }
        }
        Check(std::memcmp(&before0, &rig.vif.regs(0), sizeof(VifRegs)) == 0 &&
                  std::memcmp(&before1, &rig.vif.regs(1), sizeof(VifRegs)) == 0,
              "STAT/MARK/CYCLE/MODE/MASK/ROW/COL writes leave both register files unchanged");
        Check(gate.WritesDropped() == expected_dropped, "WritesDropped counts exactly the dropped writes");
        // The same writes straight into Vif DO land (proof the gate is doing something).
        rig.vif.Write(Vif::kVif0Base + kMark, 4, 0xDEADBEEFu);
        Check(rig.vif.regs(0).mark == 0xDEADBEEFu, "... while a direct Vif::Write of MARK lands");
        Check(gate.ReadsZeroed() == 0, "no read was counted by the write test");
        std::printf("    writes_dropped=%llu\n", (unsigned long long)gate.WritesDropped());
    }

    // --- a VIF1 FIFO-window write reaches the unit exactly as a direct Vif::Write ---
    {
        Rig direct, gated;
        VifMmioGate gate(gated.vif);
        // Three slots fill the latch (no submit yet): the latch/fill state must match.
        const uint32_t words[3] = {0x01000404u, 0x00000000u, 0x00000000u};  // STCYCL cl 4 wl 4 + two pads
        for (int i = 0; i < 3; ++i) {
            direct.vif.Write(Vif::kVif1FifoBase + 4u * static_cast<uint32_t>(i), 4, words[i]);
            gate.Write(Vif::kVif1FifoBase + 4u * static_cast<uint32_t>(i), 4, words[i]);
        }
        Check(SameUnitFeedState(direct.vif.unit(1), gated.vif.unit(1)) && gated.vif.unit(1).latch[0] == words[0],
              "three FIFO-window slots: latch state identical to a direct Vif::Write");
        // The fourth slot submits the quadword: the STCYCL lands on both.
        direct.vif.Write(Vif::kVif1FifoBase + 0xCu, 4, 0);
        gate.Write(Vif::kVif1FifoBase + 0xCu, 4, 0);
        Check(gated.vif.regs(1).cycle == 0x0404u && direct.vif.regs(1).cycle == 0x0404u,
              "fourth slot submits: STCYCL through the FIFO window lands on the unit (cycle 0x0404)");
        Check(std::memcmp(&direct.vif.regs(1), &gated.vif.regs(1), sizeof(VifRegs)) == 0 &&
                  SameUnitFeedState(direct.vif.unit(1), gated.vif.unit(1)),
              "VIF1 register file + feed state identical after the submit");
        Check(gate.WritesDropped() == 0 && gate.ReadsZeroed() == 0, "FIFO-window writes are forwarded, not counted");
        // The window ends at +0x10: the next word is outside every VIF range (the router would never hand it over),
        // and the VIF0 FIFO (0x10004000) is not a routed window.
        gate.Write(0x10004000u, 4, 0x01000101u);
        Check(gate.WritesDropped() == 1 && gated.vif.regs(0).cycle == direct.vif.regs(0).cycle,
              "a VIF0 FIFO address is dropped (not a routed window in the reference)");
    }

    std::printf("VifMmioGate: %s\n", g_failures ? "FAIL" : "ALL PASS");
    return g_failures ? 1 : 0;
}

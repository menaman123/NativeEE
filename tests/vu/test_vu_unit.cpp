// VuUnitSurface: the unit record -- VU0/VU1 masks on the byte-loop memory
// writers/readers (wrap across the top of memory), the micro epoch, Reset
// scope, the VIF top sources, and Vu1Case load/save round trip.
#include <cstdio>
#include <cstring>
#include <memory>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static void TestMemoryMasks() {
    VuUnit vu1(1);
    VuUnit vu0(0);
    CHECK(vu1.mem_mask() == 0x3FFFu && vu0.mem_mask() == 0x0FFFu);
    CHECK(vu1.xgkick() == nullptr && vu0.xgkick() == nullptr);
    CHECK(vu1.regs().VF[0].w == 1.0f && vu1.regs().Q == 1.0f);   // VUregs::Reset

    const uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    // VU1: a write straddling 0x3FFC wraps to 0.
    vu1.WriteDataMem(0x3FFC, src, 8);
    CHECK(vu1.state().data[0x3FFC] == 1 && vu1.state().data[0x3FFF] == 4);
    CHECK(vu1.state().data[0] == 5 && vu1.state().data[3] == 8);
    uint8_t rd[8] = {};
    vu1.ReadDataMem(0x3FFC, rd, 8);
    CHECK(std::memcmp(rd, src, 8) == 0);
    // Addresses above the mask fold down.
    vu1.WriteDataMem(0x14000, src, 4);
    CHECK(vu1.state().data[0] == 1);
    // VU0: 4 KB wrap; the upper 12 KB are never reached.
    vu0.WriteDataMem(0x0FFE, src, 4);
    CHECK(vu0.state().data[0x0FFE] == 1 && vu0.state().data[0x0FFF] == 2);
    CHECK(vu0.state().data[0] == 3 && vu0.state().data[1] == 4);
    CHECK(vu0.state().data[0x1000] == 0);
    vu0.WriteMicroMem(0x1008, src, 8);                        // folds to 8
    CHECK(vu0.state().micro[8] == 1 && vu0.state().micro[0x1008] == 0);
    // Micro epoch: bumped by every micro store and by the window path only.
    const uint64_t g0 = vu1.micro_generation();
    vu1.WriteMicroMem(0, src, 8);
    CHECK(vu1.micro_generation() == g0 + 1);
    vu1.WriteDataMem(0, src, 8);
    CHECK(vu1.micro_generation() == g0 + 1);
    vu1.BumpMicroEpoch();
    CHECK(vu1.micro_generation() == g0 + 2);
}

static void TestResetScope() {
    RegisterOpStream ops;
    GifRegs gregs;
    GifParser parser(ops, gregs);
    GifArbiter arbiter(parser);
    VuUnit vu(1, &arbiter, true);
    CHECK(vu.xgkick() != nullptr && vu.xgkick()->paced_drain());
    VUregs& r = vu.regs();
    r.VF[3].x = 2.5f;
    vu.flags().SetCycle(12345);
    vu.flags().PushClip(r, 0x123);
    vu.flags().PushMac(r, 0x000F);
    vu.flags().SetQPending(true, 99999, 3.0f, 0);
    vu.flags().SetPPending(true, 99999, 4.0f);
    vu.state().xgkick_count = 7;
    vu.state().suspended = true;
    vu.Reset();
    CHECK(vu.regs().VF[3].x == 2.5f);                          // registers untouched
    CHECK(vu.cycle() == 12345);                                // clock untouched
    CHECK(vu.flags().clip_depth() == 1 && vu.flags().mac_depth() == 1);  // CLIP / MAC rings untouched
    CHECK(vu.flags().q_pending());                             // Q latch untouched
    CHECK(!vu.flags().p_pending());                            // P latch dropped
    CHECK(vu.stalls().fmac_newest_retire() == 0 && vu.stalls().ialu_newest_retire() == 0);  // writebacks forgotten
    CHECK(!vu.xgkick()->armed());
    CHECK(vu.state().xgkick_count == 0 && !vu.state().suspended);
    vu.SetVifTop(0x1234, 0x0056);
    CHECK(vu.vif_top() == 0x1234 && vu.vif_itop() == 0x0056);
}

static void TestCaseRoundTrip() {
    VuUnit vu(1);
    for (uint32_t i = 0; i < 16384; ++i) {
        const uint8_t m = static_cast<uint8_t>(i * 7);
        const uint8_t d = static_cast<uint8_t>(i * 13 + 1);
        vu.WriteMicroMem(i, &m, 1);
        vu.WriteDataMem(i, &d, 1);
    }
    vu.regs().VF[5].y = -3.0f;
    vu.regs().VI[7] = 0x1234;
    vu.regs().status_flag = 0x0C3;
    vu.SetVifTop(0x40, 0x0A);
    vu.flags().SetCycle(777777);
    vu.flags().SetQPending(true, 777780, 0.25f, 0x030);
    vu.flags().SetPPending(true, 777800, 6.0f);

    std::unique_ptr<Vu1Case> c(new Vu1Case());
    vu.SaveCase(*c, 0x1748);
    CHECK(c->magic == Vu1Case::kMagic && c->version == Vu1Case::kVersion && c->start_pc == 0x1748u);
    CHECK(c->vif1_top == 0x40u && c->vif1_itop == 0x0Au);
    CHECK(c->q_pending_valid == 1 && c->q_pending_retire == 777780u && c->q_pending_status == 0x030u);
    CHECK(c->q_pending_value_bits == VuFloat::F2Bits(0.25f));
    CHECK(c->p_pending_valid == 1 && c->p_pending_retire == 777800u && c->p_pending_value_bits == VuFloat::F2Bits(6.0f));
    CHECK(c->vu_cycle == 777777u);
    CHECK(std::memcmp(&c->regs, &vu.regs(), sizeof(VUregs)) == 0);
    CHECK(std::memcmp(c->micro_mem, vu.state().micro, 16384) == 0);
    CHECK(std::memcmp(c->data_mem, vu.state().data, 16384) == 0);

    VuUnit copy(1);
    const uint64_t g0 = copy.micro_generation();
    copy.LoadCase(*c);
    CHECK(copy.micro_generation() == g0 + 1);
    CHECK(std::memcmp(&copy.regs(), &vu.regs(), sizeof(VUregs)) == 0);
    CHECK(std::memcmp(copy.state().micro, vu.state().micro, 16384) == 0);
    CHECK(std::memcmp(copy.state().data, vu.state().data, 16384) == 0);
    CHECK(copy.vif_top() == 0x40 && copy.vif_itop() == 0x0A);
    CHECK(copy.cycle() == 777777u);
    CHECK(copy.flags().q_pending() && copy.flags().q_pending_retire() == 777780u &&
          copy.flags().q_pending_value() == 0.25f && copy.flags().q_pending_status() == 0x030);
    CHECK(copy.flags().p_pending() && copy.flags().p_pending_retire() == 777800u && copy.flags().p_pending_value() == 6.0f);
    std::unique_ptr<Vu1Case> c2(new Vu1Case());
    copy.SaveCase(*c2, 0x1748);
    CHECK(std::memcmp(c.get(), c2.get(), sizeof(Vu1Case)) == 0);
}

int main() {
    TestMemoryMasks();
    TestResetScope();
    TestCaseRoundTrip();
    if (g_fail) {
        std::printf("VuUnitSurface: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("VuUnitSurface: PASS\n");
    return 0;
}

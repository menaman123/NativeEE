// VifsSection: the VIFS savestate section over a full Vif rig -- 184-byte
// payload = VIF0 then VIF1 VifRegs in field order, round trip into a fresh
// rig byte-equal, restore idles each unit's transient state (latch, FIFO,
// cursor, pending command, DIRECT buffer) while keeping the restored
// registers, wrong lengths refused, REQUIRED through the registry.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/savestate/vifs_section.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_regs.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

struct TestVifsSection {
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0;
    VuUnit vu1;
    Vif vif;
    TestVifsSection() : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs) {}
};
using Rig = TestVifsSection;

void FillRegs(VifRegs& r, uint32_t seed) {
    uint32_t* w = reinterpret_cast<uint32_t*>(&r);
    for (size_t i = 0; i < sizeof(VifRegs) / 4; ++i) w[i] = seed * 0x9E3779B9u + static_cast<uint32_t>(i) * 0x01000193u;
}

}  // namespace

int main() {
    using namespace ps2r::savestate;
    std::printf("VifsSection\n");

    Rig src;
    FillRegs(src.vif.regs(0), 11);
    FillRegs(src.vif.regs(1), 22);
    VifsSection section(src.vif);
    std::vector<uint8_t> payload;
    section.Serialize(payload);
    Check(payload.size() == 184 && payload.size() == VifsSection::kPayloadBytes, "payload is 184 bytes (2 x 92)");
    Check(std::memcmp(payload.data(), &src.vif.regs(0), 92) == 0 && std::memcmp(payload.data() + 92, &src.vif.regs(1), 92) == 0,
          "VIF0 record then VIF1 record, raw field order");
    uint32_t stat0 = 0;
    std::memcpy(&stat0, payload.data(), 4);
    Check(stat0 == src.vif.regs(0).stat, "first word is VIF0 STAT");

    // A fresh rig with transient state in flight on VIF1: an UNPACK V4-32 of one
    // quadword whose payload has not arrived (the command word alone), so the
    // unit is waiting for data (or holding the word in its latch).
    Rig dst;
    const uint32_t unpack_v4_32_num1 = 0x6C010000u;
    dst.vif.ProcessData(1, reinterpret_cast<const uint8_t*>(&unpack_v4_32_num1), 4);
    Check(!dst.vif.IsIdle(1), "VIF1 not idle before restore (UNPACK awaiting its payload)");
    VifsSection restored(dst.vif);
    std::string err;
    Check(restored.Restore(payload, &err), ("restore ok: " + err).c_str());
    Check(std::memcmp(&dst.vif.regs(0), &src.vif.regs(0), 92) == 0 && std::memcmp(&dst.vif.regs(1), &src.vif.regs(1), 92) == 0,
          "both register files byte-equal after restore");
    Check(dst.vif.IsIdle(0) && dst.vif.IsIdle(1), "both units idle after restore (transient state reset)");
    Check(dst.vif.unit(1).direct_buffer.empty() && dst.vif.unit(1).pending_bytes == 0 && dst.vif.unit(1).latch_fill == 0,
          "DIRECT buffer, pending command and latch cleared");
    std::vector<uint8_t> again;
    restored.Serialize(again);
    Check(again == payload, "re-serialize byte-identical");
    Check(restored.RestoredCount() == 1, "restored count 1");

    // Refusals.
    Check(!VifsSection::Validate(std::vector<uint8_t>(183), &err), "183 bytes refused");
    Check(!VifsSection::Validate(std::vector<uint8_t>(185), &err), "185 bytes refused");
    Check(!VifsSection::Validate(std::vector<uint8_t>(92), &err), "one record (92) refused");

    // Registry semantics: REQUIRED; missing is a load error.
    SectionRegistry reg;
    section.Register(reg);
    const std::vector<Blob> cap = reg.Capture();
    Check(cap.size() == 1 && cap[0].tag == TAG_VIFS && (cap[0].flags & kSectionRequired) && cap[0].payload == payload,
          "registry capture: TAG_VIFS REQUIRED, same bytes");
    std::vector<Blob> none;
    Check(!reg.Validate(none, &err) && err.find("VIFS") != std::string::npos, ("missing VIFS is a load error: " + err).c_str());

    // ResetTransient keeps the register file untouched.
    Rig keep;
    FillRegs(keep.vif.regs(0), 33);
    VifRegs before = keep.vif.regs(0);
    keep.vif.ResetTransient(0);
    Check(std::memcmp(&keep.vif.regs(0), &before, 92) == 0 && keep.vif.IsIdle(0), "ResetTransient leaves registers, idles the unit");

    std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}

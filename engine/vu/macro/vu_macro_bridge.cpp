// VuMacroBridge -- rules in the header. Also defines the recomp-ABI seam
// bodies Vu0Macro::Execute / Vu0Macro::ResumeIfWaiting / Vu1Window::Execute
// (declared in engine/ee/recomp_abi/host_app/vu.h): the provider of row 27
// of the seam roster, the way ps2r_gs_sync provides row 15.
#include "engine/vu/macro/vu_macro_bridge.h"

#include <cstdio>
#include <cstring>

#include "engine/core/log/log.h"
#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/recomp_abi/host_app/vu.h"
#include "engine/vu/vu0/vu0_registry.h"
#include "engine/vu/vu0/vu0_trace.h"
#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

namespace {

VuUnit* g_vu0_unit = nullptr;
VuUnit* g_vu1_unit = nullptr;
Vu0Registry* g_census = nullptr;
Vu0Trace* g_trace = nullptr;
VuMacroBridge::Census g_stats;

static_assert(sizeof(VUregs) == 608, "the COP2 register block is the frozen 608-byte VUregs POD");

// The unbound park keeps the Phase-3 text so a leg without the units wired
// attributes the same way: the op and its micro-memory entry (imm15 * 8).
[[noreturn]] void ParkAbsent(CpuContext& ctx, const char* op, uint32_t start_addr) {
    static char buf[96];
    std::snprintf(buf, sizeof(buf), "%s micro pc 0x%x (VU micro memory + interpreter absent)", op, start_addr);
    GuestPark::Forever("vu_micro_absent", ctx.cpuRegs.pc, buf);
}

}  // namespace

void VuMacroBridge::Bind(VuUnit* vu0, VuUnit* vu1, Vu0Registry* census) {
    g_vu0_unit = vu0;
    g_vu1_unit = vu1;
    g_census = census;
}

void VuMacroBridge::SetTrace(Vu0Trace* trace) { g_trace = trace; }

void VuMacroBridge::Unbind() {
    g_vu0_unit = nullptr;
    g_vu1_unit = nullptr;
    g_census = nullptr;
    g_trace = nullptr;
}

bool VuMacroBridge::Bound() { return g_vu0_unit != nullptr && g_vu1_unit != nullptr; }

const VuMacroBridge::Census& VuMacroBridge::Stats() { return g_stats; }
void VuMacroBridge::ResetStats() { g_stats = Census{}; }

void VuMacroBridge::NoteVu0Entry(uint32_t start_addr) {
    if (!g_census) return;
    const Vu1State& st = g_vu0_unit->state();
    const Vu0Registry::Note note = g_census->NoteEntry(0, start_addr, st.micro, st.mem_mask + 1u);
    const char* name = note.descriptor ? note.descriptor->name : "?";
    switch (note.event) {
        case Vu0Registry::Event::FirstKnown:
            PS2R_LOG(VU, Info, "vu0 entry 0x%x %s (%s) first call, live %016llx %016llx", start_addr, name,
                     Vu0Registry::KindName(note.descriptor->kind), static_cast<unsigned long long>(note.live_lo),
                     static_cast<unsigned long long>(note.live_hi));
            break;
        case Vu0Registry::Event::FirstUnknown:
            PS2R_LOG(VU, Warn, "vu0 entry 0x%x unregistered, live %016llx %016llx", start_addr,
                     static_cast<unsigned long long>(note.live_lo), static_cast<unsigned long long>(note.live_hi));
            break;
        case Vu0Registry::Event::Periodic:
            PS2R_LOG(VU, Info, "vu0 entry 0x%x %s call %llu", start_addr, name,
                     static_cast<unsigned long long>(note.call_number));
            break;
        case Vu0Registry::Event::None:
            break;
    }
    if (note.fingerprint_mismatch)
        PS2R_LOG(VU, Warn, "vu0 entry 0x%x %s: live bytes differ from the registered fingerprint (%016llx %016llx)",
                 start_addr, name, static_cast<unsigned long long>(note.live_lo),
                 static_cast<unsigned long long>(note.live_hi));
}

void VuMacroBridge::ExecuteVu0(CpuContext& ctx, uint32_t start_addr) {
    if (!g_vu0_unit) ParkAbsent(ctx, "vcallms", start_addr);
    ++g_stats.vu0_calls;
    NoteVu0Entry(start_addr);
    const bool traced = g_trace != nullptr && g_trace->Wants(start_addr);
    if (traced) {
        const Vu1State& st = g_vu0_unit->state();
        g_trace->Begin(start_addr, ctx.vuRegs, st.micro, st.mem_mask + 1u);
        g_trace->NoteEeRegs(ctx);
    }
    std::memcpy(&g_vu0_unit->regs(), &ctx.vuRegs, sizeof(VUregs));
    g_vu0_unit->Execute(start_addr);
    std::memcpy(&ctx.vuRegs, &g_vu0_unit->regs(), sizeof(VUregs));
    if (traced) g_trace->End(start_addr, ctx.vuRegs);
}

void VuMacroBridge::ResumeVu0IfWaiting(CpuContext& ctx) {
    // Nothing parked = nothing touched: the EE's COP2 block stays the truth.
    if (!g_vu0_unit || !g_vu0_unit->state().suspended) return;
    ++g_stats.vu0_park_resumes;
    std::memcpy(&g_vu0_unit->regs(), &ctx.vuRegs, sizeof(VUregs));
    g_vu0_unit->ResumeIfWaiting();
    std::memcpy(&ctx.vuRegs, &g_vu0_unit->regs(), sizeof(VUregs));
}

void VuMacroBridge::ExecuteVu1(CpuContext& ctx, uint32_t start_addr) {
    if (!g_vu1_unit) ParkAbsent(ctx, "cmsar1 start", start_addr);
    ++g_stats.vu1_kicks;
    g_vu1_unit->Execute(start_addr);
}

// Macro-mode FMAC flags (engine/ee/recomp_abi/host_app/vu.h). The same pack
// and STATUS fold as the microprogram tiers' FlagPipeline::ApplyMacToStatus;
// macro mode commits at once (the EE reads CFC2 $vi16/$vi17 after its own
// VNOP spacing, as the manual asks of it).
void VuMacroCommitFlags(VUregs& regs, const VR_reg& result, uint8_t mask) {
    const uint16_t mac = VuFloat::PackMacBits(result.I, mask);
    regs.mac_flag = mac;
    uint16_t status = static_cast<uint16_t>(regs.status_flag & 0xFF0u);
    if (mac & 0x000Fu) status |= vu_status::Z;
    if (mac & 0x00F0u) status |= vu_status::S;
    if (mac & 0x0F00u) status |= vu_status::U;
    if (mac & 0xF000u) status |= vu_status::O;
    if (status & vu_status::Z) status |= vu_status::ZS;
    if (status & vu_status::S) status |= vu_status::SS;
    if (status & vu_status::U) status |= vu_status::US;
    if (status & vu_status::O) status |= vu_status::OS;
    regs.status_flag = status;
}

// ---- row 27 seam bodies (the generated code's g_vu0 / g_vu1 objects) ------
void Vu0Macro::Execute(CpuContext& ctx, uint32_t start_addr) { VuMacroBridge::ExecuteVu0(ctx, start_addr); }
void Vu0Macro::ResumeIfWaiting(CpuContext& ctx) { VuMacroBridge::ResumeVu0IfWaiting(ctx); }
void Vu1Window::Execute(CpuContext& ctx, uint32_t start_addr) { VuMacroBridge::ExecuteVu1(ctx, start_addr); }

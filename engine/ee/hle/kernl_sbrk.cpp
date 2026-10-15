#include "engine/ee/hle/kernl_sbrk.h"

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/boot_env.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/recomp_abi/host_app/memory.h"

namespace {
uint64_t g_calls = 0;
}

void KernlSbrk::Register(SdkBindingManifest& m) { m.Rebind("libkernl.a", "sbrk", &KernlSbrk::Sbrk); }

uint64_t KernlSbrk::Calls() { return g_calls; }

void KernlSbrk::Sbrk(CpuContext& ctx) {
    ++g_calls;
    const uint32_t brk_word = BootEnv::Instance().Params().sbrk_break_word;
    // No break word configured: the title never seeded one, nothing to grow.
    if (brk_word == 0) GuestPark::Forever("sbrk_no_break_word", ctx.cpuRegs.GPR.r[31].UL[0]);
    const uint32_t incr = (ctx.cpuRegs.GPR.r[4].UL[0] + 15u) & ~15u;
    const uint32_t old_break = memory::read<uint32_t>(brk_word);
    const uint32_t new_break = old_break + incr;
    if (new_break > kRamEnd || new_break < old_break) {
        // sbrk returns (void*)-1 on exhaustion: a 32-bit -1 sign-extended into
        // the 64-bit register, as every MIPS word result is.
        ctx.cpuRegs.GPR.r[2].SD[0] = static_cast<int32_t>(-1);
        return;
    }
    memory::write<uint32_t>(brk_word, new_break);
    ctx.cpuRegs.GPR.r[2].UD[0] = old_break;
}

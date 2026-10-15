// libdev.a rows: VIF/VU reset helpers. The real bodies write the FBRST control
// bits and poll for completion; the reset is instantaneous here, result 0.
#include "engine/ee/hle/sdk_binding_manifest.h"

namespace {
void Ret0(CpuContext& ctx) { ctx.cpuRegs.GPR.r[2].SD[0] = 0; }
}  // namespace

void RegisterLibdevBindings(SdkBindingManifest& m) {
    m.Register("libdev.a", "sceDevVif0Reset", &Ret0);
    m.Register("libdev.a", "sceDevVif1Reset", &Ret0);
    m.Register("libdev.a", "sceDevVu0Reset", &Ret0);
    m.Register("libdev.a", "sceDevVu1Reset", &Ret0);
}

// libscf.a rows (system configuration): RTC/BCD helpers and the OSD language
// answers. Retail unit, not a TOOL: IsT10K = 0. The RTC row reads
// DerivedClocks::Rtc (the fixed 2005-06-15 12:00:00 clock under the
// deterministic profile), never a host clock.
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/timing/derived_clocks.h"

namespace {
const DerivedClocks* g_clocks = nullptr;

void Ret(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }
void Ret0(CpuContext& ctx) { Ret(ctx, 0); }
void ToBcd(CpuContext& ctx) {
    const uint32_t v = ctx.cpuRegs.GPR.r[4].UL[0] % 100u;
    Ret(ctx, static_cast<int32_t>(((v / 10u) << 4) | (v % 10u)));
}
void FromBcd(CpuContext& ctx) {
    const uint32_t b = ctx.cpuRegs.GPR.r[4].UL[0] & 0xffu;
    Ret(ctx, static_cast<int32_t>((b >> 4) * 10u + (b & 0xfu)));
}

// sceScfGetLocalTimefromRTC(out): the local time as six binary bytes
// {second, minute, hour, day, month, year - 2000} at out+0..5, returns 0 --
// the layout the title's clock routine consumes (the original runtime's row;
// the SDK's in-place BCD sceCdCLOCK conversion is NOT what this title expects).
// Time zone 0 / no summer time (the sceScfGetTimeZone/GetSummerTime rows), so
// local time = the RTC. A null pointer writes nothing; every other pointer is
// written through the address map (main RAM, scratchpad or MMIO route as the
// map decides -- never a silent skip on a range the row second-guesses).
void LocalTimeFromRtc(CpuContext& ctx) {
    const uint32_t out = ctx.cpuRegs.GPR.r[4].UL[0];
    if (out != 0) {
        const DerivedClocks::RtcTime t =
            g_clocks ? g_clocks->Rtc() : DerivedClocks::EpochToCivil(DerivedClocks::kRtcFixedEpochS);
        const uint8_t bytes[6] = {static_cast<uint8_t>(t.second), static_cast<uint8_t>(t.minute),
                                  static_cast<uint8_t>(t.hour),   static_cast<uint8_t>(t.day),
                                  static_cast<uint8_t>(t.month),  static_cast<uint8_t>((t.year - 2000) & 0xff)};
        for (uint32_t i = 0; i < 6; i++) memory::write<uint8_t>(out + i, bytes[i]);
    }
    Ret(ctx, 0);
}
}  // namespace

void SetLibscfClocks(const DerivedClocks* clocks) { g_clocks = clocks; }

void RegisterLibscfBindings(SdkBindingManifest& m) {
    m.Register("libscf.a", "tobcd", &ToBcd);
    m.Register("libscf.a", "frombcd", &FromBcd);
    m.Register("libscf.a", "IsT10K", &Ret0);
    m.Register("libscf.a", "GetRomName", &Ret0);
    m.Register("libscf.a", "AdjustTime", &Ret0);
    m.Register("libscf.a", "sceScfGetSummerTime", &Ret0);
    m.Register("libscf.a", "sceScfGetTimeZone", &Ret0);
    m.Register("libscf.a", "sceScfGetLocalTimefromRTC", &LocalTimeFromRtc);
}

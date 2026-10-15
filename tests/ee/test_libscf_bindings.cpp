// The libscf RTC row (sceScfGetLocalTimefromRTC) over guest memory:
//   * resolves under "libscf.a" through the SdkBindingManifest;
//   * writes the fixed RTC (deterministic profile, 2005-06-15 12:00:00) as six
//     BINARY bytes {sec, min, hour, day, month, year-2000} = 00 00 0c 0f 06 05,
//     leaves the guard bytes after them intact, returns 0;
//   * a null pointer writes nothing (guard pattern intact), returns 0;
//   * a scratchpad pointer is written through the address map (no silent skip
//     on a range outside main RAM).
#include <cstdio>
#include <cstring>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/ee/timing/derived_clocks.h"
#include "engine/ee/timing/emu_domain.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

static uint32_t Call(void (*fn)(CpuContext&), uint32_t a0) {
    static CpuContext ctx;
    ctx.cpuRegs.GPR.r[4].UL[0] = a0;
    ctx.cpuRegs.GPR.r[2].UD[0] = 0xDEADBEEFull;
    fn(ctx);
    return ctx.cpuRegs.GPR.r[2].UL[0];
}

static bool BytesAt(uint32_t addr, const uint8_t* want, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (memory::read<uint8_t>(addr + static_cast<uint32_t>(i)) != want[i]) return false;
    return true;
}

int main() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);

    EmuDomain domain;
    DerivedClocks clocks(domain);
    clocks.SetRtcFixed(true);
    SetLibscfClocks(&clocks);

    SdkBindingManifest& m = SdkBindingManifest::Instance();
    m.ResetForTest();
    RegisterLibscfBindings(m);
    const SdkBindingManifest::EngineRow* row = m.Find("libscf.a", "sceScfGetLocalTimefromRTC");
    CHECK(row && row->handler, "row_resolves_under_libscf");
    if (!row || !row->handler) return 1;

    const uint8_t expect[6] = {0x00, 0x00, 0x0c, 0x0f, 0x06, 0x05};
    const uint8_t guard[4] = {0xA5, 0x5A, 0xC3, 0x3C};

    const uint32_t out = 0x00300000;
    for (uint32_t i = 0; i < 6; i++) memory::write<uint8_t>(out + i, 0xEE);
    for (uint32_t i = 0; i < 4; i++) memory::write<uint8_t>(out + 6 + i, guard[i]);
    CHECK(Call(row->handler, out) == 0, "returns_0");
    CHECK(BytesAt(out, expect, 6), "six_binary_bytes_00_00_0c_0f_06_05");
    CHECK(BytesAt(out + 6, guard, 4), "guard_after_the_six_bytes_intact");

    // Null pointer: nothing written anywhere near address 0.
    for (uint32_t i = 0; i < 8; i++) memory::write<uint8_t>(i, guard[i & 3]);
    CHECK(Call(row->handler, 0) == 0, "null_returns_0");
    bool zero_page_intact = true;
    for (uint32_t i = 0; i < 8; i++) zero_page_intact &= memory::read<uint8_t>(i) == guard[i & 3];
    CHECK(zero_page_intact, "null_writes_nothing");

    // Scratchpad pointer: routed through the address map, written -- not skipped.
    const uint32_t spad_out = 0x70000100;
    for (uint32_t i = 0; i < 6; i++) memory::write<uint8_t>(spad_out + i, 0xEE);
    CHECK(Call(row->handler, spad_out) == 0, "scratchpad_returns_0");
    CHECK(BytesAt(spad_out, expect, 6), "scratchpad_pointer_written_through_the_map");

    // Without an installed clock the row still answers the fixed epoch.
    SetLibscfClocks(nullptr);
    for (uint32_t i = 0; i < 6; i++) memory::write<uint8_t>(out + i, 0xEE);
    Call(row->handler, out);
    CHECK(BytesAt(out, expect, 6), "no_clock_installed_is_the_fixed_epoch");

    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}

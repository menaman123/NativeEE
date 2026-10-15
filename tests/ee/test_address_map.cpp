// AddressMap: translation order (scratchpad -> HW window -> RAM+mirrors ->
// extra regions -> BIOS mirror -> unmapped), straddle = unmapped, generic
// window/region registration, and the MmioSink idle table.
#include <cstdio>
#include <vector>

#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

int main() {
    MainMemory ram;
    Scratchpad spad;
    AddressMap map(ram, spad);

    // RAM and its mirrors.
    CHECK(map.Translate(0x00000000u, 4) == ram.Data());
    CHECK(map.Translate(0x00100000u, 4) == ram.Data() + 0x100000u);
    CHECK(map.Translate(0x80100000u, 4) == ram.Data() + 0x100000u);  // KSEG0
    CHECK(map.Translate(0xA0100000u, 4) == ram.Data() + 0x100000u);  // KSEG1
    CHECK(map.Translate(0x20100000u, 4) == ram.Data() + 0x100000u);  // phys wrap
    CHECK(map.Translate(0x01FFFFFCu, 4) == ram.Data() + 0x01FFFFFCu);
    CHECK(map.Translate(0x01FFFFFEu, 4) == nullptr);  // straddles the 32 MB end
    CHECK(map.Translate(0x02000000u, 4) == nullptr);  // nothing registered above RAM

    // Scratchpad, including the (addr & 0x70000000) == 0x70000000 pattern.
    CHECK(map.Translate(0x70000000u, 4) == spad.Data());
    CHECK(map.Translate(0x70003FFCu, 4) == spad.Data() + 0x3FFC);
    CHECK(map.Translate(0x70003FFEu, 4) == nullptr);  // straddle
    CHECK(map.Translate(0xF0000010u, 4) == spad.Data() + 0x10);
    CHECK(map.Translate(0x70004000u, 4) == spad.Data());  // offset wraps mod 16 KB (as OLD)

    // Hardware window: IO image and BIOS are built in; anything else unmapped
    // until a subsystem registers a window.
    CHECK(map.Translate(0x10000000u, 4) == map.IoImage());
    CHECK(map.Translate(0x1000FFFCu, 4) == map.IoImage() + 0xFFFC);
    CHECK(map.Translate(0x10010000u, 4) == nullptr);
    CHECK(map.Translate(0x11000000u, 4) == nullptr);
    CHECK(map.Translate(0x12000000u, 4) == nullptr);  // GS priv: MMIO, not pointer memory
    CHECK(map.Translate(0x1C000000u, 4) == nullptr);
    CHECK(map.Translate(0x1FC00000u, 4) == map.Bios());
    CHECK(map.Translate(0x1FFFFFFCu, 4) == map.Bios() + 0x3FFFFC);
    CHECK(map.Translate(0x9FC00000u, 4) == map.Bios());  // BIOS mirror via phys
    CHECK(map.Translate(0xBFC00000u, 4) == map.Bios());

    std::vector<uint8_t> vu1_code(16384, 0), vu1_data(16384, 0);
    map.RegisterHwWindow(0x11008000u, 16384, vu1_code.data(), "vu1_code");
    map.RegisterHwWindow(0x1100C000u, 16384, vu1_data.data(), "vu1_data");
    CHECK(map.Translate(0x11008000u, 16) == vu1_code.data());
    CHECK(map.Translate(0x1100BFF0u, 16) == vu1_code.data() + 0x3FF0);
    CHECK(map.Translate(0x1100BFF8u, 16) == nullptr);  // straddles the window end
    CHECK(map.Translate(0x1100C000u, 16) == vu1_data.data());
    CHECK(map.Translate(0x11000000u, 4) == nullptr);  // VU0 windows still unregistered

    // Extra region (title arena) by physical address, reachable through KSEG0 too.
    std::vector<uint8_t> arena(2u * 1024u * 1024u, 0);
    map.RegisterExtraRegion(0x02000000u, static_cast<uint32_t>(arena.size()), arena.data(), "arena");
    CHECK(map.Translate(0x02000000u, 4) == arena.data());
    CHECK(map.Translate(0x82000010u, 4) == arena.data() + 0x10);
    CHECK(map.Translate(0x021FFFFCu, 4) == arena.data() + 0x1FFFFC);
    CHECK(map.Translate(0x021FFFFEu, 4) == nullptr);
    CHECK(map.Translate(0x02200000u, 4) == nullptr);
    CHECK(map.ExtraRegions().size() == 1 && map.HwWindows().size() == 4);

    // MMIO sink idle table (values lifted from the original read path).
    MmioSink sink;
    CHECK(sink.IdleValue(0x12001000u) == 0x8u);   // GS_CSR: VSINT set
    CHECK(sink.IdleValue(0x12000000u) == 0x0u);   // PMODE
    CHECK(sink.IdleValue(0x10003020u) == 0x0u);   // GIF_STAT idle
    CHECK(sink.IdleValue(0x10003C00u) == 0x0u);   // VIF1_STAT idle
    CHECK(sink.IdleValue(0x10008000u) == 0x0u);   // D0_CHCR: STR=0
    CHECK(sink.IdleValue(0x1000E010u) == 0x0u);   // D_STAT
    CHECK(sink.IdleValue(0x1000F520u) == 0x0u);   // D_ENABLER latch at power-on
    CHECK(sink.IdleValue(0x1000F590u) == 0x0u);   // D_ENABLEW
    CHECK(sink.IdleValue(0x1000F230u) == 0x0u);   // SIF SMFLAG
    CHECK(sink.IdleValue(0x1000F000u) == 0x0u);   // INTC_STAT
    CHECK(sink.IdleValue(0x10000000u) == 0x0u);   // T0_COUNT
    CHECK(sink.IdleValue(0x10002010u) == 0x0u);   // IPU_CTRL
    CHECK(sink.Read(0x12001000u, 4) == 0x8u);
    CHECK(sink.Read(0x12001000u, 8) == 0x8ull);
    CHECK(sink.Read(0x12001002u, 2) == 0x0u);
    sink.Write(0x12001000u, 4, 0xFFFFFFFFu);
    CHECK(sink.Read(0x12001000u, 4) == 0x8u);  // writes never change idle reads
    CHECK(sink.ReadCount() == 4 && sink.WriteCount() == 1);

    // Router: last registration wins; the fallback counts distinct pages.
    MmioRouter router(sink);
    CHECK(MmioRouter::IsIoRegister(0x1000FFFFu) && !MmioRouter::IsIoRegister(0x10010000u));
    CHECK(MmioRouter::IsIoRegister(0x12001FFFu) && !MmioRouter::IsIoRegister(0x12002000u));
    CHECK(router.Read(0x1000F000u, 4) == 0);
    CHECK(router.Read(0x1000F010u, 4) == 0);
    CHECK(router.UnhandledPageCount() == 1);  // both in page 0x1000F000
    MmioSink displacer;
    router.Register(0x1000F000u, 0x20u, displacer, "intc");
    router.Read(0x1000F000u, 4);
    CHECK(displacer.ReadCount() == 1);
    CHECK(router.UnhandledPageCount() == 1);
    router.Read(0x10008000u, 4);
    CHECK(router.UnhandledPageCount() == 2);

    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS AddressMap + MmioSinkIdleValues (%zu idle rows)\n", MmioSink::IdleTable().size());
    return 0;
}

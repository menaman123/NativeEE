// SifState: the SIF register block's semantics on the router -- the five words
// at 16-byte stride, plain stores, sub-word lanes, other offsets idle 0, the
// IOP-side flag face, and the displacement of the Phase-3 sink (the SIF page
// no longer reaches the fallback).
#include <cstdio>

#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/sif/sif_state.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);      \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

int main() {
    SifState sif;
    // Plain stores and reads for the five words (OLD memory.cpp:962-971/:1108-1114).
    sif.Write(0x1000F200u, 4, 0x11111111u);
    sif.Write(0x1000F210u, 4, 0x22222222u);
    sif.Write(0x1000F220u, 4, 0x00030000u);
    sif.Write(0x1000F230u, 4, 0x00020000u);
    sif.Write(0x1000F240u, 4, 0x55u);
    CHECK(sif.Read(0x1000F200u, 4) == 0x11111111u && sif.Mscom() == 0x11111111u);
    CHECK(sif.Read(0x1000F210u, 4) == 0x22222222u && sif.Smcom() == 0x22222222u);
    CHECK(sif.Read(0x1000F220u, 4) == 0x00030000u && sif.Msflag() == 0x00030000u);
    CHECK(sif.Read(0x1000F230u, 4) == 0x00020000u && sif.Smflag() == 0x00020000u);
    CHECK(sif.Read(0x1000F240u, 4) == 0x55u && sif.Ctrl() == 0x55u);
    // A flag write replaces the word (no set/clear semantics, as the original).
    sif.Write(0x1000F230u, 4, 0u);
    CHECK(sif.Smflag() == 0u);
    // Words at +4/+8/+C of each stride and offsets past +0x40 read 0, drop writes.
    sif.Write(0x1000F204u, 4, 0xDEADBEEFu);
    sif.Write(0x1000F260u, 4, 0xDEADBEEFu);
    CHECK(sif.Read(0x1000F204u, 4) == 0u && sif.Read(0x1000F260u, 4) == 0u);
    CHECK(sif.Mscom() == 0x11111111u);
    // Sub-word lanes and the 64-bit pairing (+4 word is 0).
    CHECK(sif.Read(0x1000F201u, 1) == 0x11u);
    CHECK(sif.Read(0x1000F212u, 2) == 0x2222u);
    CHECK(sif.Read(0x1000F200u, 8) == 0x11111111ull);
    sif.Write(0x1000F202u, 2, 0xABCDu);
    CHECK(sif.Mscom() == 0xABCD1111u);
    // The IOP-side face.
    sif.SetSmflagBits(SifState::kCmdInit);
    sif.SetSmflagBits(SifState::kBootEnd);
    CHECK(sif.Smflag() == 0x60000u);
    sif.ClearMsflagBits(SifState::kSifInit);
    CHECK(sif.Msflag() == 0x20000u);
    CHECK(sif.ReadCount() == 10 && sif.WriteCount() == 9);
    sif.Reset();
    CHECK(sif.Mscom() == 0 && sif.Smflag() == 0 && sif.Ctrl() == 0);

    // On the router the SIF page displaces the sink: no unhandled page appears.
    MmioSink sink;
    MmioRouter router(sink);
    SifState routed;
    routed.Register(router);
    router.Write(0x1000F230u, 4, 0x20000u);
    CHECK(router.Read(0x1000F230u, 4) == 0x20000u);
    CHECK(router.Read(0x1000F2F0u, 4) == 0u);
    CHECK(router.UnhandledPageCount() == 0);
    CHECK(sink.ReadCount() == 0 && sink.WriteCount() == 0);
    bool found = false;
    for (const auto& r : router.Ranges())
        if (r.base == SifState::kBase && r.size == SifState::kSize) found = true;
    CHECK(found);

    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS SifState (5 words, sink displaced, unhandled pages 0)\n");
    return 0;
}

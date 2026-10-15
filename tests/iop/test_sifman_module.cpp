// tests/iop/test_sifman_module.cpp -- SifmanDmaStatComplete: the sifman HLE's
// sceSifDmaStat answers the SDK's "complete" (-1) for a transfer sceSifSetDma
// already performed (the copy is synchronous), and the recompiled STREAM.IRX
// CD worker's post-DMA poll -- `CpuSuspendIntr; v0 = sceSifDmaStat(id); bgez v0
// -> loop` (iop_recompiled.cpp labels 0xd54c-0xd560 and 0xd4b4-0xd4dc) -- exits
// on it. A status of 0 ("queued") kept that loop spinning forever, the RPC
// thread sleeping mid-body, and the module's reply never left (BUG-043).
#include <cstdio>
#include <cstring>

#include "engine/iop/hle/sifman/sifman_module.h"
#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"
#include "vector_ee_ram_sink.h"

using namespace ps2r::iop;
using ps2r::iop::hle::SifmanModule;

static int g_fail = 0;
#define CHECK(c, msg)                                                          \
    do {                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; } \
        else std::printf("PASS %s\n", msg);                                    \
    } while (0)

// The worker's loop shape, as the recompiled module runs it: poll the status of
// the id SetDma returned and go round again while it is non-negative. Returns the
// number of laps taken before the exit (0 = the cap was hit).
static uint32_t WorkerPollLoop(SifmanModule& sifman, IopContext& ctx, uint32_t id, uint32_t cap) {
    for (uint32_t lap = 1; lap <= cap; ++lap) {
        ctx.gpr[4] = id;
        sifman.sceSifDmaStat(ctx);
        const bool again = static_cast<int32_t>(ctx.gpr[2]) >= 0;   // bgez v0 -> loop
        if (!again) return lap;
    }
    return 0;
}

int main() {
    IopMemory mem;
    VectorEeRamSink sink;
    SifmanModule sifman(mem, &sink);
    IopContext ctx{};

    // One descriptor: 8 bytes at IOP 0x1000 -> EE 16.
    for (uint32_t i = 0; i < 8; ++i) mem.Write<uint8_t>(0x1000 + i, static_cast<uint8_t>('A' + i));
    mem.Write<uint32_t>(0x2000, 0x1000);
    mem.Write<uint32_t>(0x2004, 16);
    mem.Write<uint32_t>(0x2008, 8);
    ctx.gpr[4] = 0x2000;
    ctx.gpr[5] = 1;
    sifman.sceSifSetDma(ctx);
    const uint32_t id = ctx.gpr[2];
    CHECK(id == 1 && std::memcmp(sink.bytes + 16, "ABCDEFGH", 8) == 0, "sceSifSetDma copies synchronously and returns id 1");
    CHECK(sifman.BytesCopied() == 8, "the copy is accounted before SetDma returns");

    // The SDK contract for a completed transfer: a negative status.
    ctx.gpr[4] = id;
    ctx.gpr[2] = 0xAAAAAAAAu;
    sifman.sceSifDmaStat(ctx);
    CHECK(static_cast<int32_t>(ctx.gpr[2]) < 0, "sceSifDmaStat(id) after SetDma is negative (SDK: -1 = complete)");
    CHECK(static_cast<int32_t>(ctx.gpr[2]) == SifmanModule::kDmaStatComplete, "the value is exactly -1");

    // The recompiled worker's poll (bgez v0 -> loop) leaves on the first lap.
    const uint32_t laps = WorkerPollLoop(sifman, ctx, id, 1000);
    CHECK(laps == 1, "the module's post-DMA poll loop exits on its first sceSifDmaStat");
    // An id the HLE never issued is reported complete too (nothing is queued in a synchronous sifman).
    CHECK(WorkerPollLoop(sifman, ctx, 0x7777, 1000) == 1, "an unknown id is complete as well (no queue exists)");
    CHECK(sifman.DmaStatPolls() == 3, "the polls are counted (census)");

    std::printf("%s: %d failure(s)\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}

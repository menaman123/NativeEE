// SifRpcRouter: bind table (32 slots, re-bind in place), call routing to the
// registered server face, the unbound-server report, and the end-callback
// queue delivered in order at the slot-4 duty with a0/ra prepared and the
// borrowed registers restored.
#include <cstdio>
#include <map>
#include <vector>

#include "engine/ee/timing/boundary_sequencer.h"
#include "engine/sif/hle/rpc_router.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);      \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {
struct TestRpcRouter : RpcRouter::Server {  // the recording server face (primary type of this file)
    std::vector<RpcRouter::CallRequest> calls;
    void Call(const RpcRouter::CallRequest& req) override { calls.push_back(req); }
};
}  // namespace

int main() {
    RpcRouter router;
    TestRpcRouter loadfile, stream;
    router.RegisterServer(0x80000006u, loadfile, "LOADFILE");
    router.RegisterServer(0x12345u, stream, "STREAM");
    CHECK(router.FindServer(0x80000006u) == &loadfile && router.FindServer(0x1u) == nullptr);
    CHECK(std::string(router.ServerName(0x12345u)) == "STREAM" && std::string(router.ServerName(9)) == "unbound");
    // Re-registration replaces the face.
    TestRpcRouter stream2;
    router.RegisterServer(0x12345u, stream2, "STREAM2");
    CHECK(router.FindServer(0x12345u) == &stream2);

    // Bind: slots in order, re-bind updates in place, full table -> -1.
    CHECK(router.Bind(0x3B4000u, 0x80000006u) == 0);
    CHECK(router.Bind(0x3B4100u, 0x12345u) == 1);
    CHECK(router.Bind(0x3B4000u, 0x80000003u) == 0);  // re-bind same client
    CHECK(router.FindByClient(0x3B4000u)->server_id == 0x80000003u);
    CHECK(router.FindByServer(0x12345u)->client_addr == 0x3B4100u);
    CHECK(router.BoundCount() == 2);
    for (uint32_t i = 0; i < 30; ++i) CHECK(router.Bind(0x100000u + i * 0x40u, 0x1000u + i) == static_cast<int>(2 + i));
    CHECK(router.Bind(0x900000u, 1) == -1);
    CHECK(router.BoundCount() == 32);
    CHECK(router.BoundCensus().rfind("unbound@0x80000003,STREAM2@0x12345,", 0) == 0);

    // Call: routed to the face with the request intact; unbound -> false, counted.
    RpcRouter::CallRequest req;
    req.server_id = 0x12345u;
    req.func = 2;
    req.send = 0x3B3000u;
    req.send_size = 0x40;
    req.recv = 0x3B3100u;
    req.recv_size = 0x40;
    req.client_addr = 0x3B4100u;
    CHECK(router.Call(req));
    CHECK(stream2.calls.size() == 1 && stream2.calls[0].func == 2 && stream2.calls[0].recv == 0x3B3100u);
    CHECK(stream.calls.empty());
    req.server_id = 0x80000592u;  // CDVD_INIT: no face registered
    CHECK(!router.Call(req));
    CHECK(router.Calls() == 2 && router.UnboundCalls() == 1);

    // End callbacks: queued client records run in order through the guest
    // call with a0 = end_param, ra = sentinel; a record with end_func 0 is
    // skipped; the borrowed registers come back.
    std::map<uint32_t, uint32_t> ram = {
        {0x3B4100u + RpcRouter::kClientEndFunc, 0x1E6968u}, {0x3B4100u + RpcRouter::kClientEndParam, 0x11u},
        {0x3B4000u + RpcRouter::kClientEndFunc, 0u},        {0x3B4000u + RpcRouter::kClientEndParam, 0x22u},
        {0x3B4200u + RpcRouter::kClientEndFunc, 0x1E6968u}, {0x3B4200u + RpcRouter::kClientEndParam, 0x33u},
    };
    router.QueueEndCallback(0x3B4100u);
    router.QueueEndCallback(0x3B4000u);
    router.QueueEndCallback(0x3B4200u);
    CHECK(router.PendingCount() == 3);
    CpuContext ctx{};
    ctx.cpuRegs.GPR.r[4].UD[0] = 0xAAAA;
    ctx.cpuRegs.GPR.r[2].UD[0] = 0xBBBB;
    ctx.cpuRegs.GPR.r[31].UD[0] = 0xCCCC;
    ctx.cpuRegs.pc = 0x100008;
    std::vector<std::pair<uint32_t, uint32_t>> ran;  // (addr, a0) with ra seen
    std::vector<uint32_t> ra_seen;
    const int n = router.CompletePending(
        ctx,
        [&](uint32_t addr, CpuContext& c) {
            ran.emplace_back(addr, static_cast<uint32_t>(c.cpuRegs.GPR.r[4].UD[0]));
            ra_seen.push_back(static_cast<uint32_t>(c.cpuRegs.GPR.r[31].UD[0]));
            c.cpuRegs.GPR.r[2].UD[0] = 0;  // the callback clobbers v0
        },
        [&](uint32_t a) { return ram.count(a) ? ram[a] : 0u; });
    CHECK(n == 2 && router.PendingCount() == 0 && router.Completed() == 2);
    CHECK(ran.size() == 2 && ran[0].first == 0x1E6968u && ran[0].second == 0x11u && ran[1].second == 0x33u);
    CHECK(ra_seen.size() == 2 && ra_seen[0] == RpcRouter::kReturnSentinel);
    CHECK(ctx.cpuRegs.GPR.r[4].UD[0] == 0xAAAA && ctx.cpuRegs.GPR.r[2].UD[0] == 0xBBBB);
    CHECK(ctx.cpuRegs.GPR.r[31].UD[0] == 0xCCCC && ctx.cpuRegs.pc == 0x100008);

    // Slot-4 delivery at the boundary itself: a callback queued before the
    // boundary is delivered by Step::MaintDuties BEFORE Step::IopDispatch runs
    // (the sequencer's compile-time order), and a boundary with an empty queue
    // delivers nothing.
    {
        BoundarySequencer seq;
        std::vector<std::string> order;
        seq.SetStep(BoundarySequencer::Step::MaintDuties, [&](CpuContext& c) {
            const int delivered = router.CompletePending(
                c, [&](uint32_t addr, CpuContext&) { order.push_back("cb@" + std::to_string(addr)); },
                [&](uint32_t a) { return ram.count(a) ? ram[a] : 0u; });
            order.push_back("maint:" + std::to_string(delivered));
            return true;
        });
        seq.SetStep(BoundarySequencer::Step::IopDispatch, [&](CpuContext&) {
            order.push_back("iop");
            return true;
        });
        router.QueueEndCallback(0x3B4200u);
        CHECK(seq.RunBoundary(ctx));
        CHECK(order.size() == 3 && order[0] == "cb@1993064" && order[1] == "maint:1" && order[2] == "iop");
        order.clear();
        CHECK(seq.RunBoundary(ctx));
        CHECK(order.size() == 2 && order[0] == "maint:0" && order[1] == "iop");
        CHECK(router.Completed() == 3 && seq.Completed() == 2);
    }

    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS SifRpcRouter (32 slots, 2 servers, 2 callbacks delivered in order, slot 4 before IopDispatch)\n");
    return 0;
}

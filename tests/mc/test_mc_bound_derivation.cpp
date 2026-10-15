// McCommandModel::Bound at a savestate restore: libmc 3.0.1's readiness is
// the bound-server word of its own sceSifClientData (+0x24), which the RAM
// image carries; the model derives Bound from that word (DeriveBound) instead
// of keeping a flag of its own that no restore could raise. Pinned:
//   * a fresh model is unbound; DeriveBound(0) keeps it unbound (-100 rows);
//   * DeriveBound(non-zero handle) binds it, BoundDerived() reports the source;
//   * MCHL carries no Bound: restoring a 12-byte MCHL blob into a model leaves
//     Bound exactly as derived (the two sources are independent);
//   * the mcserv init command still binds a cold boot and records the client
//     record it arrived on (the title datum's cold-boot witness).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/mc/hle/mc_command_model.h"
#include "engine/mc/hle/mcserv_module.h"
#include "engine/mc/savestate/mchl_section.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

int main() {
    constexpr uint32_t kMcservHandle = 0xDEAD0400u;   // SceSif::ServerHandle(0x80000400): the anchors' word

    McCommandModel fresh(0);
    CHECK(!fresh.Bound());
    CHECK(!fresh.BoundDerived());
    fresh.DeriveBound(0);
    CHECK(!fresh.Bound());
    CHECK(fresh.BoundDerived());

    McCommandModel restored(0);
    restored.DeriveBound(kMcservHandle);
    CHECK(restored.Bound());
    CHECK(restored.BoundDerived());
    // Bound does not gate sceMcSync; GetInfo's result contract is unchanged.
    const McCommandModel::GetInfoResult gi = restored.GetInfo(0, 0);
    CHECK(gi.result == McCommandModel::kResChangedFormatted);

    // MCHL (12 bytes: pending pair + seen flags) carries no Bound.
    McCommandModel src(0);
    MchlSection src_section(src);
    std::vector<uint8_t> payload;
    src_section.Serialize(payload);
    CHECK(payload.size() == MchlSection::kPayloadBytes);
    McCommandModel dst(0);
    dst.DeriveBound(kMcservHandle);
    MchlSection dst_section(dst);
    std::string err;
    CHECK(dst_section.Restore(payload, &err));
    CHECK(dst.Bound());
    McCommandModel dst2(0);
    dst2.DeriveBound(0);
    MchlSection dst2_section(dst2);
    CHECK(dst2_section.Restore(payload, &err));
    CHECK(!dst2.Bound());

    // Cold boot: the mcserv face's init command binds and records its client record.
    McCommandModel cold(0);
    std::vector<std::pair<uint32_t, uint32_t>> writes;
    McservModule face(cold, [&writes](uint32_t a, uint32_t v) { writes.emplace_back(a, v); });
    RpcRouter::CallRequest req;
    req.server_id = McservModule::kServerId;
    req.func = McservModule::kFnoInit;
    req.recv = 0x1000;
    req.recv_size = McservModule::kReplyBytes;
    req.client_addr = 0x2000;
    face.Call(req);
    CHECK(cold.Bound());
    CHECK(!cold.BoundDerived());
    CHECK(face.InitClientAddr() == 0x2000u);
    CHECK(face.InitCalls() == 1);

    std::printf("McBoundDerivedFromClientRecord: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}

// tests/core/test_quiescence.cpp -- the save-point refusal predicate: named
// predicates asked in registration order, the first refusal named with its
// reason and counted per predicate; a save proceeds only when none refuses.
#include <cstdio>
#include <string>

#include "engine/core/savestate/quiescence.h"

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

int main() {
    using ps2r::savestate::Quiescence;
    Quiescence q;
    CHECK(q.Count() == 0 && q.Check() && q.Checks() == 1 && q.Refusals() == 0);  // nothing registered: a save proceeds

    bool fmv = false, dma = false;
    q.Register("fmv_active", [&fmv](std::string* why) {
        if (!fmv) return true;
        if (why) *why = "FMV actively decoding";
        return false;
    });
    q.Register("dma_mid_transfer", [&dma](std::string* why) {
        if (!dma) return true;
        if (why) *why = "VIF1 mid-transfer";
        return false;
    });
    q.Register("null", nullptr);  // an empty predicate is not registered
    CHECK(q.Count() == 2);

    std::string by, why;
    CHECK(q.Check(&by, &why) && by.empty() && why.empty());

    fmv = true;
    CHECK(!q.Check(&by, &why) && by == "fmv_active" && why == "FMV actively decoding");
    CHECK(q.RefusalsBy("fmv_active") == 1 && q.RefusalsBy("dma_mid_transfer") == 0 && q.Refusals() == 1);

    dma = true;   // both refuse: the first registered names the refusal; the second is not asked
    CHECK(!q.Check(&by, &why) && by == "fmv_active");
    CHECK(q.RefusalsBy("fmv_active") == 2 && q.RefusalsBy("dma_mid_transfer") == 0);

    fmv = false;
    CHECK(!q.Check(&by, &why) && by == "dma_mid_transfer" && why == "VIF1 mid-transfer");
    CHECK(q.RefusalsBy("dma_mid_transfer") == 1 && q.Refusals() == 3);

    dma = false;
    CHECK(q.Check(nullptr, nullptr) && q.Checks() == 6 && q.Refusals() == 3);
    CHECK(q.RefusalsBy("unknown") == 0);

    std::printf("test_quiescence: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}

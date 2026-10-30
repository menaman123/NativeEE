// YieldSection (YLDP) + EeYieldPoint's staged budget restore: the back-edge budget
// remaining at a captured boundary rides the image and is seated into the live
// counter exactly once, by the relaunch, right before the boundary thread resumes.
//   - Serialize reads the live counter (g_ee_backedge_budget) and the reload value.
//   - Validate refuses a bad length / a remaining count outside 1..budget.
//   - Restore refuses a reload value other than the live EeYieldPoint::Budget() by
//     name, and otherwise STAGES (the live counter is untouched until Apply).
//   - ApplyRestoredBudget seats the staged value once (false the second time),
//     clamps a 0 to 1 (the generated code never leaves a tripped counter standing)
//     and counts budget_restores / last_restored_remaining.
//   - ReloadBudget puts the full budget back (phase 1 of the relaunch).
//   - The file version: a YLDP blob stamps kFileVersionYldp.
#include <cstdio>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/core/savestate/savestate_format.h"
#include "engine/ee/recomp_abi/host_app/syscalls.h"
#include "engine/ee/savestate/yield_section.h"
#include "engine/ee/sched/ee_yield_point.h"

// The seam TU pulls in the shadow-stack accessors the generated common TU defines (as test_ee_yield_point.cpp).
extern "C" uint32_t ee_get_call_stack_depth() { return 0; }
extern "C" bool ee_get_call_stack_top(uint32_t, uint32_t*, uint32_t*) { return false; }
extern "C" bool ee_call_stack_push(uint32_t, uint32_t) { return false; }
extern "C" void ee_call_stack_pop() {}

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name)                                                                  \
    do {                                                                                   \
        if (cond) { ++g_pass; std::printf("  PASS: %s\n", name); }                         \
        else { ++g_fail; std::printf("  FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

int main() {
    EeYieldPoint& point = EeYieldPoint::Instance();
    point.ResetForTest();
    const uint32_t budget = point.Budget();
    CHECK(budget == EeYieldPoint::kDefaultBudget, "unbound point counts the default budget");
    YieldSection section(point);

    // Serialize: the live counter as the generated code left it.
    g_ee_backedge_budget = 12345;
    std::vector<uint8_t> payload;
    section.Serialize(payload);
    uint32_t saved_budget = 0, saved_remaining = 0;
    CHECK(payload.size() == YieldSection::kPayloadBytes && YieldSection::Decode(payload, &saved_budget, &saved_remaining) &&
              saved_budget == budget && saved_remaining == 12345,
          "serialize = {reload value, remaining}");

    // Validate.
    std::string err;
    CHECK(YieldSection::Validate(payload, &err), "validate accepts the serialized payload");
    std::vector<uint8_t> short_payload(payload.begin(), payload.begin() + 4);
    CHECK(!YieldSection::Validate(short_payload, &err) && err.find("YLDP") != std::string::npos, "validate refuses a short payload");
    {
        std::vector<uint8_t> bad = payload;
        bad[4] = bad[5] = bad[6] = bad[7] = 0;   // remaining = 0
        CHECK(!YieldSection::Validate(bad, &err), "validate refuses remaining = 0");
        bad = payload;
        bad[4] = 1; bad[5] = 0; bad[6] = 0; bad[7] = 1;   // remaining = 0x01000001 > 2^18
        CHECK(!YieldSection::Validate(bad, &err), "validate refuses remaining > budget");
    }

    // Restore stages; the live counter is untouched until the relaunch applies it.
    g_ee_backedge_budget = 777;
    CHECK(section.Restore(payload, &err), "restore accepts a matching reload value");
    CHECK(g_ee_backedge_budget == 777, "restore does not touch the live counter (staged)");
    point.ReloadBudget();
    CHECK(g_ee_backedge_budget == budget, "ReloadBudget seats the full budget (relaunch phase 1)");
    CHECK(point.ApplyRestoredBudget() && g_ee_backedge_budget == 12345, "ApplyRestoredBudget seats the staged remaining count");
    CHECK(!point.ApplyRestoredBudget() && g_ee_backedge_budget == 12345, "a second apply is a no-op");
    CHECK(point.Stats().budget_restores == 1 && point.Stats().last_restored_remaining == 12345, "census counts the restore");

    // A reload value other than the live budget is refused by name.
    {
        std::vector<uint8_t> other = payload;
        other[0] = 0; other[1] = 0; other[2] = 2; other[3] = 0;   // budget = 0x20000
        other[4] = 1; other[5] = 0; other[6] = 0; other[7] = 0;   // remaining = 1
        CHECK(!section.Restore(other, &err) && err.find("differs from the live") != std::string::npos,
              "restore refuses a different reload value by name");
        CHECK(!point.ApplyRestoredBudget(), "a refused restore stages nothing");
    }

    // Clamp: a staged 0 seats 1 (never a standing tripped counter).
    point.StageRestoredBudget(0);
    CHECK(point.ApplyRestoredBudget() && g_ee_backedge_budget == 1, "a staged 0 seats 1");

    // Absent: counted, nothing seated.
    ps2r::savestate::SectionSpec spec = section.Spec();
    CHECK(spec.tag == ps2r::savestate::TAG_YLDP && !spec.required && spec.version == YieldSection::kVersion, "spec: YLDP v1, optional");
    spec.absent();
    CHECK(point.Stats().budget_absent == 1, "an image without YLDP is counted absent");

    // The file version a YLDP blob implies.
    ps2r::savestate::Blob blob;
    blob.tag = ps2r::savestate::TAG_YLDP;
    blob.version = YieldSection::kVersion;
    blob.payload = payload;
    CHECK(ps2r::savestate::SectionRegistry::FileVersionFor({blob}) == ps2r::savestate::kFileVersionYldp,
          "a file carrying YLDP is stamped kFileVersionYldp");
    CHECK(ps2r::savestate::SectionRegistry::FileVersionFor({}) == ps2r::savestate::kFileVersion,
          "a file without YLDP keeps the base version");

    point.ResetForTest();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

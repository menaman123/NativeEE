// Links only the generic sequence runner: a dispatch/title dependency must
// fail this target's link, rather than hide behind a generated-ABI fixture.
#include <cstdio>
#include <utility>

#include "engine/ee/timing/boundary_sequencer.h"

namespace {
int checks = 0, failures = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; \
    std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); } } while (false)

void DirectAndDetached() {
    BoundarySequencer sequence;
    CpuContext ctx{}, other{};
    int delivered = 0;
    CHECK(!sequence.DeferServiceReturn(ctx, 0));
    CHECK(sequence.RejectedServiceReturns() == 0);
    CHECK(sequence.AttachCompletedService(&sequence, [&](CpuContext& completed, uint64_t frame) {
        CHECK(&completed == &ctx && frame == 1);
        ++delivered;
    }));
    CHECK(sequence.RunBoundary(ctx));
    CHECK(!sequence.DeferServiceReturn(ctx, 1)); // No scheduler, no hidden dispatch.
    CHECK(sequence.RejectedServiceReturns() == 1 && delivered == 0);
    CHECK(!sequence.CompleteServiceReturn(other, 1));
    CHECK(sequence.CompleteServiceReturn(ctx, 1) && delivered == 1);
    CHECK(!sequence.CompleteServiceReturn(ctx, 1));
    sequence.DetachCompletedService(&other); // Foreign detach cannot steal owner.
    CHECK(!sequence.AttachCompletedService(&other, [](CpuContext&, uint64_t) {}));
    sequence.DetachCompletedService(&sequence);
    CHECK(sequence.RunBoundary(ctx));
    CHECK(!sequence.CompleteServiceReturn(ctx, 2));
}

void InjectedReturnAndInvalidation() {
    BoundarySequencer sequence;
    CpuContext ctx{};
    BoundarySequencer::ServiceReturn pending;
    int delivered = 0, scheduled = 0;
    auto complete = [&](CpuContext& completed, uint64_t frame) {
        CHECK(&completed == &ctx && frame == 3 && completed.cpuRegs.pc == 0x1234);
        ++delivered;
    };
    auto schedule = [&](CpuContext& owner, BoundarySequencer::ServiceReturn callback) {
        CHECK(&owner == &ctx);
        ++scheduled;
        pending = std::move(callback);
        return true;
    };
    CHECK(sequence.AttachCompletedService(&sequence, complete, schedule));
    CHECK(sequence.RunBoundary(ctx) && sequence.DeferServiceReturn(ctx, 1));
    CHECK(delivered == 0 && scheduled == 1);
    sequence.DetachCompletedService(&sequence);
    CHECK(sequence.AttachCompletedService(&sequence, complete, schedule));
    pending(ctx); // Detached owner's callback cannot enter the new consumer.
    CHECK(delivered == 0);
    CHECK(sequence.RunBoundary(ctx) && sequence.DeferServiceReturn(ctx, 2));
    sequence.ResetServiceCompletion(); // Restore invalidates the queued epoch.
    pending(ctx);
    CHECK(delivered == 0);
    CHECK(sequence.RunBoundary(ctx) && sequence.DeferServiceReturn(ctx, 3));
    CHECK(!sequence.DeferServiceReturn(ctx, 3));
    CHECK(!sequence.CompleteServiceReturn(ctx, 3)); // Scheduled owner must finish first.
    ctx.cpuRegs.pc = 0x1234; // Caller finalizes its result before returning.
    pending(ctx);
    CHECK(delivered == 1 && scheduled == 3);
    pending(ctx);
    CHECK(delivered == 1); // A completed epoch cannot be replayed.
    sequence.DetachCompletedService(&sequence);
    CHECK(sequence.AttachCompletedService(&sequence, complete,
        [](CpuContext&, BoundarySequencer::ServiceReturn) { return false; }));
    CHECK(sequence.RunBoundary(ctx) && !sequence.DeferServiceReturn(ctx, 4));
    CHECK(delivered == 1);
}
} // namespace

int main() {
    DirectAndDetached();
    InjectedReturnAndInvalidation();
    std::printf("%s BoundaryCompletion: %d checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}

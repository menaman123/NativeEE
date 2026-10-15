#include "app/host/session_clock.h"

#include <cstdio>
#include <limits>
#include <stdexcept>

using ps2r::app::SessionClock;
using ps2r::savestate::Blob;
static int failures = 0, checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (false)

static void Advance(EmuDomain& domain, TickSource& source, unsigned frame) {
    const unsigned ticks = frame % 31 == 0 ? 9 : 2;
    domain.NoteArrival(source.RawCount());
    for (unsigned i = 0; i < ticks; ++i) {
        if (frame % 19 == 0) domain.OnDeferredTick(source.Tick());
        else domain.OnRawTick(source.Tick());
    }
    domain.OnBoundary(source.RawCount());
}

static Blob Pack(SessionClock& clock) {
    Blob blob;
    blob.tag = SessionClock::kTag; blob.version = SessionClock::kVersion;
    blob.flags = ps2r::savestate::kSectionRequired;
    std::string error;
    CHECK(SessionClock::Encode(clock.Capture(), blob.payload, error));
    return blob;
}

static void Continuation(bool play, int pin) {
    EmuDomain live, restored; TickSource source, target; TickSync ticks, target_ticks;
    live.SetPacerRelease(play); restored.SetPacerRelease(play);
    live.SetRegimePin(pin); restored.SetRegimePin(pin);
    SessionClock original(live, source, ticks, "title:owned-services:v1");
    SessionClock loaded(restored, target, target_ticks, "title:owned-services:v1");
    for (unsigned frame = 1; frame < 14; ++frame) Advance(live, source, frame);
    const auto blob = Pack(original);
    SessionClock::Snapshot snapshot;
    std::string error;
    CHECK(SessionClock::Decode(blob, snapshot, error));
    // An unrelated live clock must actually rewind, not merely re-anchor.
    for (unsigned frame = 1; frame < 200; ++frame) Advance(restored, target, frame);
    CHECK(loaded.Restore(snapshot, error));
    CHECK(Pack(original).payload == Pack(loaded).payload);
    for (unsigned frame = 14; frame < 380; ++frame) {
        Advance(live, source, frame); Advance(restored, target, frame);
        CHECK(Pack(original).payload == Pack(loaded).payload);
        CHECK(live.DomainTick() * 9600 == restored.DomainTick() * 9600);
        CHECK((live.DomainTick() & 1) == (restored.DomainTick() & 1));
    }
    const auto before = Pack(loaded);
    auto corrupt = snapshot;
    corrupt.domain.lead = std::numeric_limits<int64_t>::min();
    CHECK(!loaded.Restore(corrupt, error));
    corrupt = snapshot; corrupt.domain.tick = UINT64_MAX;
    CHECK(!loaded.Restore(corrupt, error));
    corrupt = snapshot; corrupt.domain.detector.samples = 120;
    CHECK(!loaded.Restore(corrupt, error));
    corrupt = snapshot; corrupt.raw_tick = 0;
    CHECK(!loaded.Restore(corrupt, error));
    CHECK(Pack(loaded).payload == before.payload);
}

static void ImageAdmission() {
    EmuDomain domain; TickSource source; TickSync ticks;
    SessionClock clock(domain, source, ticks, "title:files1:input1:saves1");
    Advance(domain, source, 1);
    const Blob blob = Pack(clock);
    ps2r::savestate::FileMeta meta;
    meta.vsync_frame = static_cast<uint32_t>(domain.BoundaryCount());
    std::string error;
    CHECK(clock.ValidateImage(meta, {blob}, error));
    CHECK(!clock.ValidateImage(meta, {}, error));
    CHECK(clock.ValidateBootImage(meta, {}, true, error));
    CHECK(!clock.ValidateBootImage(meta, {}, false, error));
    CHECK(!clock.ValidateImage(meta, {blob, blob}, error));
    CHECK(!clock.ValidateBootImage(meta, {blob, blob}, true, error));
    ++meta.vsync_frame;
    CHECK(!clock.ValidateImage(meta, {blob}, error));
    CHECK(!clock.ValidateBootImage(meta, {blob}, true, error));
    SessionClock other(domain, source, ticks, "title:files0:input1:saves1");
    --meta.vsync_frame;
    CHECK(!other.ValidateImage(meta, {blob}, error));
    CHECK(!other.ValidateBootImage(meta, {blob}, true, error));
    SessionClock::Snapshot untouched; untouched.providers = "sentinel";
    for (size_t size = 0; size < blob.payload.size(); ++size) {
        auto bad = blob; bad.payload.resize(size);
        CHECK(!SessionClock::Decode(bad, untouched, error));
        CHECK(untouched.providers == "sentinel");
    }
    auto bad = blob; bad.flags = 0;
    CHECK(!SessionClock::Decode(bad, untouched, error));
    bad = blob; ++bad.version;
    CHECK(!SessionClock::Decode(bad, untouched, error));
    bad = blob; bad.payload.push_back(0);
    CHECK(!SessionClock::Decode(bad, untouched, error));
    ps2r::savestate::SectionRegistry registry;
    CHECK(registry.Register(clock.Section()));
    CHECK(registry.Capture().front().flags == ps2r::savestate::kSectionRequired);
    CHECK(registry.Validate({}, &error)); // boot legacy policy is separate
    CHECK(!clock.ValidateImage(meta, {}, error)); // interactive requires owned state
}

static void PendingContinuation(bool play) {
    EmuDomain live, restored; TickSource source, target; TickSync ticks, target_ticks;
    live.SetPacerRelease(play); restored.SetPacerRelease(play);
    SessionClock original(live, source, ticks, "pending-requests");
    SessionClock loaded(restored, target, target_ticks, "pending-requests");
    std::vector<uint64_t> emitted, resumed;
    ticks.SetBody([&](TickSync::Cause cause) {
        CHECK(cause == TickSync::Cause::Deferred);
        live.OnDeferredTick(source.Tick()); emitted.push_back(live.DomainTick());
        return source.RawCount();
    });
    target_ticks.SetBody([&](TickSync::Cause cause) {
        CHECK(cause == TickSync::Cause::Deferred);
        restored.OnDeferredTick(target.Tick()); resumed.push_back(restored.DomainTick());
        return target.RawCount();
    });
    for (unsigned frame = 1; frame < 14; ++frame) Advance(live, source, frame);
    ticks.NotePending(TickSync::Cause::Demand);
    ticks.NotePending(TickSync::Cause::CdvdDelivery);
    const auto saved = Pack(original);
    SessionClock::Snapshot snapshot;
    std::string error;
    CHECK(SessionClock::Decode(saved, snapshot, error) && snapshot.pending_ticks == 2);
    for (unsigned frame = 1; frame < 140; ++frame) Advance(restored, target, frame);
    for (unsigned i = 0; i < 7; ++i) target_ticks.NotePending(TickSync::Cause::Demand);
    const auto noted = target_ticks.Noted(), fired = target_ticks.Fired();
    CHECK(loaded.Restore(snapshot, error));
    CHECK(target_ticks.Pending() == 2 && target_ticks.Noted() == noted && target_ticks.Fired() == fired);
    CHECK(ticks.FirePending() == 2 && target_ticks.FirePending() == 2);
    CHECK(emitted == resumed && emitted.size() == 2);
    live.OnBoundary(source.RawCount()); restored.OnBoundary(target.RawCount());
    CHECK(Pack(original).payload == Pack(loaded).payload);
    CHECK(target_ticks.Pending() == 0 && target_ticks.Fired() == fired + 2);
    const auto before = Pack(loaded);
    auto bad = snapshot; bad.pending_ticks = uint64_t{1} + std::numeric_limits<int>::max();
    CHECK(!loaded.Restore(bad, error) && Pack(loaded).payload == before.payload);
    bad = snapshot; bad.pending_ticks = UINT64_MAX;
    CHECK(!loaded.Restore(bad, error) && Pack(loaded).payload == before.payload);
    auto older = saved; older.version = 1; older.payload[0] = 1;
    older.payload.resize(older.payload.size() - 8); // previous format omitted these requests
    CHECK(!SessionClock::Decode(older, bad, error)); // cannot silently invent an empty queue

    target_ticks.NotePending(TickSync::Cause::Demand);
    target_ticks.SetBody([&](TickSync::Cause) {
        const auto raw = target.RawCount(), tick = restored.DomainTick(), pending = target_ticks.Pending();
        CHECK(!loaded.Restore(snapshot, error));
        CHECK(target.RawCount() == raw && restored.DomainTick() == tick && target_ticks.Pending() == pending);
        bool refused = false;
        try { (void)loaded.Capture(); } catch (const std::runtime_error&) { refused = true; }
        CHECK(refused); // a partially applied tick cannot be a reproduction boundary
        return raw;
    });
    target_ticks.FireInline(TickSync::Cause::Demand);
    CHECK(!target_ticks.BodyActive() && target_ticks.Pending() == 1);
}

int main() {
    Continuation(false, -1); Continuation(true, -1); Continuation(false, 2);
    ImageAdmission();
    PendingContinuation(false); PendingContinuation(true);
    std::printf("SessionClock: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

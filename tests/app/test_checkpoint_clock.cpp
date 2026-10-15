#include "app/host/checkpoint_capture.h"
#include "app/host/session_clock.h"
#include "engine/core/savestate/quiescence.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/savestate/memory_sections.h"
#include "engine/ee/savestate/state_restorer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace ps2r::savestate;
using ps2r::app::SessionClock;
static unsigned checks = 0, failures = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (false)

int main() {
    char directory[] = "/tmp/checkpoint_clock_XXXXXX";
    CHECK(mkdtemp(directory) != nullptr);
    MainMemory ram;
    Scratchpad scratch;
    MemorySections memory(ram, scratch, nullptr, 0);
    StateRestorer restorer(memory);
    EmuDomain domain;
    TickSource source;
    TickSync ticks;
    SessionClock clock(domain, source, ticks, "completed-boundary-fixture");
    CHECK(restorer.Registry().Register(clock.Section()));
    for (unsigned i = 0; i < 14; ++i) {
        domain.NoteArrival(source.RawCount());
        domain.OnRawTick(source.Tick());
        domain.OnBoundary(source.RawCount());
    }
    ticks.NotePending(TickSync::Cause::Demand);
    ticks.NotePending(TickSync::Cause::Demand);
    Quiescence quiescence;
    unsigned drains = 0;
    quiescence.Register("owned", [&](std::string*) { ++drains; return true; });
    ram.Data()[0x100] = 79;
    ps2r::app::CheckpointCapture::Write(directory, 0, domain.BoundaryCount(), ram, restorer, quiescence);
    const std::string path = std::string(directory) + "/state_b0.ps2st";
    FileMeta meta;
    std::vector<Blob> blobs;
    std::string error;
    CHECK(ReadStateFile(path, &meta, &blobs, &error));
    CHECK(meta.vsync_frame == 14 && drains == 1);
    CHECK(clock.ValidateImage(meta, blobs, error));
    const auto before = clock.Capture();
    CHECK(before.pending_ticks == 2);
    SessionClock::Snapshot stored;
    for (const auto& blob : blobs) if (blob.tag == SessionClock::kTag)
        CHECK(SessionClock::Decode(blob, stored, error));
    CHECK(stored.pending_ticks == 2 && ticks.Pending() == 2);
    meta.vsync_frame = 0; // Old bug: the harness filename index was used as the clock.
    CHECK(!clock.ValidateImage(meta, blobs, error));
    CHECK(clock.Capture().raw_tick == before.raw_tick && ram.Data()[0x100] == 79);
    CHECK(!clock.ValidateBootImage(meta, blobs, true, error)); // legacy policy cannot hide a bad present clock
    meta.vsync_frame = 14;
    const auto image = SerializeStateFile(meta, blobs);
    ram.Data()[0x100] = 97;
    for (unsigned i = 0; i < 5; ++i) ticks.NotePending(TickSync::Cause::Demand);
    for (unsigned i = 0; i < 8; ++i) {
        domain.OnRawTick(source.Tick()); domain.OnBoundary(source.RawCount());
    }
    CHECK(restorer.RestoreImage(image.data(), image.size(), &error));
    CHECK(ram.Data()[0x100] == 79 && ticks.Pending() == 2 && ticks.Noted() == 7);
    CHECK(clock.Capture().raw_tick == before.raw_tick && domain.BoundaryCount() == 14);

    // OLD E2_SAVE_REVIEW: CRC-valid semantic corruption was discovered after
    // teardown. Prepare the actual registered image and stage once before the
    // destructive step; apply only the retained bytes and the same owners.
    unsigned stages = 0, teardowns = 0;
    bool can_stage = true;
    restorer.AddImagePreflight([&](const auto&, std::string* why) {
        ++stages;
        if (!can_stage && why) *why = "selected resource allocation failed";
        return can_stage;
    });
    restorer.SetPreRestore([&](std::string*) { ++teardowns; return true; });
    ram.Data()[0x100] = 37;
    CHECK(restorer.PrepareImage(image.data(), image.size(), &error));
    CHECK(restorer.HasPrepared() && stages == 1 && teardowns == 0 && ram.Data()[0x100] == 37);
    CHECK(!restorer.PrepareImage(image.data(), image.size(), &error) && stages == 1);
    auto changed = image;
    changed.back() ^= 1;
    CHECK(!restorer.RestorePrepared(changed.data(), changed.size(), &error));
    CHECK(teardowns == 0 && ram.Data()[0x100] == 37 && restorer.HasPrepared());
    CHECK(restorer.RestorePrepared(image.data(), image.size(), &error));
    CHECK(!restorer.HasPrepared() && teardowns == 1 && stages == 1 && ram.Data()[0x100] == 79);
    CHECK(!restorer.RestorePrepared(image.data(), image.size(), &error));
    CHECK(teardowns == 1);
    ram.Data()[0x100] = 38;
    CHECK(restorer.PrepareImage(image.data(), image.size(), &error));
    restorer.SetImageValidation([](const auto&, const auto&, std::string*) { return true; });
    CHECK(!restorer.RestorePrepared(image.data(), image.size(), &error));
    CHECK(teardowns == 1 && ram.Data()[0x100] == 38);
    restorer.DiscardPrepared();
    CHECK(!restorer.HasPrepared());
    can_stage = false;
    CHECK(!restorer.PrepareImage(image.data(), image.size(), &error));
    CHECK(error == "selected resource allocation failed" && !restorer.HasPrepared());
    CHECK(teardowns == 1 && stages == 3 && ram.Data()[0x100] == 38);
    can_stage = true;
    auto invalid = blobs;
    for (auto& blob : invalid) if (blob.tag == SessionClock::kTag) ++blob.version;
    const auto invalid_image = SerializeStateFile(meta, invalid);
    CHECK(!restorer.PrepareImage(invalid_image.data(), invalid_image.size(), &error));
    CHECK(teardowns == 1 && stages == 3 && ram.Data()[0x100] == 38);

    // A reused name cannot expose its previous success after an overflow.
    ps2r::app::CheckpointCapture::Write(directory, 0, UINT64_MAX, ram, restorer, quiescence);
    CHECK(!std::filesystem::exists(path) && drains == 1);
    CHECK(std::filesystem::exists(std::string(directory) + "/memory_b0.bin"));
    std::ifstream refusal(std::string(directory) + "/state_b0.refused");
    std::string detail;
    std::getline(refusal, detail);
    CHECK(detail.find("exceeds the reproduction format") != std::string::npos);
    meta.vsync_frame = 71;
    CHECK(!SessionClock::SetFrame(meta, UINT64_MAX, error) && meta.vsync_frame == 71);
    CHECK(SessionClock::SetFrame(meta, UINT32_MAX, error) && meta.vsync_frame == UINT32_MAX);
    std::filesystem::remove_all(directory);
    std::printf("CheckpointClock: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}

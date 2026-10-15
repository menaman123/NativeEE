// SavestateSlots / SavestateTrigger pure pins: the slot path and root
// precedence, the .meta sidecar (frame, build id, composition, section tags),
// Latest() by modification time, Preflight() naming the first missing
// required section AND its owner, ReadMeta + the composition preflight (a
// slot of another profile/hle.* composition refused BY NAME, build_id a
// warning), the resolved running image, the F8 relaunch argv, and the run.meta
// `savestate=` line. No game process, no renderer.
#include "app/host/savestate_slots.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "engine/core/config/config.h"
#include "engine/core/savestate/savestate_format.h"
#include "engine/core/savestate/section_registry.h"

using ps2r::Config;
using ps2r::app::SavestateSlots;
namespace fs = std::filesystem;
namespace sv = ps2r::savestate;

static int failures = 0;
static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

namespace {
constexpr uint32_t Tag(const char (&s)[5]) {
    return static_cast<uint32_t>(s[0]) | (static_cast<uint32_t>(s[1]) << 8) |
           (static_cast<uint32_t>(s[2]) << 16) | (static_cast<uint32_t>(s[3]) << 24);
}
const uint32_t TAG_GCLK = Tag("GCLK");
const uint32_t TAG_SVCS = Tag("SVCS");
const uint32_t TAG_TIMR = Tag("TIMR");

bool WriteImage(const std::string& path, uint32_t frame, const std::vector<uint32_t>& tags, std::string* err) {
    sv::FileMeta meta;
    meta.vsync_frame = frame;
    std::vector<sv::Blob> blobs;
    for (uint32_t tag : tags) {
        sv::Blob blob;
        blob.tag = tag;
        blob.version = 2;
        blob.payload = {1, 2, 3, 4};
        blobs.push_back(blob);
    }
    return sv::WriteStateFile(path, meta, blobs, err);
}

std::string ReadAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void PathsAndRootPrecedence() {
    Config cfg;
    cfg.paths.log_dir = "L";
    CHECK(SavestateSlots::Root(cfg) == "L");
    cfg.harness.out_dir = "H";
    CHECK(SavestateSlots::Root(cfg) == "H");
    cfg.paths.save_dir = "S";
    CHECK(SavestateSlots::Root(cfg) == "S");
    CHECK(SavestateSlots::Directory(cfg) == (fs::path("S") / "savestates").string());
    CHECK(SavestateSlots::SlotPath(cfg) == (fs::path("S") / "savestates" / "slot0.ps2st").string());
    cfg.save.slot = 7;
    CHECK(SavestateSlots::SlotPath(cfg) == (fs::path("S") / "savestates" / "slot7.ps2st").string());
    CHECK(SavestateSlots::SlotPath("D", -3) == (fs::path("D") / "slot0.ps2st").string());
    CHECK(SavestateSlots::MetaPath("x/slot1.ps2st") == "x/slot1.ps2st.meta");
    Config defaults;
    defaults.paths.log_dir.clear();
    defaults.harness.out_dir.clear();
    CHECK(SavestateSlots::Root(defaults) == "logs");
    // The composition names every hle.* key and the profile.
    const std::string composition = SavestateSlots::Composition(defaults);
    CHECK(composition.find("hle.") != std::string::npos);
    CHECK(composition.find("profile=") != std::string::npos);
    CHECK(composition.find("paths.") == std::string::npos);
}

void MetaSidecarAndLatest(const fs::path& dir) {
    std::string err;
    const std::string slot0 = SavestateSlots::SlotPath(dir.string(), 0);
    const std::string slot1 = SavestateSlots::SlotPath(dir.string(), 1);
    CHECK(WriteImage(slot0, 1300, {TAG_GCLK, TAG_SVCS}, &err));
    SavestateSlots::Meta meta;
    meta.frame = 1300;
    meta.wall_ms = 42;
    meta.build_id = "BUILD-X";
    meta.composition = "hle.a=on;profile=default";
    CHECK(SavestateSlots::WriteMeta(slot0, meta, &err));
    const std::string text = ReadAll(SavestateSlots::MetaPath(slot0));
    CHECK(text.find("frame=1300\n") != std::string::npos);
    CHECK(text.find("build_id=BUILD-X\n") != std::string::npos);
    CHECK(text.find("composition=hle.a=on;profile=default\n") != std::string::npos);
    CHECK(text.find("sections=GCLK:v2,SVCS:v2\n") != std::string::npos);
    CHECK(!fs::exists(SavestateSlots::MetaPath(slot0) + ".tmp"));
    // WriteMeta on a missing image fails and names it.
    CHECK(!SavestateSlots::WriteMeta((dir / "slot9.ps2st").string(), meta, &err) && !err.empty());
    // Latest: none yet besides slot0 -> slot0; then a newer slot1 wins; a
    // non-slot file is ignored.
    err.clear();
    CHECK(SavestateSlots::Latest(dir.string(), &err) == slot0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(WriteImage(slot1, 1400, {TAG_GCLK}, &err));
    std::ofstream(dir / "not-a-slot.ps2st") << "x";
    std::ofstream(dir / "slotX.ps2st") << "x";
    const std::string latest = SavestateSlots::Latest(dir.string(), &err);
    CHECK(latest == slot1);
    // An empty directory refuses with the F5 hint.
    const fs::path empty = dir / "empty";
    fs::create_directories(empty);
    err.clear();
    CHECK(SavestateSlots::Latest(empty.string(), &err).empty());
    CHECK(err.find("press F5 first") != std::string::npos && err.find(empty.string()) != std::string::npos);
}

void PreflightNamesTheMissingOwner(const fs::path& dir) {
    sv::SectionRegistry registry;
    sv::SectionSpec clock;
    clock.tag = TAG_GCLK;
    clock.owner = "SessionClock";
    clock.required = true;
    sv::SectionSpec services;
    services.tag = TAG_SVCS;
    services.owner = "ServiceCheckpoint";
    services.required = false;
    services.capture_required = true;
    sv::SectionSpec timers;
    timers.tag = TAG_TIMR;
    timers.owner = "Timers";
    timers.required = false;
    CHECK(registry.Register(clock) && registry.Register(services) && registry.Register(timers));
    std::string err;
    const std::string full = SavestateSlots::SlotPath(dir.string(), 0);     // GCLK + SVCS
    const std::string partial = SavestateSlots::SlotPath(dir.string(), 1);  // GCLK only
    sv::FileMeta meta;
    CHECK(SavestateSlots::Preflight(full, registry, &err, &meta));
    CHECK(meta.vsync_frame == 1300);
    err.clear();
    CHECK(!SavestateSlots::Preflight(partial, registry, &err));
    CHECK(err == "required section missing: SVCS (ServiceCheckpoint) -- the image was not captured under this composition");
    err.clear();
    CHECK(!SavestateSlots::Preflight((dir / "absent.ps2st").string(), registry, &err) && !err.empty());
    // A corrupt file refuses before any owner check.
    std::ofstream(dir / "corrupt.ps2st") << "not a state file";
    err.clear();
    CHECK(!SavestateSlots::Preflight((dir / "corrupt.ps2st").string(), registry, &err) && !err.empty());
}

void RelaunchArguments() {
    const std::vector<std::string> argv = {
        "/bin/nativeee", "--profile", "default", "--harness-window", "--record-input", "/p/s3/session.padlog",
        "--frames", "36000", "--harness-out", "/p/s3", "--inputs", "/p/old.padlog", "--load-state", "/p/old.ps2st",
        "--warp-to", "77", "--set", "paths.mc_dir=/p/saves", "--set", "hle.graphics_shadows=off"};
    const auto out = SavestateSlots::RelaunchArguments(argv, "/p/s3/savestates/slot0.ps2st", "/p/s3", 1234);
    const std::vector<std::string> expected = {
        "/bin/nativeee", "--profile", "default", "--harness-window", "--record-input", "/p/s3/session-r1234.padlog",
        "--frames", "36000", "--harness-out", "/p/s3/restores/1234", "--set", "paths.mc_dir=/p/saves", "--set",
        "hle.graphics_shadows=off", "--set", "paths.save_dir=/p/s3", "--load-state", "/p/s3/savestates/slot0.ps2st"};
    CHECK(out == expected);
    if (out != expected) for (const auto& a : out) std::printf("  %s\n", a.c_str());
    // The one-shot harness triggers are dropped: the relaunch must not save/press again.
    const auto once = SavestateSlots::RelaunchArguments({"x", "--set", "harness.save_at_frame=60", "--set",
        "harness.restore_at_frame=100", "--set", "save.slot=2"}, "/q/s.ps2st", "/q", 5);
    CHECK((once == std::vector<std::string>{"x", "--set", "save.slot=2", "--set", "paths.save_dir=/q",
                                           "--load-state", "/q/s.ps2st"}));
    // An explicit save_dir is kept, not doubled; a value-less flag at the end is harmless.
    const auto kept = SavestateSlots::RelaunchArguments({"x", "--set", "paths.save_dir=/q", "--load-state"}, "/q/s.ps2st", "/q", 1);
    CHECK((kept == std::vector<std::string>{"x", "--set", "paths.save_dir=/q", "--load-state", "/q/s.ps2st"}));
    // A key that merely contains the text is not the save_dir key.
    const auto other = SavestateSlots::RelaunchArguments({"x", "--set", "xpaths.save_dir=/q"}, "/q/s.ps2st", "/r", 1);
    CHECK((other == std::vector<std::string>{"x", "--set", "xpaths.save_dir=/q", "--set", "paths.save_dir=/r",
                                            "--load-state", "/q/s.ps2st"}));
}

void TriggerMetaLines() {
    CHECK(SavestateSlots::TriggerMetaLine("written", "/s/slot0.ps2st", 1300, 1299, "") ==
          "written,path=/s/slot0.ps2st,frame=1300,boundary=1299");
    CHECK(SavestateSlots::TriggerMetaLine("refused", "", 0, 0, "No admissible boundary in 120 consecutive boundaries; the last owner to refuse: graphics: Graphics owned pending=2,failed=0") ==
          "refused,No admissible boundary in 120 consecutive boundaries; the last owner to refuse: graphics: Graphics owned pending=2,failed=0");
    CHECK(SavestateSlots::TriggerMetaLine("failed", "", 0, 0, "x") == "failed,x");
    CHECK(SavestateSlots::TriggerMetaLine("waiting", "", 0, 0, "x") == "pending");
}

// The F8 composition preflight (review loop 1, item 1): the sidecar is READ
// back, compared with the live Composition() and a slot of another profile /
// hle.* composition is refused BY NAME with the session still open; a
// differing build_id is a warning, not a refusal; a missing sidecar refuses.
void ReadMetaAndCompositionPreflight(const fs::path& dir) {
    std::string err;
    // ReadMeta round-trips what WriteMeta rendered (slot0 from MetaSidecarAndLatest).
    const std::string slot0 = SavestateSlots::SlotPath(dir.string(), 0);
    SavestateSlots::Meta meta;
    CHECK(SavestateSlots::ReadMeta(SavestateSlots::MetaPath(slot0), &meta, &err));
    CHECK(meta.frame == 1300);
    CHECK(meta.wall_ms == 42);
    CHECK(meta.build_id == "BUILD-X");
    CHECK(meta.composition == "hle.a=on;profile=default");
    CHECK((meta.sections == std::vector<std::string>{"GCLK:v2", "SVCS:v2"}));
    // A missing sidecar / one without composition= refuses and names the file.
    err.clear();
    CHECK(!SavestateSlots::ReadMeta((dir / "none.meta").string(), &meta, &err) && err.find("none.meta") != std::string::npos);
    std::ofstream(dir / "bare.meta") << "frame=1\nbuild_id=B\n";
    err.clear();
    CHECK(!SavestateSlots::ReadMeta((dir / "bare.meta").string(), &meta, &err) && err.find("no composition=") != std::string::npos);
    // Unknown keys are ignored; CRLF tolerated.
    std::ofstream(dir / "extra.meta") << "frame=7\r\nfuture=1\r\ncomposition=hle.a=on;profile=default\r\nsections=\r\n";
    err.clear();
    CHECK(SavestateSlots::ReadMeta((dir / "extra.meta").string(), &meta, &err));
    CHECK(meta.frame == 7 && meta.composition == "hle.a=on;profile=default" && meta.sections.empty());

    // CompositionDifference names the differing fields, nothing else.
    CHECK(SavestateSlots::CompositionDifference("hle.a=on;profile=default", "hle.a=on;profile=default").empty());
    CHECK(SavestateSlots::CompositionDifference("hle.a=on;hle.b=off;profile=deterministic",
                                                "hle.a=on;hle.b=off;profile=default") ==
          "profile(saved=deterministic,live=default)");
    CHECK(SavestateSlots::CompositionDifference("hle.a=on;hle.b=off;profile=deterministic",
                                                "hle.a=off;hle.b=off;profile=default") ==
          "hle.a(saved=on,live=off); profile(saved=deterministic,live=default)");
    CHECK(SavestateSlots::CompositionDifference("hle.a=on;profile=default", "hle.a=on;hle.new=on;profile=default") ==
          "hle.new(saved=<absent>,live=on)");
    CHECK(SavestateSlots::CompositionDifference("hle.old=on;hle.a=on;profile=default", "hle.a=on;profile=default") ==
          "hle.old(saved=on,live=<absent>)");
    CHECK(SavestateSlots::CompositionDifference("", "hle.a=on;profile=default") ==
          "hle.a(saved=<absent>,live=on); profile(saved=<absent>,live=default)");

    // The real Composition(): two Configs that differ in profile only differ by that name.
    Config live;
    live.meta.profile = "default";
    Config saved = live;
    saved.meta.profile = "deterministic";
    CHECK(SavestateSlots::CompositionDifference(SavestateSlots::Composition(saved), SavestateSlots::Composition(live)) ==
          "profile(saved=deterministic,live=default)");

    // PreflightComposition on a slot: same composition + same build -> accepted, no warning.
    std::string warning = "stale";
    CHECK(SavestateSlots::PreflightComposition(slot0, "hle.a=on;profile=default", "BUILD-X", &err, &warning));
    CHECK(warning.empty());
    // Same composition, another build -> accepted with the build_id warning.
    CHECK(SavestateSlots::PreflightComposition(slot0, "hle.a=on;profile=default", "BUILD-Y", &err, &warning));
    CHECK(warning.find("build_id differs: saved=BUILD-X, live=BUILD-Y") != std::string::npos);
    // Another profile -> REFUSED by name, before anything would unwind.
    err.clear();
    CHECK(!SavestateSlots::PreflightComposition(slot0, "hle.a=on;profile=deterministic", "BUILD-X", &err, &warning));
    CHECK(err.find("composition differs: profile(saved=default,live=deterministic)") == 0);
    CHECK(err.find("session stays open") != std::string::npos);
    // Another hle.* key -> refused naming the key.
    err.clear();
    CHECK(!SavestateSlots::PreflightComposition(slot0, "hle.a=off;profile=default", "BUILD-X", &err, &warning));
    CHECK(err.find("hle.a(saved=on,live=off)") != std::string::npos);
    // No sidecar at all (slot1 has an image but no .meta) -> refused, naming the sidecar.
    const std::string slot1 = SavestateSlots::SlotPath(dir.string(), 1);
    err.clear();
    CHECK(!SavestateSlots::PreflightComposition(slot1, "hle.a=on;profile=default", "BUILD-X", &err, &warning));
    CHECK(err.find(SavestateSlots::MetaPath(slot1)) != std::string::npos && err.find("cannot be checked") != std::string::npos);
}

// Review loop 1, item 2: the relaunch exec's the RUNNING image, not argv[0].
void RunningImageResolves() {
    std::string err;
    const std::string image = SavestateSlots::RunningImage(&err);
    CHECK(!image.empty());
    CHECK(!image.empty() && image.front() == '/');
    CHECK(fs::exists(image));
    CHECK(fs::path(image).filename().string().find("test_savestate_slots") != std::string::npos);
    if (image.empty()) std::printf("  RunningImage: %s\n", err.c_str());
}
}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("ps2r-savestate-slots-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    PathsAndRootPrecedence();
    MetaSidecarAndLatest(dir);
    PreflightNamesTheMissingOwner(dir);
    RelaunchArguments();
    TriggerMetaLines();
    ReadMetaAndCompositionPreflight(dir);
    RunningImageResolves();
    fs::remove_all(dir);
    std::printf("SavestateSlots: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

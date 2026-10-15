#include "app/host/session_files.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using ps2r::app::SessionFiles;
static unsigned checks = 0, failures = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (false)

static std::string Read(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main() {
    char name[] = "/tmp/session_files_XXXXXX";
    CHECK(mkdtemp(name) != nullptr);
    const fs::path root(name), leg = root / "leg", dumps = root / "frames";
    fs::create_directory(leg);
    {
        SessionFiles files(dumps.string());
        const std::string& directory = files.FrameDirectory(); // root keeps this same reference
        CHECK(directory == dumps.string());
        files.Select(leg.string(), 0);
        CHECK(fs::is_directory(dumps)); // no prior readable frame/dump is required
        files.WriteHashes(0, 0x42, 0x17);
        files.Select(leg.string(), 0); // no reopening/truncation each boundary
        files.WriteHashes(1, 0x84, 0x34);
        const auto old_operations = Read(leg / "gsops.ohash"), old_draws = Read(leg / "drawir.ohash");
        CHECK(old_operations == "0,0x0000000000000042\n1,0x0000000000000084\n");
        const fs::path next = leg / "output-000001";
        fs::create_directory(next);
        files.Select(next.string(), 1);
        CHECK(directory == (dumps / "output-000001").string());
        std::ofstream(directory + "/f2.ppm") << "new frame";
        files.WriteHashes(2, 0x168, 0x68);
        CHECK(Read(leg / "gsops.ohash") == old_operations && Read(leg / "drawir.ohash") == old_draws);
        CHECK(Read(next / "gsops.ohash") == "2,0x0000000000000168\n");
        CHECK(!fs::exists(dumps / "f2.ppm"));
        const auto saved = Read(next / "drawir.ohash");
        unsigned refused = 0;
        try { files.Select(leg.string(), 0); } catch (const std::runtime_error&) { ++refused; }
        try { files.WriteHashes(3, 1, 2); } catch (const std::runtime_error&) { ++refused; }
        CHECK(refused == 2 && Read(next / "drawir.ohash") == saved);
    }
    {
        const fs::path next = root / "collision";
        fs::create_directory(next);
        std::ofstream(next / "drawir.ohash") << "foreign recording";
        SessionFiles files(dumps.string());
        unsigned refused = 0;
        try { files.Select(next.string(), 2); } catch (const std::runtime_error&) { ++refused; }
        CHECK(refused == 1 && Read(next / "drawir.ohash") == "foreign recording");
        CHECK(!fs::exists(next / "gsops.ohash") && !fs::exists(dumps / "output-000002"));
    }
    {
        SessionFiles quiet("");
        quiet.Select("", 0); quiet.WriteHashes(0, 1, 2);
        quiet.Select("", 1); quiet.WriteHashes(1, 3, 4);
        CHECK(quiet.FrameDirectory().empty());
    }
    {
        const auto fresh = root / "no-frame-yet" / "frames";
        SessionFiles files(fresh.string());
        files.Select("", 1); // first operation may already be a restore epoch
        CHECK(fs::is_directory(fresh / "output-000001"));
        const auto occupied = root / "occupied";
        std::ofstream(occupied) << "existing data";
        SessionFiles refused(occupied.string());
        bool failed = false;
        try { refused.Select("", 1); } catch (const std::runtime_error&) { failed = true; }
        CHECK(failed && Read(occupied) == "existing data");
    }
    fs::remove_all(root);
    std::printf("SessionFiles: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}

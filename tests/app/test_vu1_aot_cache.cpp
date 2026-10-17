// tests/app/test_vu1_aot_cache.cpp -- ctest Vu1AotCacheManifest: the AOT tier's persistent
// side (app/host/vu1_aot_cache.h), pure and on a temp root. The manifest row grammar
// round-trips (a refused row's divergence text with spaces included; a comment, an empty
// line and a row without a status are rejected); the build-id directory name is
// sanitised; Open on a root holding a two-case corpus of one image queues it, the verifier
// thread publishes Verified to the tier and appends the row; a second cache on the same
// root seeds the verdict from the manifest without re-verifying; a kick of a NEW image
// through the tier is collected into the corpus dir (cases_per_image = 1), judged, and
// the next kick runs native.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

#include "app/host/vu1_aot_cache.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"

using ps2r::app::Vu1AotCache;

static int g_fails = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
            ++g_fails;                                                      \
        }                                                                   \
    } while (0)

namespace {

void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// ADD.xyzw VF01, VF02, VF03 | NOP ; NOP | SQ VF01,(VI01) ; NOP[E] | NOP ; NOP | NOP  (+ `salt` words after it: another image)
void Fabricate(Vu1Case& c, uint32_t seed, uint32_t salt) {
    std::memset(&c, 0, sizeof(c));
    c.magic = Vu1Case::kMagic;
    c.version = Vu1Case::kVersion;
    const uint32_t upper_nop = 0x2FFu, lower_nop = 0x8000033Cu;
    Put32(c.micro_mem + 0, lower_nop); Put32(c.micro_mem + 4, 0x28u | (1u << 6) | (2u << 11) | (3u << 16) | (0xFu << 21));
    Put32(c.micro_mem + 8, (0x01u << 25) | (0xFu << 21) | (1u << 16) | (1u << 11)); Put32(c.micro_mem + 12, upper_nop);
    Put32(c.micro_mem + 16, lower_nop); Put32(c.micro_mem + 20, upper_nop | (1u << 30));
    Put32(c.micro_mem + 24, lower_nop); Put32(c.micro_mem + 28, upper_nop);
    Put32(c.micro_mem + 64, salt);
    c.regs.VF[0].w = 1.0f;
    c.regs.VF[2].x = 1.0f + seed; c.regs.VF[3].y = 2.0f * seed;
    c.regs.VI[1] = static_cast<uint16_t>(16 + seed);
    c.vu_cycle = 100;
}

bool WaitStatus(const Vu1AotTier& tier, uint64_t h, Vu1AotTier::Status want, int ms) {
    for (int i = 0; i < ms; ++i) {
        if (tier.status(h) == want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return tier.status(h) == want;
}

}  // namespace

int main() {
    // ---- the row grammar ----
    {
        Vu1AotCache::ManifestRow r;
        r.image = 0x3326286ecab9d6f6ull;
        r.status = Vu1AotTier::Status::Refused;
        r.cases = 12;
        r.corpus = 0xdeadbeef01234567ull;
        r.engine = "NATIVEEE-BUILD-ID 2026-10-17 03:24:13";
        r.verified_at = 1792221875;
        r.bundles = 355292;
        r.divergence = "3: VF05 native=00000000,3f800000 interp=00000000,3f800001";
        const std::string line = Vu1AotCache::FormatRow(r);
        Vu1AotCache::ManifestRow p;
        CHECK(Vu1AotCache::ParseRow(line, p), "parse: %s", line.c_str());
        CHECK(p.image == r.image && p.status == r.status && p.cases == 12 && p.corpus == r.corpus && p.verified_at == r.verified_at &&
                  p.bundles == r.bundles,
              "fields round-trip: %s", line.c_str());
        CHECK(p.engine == "NATIVEEE-BUILD-ID_2026-10-17_03_24_13", "the engine field is the build token: '%s'", p.engine.c_str());
        CHECK(Vu1AotCache::BuildToken(r.engine) == p.engine, "token");
        CHECK(p.divergence == r.divergence, "divergence keeps its spaces: '%s'", p.divergence.c_str());
        r.engine = "abc123";
        r.status = Vu1AotTier::Status::Verified;
        r.divergence.clear();
        CHECK(Vu1AotCache::ParseRow(Vu1AotCache::FormatRow(r), p) && p.status == Vu1AotTier::Status::Verified && p.engine == "abc123" &&
                  p.divergence.empty(),
              "verified row");
        CHECK(!Vu1AotCache::ParseRow("# comment", p), "comment rejected");
        CHECK(!Vu1AotCache::ParseRow("", p), "empty rejected");
        CHECK(!Vu1AotCache::ParseRow("image=0123 cases=4", p), "no status rejected");
        CHECK(!Vu1AotCache::ParseRow("image=0123 status=maybe", p), "unknown status rejected");
        CHECK(Vu1AotCache::ManifestPath("/r", "NATIVEEE-BUILD-ID 2026-10-17 03:24:13") == "/r/NATIVEEE-BUILD-ID_2026-10-17_03_24_13/manifest.txt",
              "sanitised dir: %s", Vu1AotCache::ManifestPath("/r", "NATIVEEE-BUILD-ID 2026-10-17 03:24:13").c_str());
        CHECK(Vu1AotCache::ManifestPath("/r", "") == "/r/unknown-build/manifest.txt", "empty build id");
        CHECK(Vu1AotCache::CorpusDir("/r") == "/r/corpus", "corpus dir");
    }

    // ---- a temp root with a two-case corpus ----
    const std::string root = (std::filesystem::temp_directory_path() / ("vu1_aot_cache_test_" + std::to_string(::getpid()))).string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(Vu1AotCache::CorpusDir(root), ec);
    Vu1Case* cases = new Vu1Case[3];
    Fabricate(cases[0], 0, 0);
    Fabricate(cases[1], 1, 0);
    Fabricate(cases[2], 0, 0x11111111u);  // the NEW image the collector will see
    CHECK(vu1_case::WriteCase((Vu1AotCache::CorpusDir(root) + "/a.bin").c_str(), cases[0]), "write a");
    CHECK(vu1_case::WriteCase((Vu1AotCache::CorpusDir(root) + "/b.bin").c_str(), cases[1]), "write b");
    const uint64_t h = Vu1AotVerifier::ImageHashOf(cases[0]);
    const uint64_t h2 = Vu1AotVerifier::ImageHashOf(cases[2]);
    CHECK(h != h2, "two images");
    {
        VuUnit vu(1);
        vu.dispatch().settings().vu1_mode = VuTierMode::Aot;
        Vu1AotCache::Settings s;
        s.root = root;
        s.build_id = "test build 1";
        s.cases_per_image = 1;
        Vu1AotCache cache(vu.dispatch().aot(), s);
        std::string note;
        CHECK(cache.Open(&note), "open: %s", note.c_str());
        CHECK(note.find("corpus_cases=2") != std::string::npos && note.find("queued=1") != std::string::npos, "note: %s", note.c_str());
        CHECK(WaitStatus(vu.dispatch().aot(), h, Vu1AotTier::Status::Verified, 5000), "the corpus image is verified by the thread");
        // A kick of the verified image runs native.
        vu.LoadCase(cases[0]);
        vu.Execute(0);
        CHECK(vu.dispatch().aot().stats().native_kicks == 1, "native kick");
        // A kick of a NEW image: collected (quota 1), judged, the next kick native.
        vu.LoadCase(cases[2]);
        vu.Execute(0);
        CHECK(vu.dispatch().aot().stats().fallback_kicks == 1, "first kick of the new image on the fallback");
        CHECK(WaitStatus(vu.dispatch().aot(), h2, Vu1AotTier::Status::Verified, 5000), "the collected image is verified");
        vu.LoadCase(cases[2]);
        vu.Execute(0);
        CHECK(vu.dispatch().aot().stats().native_kicks == 2, "second kick native");
        size_t collected_files = 0;
        for (const auto& e : std::filesystem::directory_iterator(Vu1AotCache::CorpusDir(root), ec)) {
            const std::string n = e.path().filename().string();
            if (n.find("_0000_000.bin") != std::string::npos) ++collected_files;
        }
        CHECK(collected_files == 1, "one collected case on disk (%zu)", collected_files);
        const std::string row = cache.CensusRow();
        CHECK(row.find("programs:2:verified:2:refused:0:pending:0:native_kicks:2:fallback_kicks:1") == 0, "census: %s", row.c_str());
        CHECK(row.find(":corpus:2:collected:1:verify_runs:2:") != std::string::npos, "census tail: %s", row.c_str());
        cache.Close(/*drain=*/true);
    }
    // ---- the manifest persists the verdicts: a fresh tier is seeded without re-verifying ----
    {
        VuUnit vu(1);
        vu.dispatch().settings().vu1_mode = VuTierMode::Aot;
        Vu1AotCache::Settings s;
        s.root = root;
        s.build_id = "test build 1";
        Vu1AotCache cache(vu.dispatch().aot(), s);
        std::string note;
        CHECK(cache.Open(&note), "reopen: %s", note.c_str());
        CHECK(note.find("manifest_rows=2") != std::string::npos && note.find("queued=0") != std::string::npos, "reopen note: %s", note.c_str());
        CHECK(vu.dispatch().aot().status(h) == Vu1AotTier::Status::Verified && vu.dispatch().aot().status(h2) == Vu1AotTier::Status::Verified,
              "seeded from the manifest");
        cache.Close(true);
        CHECK(cache.census().verify_runs == 0, "nothing re-verified");
    }
    // ---- another engine build: its own manifest dir, the verdicts do not carry ----
    {
        VuUnit vu(1);
        Vu1AotCache::Settings s;
        s.root = root;
        s.build_id = "test build 2";
        Vu1AotCache cache(vu.dispatch().aot(), s);
        std::string note;
        CHECK(cache.Open(&note) && note.find("manifest_rows=0") != std::string::npos && note.find("queued=2") != std::string::npos,
              "new build re-verifies: %s", note.c_str());
        cache.Close(true);
        CHECK(cache.census().verify_runs == 2, "both images judged under the new build");
        CHECK(std::filesystem::exists(Vu1AotCache::ManifestPath(root, "test build 2")), "second manifest");
    }
    std::filesystem::remove_all(root, ec);
    delete[] cases;
    std::printf("Vu1AotCacheManifest: %s\n", g_fails ? "FAIL" : "ALL PASS");
    return g_fails ? 1 : 0;
}

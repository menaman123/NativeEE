// test_word_watch.cpp -- the --watch-word harness knob (ConfigLoader: CLI only,
// not a Config key, recorded in HarnessRequest) and, in diagnostics builds, the
// WordWatch census (Parse, overlap filter, old->new recording, Summary, Flush).
// Usage: test_word_watch <scratch dir>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/core/config/config_loader.h"
#if PS2R_DIAGNOSTICS
#include "engine/core/diag/word_watch.h"
#endif

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failures++;
}

ps2r::ConfigLoader::Result Load(std::vector<const char*> args, ps2r::Config& cfg) {
    args.insert(args.begin(), "nativeee");
    ps2r::ConfigLoader loader;
    loader.SetSearchDir("/nonexistent-search-dir");
    return loader.Load(static_cast<int>(args.size()), const_cast<char**>(args.data()), cfg);
}
}  // namespace

int main(int argc, char** argv) {
    const std::string scratch = argc > 1 ? argv[1] : ".";
    ps2r::Config cfg;

    // The knob: repeatable, joined with ',', recorded in HarnessRequest, listed in sources.
    ps2r::ConfigLoader::ResetRequestForTest();
    ps2r::ConfigLoader::Result r = Load({"--headless", "--watch-word", "0xe37c20,0xc95664", "--watch-word", "0xd66878"}, cfg);
    Check(r.ok, "watch-word accepted");
    Check(r.watch_words == "0xe37c20,0xc95664,0xd66878", "result carries the joined list");
    Check(ps2r::ConfigLoader::Request().watch_words == "0xe37c20,0xc95664,0xd66878", "HarnessRequest carries the list");
    bool listed = false;
    for (const std::string& s : r.sources) listed = listed || s == "--watch-word 0xe37c20,0xc95664,0xd66878";
    Check(listed, "sources list the knob");
    {
        ps2r::Config plain;
        ps2r::ConfigLoader::Result r0 = Load({"--headless"}, plain);
        Check(r0.ok && plain.Fingerprint() == cfg.Fingerprint(), "fingerprint equal with and without the knob");
    }
    ps2r::ConfigLoader::ResetRequestForTest();
    Check(ps2r::ConfigLoader::Request().watch_words.empty(), "reset clears the request");
    r = Load({"--watch-word"}, cfg);
    Check(!r.ok && r.error.find("requires a value") != std::string::npos, "missing value is an error");
    std::string err;
    Check(!cfg.Set("harness.watch_word", "0x1", &err), "no Config key of that name");

#if PS2R_DIAGNOSTICS
    ps2r::diag::WordWatch& w = ps2r::diag::WordWatch::Instance();
    w.Clear();
    Check(!w.Armed(), "fresh watch is unarmed");
    Check(!w.Parse("0xe37c20,zz", &err) && err.find("zz") != std::string::npos, "malformed item refused");
    Check(!w.Parse("", &err), "empty list refused");
    Check(w.Parse("0xe37c22,0x00585410", &err), "parse ok");
    Check(w.Words().size() == 2 && w.Words()[0] == 0xe37c20 && w.Words()[1] == 0x585410, "words aligned to 4");
    Check(w.RangeLo() == 0x585410 && w.RangeHi() == 0xe37c24, "range covers both words");
    w.SetBoundary(7);
    ps2r::diag::WordWatch::OnWrite(&w, 0xe37c20, 4, 0xc95734, 0xe37ca0);        // exact word
    ps2r::diag::WordWatch::OnWrite(&w, 0xe37c1c, 8, 0x1122334455667788ull, 0); // 8-byte store overlapping the word
    ps2r::diag::WordWatch::OnWrite(&w, 0xe37c24, 4, 1, 2);                      // neighbour: no hit
    ps2r::diag::WordWatch::OnWrite(&w, 0x80585412, 1, 0xaa, 0xbb);              // KSEG0 alias, byte inside the word
    Check(w.Hits() == 3, "three overlapping stores counted");
    Check(w.Summary() == "0xe37c20:2,0x585410:1", std::string("summary per word: " + w.Summary()).c_str());
    const std::string path = scratch + "/watch_test.log";
    Check(w.Flush(path), "flush writes the file");
    std::ifstream in(path);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) lines.push_back(line);
    Check(lines.size() == 3, "three lines");
    Check(lines.size() >= 1 && lines[0].rfind("watch=0xe37c20:w4:b7:", 0) == 0 && lines[0].find(":0xc95734->0xe37ca0") != std::string::npos,
          "line 0 = addr:width:boundary:site:old->new");
    Check(lines.size() >= 3 && lines[2].rfind("watch=0x80585412:w1:b7:", 0) == 0 && lines[2].find(":0xaa->0xbb") != std::string::npos,
          "byte store masked to its width");
    std::remove(path.c_str());
    w.Clear();
#else
    std::printf("SKIP: WordWatch census is a diagnostics-build class (knob pins ran)\n");
#endif

    if (g_failures) {
        std::printf("FAILURES: %d\n", g_failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}

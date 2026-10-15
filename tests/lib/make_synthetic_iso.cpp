// make_synthetic_iso -- the Iso9660Image writer as a tool for the Root pins:
//   make_synthetic_iso <out> plain|raw [--volume ID] <PATH=hostfile | PATH=@inline-text>...
// A fake disc for RootFirstRunExtract and the DiscVerify vectors; never a real
// disc image is involved.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "tests/lib/iso9660_image.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: make_synthetic_iso <out> plain|raw [--volume ID] PATH=hostfile|PATH=@text ...\n");
        return 2;
    }
    const std::string out = argv[1];
    const bool raw = std::strcmp(argv[2], "raw") == 0;
    ps2r::testlib::Iso9660Image img;
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--volume" && i + 1 < argc) { img.SetVolumeId(argv[++i]); continue; }
        const size_t eq = a.find('=');
        if (eq == std::string::npos) { std::fprintf(stderr, "bad spec %s\n", a.c_str()); return 2; }
        const std::string path = a.substr(0, eq), src = a.substr(eq + 1);
        std::vector<uint8_t> bytes;
        if (!src.empty() && src[0] == '@') {
            bytes.assign(src.begin() + 1, src.end());
        } else {
            std::ifstream in(src, std::ios::binary);
            if (!in) { std::fprintf(stderr, "cannot read %s\n", src.c_str()); return 2; }
            bytes.assign(std::istreambuf_iterator<char>(in), {});
        }
        img.AddFile(path, bytes);
    }
    std::string error;
    if (!img.Write(out, raw, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    std::printf("%s: %llu logical bytes (%s)\n", out.c_str(), (unsigned long long)img.LogicalBytes(), raw ? "raw 2352" : "plain 2048");
    return 0;
}

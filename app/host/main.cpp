// app/host/main.cpp -- the composition root.
//
// This build links NO title. The engine is a title-neutral model of the PS2's subsystems
// (EE, IOP, DMAC, GIF, VIF, VU0/VU1, GS, CDVD, IPU, SPU2, pads, memory cards) plus the host
// services around it; what turns that into a playable game is a title module under
// games/<serial>_<slug>/ supplying the recompiled EE code, the per-subsystem HLE and the
// composition that wires them together (README.md, "Per-title HLE").
//
// Until a title is ported, this binary reports its identity and exits. It deliberately does
// NOT pretend to boot: a composition root with nothing to compose should say so rather than
// fail somewhere deeper with a confusing message.
#include <cstring>
#include <iostream>

#include "app/host/build_id.h"

namespace {

// ActiveTitleProfile() (engine/title/title_profile.h) is DECLARED by the engine and DEFINED by
// the linked title module. No title is linked here, so there is nothing to call; the build
// system is the thing that knows, and it tells us through this definition.
#ifdef PS2R_TITLE_ID
constexpr const char* kTitleId = PS2R_TITLE_ID;
#else
constexpr const char* kTitleId = "";
#endif

bool HaveTitle() { return kTitleId[0] != '\0'; }

int PrintVersion() {
    std::cout << g_build_id << '\n';
    return 0;
}

int PrintHelp() {
    std::cout <<
        "nativeee -- a static recompiler runtime for PS2 titles\n"
        "\n"
        "  --version   the build identity (also written into run.meta by the harness)\n"
        "  --help      this text\n"
        "\n";
    if (HaveTitle()) {
        std::cout << "Title linked: " << kTitleId << '\n';
    } else {
        std::cout <<
            "No title is linked into this build, so there is no game to run.\n"
            "\n"
            "The engine, its component tests and the tools build and run without one. To play a\n"
            "title you need a title module under games/<serial>_<slug>/ -- see games/README.md\n"
            "for the directory contract and README.md (\"Per-title HLE\") for what porting one\n"
            "involves. With a module present the build discovers it automatically; name one\n"
            "explicitly with -DPS2R_TITLE=<directory> when several are present.\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) return PrintVersion();
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) return PrintHelp();
    }
    if (!HaveTitle()) {
        std::cerr <<
            "nativeee: no title is linked into this build -- there is no game to run.\n"
            "Run `nativeee --help` for what a title module is and where it goes.\n";
        return 2;
    }
    // A title is linked but this build carries no play composition: the title module owns it
    // (games/<serial>_<slug>/), and a module that does not provide one cannot boot.
    std::cerr << "nativeee: title '" << kTitleId
              << "' is linked but supplies no play composition; nothing to run.\n";
    return 2;
}

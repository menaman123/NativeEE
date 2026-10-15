#pragma once
// PrivateFixtures -- the C++ face of cmake/Ps2rPrivateFixtures.cmake.
//
// The public tree carries no game bytes. A test that needs one of the removed files (the retail
// VU0 microprogram captures, the title's .data word dump, a RAM probe ...) asks for it by its
// ORIGINAL repo-relative path; the root is $NATIVEEE_PRIVATE_FIXTURES when set, else the source
// tree (PS2R_SOURCE_ROOT, a developer who put the files back in place). When the file is absent
// the test must SKIP (exit kSkip = 77, ctest SKIP_RETURN_CODE), never fail:
//
//     const std::string map = PrivateFixtures::Path("games/<title>/data/ram_data_map_v50.txt");
//     if (!PrivateFixtures::Exists(map)) return PrivateFixtures::Skip(map);
#include <string>

class PrivateFixtures {
public:
    static constexpr int kSkip = 77;
    static constexpr const char* kEnv = "NATIVEEE_PRIVATE_FIXTURES";
    // $NATIVEEE_PRIVATE_FIXTURES when set and non-empty, else PS2R_SOURCE_ROOT.
    static std::string Root();
    // Root() + "/" + rel (the file need not exist).
    static std::string Path(const std::string& rel);
    static bool Exists(const std::string& path);
    // Prints "SKIP: private fixture absent: <path> (<how the root was chosen>)" and returns kSkip.
    static int Skip(const std::string& path);
};

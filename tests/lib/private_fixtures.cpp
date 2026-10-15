#include "tests/lib/private_fixtures.h"

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#ifndef PS2R_SOURCE_ROOT
#error "PS2R_SOURCE_ROOT must be defined (link ps2r_private_fixtures)"
#endif

std::string PrivateFixtures::Root() {
    const char* env = std::getenv(kEnv);
    if (env && *env) return env;
    return PS2R_SOURCE_ROOT;
}

std::string PrivateFixtures::Path(const std::string& rel) { return Root() + "/" + rel; }

bool PrivateFixtures::Exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

int PrivateFixtures::Skip(const std::string& path) {
    const char* env = std::getenv(kEnv);
    if (env && *env) {
        std::printf("SKIP: private fixture absent: %s (%s=%s)\n", path.c_str(), kEnv, env);
    } else {
        std::printf("SKIP: private fixture absent: %s (%s unset)\n", path.c_str(), kEnv);
    }
    std::fflush(stdout);
    return kSkip;
}

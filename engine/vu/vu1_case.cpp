// Vu1Case raw file I/O (layout in the header).
#include "engine/vu/vu1_case.h"

#include <cstdio>

namespace vu1_case {

bool ReadCase(const char* path, Vu1Case& c) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    const bool ok = std::fread(&c, sizeof(Vu1Case), 1, f) == 1;
    std::fclose(f);
    return ok && c.magic == Vu1Case::kMagic && c.version == Vu1Case::kVersion;
}

bool WriteCase(const char* path, const Vu1Case& c) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(&c, sizeof(Vu1Case), 1, f) == 1;
    std::fclose(f);
    return ok;
}

}  // namespace vu1_case

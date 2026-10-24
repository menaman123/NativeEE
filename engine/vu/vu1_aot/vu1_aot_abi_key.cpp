// Vu1AotAbiKey -- see the header; the grammar is tools/vu1_aot_stage_prelude.py's.
#include "engine/vu/vu1_aot/vu1_aot_abi_key.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "engine/core/hash/sha256.h"

namespace {

bool ReadWhole(const std::string& path, std::vector<unsigned char>* out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    out->clear();
    unsigned char buf[16384];
    for (;;) {
        const size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        out->insert(out->end(), buf, buf + n);
    }
    std::fclose(f);
    return true;
}

}  // namespace

std::vector<std::string> Vu1AotAbiKey::BuildDefines() {
    std::vector<std::string> d;
#if defined(PS2R_SIMD) && PS2R_SIMD
    d.push_back("PS2R_SIMD=1");
#endif
    return d;
}

std::string Vu1AotAbiKey::ComputeForThisBuild(const std::string& prelude_dir, std::string* err) {
    return Compute(prelude_dir, BuildDefines(), err);
}

std::string Vu1AotAbiKey::Compute(const std::string& prelude_dir, const std::vector<std::string>& defines, std::string* err) {
    std::FILE* m = std::fopen((prelude_dir + "/MANIFEST.txt").c_str(), "r");
    if (m == nullptr) {
        if (err) *err = "no MANIFEST.txt under " + prelude_dir;
        return std::string();
    }
    std::vector<std::string> files;
    char line[1024];
    while (std::fgets(line, sizeof(line), m)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) files.push_back(s);
    }
    std::fclose(m);
    ps2r::hash::Sha256 h;
    std::vector<unsigned char> bytes;
    for (const std::string& rel : files) {
        if (!ReadWhole(prelude_dir + "/" + rel, &bytes)) {
            if (err) *err = "cannot read " + rel + " under " + prelude_dir;
            return std::string();
        }
        h.Update(rel.data(), rel.size());
        h.Update("\n", 1);
        if (!bytes.empty()) h.Update(bytes.data(), bytes.size());
        h.Update("", 1);   // the NUL
    }
    std::vector<std::string> defs(defines);
    std::sort(defs.begin(), defs.end());
    for (const std::string& d : defs) {
        const std::string row = "define=" + d + "\n";
        h.Update(row.data(), row.size());
    }
    unsigned char digest[ps2r::hash::Sha256::kDigestBytes];
    h.Finish(digest);
    return "abi-" + ps2r::hash::Sha256::Hex(digest).substr(0, 16);
}

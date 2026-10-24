// Vu1AotPack -- the pre-built library pack's manifest and paths (see .h).
#include "app/host/vu1_aot_pack.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>

#include "engine/platform/host/platform.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"

namespace ps2r::app {

namespace {

namespace fs = std::filesystem;

// `key=value` fields of one line, in order.
std::vector<std::pair<std::string, std::string>> Fields(const std::string& line) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(line);
    std::string tok;
    while (in >> tok) {
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) out.emplace_back(tok, std::string());
        else out.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
    }
    return out;
}

}  // namespace

const char* Vu1AotPack::PlatformToken() {
#if PS2R_HOST_MACOS
#if defined(__aarch64__) || defined(__arm64__)
    return "macos-arm64";
#else
    return "macos-x86_64";
#endif
#elif PS2R_HOST_LINUX
#if defined(__aarch64__)
    return "linux-arm64";
#else
    return "linux-x86_64";
#endif
#elif PS2R_HOST_WINDOWS
    return "windows-x64";
#else
    return "unknown";
#endif
}

std::string Vu1AotPack::DefaultDir() {
    std::string err;
    const std::string exe = ps2r::host::ExecutableImagePath(&err);
    if (exe.empty()) return std::string();
    std::error_code ec;
    const fs::path dir = fs::path(exe).parent_path();
    return (dir / "vu1_aot_pack").string();
}

std::string Vu1AotPack::ManifestPath(const std::string& dir) { return dir + "/pack.txt"; }

std::string Vu1AotPack::LibraryPath(const std::string& dir, uint64_t image, int opt) {
    return dir + "/" + Vu1AotEmitter::Hex16(image) + ".O" + std::to_string(opt) + "." + ps2r::host::DynamicLibraryExtension();
}

std::string Vu1AotPack::FormatHeader(const Manifest& m) {
    return "vu1_aot_pack version=" + std::to_string(kVersion) + " abi=" + m.abi + " platform=" + m.platform + " compiler=" +
           (m.compiler.empty() ? std::string("unknown") : m.compiler) + " opt=" + std::to_string(m.opt) + " images=" +
           std::to_string(m.rows.size());
}

bool Vu1AotPack::ParseHeader(const std::string& line, Manifest* m) {
    const auto f = Fields(line);
    if (f.empty() || f[0].first != "vu1_aot_pack") return false;
    Manifest parsed;
    parsed.rows = m->rows;
    bool version_ok = false;
    for (const auto& kv : f) {
        if (kv.first == "version") version_ok = std::strtoul(kv.second.c_str(), nullptr, 10) == kVersion;
        else if (kv.first == "abi") parsed.abi = kv.second;
        else if (kv.first == "platform") parsed.platform = kv.second;
        else if (kv.first == "compiler") parsed.compiler = kv.second;
        else if (kv.first == "opt") parsed.opt = std::atoi(kv.second.c_str());
    }
    if (!version_ok || parsed.abi.empty() || parsed.platform.empty()) return false;   // `m` untouched on failure
    *m = std::move(parsed);
    return true;
}

std::string Vu1AotPack::FormatRow(const Row& r) {
    return "image=" + Vu1AotEmitter::Hex16(r.image) + " opt=" + std::to_string(r.opt) + " blocks=" + std::to_string(r.blocks) +
           " ops=" + std::to_string(r.ops) + " bytes=" + std::to_string(r.bytes);
}

bool Vu1AotPack::ParseRow(const std::string& line, Row* r) {
    const auto f = Fields(line);
    if (f.empty() || f[0].first != "image") return false;
    bool have_image = false;
    for (const auto& kv : f) {
        if (kv.first == "image") {
            r->image = std::strtoull(kv.second.c_str(), nullptr, 16);
            have_image = kv.second.size() == 16;
        } else if (kv.first == "opt") r->opt = std::atoi(kv.second.c_str());
        else if (kv.first == "blocks") r->blocks = static_cast<uint32_t>(std::strtoul(kv.second.c_str(), nullptr, 10));
        else if (kv.first == "ops") r->ops = std::strtoull(kv.second.c_str(), nullptr, 10);
        else if (kv.first == "bytes") r->bytes = std::strtoull(kv.second.c_str(), nullptr, 10);
    }
    return have_image;
}

bool Vu1AotPack::Read(const std::string& dir, Manifest* out, std::string* err) {
    std::FILE* f = std::fopen(ManifestPath(dir).c_str(), "r");
    if (f == nullptr) {
        if (err) *err = "no pack.txt under " + dir;
        return false;
    }
    Manifest m;
    char line[1024];
    bool header = false;
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        if (!header) {
            if (!ParseHeader(s, &m)) {
                std::fclose(f);
                if (err) *err = "malformed pack header: " + s;
                return false;
            }
            header = true;
            continue;
        }
        Row r;
        if (ParseRow(s, &r)) m.rows.push_back(r);
    }
    std::fclose(f);
    if (!header) {
        if (err) *err = "empty pack.txt under " + dir;
        return false;
    }
    *out = m;
    return true;
}

bool Vu1AotPack::Write(const std::string& dir, const Manifest& m, std::string* err) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::FILE* f = std::fopen(ManifestPath(dir).c_str(), "w");
    if (f == nullptr) {
        if (err) *err = "cannot write " + ManifestPath(dir);
        return false;
    }
    std::string text = "# NativeEE VU1 AOT library pack -- generated by tools/vu1_aot_pack; game-derived, do not commit or publish.\n";
    text += FormatHeader(m) + "\n";
    for (const Row& r : m.rows) text += FormatRow(r) + "\n";
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!ok && err) *err = "short write of " + ManifestPath(dir);
    return ok;
}

const Vu1AotPack::Row* Vu1AotPack::Find(const Manifest& m, uint64_t image) {
    for (const Row& r : m.rows)
        if (r.image == image) return &r;
    return nullptr;
}

}  // namespace ps2r::app

// recomp_overrides_config.h -- [RECOMP-P1] Pure per-function override-config
// logic for the v2 engine's legacy-pin channel (RECOMP_P0_DESIGN.md §8).
//
// One header, no repo dependencies, following the vu_aot_config.h pattern
// (host_app/vu_aot_config.h), shared by:
//   * recompiler_tool v2 emit (P3)   -- applies pins at generation time
//   * recompiler_tool/legacy/overrides.cpp (P3) -- installs legacy bodies
//   * tests/test_recomp_overrides_config.cpp -- the pure ctest (RecompOverridesConfig)
//
// Grammar is the display.ini / vu_aot.ini convention: `key = value`, `;`
// comments, whitespace-trimmed, unknown KEYS silently ignored (forward
// compat). The `pin` key is REPEATABLE and each value is a comma-separated
// list of 0x-prefixed hex EE addresses, or the sentinel `ALL_MANUAL`
// (convenience: pin every manual-dispatcher address). Unknown/garbage VALUES
// keep defaults and emit ONE warning string each.
//
// HARD CONTRACT (design §8): under RECOMP_ENGINE=v2, a pinned address
// emits/installs its legacy body (recompiler_tool/legacy/manual_bodies/)
// through the existing install(...) seam, so the 143 manual implementations
// stay individually selectable forever, and A/B bisection of a v2 regression
// is one config line. Under legacy the file is ignored entirely.
#pragma once

#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace recompovr {

struct Config {
    std::set<uint32_t> pins;     // explicit pinned addresses (sorted, deduped)
    bool pin_all_manual = false; // `pin = ALL_MANUAL` seen
};

namespace detail {
inline void Trim(std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

// Strict 0x-prefixed hex EE address parse. Returns false (out untouched) on
// anything else: no bare decimal, no negative, no overflow past 32 bits,
// no trailing junk. Hex digits may be either case; the 0x prefix must be
// lowercase (display.ini strictness precedent).
inline bool ParseAddr(const std::string& s, uint32_t& out) {
    if (s.size() < 3 || s.size() > 10) return false; // "0x0".."0xFFFFFFFF"
    if (s[0] != '0' || s[1] != 'x') return false;
    uint64_t v = 0;
    for (size_t i = 2; i < s.size(); ++i) {
        const char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
        if (v > 0xFFFFFFFFull) return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}
} // namespace detail

// Parse ini TEXT into cfg (pure; testable without files). Repeatable `pin`
// keys accumulate. Bad address tokens keep the config unchanged for that
// token and append one warning each to `warnings` (if non-null).
inline void ParseConfigText(const std::string& text, Config& cfg,
                            std::vector<std::string>* warnings) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t sc = line.find(';');
        if (sc != std::string::npos) line.resize(sc);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        detail::Trim(key);
        detail::Trim(val);
        if (key != "pin") continue; // unknown keys ignored (forward compat)
        // Comma-separated token list; each token = 0xADDR or ALL_MANUAL.
        std::istringstream toks(val);
        std::string tok;
        while (std::getline(toks, tok, ',')) {
            detail::Trim(tok);
            if (tok.empty()) continue;
            if (tok == "ALL_MANUAL") {
                cfg.pin_all_manual = true;
                continue;
            }
            uint32_t addr = 0;
            if (detail::ParseAddr(tok, addr)) {
                cfg.pins.insert(addr);
            } else if (warnings) {
                warnings->push_back("unknown pin value '" + tok +
                                    "' -- ignored");
            }
        }
    }
}

// Load from a file. Absent/unreadable file => false, cfg untouched (the
// config is optional; no file means no pins).
inline bool LoadConfigFile(const std::string& path, Config& cfg,
                           std::vector<std::string>* warnings) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    ParseConfigText(ss.str(), cfg, warnings);
    return true;
}

// Canonical text form (round-trips through ParseConfigText).
inline std::string WriteConfigText(const Config& cfg) {
    std::ostringstream out;
    out << "; recomp_overrides.ini -- v2 engine only (RECOMP_P0_DESIGN.md \xc2\xa7"
           "8)\n";
    out << "; pin = comma/newline list of addresses forced to their legacy "
           "body\n";
    if (cfg.pin_all_manual) out << "pin = ALL_MANUAL\n";
    for (uint32_t a : cfg.pins) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08x", a);
        out << "pin = " << buf << "\n";
    }
    return out.str();
}

inline bool SaveConfigFile(const std::string& path, const Config& cfg) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << WriteConfigText(cfg);
    return f.good();
}

// THE selection predicate (the only thing the config changes): is `addr`
// pinned to its legacy body? `manual_addrs` is the registered
// manual-dispatcher address set (host_app RegisterManualDispatcherAddr /
// legacy/manual_bodies inventory) that ALL_MANUAL expands over.
inline bool IsPinned(const Config& cfg, uint32_t addr,
                     const std::set<uint32_t>& manual_addrs) {
    if (cfg.pins.count(addr)) return true;
    if (cfg.pin_all_manual && manual_addrs.count(addr)) return true;
    return false;
}

} // namespace recompovr

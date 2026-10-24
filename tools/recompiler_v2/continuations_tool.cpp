// tools/recompiler_v2/continuations_tool.cpp -- the continuation post-pass as a host tool.
//
// A line-for-line port of continuations.py (which stays beside it as the reference and the
// `--check` / `--stack-fixture` helper for CI): reads the emitted shards, writes continuations.h
// (the exact entry switches per generated body + the shard SHA-256 provenance) and instruments
// the common TU (recompiled.cpp) with the DispatchTable::GeneratedBody wrappers and the shadow
// call-stack hooks. Byte-identical to the Python by construction -- games/<title>/recomp/
// MANIFEST.sha256 pins both outputs, and the regeneration gate (cmake/ps2r_verify_manifest.cmake)
// fails the build if the two ever differ. Exists so the prepared-game pipeline (tools/prepare)
// needs no Python on the player's machine (the engine bundle ships this tool).
//
//   ps2r_continuations <generated dir> [--check] [--audits continuation_audits.json]
//
// The audits file is the one the Python reads at import (hand-audited wrapper / prologue hashes
// of the first title); without --audits nothing is audited, which is the Python's behaviour for a
// title none of whose bodies carry those exact texts.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "engine/core/hash/sha256.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace {

std::string ReadFile(const std::string& path, bool* ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { *ok = false; return {}; }
    std::ostringstream ss;
    ss << in.rdbuf();
    *ok = true;
    return ss.str();
}

bool WriteFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

std::vector<std::string> ListDir(const std::string& dir) {
    std::vector<std::string> names;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { names.emplace_back(fd.cFileName); } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) names.emplace_back(e->d_name);
        closedir(d);
    }
#endif
    return names;
}

// Python's str.isspace() on the ASCII range (what the generated code holds).
bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' ||
           (c >= 0x1c && c <= 0x1f);
}
bool IsWord(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
bool IsHexLower(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

std::string Strip(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && IsSpace(s[b])) ++b;
    while (e > b && IsSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}
std::string RStrip(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && IsSpace(s[e - 1])) --e;
    return s.substr(0, e);
}

bool StartsWith(const std::string& s, size_t at, const char* lit) {
    const size_t n = std::strlen(lit);
    return at + n <= s.size() && s.compare(at, n, lit) == 0;
}

// `0x[0-9a-f]+` at `at`; returns the length (0 = no match).
size_t MatchHex(const std::string& s, size_t at) {
    if (!StartsWith(s, at, "0x")) return 0;
    size_t i = at + 2;
    while (i < s.size() && IsHexLower(s[i])) ++i;
    return i - at >= 3 ? i - at : 0;
}
// `\w+` at `at`.
size_t MatchWord(const std::string& s, size_t at) {
    size_t i = at;
    while (i < s.size() && IsWord(s[i])) ++i;
    return i - at;
}
bool AtLineStart(const std::string& s, size_t at) { return at == 0 || s[at - 1] == '\n'; }

uint32_t ParseHex(const std::string& s) { return static_cast<uint32_t>(std::stoul(s, nullptr, 16)); }

std::string Hex(uint32_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

// --- the audits (continuation_audits.json: {"wrappers": {"0x..": {"sha256": ".."}}, "prologues": {...}}) ----
struct Audits {
    std::map<std::string, std::string> wrappers, prologues;   // hex address -> sha256
};

// A purpose-built reader for the file's fixed shape (two objects of address -> {sha256, reason}).
void ReadAudits(const std::string& text, Audits* out) {
    auto section = [&](const char* kind, std::map<std::string, std::string>* into) {
        const std::string key = std::string("\"") + kind + "\"";
        size_t p = text.find(key);
        if (p == std::string::npos) return;
        p = text.find('{', p);
        if (p == std::string::npos) return;
        int depth = 0;
        size_t end = p;
        for (; end < text.size(); ++end) {
            if (text[end] == '{') ++depth;
            else if (text[end] == '}' && --depth == 0) break;
        }
        const std::string body = text.substr(p, end - p);
        size_t q = 0;
        while ((q = body.find("\"0x", q)) != std::string::npos) {
            const size_t close = body.find('"', q + 1);
            if (close == std::string::npos) break;
            const std::string addr = body.substr(q + 1, close - q - 1);
            const size_t sh = body.find("\"sha256\"", close);
            if (sh == std::string::npos) break;
            const size_t v1 = body.find('"', body.find(':', sh) + 1);
            const size_t v2 = body.find('"', v1 + 1);
            (*into)[addr] = body.substr(v1 + 1, v2 - v1 - 1);
            q = v2 + 1;
        }
    };
    section("wrappers", &out->wrappers);
    section("prologues", &out->prologues);
}

bool Audited(const std::map<std::string, std::string>& table, uint32_t address, const std::string& text) {
    auto it = table.find(Hex(address));
    if (it == table.end()) return false;
    return it->second == ps2r::hash::Sha256::HexOf(text.data(), text.size());
}

// --- collect() -----------------------------------------------------------------------------
struct Record {
    uint32_t address;
    std::vector<uint32_t> entries;   // sorted
    int wait;
};

struct FunctionMatch { size_t start, end; std::string name; uint32_t address; };

// FUNCTION = ^// Function: (\w+) at (0x[0-9a-f]+)\nvoid \1\(CpuContext& ctx, uint32_t addr\) \{   (re.M)
std::vector<FunctionMatch> FindFunctions(const std::string& s) {
    std::vector<FunctionMatch> out;
    size_t p = 0;
    while ((p = s.find("// Function: ", p)) != std::string::npos) {
        const size_t start = p;
        p += 1;
        if (!AtLineStart(s, start)) continue;
        size_t i = start + std::strlen("// Function: ");
        const size_t nl = MatchWord(s, i);
        if (!nl) continue;
        const std::string name = s.substr(i, nl);
        i += nl;
        if (!StartsWith(s, i, " at ")) continue;
        i += 4;
        const size_t hl = MatchHex(s, i);
        if (!hl) continue;
        const std::string hex = s.substr(i, hl);
        i += hl;
        const std::string tail = "\nvoid " + name + "(CpuContext& ctx, uint32_t addr) {";
        if (!StartsWith(s, i, tail.c_str())) continue;
        i += tail.size();
        out.push_back({start, i, name, ParseHex(hex)});
        p = i;
    }
    return out;
}

// re.sub(r"//[^\n]*", "", x) then re.sub(r"\s+", "", x)
std::string Normalize(const std::string& block) {
    std::string out;
    out.reserve(block.size());
    for (size_t i = 0; i < block.size();) {
        if (block[i] == '/' && i + 1 < block.size() && block[i + 1] == '/') {
            while (i < block.size() && block[i] != '\n') ++i;
            continue;
        }
        if (!IsSpace(block[i])) out.push_back(block[i]);
        ++i;
    }
    return out;
}

// The prologue tokens the Python's re.sub removes (alternation, leftmost occurrence first).
std::string StripPrologueTokens(const std::string& text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (StartsWith(text, i, "bool branch_taken;")) { i += std::strlen("bool branch_taken;"); continue; }
        if (StartsWith(text, i, "bool cond;")) { i += std::strlen("bool cond;"); continue; }
        if (StartsWith(text, i, "uint64_t _native_saved_r")) {
            size_t j = i + std::strlen("uint64_t _native_saved_r");
            size_t d = j;
            while (d < text.size() && text[d] >= '0' && text[d] <= '9') ++d;
            if (d > j && StartsWith(text, d, " = 0;")) { i = d + 5; continue; }
        }
        if (StartsWith(text, i, "bool _native_valid = false; (void)_native_valid;")) {
            i += std::strlen("bool _native_valid = false; (void)_native_valid;"); continue;
        }
        if (StartsWith(text, i, "initialize_recompiled_functions();")) {
            i += std::strlen("initialize_recompiled_functions();"); continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

bool Collect(const std::string& dir, const std::vector<std::string>& shards, const Audits& audits,
             std::vector<Record>* records, std::vector<std::pair<std::string, std::string>>* hashes, std::string* err) {
    for (const std::string& name : shards) {
        bool ok = false;
        const std::string source = ReadFile(dir + "/" + name, &ok);
        if (!ok) { *err = "cannot read " + dir + "/" + name; return false; }
        hashes->emplace_back(name, ps2r::hash::Sha256::HexOf(source.data(), source.size()));
        const std::vector<FunctionMatch> functions = FindFunctions(source);
        for (size_t index = 0; index < functions.size(); ++index) {
            const uint32_t address = functions[index].address;
            const size_t end = index + 1 < functions.size() ? functions[index + 1].start : source.size();
            const std::string body = source.substr(functions[index].end, end - functions[index].end);
            // start = re.search(r"^Label_0000: // 0x[0-9a-f]+\n", body, re.M)
            size_t sstart = std::string::npos, send = 0;
            for (size_t p = 0; (p = body.find("Label_0000: // ", p)) != std::string::npos; p += 1) {
                if (!AtLineStart(body, p)) continue;
                const size_t h = p + std::strlen("Label_0000: // ");
                const size_t hl = MatchHex(body, h);
                if (hl && h + hl < body.size() && body[h + hl] == '\n') { sstart = p; send = h + hl + 1; break; }
            }
            if (sstart == std::string::npos || !StartsWith(body, 0, "\nbool branch_taken;\nbool cond;\n")) continue;
            const std::string prefix = body.substr(0, sstart);
            const size_t sw = prefix.find("    switch (addr)");
            const std::string raw_prologue = sw == std::string::npos ? prefix : prefix.substr(0, sw);
            const std::string prologue = StripPrologueTokens(raw_prologue);
            if (!Strip(prologue).empty() && !Audited(audits.prologues, address, raw_prologue)) continue;
            std::set<uint32_t> entries{address};
            // labels = set(re.findall(r"^(\w+):", body, re.M))
            std::set<std::string> labels;
            for (size_t p = 0; p < body.size();) {
                const size_t wl = MatchWord(body, p);
                if (wl && p + wl < body.size() && body[p + wl] == ':') labels.insert(body.substr(p, wl));
                const size_t nl = body.find('\n', p);
                if (nl == std::string::npos) break;
                p = nl + 1;
            }
            // for entry, label in re.findall(r"case (0x[0-9a-f]+): goto (\w+);", prefix)
            for (size_t p = 0; (p = prefix.find("case 0x", p)) != std::string::npos; p += 1) {
                const size_t h = p + 5;
                const size_t hl = MatchHex(prefix, h);
                if (!hl || !StartsWith(prefix, h + hl, ": goto ")) continue;
                const size_t l = h + hl + 7;
                const size_t wl = MatchWord(prefix, l);
                if (!wl || l + wl >= prefix.size() || prefix[l + wl] != ';') continue;
                const std::string label = prefix.substr(l, wl);
                if (!labels.count(label)) {
                    *err = dir + "/" + name + ": " + prefix.substr(h, hl) + " has no emitted target " + label;
                    return false;
                }
                entries.insert(ParseHex(prefix.substr(h, hl)));
                p = l + wl;
            }
            std::string first_block = body.substr(send);
            // following_label = re.search(r"^Label_\w+:", first_block, re.M)
            size_t fl = std::string::npos;
            for (size_t p = 0; (p = first_block.find("Label_", p)) != std::string::npos; p += 1) {
                if (!AtLineStart(first_block, p)) continue;
                const size_t wl = MatchWord(first_block, p + 6);
                if (wl && p + 6 + wl < first_block.size() && first_block[p + 6 + wl] == ':') { fl = p; break; }
            }
            if (fl != std::string::npos) first_block = first_block.substr(0, fl);
            else { first_block = RStrip(first_block); if (!first_block.empty()) first_block.pop_back(); }
            const std::string normalized = Normalize(first_block);
            int wait = 0;
            const std::pair<int, int> kinds[] = {{0x32, 1}, {0x44, 2}, {0x56, 3}};
            for (const auto& k : kinds) {
                char num[16];
                std::snprintf(num, sizeof num, "0x%x", k.first);
                const std::string expected = std::string("{") +
                    "ctx.cpuRegs.GPR.r[3].SD[0]=static_cast<int32_t>(" +
                    "static_cast<int32_t>(ctx.cpuRegs.GPR.r[0].SL[0])+" + num + ");" +
                    "runtime_syscall_dispatcher(ctx.cpuRegs.GPR.r[3].UL[0],ctx);" + "return;}";
                if (normalized == expected) wait = k.second;
            }
            records->push_back({address, std::vector<uint32_t>(entries.begin(), entries.end()), wait});
        }
    }
    std::sort(records->begin(), records->end(), [](const Record& a, const Record& b) {
        if (a.address != b.address) return a.address < b.address;
        if (a.entries != b.entries) return a.entries < b.entries;
        return a.wait < b.wait;
    });
    for (size_t i = 1; i < records->size(); ++i)
        if ((*records)[i].address == (*records)[i - 1].address) { *err = "duplicate generated body"; return false; }
    return true;
}

// --- metadata() ----------------------------------------------------------------------------
std::string Metadata(const std::vector<Record>& records, const std::vector<std::pair<std::string, std::string>>& hashes) {
    std::vector<std::string> lines = {"// Generated by tools/recompiler_v2/continuations.py; do not hand edit.",
                                      "// Exact emitted entry switches. Body-shard SHA-256 provenance:"};
    for (const auto& h : hashes) lines.push_back("// " + h.first + " " + h.second);
    lines.insert(lines.end(), {"#include \"engine/ee/cpu/dispatch_table.h\"", "#include <algorithm>",
                               "namespace {", "const uint32_t continuation_entries[] = {"});
    std::vector<uint32_t> flat;
    for (const Record& r : records) flat.insert(flat.end(), r.entries.begin(), r.entries.end());
    for (size_t i = 0; i < flat.size(); i += 10) {
        std::string line = "    ";
        for (size_t j = i; j < std::min(flat.size(), i + 10); ++j) {
            if (j > i) line += ", ";
            line += Hex(flat[j]) + "u";
        }
        lines.push_back(line + ",");
    }
    lines.insert(lines.end(), {"};", "struct ContinuationRow { uint32_t fn, offset, count; int wait; };",
                               "const ContinuationRow continuation_rows[] = {"});
    size_t offset = 0;
    for (const Record& r : records) {
        lines.push_back("    {" + Hex(r.address) + "u, " + std::to_string(offset) + "u, " +
                        std::to_string(r.entries.size()) + "u, " + std::to_string(r.wait) + "},");
        offset += r.entries.size();
    }
    lines.insert(lines.end(), {"};", "DispatchTable::Continuations recomp_continuations(uint32_t fn) {",
                               "    const auto* end = std::end(continuation_rows);",
                               "    const auto* row = std::lower_bound(std::begin(continuation_rows), end, fn,",
                               "        [](const ContinuationRow& item, uint32_t key) { return item.fn < key; });",
                               "    if (row == end || row->fn != fn) return {};",
                               "    return {continuation_entries + row->offset, row->count, row->wait};",
                               "}", "} // namespace", ""});
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

// --- instrument() --------------------------------------------------------------------------
const char* kLambdaHead = "[](CpuContext& ctx, uint32_t addr) {";

// wrapped: DispatchTable::GeneratedBody\((0x..), (\[\]\(CpuContext& ctx, uint32_t addr\) \{\n.*?\n    \}), recomp_continuations\(\1\)\)   (re.S)
// then pattern: (    recompiled_functions\[(0x..)\] = )(\[\]\(CpuContext& ctx, uint32_t addr\) \{.*?\n    \});   (re.S)
std::string InstrumentAuditedWrappers(std::string source, const Audits& audits) {
    {
        std::string out;
        size_t p = 0, last = 0;
        const char* head = "DispatchTable::GeneratedBody(";
        while ((p = source.find(head, p)) != std::string::npos) {
            size_t i = p + std::strlen(head);
            const size_t hl = MatchHex(source, i);
            bool matched = false;
            if (hl) {
                const std::string addr = source.substr(i, hl);
                i += hl;
                const std::string lam = std::string(", ") + kLambdaHead + "\n";
                if (StartsWith(source, i, lam.c_str())) {
                    const size_t lstart = i + 2;
                    const std::string tail = ", recomp_continuations(" + addr + "))";
                    size_t q = i + lam.size();
                    while ((q = source.find("\n    }", q)) != std::string::npos) {
                        const size_t lend = q + 6;
                        if (StartsWith(source, lend, tail.c_str())) {
                            const std::string lambda = source.substr(lstart, lend - lstart);
                            const size_t mend = lend + tail.size();
                            out += source.substr(last, p - last);
                            out += Audited(audits.wrappers, ParseHex(addr), lambda) ? source.substr(p, mend - p) : lambda;
                            last = mend;
                            p = mend;
                            matched = true;
                            break;
                        }
                        q += 1;
                    }
                }
            }
            if (!matched) p += 1;
        }
        out += source.substr(last);
        source = std::move(out);
    }
    {
        std::string out;
        size_t p = 0, last = 0;
        const char* head = "    recompiled_functions[";
        while ((p = source.find(head, p)) != std::string::npos) {
            size_t i = p + std::strlen(head);
            const size_t hl = MatchHex(source, i);
            bool matched = false;
            if (hl) {
                const std::string addr = source.substr(i, hl);
                i += hl;
                const std::string mid = std::string("] = ") + kLambdaHead;
                if (StartsWith(source, i, mid.c_str())) {
                    const size_t g1end = i + 4;
                    size_t q = i + mid.size();
                    while ((q = source.find("\n    };", q)) != std::string::npos) {
                        const size_t lend = q + 6;   // through "    }" ; the ';' follows
                        const std::string lambda = source.substr(g1end, lend - g1end);
                        const size_t mend = lend + 1;
                        out += source.substr(last, p - last);
                        if (Audited(audits.wrappers, ParseHex(addr), lambda))
                            out += source.substr(p, g1end - p) + "DispatchTable::GeneratedBody(" + addr + ", " + lambda +
                                   ", recomp_continuations(" + addr + "));";
                        else
                            out += source.substr(p, mend - p);
                        last = mend;
                        p = mend;
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched) p += 1;
        }
        out += source.substr(last);
        source = std::move(out);
    }
    return source;
}

bool Instrument(std::string source, const Audits& audits, std::string* out, std::string* err) {
    if (source.find("// [CONTINUATION-PROOF]") != std::string::npos) {
        *out = InstrumentAuditedWrappers(std::move(source), audits);
        return true;
    }
    source = "// [CONTINUATION-PROOF]\n#include \"continuations.h\"\n" + source;
    {
        const std::string inc = "#include \"continuations.h\"\n";
        const size_t p = source.find(inc);
        source.replace(p, inc.size(), inc + "#include \"engine/ee/cpu/shadow_call_stack.h\"\n");
    }
    // DEFAULT.subn
    {
        std::string out2;
        size_t p = 0, last = 0, count = 0;
        const char* head = "    recompiled_functions[";
        while ((p = source.find(head, p)) != std::string::npos) {
            size_t i = p + std::strlen(head);
            const size_t hl = MatchHex(source, i);
            bool matched = false;
            if (hl) {
                const std::string addr = source.substr(i, hl);
                i += hl;
                if (StartsWith(source, i, "] = ")) {
                    const size_t g3 = i + 4;
                    size_t g3end = 0;
                    // [](CpuContext& ctx, uint32_t addr) { (FUN_\w+|entry)(ctx, addr); }   |   &(FUN_\w+|entry)
                    auto callee = [&](size_t at, size_t* len) {
                        if (StartsWith(source, at, "FUN_")) {
                            const size_t wl = MatchWord(source, at + 4);
                            if (!wl) return false;
                            *len = 4 + wl;
                            return true;
                        }
                        if (StartsWith(source, at, "entry")) { *len = 5; return true; }
                        return false;
                    };
                    size_t cl = 0;
                    if (StartsWith(source, g3, kLambdaHead) && StartsWith(source, g3 + std::strlen(kLambdaHead), " ")) {
                        const size_t c = g3 + std::strlen(kLambdaHead) + 1;
                        if (callee(c, &cl) && StartsWith(source, c + cl, "(ctx, addr); }")) g3end = c + cl + std::strlen("(ctx, addr); }");
                    } else if (source[g3] == '&' && callee(g3 + 1, &cl)) {
                        g3end = g3 + 1 + cl;
                    }
                    // Python's regex alternation is greedy on FUN_\w+ -- the word run is maximal, as MatchWord gives.
                    if (g3end && g3end < source.size() && source[g3end] == ';') {
                        out2 += source.substr(last, p - last);
                        out2 += source.substr(p, g3 - p) + "DispatchTable::GeneratedBody(" + addr + ", " +
                                source.substr(g3, g3end - g3) + ", recomp_continuations(" + addr + "));";
                        last = g3end + 1;
                        p = last;
                        ++count;
                        matched = true;
                    }
                }
            }
            if (!matched) p += 1;
        }
        out2 += source.substr(last);
        if (!count) { *err = "no default registrations found"; return false; }
        source = std::move(out2);
    }
    const std::pair<const char*, const char*> changes[] = {
        {"    rf_it->second = new_impl;",
         "    const auto observed = DispatchTable::ObserveInstall(addr, new_impl);\n    rf_it->second = observed;"},
        {"r.func = new_impl;", "r.func = observed;"},
        {"g_sorted_func_impls[idx] = new_impl;", "g_sorted_func_impls[idx] = observed;"},
        {"        it->second = [original, addr](CpuContext& ctx, uint32_t call_addr) {",
         "        auto traced = [original, addr](CpuContext& ctx, uint32_t call_addr) {"},
        {"            original(ctx, call_addr);\n        };\n    }\n    // Update function_ranges",
         "            original(ctx, call_addr);\n        };\n"
         "        it->second = DispatchTable::TraceBody(addr, original, std::move(traced));\n"
         "    }\n    // Update function_ranges"},
        {"    if (g_ee_call_stack_depth >= EE_CALL_STACK_MAX) return false;",
         "    if (g_ee_call_stack_depth >= EE_CALL_STACK_MAX) {\n"
         "        ShadowCallStack::RejectedPush();\n        return false;\n    }"},
        {"    if (g_ee_call_stack_depth > 0) --g_ee_call_stack_depth;\n}",
         "    if (g_ee_call_stack_depth > 0) --g_ee_call_stack_depth;\n"
         "    if (!g_ee_call_stack_depth) ShadowCallStack::StackEmptied();\n}"},
        {"            pushed = true;\n        }\n    }\n    ~EeCallFrameGuard()",
         "            pushed = true;\n        } else {\n"
         "            ShadowCallStack::RejectedPush();\n        }\n    }\n"
         "    ~EeCallFrameGuard()"},
        {"        if (pushed && g_ee_call_stack_depth > 0) --g_ee_call_stack_depth;",
         "        if (pushed) ee_call_stack_pop();"},
    };
    for (const auto& c : changes) {
        const std::string before = c.first;
        size_t n = 0;
        for (size_t p = 0; (p = source.find(before, p)) != std::string::npos; p += before.size()) ++n;
        if (n != 1) { *err = std::string("common TU contract changed: ") + c.first; return false; }
        source.replace(source.find(before), before.size(), c.second);
    }
    *out = InstrumentAuditedWrappers(std::move(source), audits);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir, audits_path;
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--check") check = true;
        else if (a == "--audits" && i + 1 < argc) audits_path = argv[++i];
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "ps2r_continuations: unknown option %s\n", a.c_str()); return 2; }
        else dir = a;
    }
    if (dir.empty()) {
        std::fprintf(stderr, "usage: ps2r_continuations <generated dir> [--check] [--audits continuation_audits.json]\n");
        return 2;
    }
    Audits audits;
    if (!audits_path.empty()) {
        bool ok = false;
        const std::string text = ReadFile(audits_path, &ok);
        if (!ok) { std::fprintf(stderr, "ps2r_continuations: cannot read %s\n", audits_path.c_str()); return 2; }
        ReadAudits(text, &audits);
    }
    std::vector<std::string> shards;
    for (const std::string& n : ListDir(dir))
        if (n.rfind("recompiled_shard_", 0) == 0 && n.size() > 4 && n.compare(n.size() - 4, 4, ".cpp") == 0) shards.push_back(n);
    std::sort(shards.begin(), shards.end());
    if (shards.empty()) { std::fprintf(stderr, "ps2r_continuations: no generated body shards in %s\n", dir.c_str()); return 2; }
    std::vector<Record> records;
    std::vector<std::pair<std::string, std::string>> hashes;
    std::string err;
    if (!Collect(dir, shards, audits, &records, &hashes, &err)) { std::fprintf(stderr, "ps2r_continuations: %s\n", err.c_str()); return 1; }
    bool ok = false;
    const std::string common_path = dir + "/recompiled.cpp";
    const std::string common = ReadFile(common_path, &ok);
    if (!ok) { std::fprintf(stderr, "ps2r_continuations: cannot read %s\n", common_path.c_str()); return 2; }
    std::string instrumented;
    if (!Instrument(common, audits, &instrumented, &err)) { std::fprintf(stderr, "ps2r_continuations: %s\n", err.c_str()); return 1; }
    const std::pair<std::string, std::string> outputs[] = {{dir + "/continuations.h", Metadata(records, hashes)},
                                                           {common_path, instrumented}};
    for (const auto& o : outputs) {
        if (check) {
            bool have = false;
            const std::string cur = ReadFile(o.first, &have);
            if (!have || cur != o.second) { std::fprintf(stderr, "stale continuation output: %s\n", o.first.c_str()); return 1; }
        } else if (!WriteFile(o.first, o.second)) {
            std::fprintf(stderr, "ps2r_continuations: cannot write %s\n", o.first.c_str());
            return 2;
        }
    }
    size_t entries = 0, waits = 0;
    for (const Record& r : records) { entries += r.entries.size(); waits += r.wait ? 1 : 0; }
    std::printf("continuations: %zu bodies, %zu exact entries, %zu restartable wait thunks; %zu body shards unchanged\n",
                records.size(), entries, waits, shards.size());
    return 0;
}

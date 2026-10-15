// tools/gs_replay/skip_filter.cpp -- the --skip clause grammar and matcher.
#include "tools/gs_replay/skip_filter.h"

#include <cstdlib>

namespace ps2r {
namespace tools {

bool SkipFilter::Parse(const char* spec) {
    clauses_.clear();
    const std::string s = spec ? spec : "";
    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t comma = s.find(',', pos);
        const std::string cl = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!cl.empty()) {
            Clause c;
            c.text = cl;
            size_t p2 = 0;
            while (p2 <= cl.size()) {
                const size_t plus = cl.find('+', p2);
                const std::string t = cl.substr(p2, plus == std::string::npos ? std::string::npos : plus - p2);
                if (!t.empty()) {
                    const size_t eq = t.find('=');
                    if (eq == std::string::npos) {
                        std::fprintf(stderr, "[GSREPLAY] --skip: bad cond '%s'\n", t.c_str());
                        return false;
                    }
                    Cond cd;
                    cd.key = t.substr(0, eq);
                    const std::string v = t.substr(eq + 1);
                    if (cd.key == "handler") {
                        cd.sval = v;
                    } else {
                        const int base = (cd.key == "tbp" || cd.key == "tpsm" || cd.key == "nottpsm" ||
                                          cd.key == "cbp" || cd.key == "fbp" || cd.key == "fbmsk") ? 16 : 10;
                        const size_t colon = v.find(':');
                        cd.lo = std::strtoull(v.substr(0, colon).c_str(), nullptr, base);
                        cd.hi = colon == std::string::npos ? cd.lo
                                                           : std::strtoull(v.substr(colon + 1).c_str(), nullptr, base);
                    }
                    c.conds.push_back(cd);
                }
                if (plus == std::string::npos) break;
                p2 = plus + 1;
            }
            if (!c.conds.empty()) clauses_.push_back(c);
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return !clauses_.empty();
}

bool SkipFilter::CondMatches(const Cond& cd, const Fields& f) {
    if (cd.key == "tbp") return f.tex && f.tbp >= cd.lo && f.tbp <= cd.hi;
    if (cd.key == "tpsm") return f.tex && f.tpsm == cd.lo;
    if (cd.key == "nottpsm") return f.tex && f.tpsm != cd.lo;
    if (cd.key == "cbp") return f.tex && f.cbp == cd.lo;
    if (cd.key == "abe") return static_cast<uint64_t>(f.abe) == cd.lo;
    if (cd.key == "prim") return static_cast<uint64_t>(f.prim) == cd.lo;
    if (cd.key == "notprim") return static_cast<uint64_t>(f.prim) != cd.lo;
    if (cd.key == "tex") return static_cast<uint64_t>(f.tex) == cd.lo;
    if (cd.key == "fbp") return static_cast<uint64_t>(f.fbp) == cd.lo;
    if (cd.key == "fbmsk") return static_cast<uint64_t>(f.fbmsk) == cd.lo;
    if (cd.key == "seq") return f.seq >= cd.lo && f.seq <= cd.hi;
    if (cd.key == "handler") {
        const char* n = (f.handler && *f.handler) ? f.handler : "raw";
        return cd.sval == n;
    }
    return false;   // an unknown key never matches
}

bool SkipFilter::Match(const Fields& f) {
    for (Clause& c : clauses_) {
        bool ok = true;
        for (const Cond& cd : c.conds)
            if (!CondMatches(cd, f)) { ok = false; break; }
        if (ok) {
            c.hits++;
            return true;
        }
    }
    return false;
}

void SkipFilter::Emit(std::FILE* out) const {
    if (clauses_.empty()) return;
    uint64_t total = 0;
    for (const Clause& c : clauses_) {
        std::fprintf(out, "[GSREPLAY] skip clause='%s' skipped=%llu\n", c.text.c_str(),
                     static_cast<unsigned long long>(c.hits));
        total += c.hits;
    }
    std::fprintf(out, "[GSREPLAY] skip total_skipped=%llu\n", static_cast<unsigned long long>(total));
}

}  // namespace tools
}  // namespace ps2r

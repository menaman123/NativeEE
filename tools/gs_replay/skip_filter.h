// tools/gs_replay/skip_filter.h -- SkipFilter: the `--skip SPEC` ablation filter
// of the replay mode. SPEC = clause[,clause...]; clause = cond[+cond...];
// cond = key=value with keys tbp (hex, lo:hi range), tpsm / nottpsm / cbp (hex),
// fbp / fbmsk (hex), abe / prim / notprim / tex (decimal), seq (decimal lo:hi),
// handler (the HLE program name, "raw" for an unstamped draw). A draw matching
// any clause is counted for that clause and not replayed (it still counts in the
// census and the per-frame draw total, exactly as the reference tool did).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ps2r {
namespace tools {

class SkipFilter {
public:
    struct Fields {
        uint32_t seq = 0;
        uint8_t prim = 0, abe = 0, tex = 0;
        uint32_t tbp = 0, tpsm = 0, cbp = 0;
        uint32_t fbp = 0, fbmsk = 0;
        const char* handler = nullptr;   // nullptr / empty = "raw"
    };

    // False on a malformed clause (message on stderr) or an empty spec.
    bool Parse(const char* spec);
    bool Empty() const { return clauses_.empty(); }
    // True when a clause matches (its hit count advances).
    bool Match(const Fields& f);
    // One line per clause + the total; nothing when no spec was given.
    void Emit(std::FILE* out) const;

private:
    struct Cond {
        std::string key;
        uint64_t lo = 0, hi = 0;
        std::string sval;
    };
    struct Clause {
        std::vector<Cond> conds;
        uint64_t hits = 0;
        std::string text;
    };
    static bool CondMatches(const Cond& cd, const Fields& f);
    std::vector<Clause> clauses_;
};

}  // namespace tools
}  // namespace ps2r

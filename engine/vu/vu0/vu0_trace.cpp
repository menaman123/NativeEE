// Vu0Trace -- the VU0 macro-mode operand census (rules in the header).
#include "engine/vu/vu0/vu0_trace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/ee/cpu/cpu_state.h"

namespace {

std::string HexU(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

// One register line: "<tag> vfNN=(x,y,z,w) hex=XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX".
std::string RegLine(const char* tag, const char* name, const VR_reg& v) {
    char buf[200];
    std::snprintf(buf, sizeof buf, " %s %s=(%.9g,%.9g,%.9g,%.9g) hex=%08x %08x %08x %08x", tag, name, v.F[0], v.F[1],
                  v.F[2], v.F[3], v.I[0], v.I[1], v.I[2], v.I[3]);
    return buf;
}

}  // namespace

void Vu0Trace::Clear() {
    pcs_.clear();
    per_pc_calls_.clear();
    last_head_lo_.clear();
    last_head_hi_.clear();
    code_seen_.clear();
    lines_.clear();
    boundary_ = 0;
    calls_ = 0;
}

bool Vu0Trace::Parse(const std::string& list, std::string* err) {
    std::vector<uint32_t> pcs;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos) end = list.size();
        const std::string item = list.substr(start, end - start);
        start = end + 1;
        if (item.empty()) {
            if (err) *err = "--vu0-trace: empty item in '" + list + "'";
            return false;
        }
        char* stop = nullptr;
        const unsigned long long v = std::strtoull(item.c_str(), &stop, 0);
        if (!stop || *stop != '\0' || v > 0xFFFFull) {
            if (err) *err = "--vu0-trace: expected a VU0 micro pc (< 0x10000), got '" + item + "'";
            return false;
        }
        pcs.push_back(static_cast<uint32_t>(v) & ~7u);
        if (end == list.size()) break;
    }
    if (pcs.empty()) {
        if (err) *err = "--vu0-trace: no pc given";
        return false;
    }
    Clear();
    pcs_ = pcs;
    per_pc_calls_.assign(pcs_.size(), 0);
    last_head_lo_.assign(pcs_.size(), 0);
    last_head_hi_.assign(pcs_.size(), 0);
    code_seen_.assign(pcs_.size(), false);
    return true;
}

bool Vu0Trace::Wants(uint32_t pc) const {
    for (uint32_t p : pcs_)
        if (p == (pc & ~7u)) return true;
    return false;
}

void Vu0Trace::AppendBlock(const char* tag, const VUregs& r, bool with_i) {
    if (lines_.size() >= kMaxLines) return;
    char name[8];
    for (int i = kFirstReg; i <= kLastReg; ++i) {
        std::snprintf(name, sizeof name, "vf%02d", i);
        lines_.push_back(RegLine(tag, name, r.VF[i]));
    }
    lines_.push_back(RegLine(tag, "acc", r.ACC));
    uint32_t q_bits;
    std::memcpy(&q_bits, &r.Q, sizeof q_bits);
    char buf[120];
    std::snprintf(buf, sizeof buf, " %s q=%.9g hex=%08x", tag, r.Q, q_bits);
    lines_.push_back(buf);
    // The flag registers the program may branch on or inherit (sticky bits).
    std::snprintf(buf, sizeof buf, " %s flags status=%04x mac=%04x clip=%06x", tag, r.status_flag, r.mac_flag,
                  r.clip_flag);
    lines_.push_back(buf);
    if (with_i) {
        float i_val;
        std::memcpy(&i_val, &r.I, sizeof i_val);
        std::snprintf(buf, sizeof buf, " %s i=%.9g hex=%08x", tag, i_val, r.I);
        lines_.push_back(buf);
    }
}

void Vu0Trace::Begin(uint32_t pc, const VUregs& in, const uint8_t* micro, size_t micro_size) {
    ++calls_;
    const uint32_t bpc = pc & ~7u;
    size_t slot = pcs_.size();
    for (size_t i = 0; i < pcs_.size(); ++i)
        if (pcs_[i] == bpc) {
            ++per_pc_calls_[i];
            slot = i;
        }
    if (lines_.size() >= kMaxLines) return;
    lines_.push_back("vu0call=" + std::to_string(calls_) + ":pc=" + HexU(pc) + ":b" + std::to_string(boundary_));
    if (slot < pcs_.size() && micro != nullptr && static_cast<size_t>(bpc) + 16 <= micro_size) {
        uint64_t lo, hi;
        std::memcpy(&lo, micro + bpc, 8);
        std::memcpy(&hi, micro + bpc + 8, 8);
        if (!code_seen_[slot] || lo != last_head_lo_[slot] || hi != last_head_hi_[slot]) {
            code_seen_[slot] = true;
            last_head_lo_[slot] = lo;
            last_head_hi_[slot] = hi;
            char buf[64];
            // The listing ends one bundle AFTER the E-bit bundle: the VU executes
            // the instruction following the E bit before it stops.
            bool e_seen = false;
            for (uint32_t b = 0; b < kCodeBundles && static_cast<size_t>(bpc) + b * 8 + 8 <= micro_size; ++b) {
                uint32_t lower, upper;
                std::memcpy(&lower, micro + bpc + b * 8, 4);
                std::memcpy(&upper, micro + bpc + b * 8 + 4, 4);
                std::snprintf(buf, sizeof buf, " code %04x lo=%08x up=%08x", bpc + b * 8, lower, upper);
                lines_.push_back(buf);
                if (e_seen) break;
                e_seen = (upper & 0x40000000u) != 0;
            }
        }
    }
    AppendBlock("in ", in, true);
}

void Vu0Trace::NoteEeRegs(const CpuContext& ctx) {
    if (lines_.size() >= kMaxLines) return;
    char buf[160];
    const auto& r = ctx.cpuRegs.GPR.r;
    std::snprintf(buf, sizeof buf, " in  ee a0=%08x a1=%08x a2=%08x a3=%08x t0=%08x t1=%08x t2=%08x t3=%08x sp=%08x",
                  r[4].UL[0], r[5].UL[0], r[6].UL[0], r[7].UL[0], r[8].UL[0], r[9].UL[0], r[10].UL[0], r[11].UL[0],
                  r[29].UL[0]);
    lines_.push_back(buf);
}

void Vu0Trace::End(uint32_t pc, const VUregs& out) {
    if (lines_.size() >= kMaxLines) return;
    AppendBlock("out", out, false);
    lines_.push_back("vu0end=" + std::to_string(calls_) + ":pc=" + HexU(pc));
}

std::string Vu0Trace::Summary() const {
    std::string s;
    for (size_t i = 0; i < pcs_.size(); ++i) {
        if (!s.empty()) s += ",";
        s += HexU(pcs_[i]) + ":" + std::to_string(per_pc_calls_[i]);
    }
    return s.empty() ? std::string("none") : s;
}

bool Vu0Trace::Flush(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    for (const std::string& l : lines_) std::fprintf(f, "%s\n", l.c_str());
    if (lines_.size() >= kMaxLines)
        std::fprintf(f, "# line cap %zu reached; %llu calls counted in total\n", kMaxLines,
                     static_cast<unsigned long long>(calls_));
    std::fclose(f);
    return true;
}

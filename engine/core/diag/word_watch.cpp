#include "engine/core/diag/word_watch.h"

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>

namespace ps2r {
namespace diag {

namespace {

std::string HexU(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

// The unqualified source name of an Itanium-mangled free function
// ("_Z12FUN_0027edf0R18EmotionEngineStatej" -> "FUN_0027edf0"); anything else
// is returned as is.
std::string Unmangle(const char* sym) {
    std::string s(sym);
    if (s.rfind("_Z", 0) != 0) return s;
    size_t i = 2, n = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') n = n * 10 + static_cast<size_t>(s[i++] - '0');
    if (n == 0 || i + n > s.size()) return s;
    return s.substr(i, n);
}

// The nearest symbol at `pc`, "name+0xoff" (empty when unknown).
std::string Symbolize(const void* pc) {
    Dl_info info{};
    if (!dladdr(pc, &info) || !info.dli_sname) return std::string();
    const uintptr_t off = reinterpret_cast<uintptr_t>(pc) - reinterpret_cast<uintptr_t>(info.dli_saddr);
    return Unmangle(info.dli_sname) + "+" + HexU(off);
}

bool IsRecompFunction(const std::string& sym) { return sym.rfind("FUN_", 0) == 0; }

}  // namespace

WordWatch& WordWatch::Instance() {
    static WordWatch w;
    return w;
}

void WordWatch::Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    words_.clear();
    per_word_hits_.clear();
    lines_.clear();
    lo_ = hi_ = 0;
    hits_ = 0;
    boundary_ = 0;
}

bool WordWatch::Parse(const std::string& list, std::string* err) {
    std::vector<uint32_t> words;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos) end = list.size();
        const std::string item = list.substr(start, end - start);
        start = end + 1;
        if (item.empty()) {
            if (err) *err = "--watch-word: empty item in '" + list + "'";
            return false;
        }
        char* stop = nullptr;
        const unsigned long long v = std::strtoull(item.c_str(), &stop, 0);
        if (!stop || *stop != '\0' || v > 0xFFFFFFFFull) {
            if (err) *err = "--watch-word: expected a 32-bit address, got '" + item + "'";
            return false;
        }
        words.push_back(static_cast<uint32_t>(v) & ~3u);
        if (end == list.size()) break;
    }
    if (words.empty()) {
        if (err) *err = "--watch-word: no address given";
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    words_ = words;
    per_word_hits_.assign(words_.size(), 0);
    lines_.clear();
    hits_ = 0;
    lo_ = 0xFFFFFFFFu;
    hi_ = 0;
    for (uint32_t w : words_) {
        const uint32_t phys = w & 0x1FFFFFFFu;
        if (phys < lo_) lo_ = phys;
        if (phys + 4 > hi_) hi_ = phys + 4;
    }
    return true;
}

void WordWatch::OnWrite(void* user, uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits) {
    static_cast<WordWatch*>(user)->Record(addr, width, old_bits, new_bits);
}

// Walks the frame records (fp -> {prev fp, return address}) above this call
// and names the first recompiled function met plus its caller; falls back to
// the innermost symbol when none is found within the walk.
std::string WordWatch::Site() {
    std::string first, second, innermost;
    void** fp = static_cast<void**>(__builtin_frame_address(0));
    for (int depth = 0; depth < 12 && fp; ++depth) {
        void** prev = static_cast<void**>(fp[0]);
        const void* ret = fp[1];
        if (!ret) break;
        const std::string sym = Symbolize(ret);
        if (innermost.empty() && !sym.empty()) innermost = sym;
        if (IsRecompFunction(sym)) {
            if (first.empty()) first = sym;
            else {
                second = sym;
                break;
            }
        }
        // Frame records grow towards higher addresses on every supported ABI;
        // anything else is a corrupt or foreign frame -- stop.
        if (!prev || prev <= fp) break;
        fp = prev;
    }
    if (first.empty()) return innermost.empty() ? std::string("?") : innermost;
    return second.empty() ? first : first + "<" + second;
}

void WordWatch::Record(uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits) {
    const uint32_t phys = addr & 0x1FFFFFFFu;
    std::lock_guard<std::mutex> lock(mu_);
    bool hit = false;
    for (size_t i = 0; i < words_.size(); ++i) {
        const uint32_t w = words_[i] & 0x1FFFFFFFu;
        if (phys < w + 4 && phys + width > w) {
            per_word_hits_[i]++;
            hit = true;
        }
    }
    if (!hit) return;
    hits_++;
    if (lines_.size() >= kMaxLines) return;
    if (width < 8) {
        const uint64_t mask = (1ull << (width * 8)) - 1;
        old_bits &= mask;
        new_bits &= mask;
    }
    lines_.push_back("watch=" + HexU(addr) + ":w" + std::to_string(width) + ":b" + std::to_string(boundary_) + ":" + Site() +
                     ":" + HexU(old_bits) + "->" + HexU(new_bits));
}

std::string WordWatch::Summary() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::string s;
    for (size_t i = 0; i < words_.size(); ++i) {
        if (!s.empty()) s += ",";
        s += HexU(words_[i]) + ":" + std::to_string(per_word_hits_[i]);
    }
    return s.empty() ? std::string("none") : s;
}

bool WordWatch::Flush(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    for (const std::string& l : lines_) std::fprintf(f, "%s\n", l.c_str());
    if (hits_ > lines_.size())
        std::fprintf(f, "# %llu further hits not recorded (cap %zu)\n",
                     static_cast<unsigned long long>(hits_ - lines_.size()), kMaxLines);
    std::fclose(f);
    return true;
}

}  // namespace diag
}  // namespace ps2r

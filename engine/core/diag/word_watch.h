// core/diag/word_watch.h -- "who writes guest word X": a diagnostics census over
// the recompiled code's main-RAM stores. The harness arms it with
// `--watch-word 0xADDR[,0xADDR...]` (a CLI knob, never a Config key); the
// composition root installs OnWrite as the FastMem write observer and bumps the
// boundary counter once per frame boundary. Every store that overlaps a watched
// word is recorded as
//   watch=<store addr>:w<width>:b<boundary>:<site>:<old>-><new>
// where <site> is the first recompiled function (FUN_xxxxxxxx+off) on the host
// call stack above the store and its caller (walked over the frame records,
// symbolized with dladdr -- Debug builds keep frame pointers and symbols).
// Stores that reach RAM without the CPU path (DMA, host-side copies) are not
// seen: a watched word that changes between boundaries without a line here was
// written by one of those. Mechanism only: no address lives here.
#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace ps2r {
namespace diag {

class WordWatch {
public:
    static WordWatch& Instance();

    // "0xADDR[,0xADDR...]" (hex or decimal, each 4-byte aligned by truncation).
    // Replaces the current list. False + *err on a malformed item.
    bool Parse(const std::string& list, std::string* err);
    void Clear();

    bool Armed() const { return !words_.empty(); }
    const std::vector<uint32_t>& Words() const { return words_; }
    // The physical range covering every watched word ([lo, hi) in bytes) --
    // the observer's cheap pre-filter.
    uint32_t RangeLo() const { return lo_; }
    uint32_t RangeHi() const { return hi_; }

    // The FastMem write-observer entry: `user` is the WordWatch.
    static void OnWrite(void* user, uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits);

    void SetBoundary(uint64_t boundary) { boundary_ = boundary; }
    uint64_t Hits() const { return hits_; }
    // "<addr>:<hits>,..." per watched word (run.meta watch_words=).
    std::string Summary() const;
    // Writes the recorded lines (the first kMaxLines; later hits are counted
    // only) to `path`; true when the file was written.
    bool Flush(const std::string& path) const;

    static constexpr size_t kMaxLines = 20000;

private:
    WordWatch() = default;
    void Record(uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits);
    static std::string Site();

    mutable std::mutex mu_;
    std::vector<uint32_t> words_;
    std::vector<uint64_t> per_word_hits_;
    uint32_t lo_ = 0, hi_ = 0;
    uint64_t boundary_ = 0;
    uint64_t hits_ = 0;
    std::vector<std::string> lines_;
};

}  // namespace diag
}  // namespace ps2r

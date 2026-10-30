#pragma once
// CorpusWriter: the RACORP1 per-call corpus container (recomp-agent
// protocol/corpus-format.md section 2; protocol/corpus.py reads it). The file:
//   "RACORP1\0" | u32 header_len | header (UTF-8 JSON) | zlib( record* )
//   record := u32 fn | u32 count | u32 frame | u8 nargs | u64 args[nargs] | u64 v0 | u64 v1
//             | u32 sp | u32 ra | u32 caller (0xffffffff = none)
//             | u32 nreads  (u32 addr, u16 len, u8 bytes[len])*
//             | u32 nwrites (u32 addr, u16 len, u8 bytes[len])*
//             | u16 ncallees u32 callees[ncallees]
// Records are deduplicated per function on a 64-bit FNV-1a hash of the record
// without its count field (fn, frame excluded: the first call's frame is kept);
// the count is the multiplicity. Everything stays in memory until Close writes
// the file in one pass (the header is written after the body is known). The
// bytes are the guest's memory: the caller puts the file in the data store.
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class CorpusWriter {
public:
    static constexpr uint32_t kNoCaller = 0xFFFFFFFFu;

    struct Run {
        uint32_t addr = 0;
        std::vector<uint8_t> bytes;
    };

    struct Record {
        uint32_t fn = 0;
        uint32_t frame = 0;
        std::vector<uint64_t> args;
        uint64_t v0 = 0, v1 = 0;
        uint32_t sp = 0, ra = 0;
        uint32_t caller = kNoCaller;
        std::vector<Run> reads;    // ascending, coalesced
        std::vector<Run> writes;   // ascending, coalesced
        std::vector<uint32_t> callees;
    };

    struct Limits {
        uint64_t max_distinct = 400000;          // distinct records kept
        uint64_t max_bytes = 768ull << 20;       // serialized bytes kept
    };

    CorpusWriter() = default;
    void SetLimits(const Limits& limits) { limits_ = limits; }
    // JSON string fields merged into the header (producer, title, functions...).
    void SetMeta(const std::string& key, const std::string& json_value);

    // Adds one call; false when the budget refused it (the record is dropped
    // and counted in Dropped()).
    bool Add(const Record& record);

    uint64_t Distinct() const { return order_.size(); }
    uint64_t Calls() const { return calls_; }
    uint64_t Dropped() const { return dropped_; }
    uint64_t Bytes() const { return bytes_; }
    bool Truncated() const { return truncated_; }

    // Writes the file (a .part is renamed into place). False with *error on failure.
    bool Write(const std::string& path, std::string* error);

    static void Serialize(const Record& record, uint32_t count, std::vector<uint8_t>& out);
    static uint64_t Key(const Record& record);

private:
    struct Entry {
        std::vector<uint8_t> bytes;   // serialized with count = 1
        uint32_t count = 0;
    };
    Limits limits_;
    std::unordered_map<uint64_t, size_t> index_;   // key -> position in order_
    std::vector<Entry> order_;
    std::vector<std::pair<std::string, std::string>> meta_;
    uint64_t calls_ = 0, dropped_ = 0, bytes_ = 0;
    bool truncated_ = false;
};

#include "engine/ee/cpu/corpus_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <zlib.h>

namespace {

void PutU8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
void PutU16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(static_cast<uint8_t>(v)); o.push_back(static_cast<uint8_t>(v >> 8)); }
void PutU32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; i++) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void PutU64(std::vector<uint8_t>& o, uint64_t v) {
    for (int i = 0; i < 8; i++) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void PutRuns(std::vector<uint8_t>& o, const std::vector<CorpusWriter::Run>& runs) {
    // Runs longer than a u16 are split; count the pieces first.
    uint32_t n = 0;
    for (const auto& r : runs) n += static_cast<uint32_t>((r.bytes.size() + 0xFFFE) / 0xFFFF);
    PutU32(o, n);
    for (const auto& r : runs) {
        size_t off = 0;
        while (off < r.bytes.size()) {
            const size_t len = std::min<size_t>(0xFFFF, r.bytes.size() - off);
            PutU32(o, r.addr + static_cast<uint32_t>(off));
            PutU16(o, static_cast<uint16_t>(len));
            o.insert(o.end(), r.bytes.begin() + static_cast<long>(off), r.bytes.begin() + static_cast<long>(off + len));
            off += len;
        }
    }
}

uint64_t Fnv1a(const uint8_t* p, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

}  // namespace

void CorpusWriter::Serialize(const Record& r, uint32_t count, std::vector<uint8_t>& o) {
    PutU32(o, r.fn);
    PutU32(o, count);
    PutU32(o, r.frame);
    PutU8(o, static_cast<uint8_t>(r.args.size()));
    for (uint64_t a : r.args) PutU64(o, a);
    PutU64(o, r.v0);
    PutU64(o, r.v1);
    PutU32(o, r.sp);
    PutU32(o, r.ra);
    PutU32(o, r.caller);
    PutRuns(o, r.reads);
    PutRuns(o, r.writes);
    const size_t nc = std::min<size_t>(r.callees.size(), 0xFFFF);
    PutU16(o, static_cast<uint16_t>(nc));
    for (size_t i = 0; i < nc; i++) PutU32(o, r.callees[i]);
}

// The dedup key covers what protocol/corpus.py's CallRecord.key() covers: fn,
// args, ret, sp, caller, the read runs, the write runs, the callees (not frame,
// not ra: the same call from the same caller through another return site is
// the same call).
uint64_t CorpusWriter::Key(const Record& r) {
    std::vector<uint8_t> o;
    PutU32(o, r.fn);
    for (uint64_t a : r.args) PutU64(o, a);
    PutU64(o, r.v0);
    PutU64(o, r.v1);
    PutU32(o, r.sp);
    PutU32(o, r.caller);
    o.push_back('R');
    PutRuns(o, r.reads);
    o.push_back('W');
    PutRuns(o, r.writes);
    for (uint32_t c : r.callees) PutU32(o, c);
    return Fnv1a(o.data(), o.size(), 0xcbf29ce484222325ull);
}

void CorpusWriter::SetMeta(const std::string& key, const std::string& json_value) {
    for (auto& kv : meta_) if (kv.first == key) { kv.second = json_value; return; }
    meta_.emplace_back(key, json_value);
}

bool CorpusWriter::Add(const Record& record) {
    const uint64_t key = Key(record);
    auto it = index_.find(key);
    if (it != index_.end()) {
        order_[it->second].count++;
        calls_++;
        return true;
    }
    if (truncated_ || order_.size() >= limits_.max_distinct || bytes_ >= limits_.max_bytes) {
        truncated_ = true;
        dropped_++;
        return false;
    }
    Entry e;
    Serialize(record, 1, e.bytes);
    e.count = 1;
    bytes_ += e.bytes.size();
    index_.emplace(key, order_.size());
    order_.push_back(std::move(e));
    calls_++;
    return true;
}

bool CorpusWriter::Write(const std::string& path, std::string* error) {
    std::string header = "{";
    for (const auto& kv : meta_) header += "\"" + kv.first + "\": " + kv.second + ", ";
    header += "\"format\": \"RACORP1\", \"distinct\": " + std::to_string(order_.size()) +
              ", \"calls\": " + std::to_string(calls_) + ", \"dropped\": " + std::to_string(dropped_) +
              ", \"truncated\": " + std::string(truncated_ ? "true" : "false") + "}";
    const std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { if (error) *error = "cannot create " + tmp; return false; }
    const uint8_t magic[8] = {'R', 'A', 'C', 'O', 'R', 'P', '1', 0};
    std::fwrite(magic, 1, 8, f);
    std::vector<uint8_t> hl;
    PutU32(hl, static_cast<uint32_t>(header.size()));
    std::fwrite(hl.data(), 1, 4, f);
    std::fwrite(header.data(), 1, header.size(), f);

    z_stream z;
    std::memset(&z, 0, sizeof(z));
    if (deflateInit(&z, 6) != Z_OK) { std::fclose(f); if (error) *error = "deflateInit failed"; return false; }
    std::vector<uint8_t> out(1 << 20);
    bool ok = true;
    auto pump = [&](int flush) {
        do {
            z.next_out = out.data();
            z.avail_out = static_cast<uInt>(out.size());
            const int rc = deflate(&z, flush);
            if (rc == Z_STREAM_ERROR) { ok = false; return; }
            const size_t have = out.size() - z.avail_out;
            if (have && std::fwrite(out.data(), 1, have, f) != have) { ok = false; return; }
        } while (z.avail_out == 0);
    };
    for (auto& e : order_) {
        // Patch the count (offset 4) before compressing.
        uint8_t* p = e.bytes.data() + 4;
        for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(e.count >> (8 * i));
        z.next_in = e.bytes.data();
        z.avail_in = static_cast<uInt>(e.bytes.size());
        pump(Z_NO_FLUSH);
        if (!ok) break;
    }
    if (ok) {
        z.next_in = nullptr;
        z.avail_in = 0;
        pump(Z_FINISH);
    }
    deflateEnd(&z);
    if (std::fclose(f) != 0) ok = false;
    if (!ok) { std::remove(tmp.c_str()); if (error) *error = "write failed: " + tmp; return false; }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { if (error) *error = "rename failed: " + path; return false; }
    return true;
}

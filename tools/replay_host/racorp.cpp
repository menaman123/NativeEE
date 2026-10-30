#include "racorp.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace racorp {

size_t Record::read_bytes() const {
    size_t n = 0;
    for (const auto& r : reads) n += r.bytes.size();
    return n;
}
size_t Record::write_bytes() const {
    size_t n = 0;
    for (const auto& r : writes) n += r.bytes.size();
    return n;
}

namespace {

struct Cursor {
    const uint8_t* p;
    size_t n;
    size_t off = 0;
    void need(size_t k) const {
        if (off + k > n) throw std::runtime_error("RACORP1: truncated record stream");
    }
    uint8_t u8() { need(1); return p[off++]; }
    uint16_t u16() { need(2); uint16_t v; std::memcpy(&v, p + off, 2); off += 2; return v; }
    uint32_t u32() { need(4); uint32_t v; std::memcpy(&v, p + off, 4); off += 4; return v; }
    uint64_t u64() { need(8); uint64_t v; std::memcpy(&v, p + off, 8); off += 8; return v; }
    void bytes(std::vector<uint8_t>& out, size_t k) { need(k); out.assign(p + off, p + off + k); off += k; }
};

std::vector<uint8_t> Inflate(const uint8_t* src, size_t n) {
    std::vector<uint8_t> out;
    out.resize(n * 4 + 1024);
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) throw std::runtime_error("zlib inflateInit failed");
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(n);
    size_t produced = 0;
    for (;;) {
        if (produced == out.size()) out.resize(out.size() * 2);
        zs.next_out = out.data() + produced;
        zs.avail_out = static_cast<uInt>(out.size() - produced);
        int rc = inflate(&zs, Z_NO_FLUSH);
        produced = out.size() - zs.avail_out;
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK) {
            inflateEnd(&zs);
            throw std::runtime_error("RACORP1: corrupt zlib record stream");
        }
    }
    inflateEnd(&zs);
    out.resize(produced);
    return out;
}

}  // namespace

Corpus Read(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open corpus " + path);
    std::vector<uint8_t> data;
    {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < 0) { std::fclose(f); throw std::runtime_error("cannot size corpus " + path); }
        data.resize(static_cast<size_t>(sz));
        if (sz && std::fread(data.data(), 1, data.size(), f) != data.size()) {
            std::fclose(f);
            throw std::runtime_error("short read on corpus " + path);
        }
        std::fclose(f);
    }
    static const char kMagic[8] = {'R', 'A', 'C', 'O', 'R', 'P', '1', '\0'};
    if (data.size() < 12 || std::memcmp(data.data(), kMagic, 8) != 0)
        throw std::runtime_error(path + ": not a RACORP1 corpus");
    uint32_t hlen;
    std::memcpy(&hlen, data.data() + 8, 4);
    if (12u + hlen > data.size()) throw std::runtime_error(path + ": header runs past the file");
    Corpus c;
    c.header_json.assign(reinterpret_cast<const char*>(data.data() + 12), hlen);
    std::vector<uint8_t> body = Inflate(data.data() + 12 + hlen, data.size() - 12 - hlen);
    Cursor cur{body.data(), body.size()};
    while (cur.off < cur.n) {
        Record r;
        r.fn = cur.u32();
        r.count = cur.u32();
        r.frame = cur.u32();
        uint8_t nargs = cur.u8();
        r.args.resize(nargs);
        for (uint8_t i = 0; i < nargs; ++i) r.args[i] = cur.u64();
        r.v0 = cur.u64();
        r.v1 = cur.u64();
        r.sp = cur.u32();
        r.ra = cur.u32();
        r.caller = cur.u32();
        for (int set = 0; set < 2; ++set) {
            uint32_t nruns = cur.u32();
            auto& runs = set == 0 ? r.reads : r.writes;
            runs.reserve(nruns);
            for (uint32_t i = 0; i < nruns; ++i) {
                Run run;
                run.addr = cur.u32();
                uint16_t len = cur.u16();
                cur.bytes(run.bytes, len);
                runs.push_back(std::move(run));
            }
        }
        uint16_t nc = cur.u16();
        r.callees.resize(nc);
        for (uint16_t i = 0; i < nc; ++i) r.callees[i] = cur.u32();
        r.index = c.records.size();
        c.records.push_back(std::move(r));
    }
    return c;
}

}  // namespace racorp

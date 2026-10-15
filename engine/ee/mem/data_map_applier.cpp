#include "engine/ee/mem/data_map_applier.h"

#include <cctype>
#include <fstream>
#include <sstream>

#include "engine/ee/mem/main_memory.h"

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool HexValue(const std::string& digits, uint64_t& out) {
    if (digits.empty() || digits.size() > 16) return false;
    out = 0;
    for (char c : digits) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        out = (out << 4) | static_cast<uint64_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(c) - 'a' + 10));
    }
    return true;
}

}  // namespace

bool DataMapApplier::ParseLine(const std::string& raw, Entry& out, bool* malformed) {
    if (malformed) *malformed = false;
    std::string line = raw;
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    line = Trim(line);
    if (line.empty()) return false;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
        if (malformed) *malformed = true;
        return false;
    }
    std::string addr = Trim(line.substr(0, colon));
    std::string val = Trim(line.substr(colon + 1));
    if (addr.rfind("0x", 0) == 0 || addr.rfind("0X", 0) == 0) addr = addr.substr(2);
    if (val.rfind("0x", 0) == 0 || val.rfind("0X", 0) == 0) val = val.substr(2);
    uint64_t a = 0, v = 0;
    if (!HexValue(addr, a) || a > 0xFFFFFFFFull || !HexValue(val, v) || (val.size() % 2) != 0) {
        if (malformed) *malformed = true;
        return false;
    }
    out.address = static_cast<uint32_t>(a);
    out.byte_count = static_cast<uint32_t>(val.size() / 2);
    out.value = v;
    return true;
}

DataMapApplier::Result DataMapApplier::ApplyText(const std::string& text, MainMemory& ram) {
    Result r{};
    std::istringstream in(text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        Entry e{};
        bool malformed = false;
        if (!ParseLine(line, e, &malformed)) {
            if (malformed) ++r.malformed_lines;
            continue;
        }
        for (uint32_t i = 0; i < e.byte_count; ++i) {
            const uint64_t addr = static_cast<uint64_t>(e.address) + i;
            if (addr >= ram.Size()) break;
            ram.Data()[addr] = static_cast<uint8_t>((e.value >> (8u * i)) & 0xFFu);
            ++r.bytes_written;
        }
        const uint32_t end = e.address + e.byte_count;
        if (first || e.address < r.lowest_address) r.lowest_address = e.address;
        if (first || end > r.highest_address_end) r.highest_address_end = end;
        first = false;
        ++r.entries;
    }
    return r;
}

bool DataMapApplier::ApplyFile(const std::string& path, MainMemory& ram, Result& out, std::string* err) {
    std::ifstream f(path);
    if (!f) {
        if (err) *err = "cannot open data map";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    out = ApplyText(ss.str(), ram);
    return true;
}

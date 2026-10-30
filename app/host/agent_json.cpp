#include "app/host/agent_json.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ps2r::app {

AgentJson AgentJson::Bool(bool b) { AgentJson j; j.kind_ = Kind::Bool; j.boolean_ = b; return j; }
AgentJson AgentJson::Number(double d) {
    AgentJson j;
    j.kind_ = Kind::Number;
    j.number_ = d;
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.2e18) {
        j.integral_ = true;
        j.integer_ = static_cast<int64_t>(d);
    }
    return j;
}
AgentJson AgentJson::Int(int64_t i) {
    AgentJson j;
    j.kind_ = Kind::Number;
    j.number_ = static_cast<double>(i);
    j.integral_ = true;
    j.integer_ = i;
    return j;
}
AgentJson AgentJson::UInt(uint64_t u) {
    AgentJson j;
    j.kind_ = Kind::Number;
    j.number_ = static_cast<double>(u);
    j.integral_ = true;
    j.integer_ = static_cast<int64_t>(u);
    j.unsigned_ = u > static_cast<uint64_t>(INT64_MAX);
    return j;
}
AgentJson AgentJson::String(std::string s) { AgentJson j; j.kind_ = Kind::String; j.string_ = std::move(s); return j; }
AgentJson AgentJson::Array() { AgentJson j; j.kind_ = Kind::Array; return j; }
AgentJson AgentJson::Object() { AgentJson j; j.kind_ = Kind::Object; return j; }

bool AgentJson::AsInt64(int64_t* out) const {
    if (kind_ == Kind::Number) {
        if (integral_ && !unsigned_) { *out = integer_; return true; }
        if (std::isfinite(number_) && number_ == std::floor(number_) && std::fabs(number_) < 9.2e18) {
            *out = static_cast<int64_t>(number_);
            return true;
        }
        return false;
    }
    if (kind_ == Kind::String && !string_.empty()) {
        const char* p = string_.c_str();
        char* end = nullptr;
        const bool hex = string_.size() > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X');
        const long long v = hex ? static_cast<long long>(std::strtoull(p + 2, &end, 16)) : std::strtoll(p, &end, 10);
        if (end && *end == '\0' && end != p + (hex ? 2 : 0)) { *out = v; return true; }
    }
    return false;
}

bool AgentJson::AsUInt64(uint64_t* out) const {
    if (kind_ == Kind::Number && integral_ && unsigned_) { *out = static_cast<uint64_t>(integer_); return true; }
    int64_t v = 0;
    if (!AsInt64(&v) || v < 0) return false;
    *out = static_cast<uint64_t>(v);
    return true;
}

const AgentJson* AgentJson::Get(const char* key) const {
    if (kind_ != Kind::Object) return nullptr;
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return &items_[i];
    return nullptr;
}

AgentJson& AgentJson::Set(const std::string& key, AgentJson value) {
    if (kind_ != Kind::Object) { kind_ = Kind::Object; keys_.clear(); items_.clear(); }
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) { items_[i] = std::move(value); return *this; }
    keys_.push_back(key);
    items_.push_back(std::move(value));
    return *this;
}

AgentJson& AgentJson::Push(AgentJson value) {
    if (kind_ != Kind::Array) { kind_ = Kind::Array; keys_.clear(); items_.clear(); }
    items_.push_back(std::move(value));
    return *this;
}

void AgentJson::SkipWs(const std::string& s, size_t* i) {
    while (*i < s.size() && (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\n' || s[*i] == '\r')) ++*i;
}

namespace {
void AppendUtf8(uint32_t cp, std::string* out) {
    if (cp < 0x80) out->push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out->push_back(static_cast<char>(0xC0 | (cp >> 6))); out->push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
bool Hex4(const std::string& s, size_t i, uint32_t* out) {
    if (i + 4 > s.size()) return false;
    uint32_t v = 0;
    for (size_t k = 0; k < 4; ++k) {
        const char c = s[i + k];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
        else return false;
    }
    *out = v;
    return true;
}
}  // namespace

bool AgentJson::ParseString(const std::string& s, size_t* i, std::string* out, std::string* error) {
    // s[*i] == '"'
    ++*i;
    while (*i < s.size()) {
        const char c = s[*i];
        if (c == '"') { ++*i; return true; }
        if (c == '\\') {
            if (*i + 1 >= s.size()) break;
            const char e = s[*i + 1];
            *i += 2;
            switch (e) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!Hex4(s, *i, &cp)) { *error = "bad \\u escape at " + std::to_string(*i); return false; }
                    *i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && *i + 6 <= s.size() && s[*i] == '\\' && s[*i + 1] == 'u') {
                        uint32_t lo = 0;
                        if (Hex4(s, *i + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            *i += 6;
                        }
                    }
                    AppendUtf8(cp, out);
                    break;
                }
                default: *error = "bad escape at " + std::to_string(*i - 1); return false;
            }
            continue;
        }
        out->push_back(c);
        ++*i;
    }
    *error = "unterminated string";
    return false;
}

bool AgentJson::ParseValue(const std::string& s, size_t* i, AgentJson* out, int depth, std::string* error) {
    if (depth > 64) { *error = "nesting too deep"; return false; }
    SkipWs(s, i);
    if (*i >= s.size()) { *error = "unexpected end of input"; return false; }
    const char c = s[*i];
    if (c == '{') {
        *out = Object();
        ++*i;
        SkipWs(s, i);
        if (*i < s.size() && s[*i] == '}') { ++*i; return true; }
        for (;;) {
            SkipWs(s, i);
            if (*i >= s.size() || s[*i] != '"') { *error = "expected a key at " + std::to_string(*i); return false; }
            std::string key;
            if (!ParseString(s, i, &key, error)) return false;
            SkipWs(s, i);
            if (*i >= s.size() || s[*i] != ':') { *error = "expected ':' at " + std::to_string(*i); return false; }
            ++*i;
            AgentJson v;
            if (!ParseValue(s, i, &v, depth + 1, error)) return false;
            out->keys_.push_back(std::move(key));
            out->items_.push_back(std::move(v));
            SkipWs(s, i);
            if (*i < s.size() && s[*i] == ',') { ++*i; continue; }
            if (*i < s.size() && s[*i] == '}') { ++*i; return true; }
            *error = "expected ',' or '}' at " + std::to_string(*i);
            return false;
        }
    }
    if (c == '[') {
        *out = Array();
        ++*i;
        SkipWs(s, i);
        if (*i < s.size() && s[*i] == ']') { ++*i; return true; }
        for (;;) {
            AgentJson v;
            if (!ParseValue(s, i, &v, depth + 1, error)) return false;
            out->items_.push_back(std::move(v));
            SkipWs(s, i);
            if (*i < s.size() && s[*i] == ',') { ++*i; continue; }
            if (*i < s.size() && s[*i] == ']') { ++*i; return true; }
            *error = "expected ',' or ']' at " + std::to_string(*i);
            return false;
        }
    }
    if (c == '"') {
        std::string v;
        if (!ParseString(s, i, &v, error)) return false;
        *out = String(std::move(v));
        return true;
    }
    if (s.compare(*i, 4, "true") == 0) { *i += 4; *out = Bool(true); return true; }
    if (s.compare(*i, 5, "false") == 0) { *i += 5; *out = Bool(false); return true; }
    if (s.compare(*i, 4, "null") == 0) { *i += 4; *out = Null(); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const size_t start = *i;
        if (s[*i] == '-') ++*i;
        bool integral = true;
        while (*i < s.size()) {
            const char d = s[*i];
            if (d >= '0' && d <= '9') { ++*i; continue; }
            if (d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') { integral = false; ++*i; continue; }
            break;
        }
        const std::string tok = s.substr(start, *i - start);
        if (integral) {
            errno = 0;
            char* end = nullptr;
            if (tok[0] == '-') {
                const long long v = std::strtoll(tok.c_str(), &end, 10);
                if (errno == 0 && end && *end == '\0') { *out = Int(v); return true; }
            } else {
                const unsigned long long v = std::strtoull(tok.c_str(), &end, 10);
                if (errno == 0 && end && *end == '\0') { *out = UInt(v); return true; }
            }
        }
        char* end = nullptr;
        const double d = std::strtod(tok.c_str(), &end);
        if (!end || *end != '\0') { *error = "bad number at " + std::to_string(start); return false; }
        *out = Number(d);
        return true;
    }
    *error = std::string("unexpected character '") + c + "' at " + std::to_string(*i);
    return false;
}

bool AgentJson::Parse(const std::string& text, AgentJson* out, std::string* error) {
    size_t i = 0;
    std::string err;
    if (!ParseValue(text, &i, out, 0, &err)) { if (error) *error = err; return false; }
    SkipWs(text, &i);
    if (i != text.size()) { if (error) *error = "trailing characters at " + std::to_string(i); return false; }
    return true;
}

void AgentJson::DumpString(const std::string& s, std::string* out) {
    out->push_back('"');
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out->append("\\\""); break;
            case '\\': out->append("\\\\"); break;
            case '\n': out->append("\\n"); break;
            case '\r': out->append("\\r"); break;
            case '\t': out->append("\\t"); break;
            case '\b': out->append("\\b"); break;
            case '\f': out->append("\\f"); break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out->append(buf);
                } else {
                    out->push_back(static_cast<char>(c));
                }
        }
    }
    out->push_back('"');
}

void AgentJson::DumpTo(std::string* out) const {
    switch (kind_) {
        case Kind::Null: out->append("null"); break;
        case Kind::Bool: out->append(boolean_ ? "true" : "false"); break;
        case Kind::Number: {
            char buf[64];
            if (integral_ && unsigned_) std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(static_cast<uint64_t>(integer_)));
            else if (integral_) std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(integer_));
            else if (!std::isfinite(number_)) std::snprintf(buf, sizeof(buf), "null");
            else std::snprintf(buf, sizeof(buf), "%.17g", number_);
            out->append(buf);
            break;
        }
        case Kind::String: DumpString(string_, out); break;
        case Kind::Array:
            out->push_back('[');
            for (size_t i = 0; i < items_.size(); ++i) {
                if (i) out->push_back(',');
                items_[i].DumpTo(out);
            }
            out->push_back(']');
            break;
        case Kind::Object:
            out->push_back('{');
            for (size_t i = 0; i < items_.size(); ++i) {
                if (i) out->push_back(',');
                DumpString(keys_[i], out);
                out->push_back(':');
                items_[i].DumpTo(out);
            }
            out->push_back('}');
            break;
    }
}

std::string AgentJson::Dump() const {
    std::string out;
    DumpTo(&out);
    return out;
}

}  // namespace ps2r::app

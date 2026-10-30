// AgentJson: the JSON value of the agent control plane (app/host/agent_server.h) -- parse one
// newline-delimited request, build and serialise one reply or event. Objects keep insertion order;
// numbers are doubles with an exact integer fast path (int64 round-trips as digits, never as 1e+09);
// strings are UTF-8 passed through, escapes \" \\ \/ \b \f \n \r \t \uXXXX (surrogate pairs joined).
// No title knowledge, no third-party library.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ps2r::app {

class AgentJson {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    AgentJson() = default;
    static AgentJson Null() { return AgentJson(); }
    static AgentJson Bool(bool b);
    static AgentJson Number(double d);
    static AgentJson Int(int64_t i);
    static AgentJson UInt(uint64_t u);
    static AgentJson String(std::string s);
    static AgentJson Array();
    static AgentJson Object();

    Kind kind() const { return kind_; }
    bool IsNull() const { return kind_ == Kind::Null; }
    bool IsBool() const { return kind_ == Kind::Bool; }
    bool IsNumber() const { return kind_ == Kind::Number; }
    bool IsString() const { return kind_ == Kind::String; }
    bool IsArray() const { return kind_ == Kind::Array; }
    bool IsObject() const { return kind_ == Kind::Object; }

    bool AsBool() const { return boolean_; }
    double AsDouble() const { return number_; }
    // The integer value: an exact integer number, or a decimal / 0x-hex string (the schema's Address).
    // False when neither.
    bool AsInt64(int64_t* out) const;
    bool AsUInt64(uint64_t* out) const;
    const std::string& AsString() const { return string_; }
    const std::vector<AgentJson>& Items() const { return items_; }
    const std::vector<std::string>& Keys() const { return keys_; }
    size_t Size() const { return items_.size(); }

    // Object member (nullptr when absent or not an object).
    const AgentJson* Get(const char* key) const;
    bool Has(const char* key) const { return Get(key) != nullptr; }
    // Object: set or replace a member (keeps the first position). Array: Push appends.
    AgentJson& Set(const std::string& key, AgentJson value);
    AgentJson& Push(AgentJson value);

    // Parses `text`; false names the byte offset and reason in *error.
    static bool Parse(const std::string& text, AgentJson* out, std::string* error);
    // Compact serialisation (one line, no trailing newline).
    std::string Dump() const;

private:
    static bool ParseValue(const std::string& s, size_t* i, AgentJson* out, int depth, std::string* error);
    static bool ParseString(const std::string& s, size_t* i, std::string* out, std::string* error);
    static void SkipWs(const std::string& s, size_t* i);
    static void DumpString(const std::string& s, std::string* out);
    void DumpTo(std::string* out) const;

    Kind kind_ = Kind::Null;
    bool boolean_ = false;
    double number_ = 0;
    bool integral_ = false;   // the number was given as / built from an integer
    int64_t integer_ = 0;
    bool unsigned_ = false;   // integer_ holds a uint64 bit pattern > INT64_MAX
    std::string string_;
    std::vector<std::string> keys_;
    std::vector<AgentJson> items_;
};

}  // namespace ps2r::app

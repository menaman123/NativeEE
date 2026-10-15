#include "engine/core/config/settings_file.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "engine/core/config/config.h"

namespace ps2r {
namespace {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
    return s.substr(b, e - b);
}

std::string StripComment(const std::string& s) {
    const size_t p = s.find_first_of("#;");
    return p == std::string::npos ? s : s.substr(0, p);
}

// The key a line sets, in `section.key` form ("" for comments/blank/headers).
// Updates `section` on a `[section]` header.
std::string LineKey(const std::string& line, std::string& section) {
    const std::string l = Trim(StripComment(line));
    if (l.empty()) return {};
    if (l.front() == '[' && l.back() == ']') {
        section = Trim(l.substr(1, l.size() - 2));
        return {};
    }
    const size_t eq = l.find('=');
    if (eq == std::string::npos) return {};
    std::string key = Trim(l.substr(0, eq));
    if (!section.empty() && key.find('.') == std::string::npos) key = section + "." + key;
    return key;
}

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

}  // namespace

bool SettingsFile::Writable(const std::string& key) {
    return Config::IsKey(key) && key.compare(0, 8, "harness.") != 0;
}

std::string SettingsFile::SetInText(const std::string& text, const std::string& key, const std::string& value,
                                    bool* changed) {
    std::vector<std::string> lines = Lines(text);
    std::string section;
    bool replaced = false;
    for (std::string& line : lines) {
        if (LineKey(line, section) != key) continue;
        // Keep the line's own key spelling (a `[section]`-scoped short key stays short).
        const std::string head = line.substr(0, line.find('='));
        const std::string next = Trim(head) + " = " + value;
        if (next != line) {
            line = next;
            if (changed) *changed = true;
        }
        replaced = true;
        break;
    }
    if (!replaced) {
        // A flat line appended at the end (outside any section, so the full key is written).
        if (!lines.empty() && !section.empty()) lines.push_back("[]");
        lines.push_back(key + " = " + value);
        if (changed) *changed = true;
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

bool SettingsFile::GetFromText(const std::string& text, const std::string& key, std::string* value) {
    std::string section;
    for (const std::string& line : Lines(text)) {
        if (LineKey(line, section) != key) continue;
        const std::string l = Trim(StripComment(line));
        if (value) *value = Trim(l.substr(l.find('=') + 1));
        return true;
    }
    return false;
}

bool SettingsFile::Set(const std::string& path, const std::string& key, const std::string& value, std::string* err) {
    if (!Writable(key)) {
        if (err) *err = key + ": not a writable setting";
        return false;
    }
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (in.is_open()) {
            std::ostringstream buf;
            buf << in.rdbuf();
            text = buf.str();
        }
    }
    bool changed = false;
    const std::string next = SetInText(text, key, value, &changed);
    if (!changed) return true;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            if (err) *err = "cannot write " + tmp;
            return false;
        }
        out << next;
        if (!out) {
            if (err) *err = "write failed: " + tmp;
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (err) *err = "cannot replace " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace ps2r

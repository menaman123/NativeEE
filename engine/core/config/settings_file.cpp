#include "engine/core/config/settings_file.h"

#include <cstdio>
#include <filesystem>
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
    // The key's own section and short name (a key without a dot has none).
    const size_t dot = key.find('.');
    const std::string key_section = dot == std::string::npos ? std::string() : key.substr(0, dot);
    const std::string key_name = dot == std::string::npos ? key : key.substr(dot + 1);
    // Pass 1: replace in place; remember where the key's section ends.
    size_t section_end = lines.size();   // index AFTER the last key line of [key_section]
    bool section_seen = false;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string& line = lines[i];
        const std::string k = LineKey(line, section);
        if (!key_section.empty() && section == key_section) {
            const std::string l = Trim(StripComment(line));
            const bool header = k.empty() && !l.empty() && l.front() == '[';
            if (header || !k.empty()) { section_seen = true; section_end = i + 1; }
        }
        if (k != key) continue;
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
        if (changed) *changed = true;
        if (!key_section.empty() && section_seen) {
            lines.insert(lines.begin() + static_cast<long>(section_end), key_name + " = " + value);
        } else if (!key_section.empty()) {
            // A new [section] at the end, blank-line separated from what precedes it.
            if (!lines.empty() && !Trim(lines.back()).empty()) lines.push_back("");
            lines.push_back("[" + key_section + "]");
            lines.push_back(key_name + " = " + value);
        } else {
            if (!lines.empty() && !section.empty()) lines.push_back("[]");
            lines.push_back(key + " = " + value);
        }
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

std::vector<std::pair<std::string, std::string>> SettingsFile::Entries(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string section;
    for (const std::string& line : Lines(text)) {
        const std::string k = LineKey(line, section);
        if (k.empty()) continue;
        const std::string l = Trim(StripComment(line));
        out.emplace_back(k, Trim(l.substr(l.find('=') + 1)));
    }
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
    std::error_code ec;   // std::filesystem::rename replaces an existing file on Windows too; std::rename does not
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (err) *err = "cannot replace " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace ps2r

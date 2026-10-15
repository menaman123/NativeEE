// core/triage/input_log.cpp
#include "engine/core/triage/input_log.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace ps2r::triage {

namespace {
const char* const kButtonTokens[16] = {"SELECT", "L3", "R3", "START", "UP", "RIGHT", "DOWN", "LEFT",
                                       "L2", "R2", "L1", "R1", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE"};
const char* const kAxisTokens[4] = {"LX", "LY", "RX", "RY"};

std::string RangeLine(uint32_t f1, uint32_t f2, const char* tok) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u-%u:%s", f1, f2, tok);
    return std::string(buf);
}
std::string AxisLine(uint32_t f1, uint32_t f2, const char* tok, uint8_t v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u-%u:%s=%u", f1, f2, tok, (unsigned)v);
    return std::string(buf);
}
}  // namespace

const char* InputLog::ButtonToken(int bit) { return (bit >= 0 && bit < 16) ? kButtonTokens[bit] : nullptr; }
const char* InputLog::AxisToken(int axis) { return (axis >= 0 && axis < 4) ? kAxisTokens[axis] : nullptr; }

uint16_t InputLog::ButtonMask(const std::string& token) {
    for (int bit = 0; bit < 16; bit++)
        if (token == kButtonTokens[bit]) return (uint16_t)(1u << bit);
    return 0;
}

const char* InputLog::ClockToken(Clock c) { return c == Clock::Frame ? "frame" : "tick"; }

std::string InputLog::HeaderLine(uint32_t start_frame, const std::string& title) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "# padlog v1 start_frame=%u title=%s", start_frame, title.c_str());
    return std::string(buf);
}

std::string InputLog::HeaderLine(uint32_t start_frame, const std::string& title, Clock clock) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "# padlog v1 start_frame=%u clock=%s title=%s", start_frame, ClockToken(clock),
                  title.c_str());
    return std::string(buf);
}

bool InputLog::ParseHeader(const std::string& line, Clock* out) {
    if (line.rfind("# padlog", 0) != 0) return false;
    Clock clock = Clock::Tick;
    const size_t at = line.find(" clock=");
    if (at != std::string::npos) {
        const size_t begin = at + 7;
        const size_t end = line.find(' ', begin);
        const std::string tok = line.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (tok == "frame") clock = Clock::Frame;
    }
    if (out) *out = clock;
    return true;
}

void InputLog::Feed(uint32_t vf, uint16_t buttons, const uint8_t axes[4], std::vector<std::string>* out) {
    if (seen_ && vf != last_vf_) CommitSample(last_vf_, last_buttons_, last_axes_, out);
    seen_ = true;
    last_vf_ = vf;
    last_buttons_ = buttons;
    for (int a = 0; a < 4; a++) last_axes_[a] = axes[a];
}

void InputLog::CommitSample(uint32_t vf, uint16_t buttons, const uint8_t axes[4],
                           std::vector<std::string>* out) {
    for (int bit = 0; bit < 16; bit++) {
        const bool now = (buttons >> bit) & 1;
        OpenRange& r = btn_[bit];
        if (now && !r.open) {
            r.open = true;
            r.f1 = vf;
        } else if (!now && r.open) {
            const uint32_t f2 = (vf > r.f1) ? vf - 1 : r.f1;
            out->push_back(RangeLine(r.f1, f2, kButtonTokens[bit]));
            r.open = false;
        }
    }
    for (int a = 0; a < 4; a++) {
        const uint8_t v = axes[a];
        OpenAxis& s = axis_[a];
        if (s.open && v != s.val) {
            if (s.f1 < vf) {
                out->push_back(AxisLine(s.f1, vf - 1, kAxisTokens[a], s.val));
                s.open = false;
            } else {
                s.val = v;   // mid-frame change: the last value wins the frame
                if (v == 0x80) s.open = false;
                continue;
            }
        }
        if (!s.open && v != 0x80) {
            s.open = true;
            s.f1 = vf;
            s.val = v;
        }
    }
}

void InputLog::Flush(uint32_t vf, std::vector<std::string>* out) {
    if (seen_) CommitSample(last_vf_, last_buttons_, last_axes_, out);
    for (int bit = 0; bit < 16; bit++) {
        OpenRange& r = btn_[bit];
        if (r.open) {
            out->push_back(RangeLine(r.f1, vf, kButtonTokens[bit]));
            r.open = false;
        }
    }
    for (int a = 0; a < 4; a++) {
        OpenAxis& s = axis_[a];
        if (s.open) {
            out->push_back(AxisLine(s.f1, vf, kAxisTokens[a], s.val));
            s.open = false;
        }
    }
    seen_ = false;
}

size_t InputLog::ParseSpec(const std::string& spec) {
    size_t accepted = 0;
    size_t pos = 0;
    while (pos < spec.size()) {
        const size_t comma = spec.find(',', pos);
        const std::string tok = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        unsigned long f1 = 0, f2 = 0;
        char name[16] = {0};
        if (std::sscanf(tok.c_str(), "%lu-%lu:%15s", &f1, &f2, name) == 3) {
            unsigned axval = 0;
            char axname[3] = {0};
            bool taken = false;
            if (std::sscanf(name, "%2[A-Z]=%u", axname, &axval) == 2 && axval <= 255) {
                for (int a = 0; a < 4; a++) {
                    if (!std::strcmp(axname, kAxisTokens[a])) {
                        axes_.push_back({(uint32_t)f1, (uint32_t)f2, (uint8_t)a, (uint8_t)axval});
                        taken = true;
                        accepted++;
                        break;
                    }
                }
            }
            if (!taken) {
                const uint16_t m = ButtonMask(name);
                if (m) {
                    presses_.push_back({(uint32_t)f1, (uint32_t)f2, m});
                    accepted++;
                }
            }
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return accepted;
}

bool InputLog::LoadFile(const std::string& path, std::string* err) {
    std::ifstream f(path);
    if (!f.is_open()) {
        if (err) *err = "cannot open pad script: " + path;
        return false;
    }
    std::string spec, line;
    bool header_seen = false;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '#') {
            // The first padlog header names the clock; a restore segment's
            // provenance comment (`# restore_tick=...`) precedes it and is skipped.
            Clock clock;
            if (!header_seen && ParseHeader(line, &clock)) {
                clock_ = clock;
                header_seen = true;
            }
            continue;
        }
        if (!spec.empty()) spec += ",";
        spec += line;
    }
    ParseSpec(spec);
    return true;
}

bool InputLog::Apply(uint32_t f, uint16_t* wire_buttons_active_low, uint8_t axes[4]) const {
    bool any = false;
    for (const Press& p : presses_) {
        if (f >= p.f1 && f <= p.f2) {
            *wire_buttons_active_low = (uint16_t)(*wire_buttons_active_low & ~p.mask);
            any = true;
        }
    }
    for (const Axis& a : axes_) {
        if (f >= a.f1 && f <= a.f2) {
            axes[a.axis] = a.value;
            any = true;
        }
    }
    return any;
}

}  // namespace ps2r::triage

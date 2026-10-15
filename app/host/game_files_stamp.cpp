#include "app/host/game_files_stamp.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace ps2r::app {

std::string GameFilesStamp::Path(const std::string& game_files_dir) {
    return (game_files_dir.empty() ? std::string(".") : game_files_dir) + "/" + kFileName;
}

GameFilesStamp::Check GameFilesStamp::Verify(const std::string& game_files_dir, const std::string& build_id) {
    Check c;
    c.path = Path(game_files_dir);
    std::ifstream in(c.path);
    if (!in.is_open()) return c;   // Absent
    std::string line;
    if (!std::getline(in, line)) {
        c.state = State::Unreadable;
        return c;
    }
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    c.stamped = line;
    c.state = line == build_id ? State::Match : State::Mismatch;
    return c;
}

bool GameFilesStamp::Write(const std::string& game_files_dir, const std::string& build_id) {
    std::ofstream out(Path(game_files_dir), std::ios::trunc);
    if (!out.is_open()) return false;
    out << build_id << '\n';
    return static_cast<bool>(out);
}

std::string GameFilesStamp::MetaValue(const Check& check) {
    switch (check.state) {
        case State::Absent: return "absent";
        case State::Match: return "ok";
        case State::Mismatch: return "mismatch:" + check.stamped;
        case State::Unreadable: return "unreadable";
    }
    return "absent";
}

std::string GameFilesStamp::Warning(const Check& check, const std::string& build_id) {
    if (check.state != State::Mismatch) return {};
    std::ostringstream o;
    o << "game files under " << check.path << " were extracted by build " << check.stamped
      << " but this binary is build " << build_id << ": re-extract the disc image if files are missing or stale";
    return o.str();
}

}  // namespace ps2r::app

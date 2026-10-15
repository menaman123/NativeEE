#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::app {

// Pure listing of reproduction/savestate files for the menu browser: no SDL,
// ImGui or GL. Scans one directory, keeps `.ps2st` files only (the atomic
// writer's `.tmp` transients and other entries are excluded), decodes the
// frozen 32-byte header for validity/frame and sorts newest first. The scan
// runs on the UI owner when the menu opens or the user refreshes; it never
// touches game state or the restore path itself.
class SavestateRows {
public:
    struct Row {
        std::string name;        // file name only
        std::string path;        // directory/name
        uint64_t size = 0;       // bytes
        int64_t mtime = 0;       // seconds since the epoch
        bool valid = false;      // magic + readable version
        uint32_t frame = 0;      // header vsync_frame when valid
        uint64_t wall_ms = 0;    // header wall clock when valid
    };
    static constexpr size_t kMaxRows = 256;

    // Case-insensitive `.ps2st` exactly at the end (so `.ps2st.tmp` is excluded).
    static bool IsSavestateName(const std::string& name);
    // 32-byte header decode; false for a short buffer, wrong magic or version.
    static bool DecodeHeader(const unsigned char* bytes, size_t length, uint32_t& frame, uint64_t& wall_ms);
    // Directory scan, bounded to kMaxRows entries; a missing directory yields
    // no rows and a false result with `error` set.
    bool Scan(const std::string& directory, std::string& error);
    const std::vector<Row>& Rows() const { return rows_; }
    const std::string& Directory() const { return directory_; }
    bool Scanned() const { return scanned_; }
    bool Truncated() const { return truncated_; }
    // Newest first by mtime, then by name, deterministic for equal stamps.
    static void SortNewestFirst(std::vector<Row>& rows);

private:
    std::vector<Row> rows_;
    std::string directory_;
    bool scanned_ = false;
    bool truncated_ = false;
};

}  // namespace ps2r::app

#include "app/gui/savestate_rows.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "engine/core/savestate/savestate_format.h"

namespace ps2r::app {

bool SavestateRows::IsSavestateName(const std::string& name) {
    static const char kExtension[] = ".ps2st";
    const size_t n = name.size(), m = sizeof(kExtension) - 1;
    if (n <= m) return false;
    for (size_t i = 0; i < m; ++i)
        if (std::tolower(static_cast<unsigned char>(name[n - m + i])) != kExtension[i]) return false;
    return true;
}

bool SavestateRows::DecodeHeader(const unsigned char* bytes, size_t length, uint32_t& frame, uint64_t& wall_ms) {
    if (!bytes || length < sizeof(savestate::FileHeader)) return false;
    savestate::FileHeader header;
    std::memcpy(&header, bytes, sizeof(header));
    if (std::memcmp(header.magic, savestate::kMagic, sizeof(savestate::kMagic)) != 0) return false;
    if (header.version < savestate::kMinFileVersion || header.version > savestate::kMaxFileVersion) return false;
    frame = header.vsync_frame;
    wall_ms = header.wall_ms;
    return true;
}

void SavestateRows::SortNewestFirst(std::vector<Row>& rows) {
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.mtime != b.mtime) return a.mtime > b.mtime;
        return a.name < b.name;
    });
}

bool SavestateRows::Scan(const std::string& directory, std::string& error) {
    rows_.clear();
    directory_ = directory;
    scanned_ = true;
    truncated_ = false;
    error.clear();
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        error = directory.empty() ? "No reproduction folder is configured." : directory + ": " + ec.message();
        return false;
    }
    for (const auto& entry : it) {
        if (rows_.size() >= kMaxRows) { truncated_ = true; break; }
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error) || entry_error) continue;
        Row row;
        row.name = entry.path().filename().string();
        if (!IsSavestateName(row.name)) continue;
        row.path = entry.path().string();
        row.size = static_cast<uint64_t>(entry.file_size(entry_error));
        if (entry_error) row.size = 0;
        const auto stamp = entry.last_write_time(entry_error);
        if (!entry_error) {
            const auto since_epoch = stamp.time_since_epoch();
            row.mtime = std::chrono::duration_cast<std::chrono::seconds>(since_epoch).count();
        }
        unsigned char header[sizeof(savestate::FileHeader)];
        if (std::FILE* file = std::fopen(row.path.c_str(), "rb")) {
            const size_t read = std::fread(header, 1, sizeof(header), file);
            std::fclose(file);
            row.valid = DecodeHeader(header, read, row.frame, row.wall_ms);
        }
        rows_.push_back(std::move(row));
    }
    SortNewestFirst(rows_);
    return true;
}

}  // namespace ps2r::app

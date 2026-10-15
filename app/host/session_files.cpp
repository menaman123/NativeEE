#include "app/host/session_files.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace ps2r::app {
namespace fs = std::filesystem;

SessionFiles::SessionFiles(std::string frame_directory)
    : frame_root_(std::move(frame_directory)), frame_directory_(frame_root_) {}

[[noreturn]] void SessionFiles::Fail(const std::string& error) {
    error_ = error;
    throw std::runtime_error(error_);
}

void SessionFiles::Select(const std::string& leg_directory, uint64_t segment) {
    if (!error_.empty()) throw std::runtime_error(error_);
    if (selected_ && segment == segment_ && leg_directory == leg_directory_) return;
    if (selected_ && (segment <= segment_ || (!leg_directory.empty() && leg_directory == leg_directory_)))
        Fail("Recording output cannot reuse a preceding epoch.");
    std::string next_leg = leg_directory, next_frames = frame_root_;
    fs::path operations_path, draws_path;
    File operations(nullptr, std::fclose), draws(nullptr, std::fclose);
    bool owned_frames = false, owned_operations = false, owned_draws = false;
    try {
        // A restore can precede the first readable/persisted frame. Directory
        // ownership cannot depend on DumpFrame having happened earlier.
        if (!frame_root_.empty()) {
            std::error_code error;
            fs::create_directories(frame_root_, error);
            if (error || !fs::is_directory(frame_root_))
                Fail("Cannot prepare frame-dump directory: " + frame_root_);
        }
        if (segment && !frame_root_.empty()) {
            char name[48];
            std::snprintf(name, sizeof(name), "output-%06llu", static_cast<unsigned long long>(segment));
            next_frames = (fs::path(frame_root_) / name).string();
            std::error_code error;
            if (!fs::create_directory(next_frames, error))
                Fail("Cannot reserve a distinct frame-dump directory: " + next_frames);
            owned_frames = true;
        }
        if (!leg_directory.empty()) {
            operations_path = fs::path(leg_directory) / "gsops.ohash";
            draws_path = fs::path(leg_directory) / "drawir.ohash";
            operations.reset(std::fopen(operations_path.string().c_str(), segment ? "wbx" : "wb"));
            if (!operations) Fail("Cannot open graphics diagnostic output: " + operations_path.string());
            owned_operations = segment != 0;
            draws.reset(std::fopen(draws_path.string().c_str(), segment ? "wbx" : "wb"));
            if (!draws) Fail("Cannot open graphics diagnostic output: " + draws_path.string());
            owned_draws = segment != 0;
        }
        // Each preceding frame was flushed. Still report close errors; once
        // close starts, an output failure cannot resume the preceding epoch.
        const bool operations_closed = !operations_ || std::fclose(operations_.release()) == 0;
        const bool draws_closed = !draws_ || std::fclose(draws_.release()) == 0;
        if (!operations_closed || !draws_closed) Fail("Cannot close preceding graphics diagnostic output.");
        operations_ = std::move(operations);
        draws_ = std::move(draws);
        frame_directory_.swap(next_frames);
        leg_directory_.swap(next_leg);
        segment_ = segment;
        selected_ = true;
    } catch (const std::exception& error) {
        operations.reset(); draws.reset();
        std::error_code ignored;
        if (owned_operations) fs::remove(operations_path, ignored);
        if (owned_draws) fs::remove(draws_path, ignored);
        if (owned_frames) fs::remove(next_frames, ignored); // never removes foreign children
        if (error_.empty()) error_ = error.what();
        throw;
    }
}

void SessionFiles::WriteHashes(uint64_t boundary, uint64_t operations, uint64_t draws) {
    if (!error_.empty()) throw std::runtime_error(error_);
    if (!selected_) Fail("Select the recording output before writing a frame.");
    if (leg_directory_.empty()) return;
    const auto write = [boundary](FILE* file, uint64_t value) {
        return file && std::fprintf(file, "%llu,0x%016llx\n",
            static_cast<unsigned long long>(boundary), static_cast<unsigned long long>(value)) >= 0 &&
            std::fflush(file) == 0;
    };
    if (!write(operations_.get(), operations) || !write(draws_.get(), draws))
        Fail("Cannot write graphics diagnostic output.");
}

} // namespace ps2r::app

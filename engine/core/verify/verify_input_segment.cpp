#include "engine/core/verify/verify_host.h"

#include <cerrno>
#include <exception>
#include <limits>

namespace ps2r::verify {

std::string VerifyHost::PadPath() const {
    return pad_name_.find('/') != std::string::npos ? pad_name_ : LegPath(pad_name_.c_str());
}

std::string VerifyHost::InputPath() const { return RecordingInput() ? PadPath() : std::string(); }

bool VerifyHost::BeginInputSegment(uint64_t restored_tick, std::string* err) {
    auto fail = [&](const std::string& why) { if (err) *err = why; return false; };
    if (!RecordingInput()) return fail("No active input recorder to segment.");
    if (restored_tick > std::numeric_limits<uint32_t>::max())
        return fail("Restored input tick exceeds the recording grammar.");
    // Reserve before flushing/changing the current recorder. Exclusive create
    // protects earlier segments even when this output directory is reused.
    FILE* next = nullptr;
    uint64_t segment = pad_segment_;
    std::string name, path;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        if (segment == std::numeric_limits<uint64_t>::max()) return fail("Input segment identity exhausted.");
        char suffix[48];
        std::snprintf(suffix, sizeof(suffix), "input-%06llu.padlog", static_cast<unsigned long long>(++segment));
        name = suffix;
        path = LegPath(name.c_str());
        next = std::fopen(path.c_str(), "wx");
        if (next) break;
        if (errno != EEXIST) return fail("Cannot create input segment: " + path);
    }
    if (!next) return fail("No unused input segment name within the reservation budget.");
    auto abandon = [&] { std::fclose(next); std::remove(path.c_str()); };
    if (std::fprintf(next, "# restore_tick=%llu previous_segment=%s\n",
                     static_cast<unsigned long long>(restored_tick), pad_name_.c_str()) < 0 ||
        std::fflush(next) != 0) {
        abandon();
        return fail("Cannot write input segment provenance.");
    }
    try { FlushInput(); } catch (const std::exception& error) {
        abandon();
        return fail(error.what());
    }
    std::lock_guard<std::mutex> lock(pad_mu_);
    const int closed = pad_f_ ? std::fclose(pad_f_) : EOF;
    pad_f_ = nullptr;
    if (closed != 0) {
        abandon();
        return fail("Cannot close preceding input segment.");
    }
    pad_f_ = next;
    pad_name_ = name;
    pad_segment_ = segment;
    pad_ = {};
    pad_seen_ = false;
    pad_last_vf_ = pad_flushed_vf_ = 0;
    pad_lines_ = 0;
    meta_->Set("pad_record_segment", std::to_string(segment));
    meta_->Set("pad_record_path", name);
    meta_->Set("pad_record_restore_tick", std::to_string(restored_tick));
    meta_->Set("pad_record_start", "pending");
    meta_->Set("pad_record_last", "pending");
    meta_->Set("pad_record_lines", "0");
    return true;
}

} // namespace ps2r::verify

#include "engine/core/verify/verify_host.h"

#include <array>
#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace ps2r::verify {

bool VerifyHost::BeginOutputSegment(uint64_t restored_frame, uint64_t restored_tick, std::string* err) {
    bool completing_previous = false;
    auto fail = [&](const std::string& why) {
        if (completing_previous) {
            output_error_ = why;
            meta_->Set("output_phase", "failed");
            meta_->Set("output_error", why);
            // The original failure can itself be a metadata I/O error.
            // Retain the in-memory reason even if that destination stays bad.
            meta_->Flush();
        }
        if (err) *err = why;
        return false;
    };
    if (OutputFailed()) return fail("Output is closed after a failed restore transition: " + output_error_);
    if (finalized_) return fail("Completed output cannot begin a restore segment.");
    if (restored_frame > UINT32_MAX || (record_input_ && restored_tick > UINT32_MAX))
        return fail("Restored frame or input tick exceeds the output grammar.");
    if (output_segment_ == std::numeric_limits<uint64_t>::max())
        return fail("Output segment identity exhausted.");
    if (!armed_) {
        ++output_segment_;
        segment_boundary_ = n_boundaries_;
        segment_restore_frame_ = static_cast<uint32_t>(restored_frame);
        return true;
    }
    uint64_t segment = output_segment_;
    std::filesystem::path directory;
    bool reserved = false;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        if (segment == std::numeric_limits<uint64_t>::max())
            return fail("Output segment identity exhausted.");
        char name[48];
        std::snprintf(name, sizeof(name), "output-%06llu", static_cast<unsigned long long>(++segment));
        directory = std::filesystem::path(output_root_) / name;
        std::error_code error;
        if (std::filesystem::create_directory(directory, error)) { reserved = true; break; }
        if (error && error != std::errc::file_exists)
            return fail("Cannot reserve output segment: " + error.message());
    }
    if (!reserved) return fail("No unused output directory within the reservation budget.");

    constexpr std::array<const char*, 4> names{{"frames.fhash", "audio.ahash", "mem.mhash", "input.padlog"}};
    std::array<FILE*, 4> files{};
    std::array<bool, 4> owned{};
    bool owns_meta = false;
    auto abandon = [&] {
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i]) { std::fclose(files[i]); files[i] = nullptr; }
            std::error_code ignored;
            if (owned[i]) std::filesystem::remove(directory / names[i], ignored);
        }
        std::error_code ignored;
        if (owns_meta) std::filesystem::remove(directory / "run.meta", ignored);
        std::filesystem::remove(directory, ignored); // never removes foreign children
    };
    try {
        for (size_t i = 0; i < (record_input_ ? 4u : 3u); ++i) {
            files[i] = std::fopen((directory / names[i]).string().c_str(), "wx");
            if (!files[i]) { abandon(); return fail("Cannot create output segment lane."); }
            owned[i] = true;
        }
        FILE* stamp = std::fopen((directory / "run.meta").string().c_str(), "wx");
        if (!stamp) { abandon(); return fail("Cannot reserve output segment provenance."); }
        owns_meta = true;
        if (std::fclose(stamp) != 0) { abandon(); return fail("Cannot close output provenance reservation."); }
        auto next_meta = meta_->CopyForSegment((directory / "run.meta").string());
        next_meta->Set("output_segment", std::to_string(segment));
        next_meta->Set("output_previous", leg_dir_);
        next_meta->Set("output_next", "none");
        next_meta->Set("output_restore_frame", std::to_string(restored_frame));
        next_meta->Set("output_restore_tick", std::to_string(restored_tick));
        next_meta->Set("output_process_boundary", std::to_string(n_boundaries_));
        next_meta->Set("output_phase", "preparing");
        if (record_input_) {
            const std::string predecessor = InputPath();
            if (std::fprintf(files[3], "# restore_tick=%llu previous_segment=%s\n",
                    static_cast<unsigned long long>(restored_tick), predecessor.c_str()) < 0 ||
                std::fflush(files[3]) != 0) {
                abandon(); return fail("Cannot write restored input provenance.");
            }
            next_meta->Set("pad_record_segment", "0");
            next_meta->Set("pad_record_path", "input.padlog");
            next_meta->Set("pad_record_restore_tick", std::to_string(restored_tick));
            next_meta->Set("pad_record_start", "pending");
            next_meta->Set("pad_record_last", "pending");
            next_meta->Set("pad_record_lines", "0");
        }
        if (!next_meta->Flush()) { abandon(); return fail("Cannot write output segment provenance."); }
        // Allocate the next hasher before completing the old epoch. Its sink
        // resolves this owner's current lane only after publication below.
        auto next_audio = std::make_unique<AudioHasher>([this](uint64_t window, uint64_t hash) {
            if (std::fprintf(audio_f_, "%llu,0x%016llx\n",
                    static_cast<unsigned long long>(window), static_cast<unsigned long long>(hash)) < 0 ||
                std::fflush(audio_f_) != 0) throw std::runtime_error("Cannot write restored audio hash lane.");
        });
        completing_previous = true;
        audio_->Finish();
        FlushInput();
        for (FILE* file : {frames_f_, audio_f_, mem_f_})
            if (!file || std::fflush(file) != 0 || std::ferror(file)) {
                abandon(); return fail("Cannot finish preceding output lanes.");
            }
        meta_->Set("output_next", directory.string());
        meta_->Finalize();
        if (!meta_->Flush()) { abandon(); return fail("Cannot finish preceding output provenance."); }
        bool closed = true;
        for (FILE** file : {&frames_f_, &audio_f_, &mem_f_, &pad_f_}) {
            if (*file && std::fclose(*file) != 0) closed = false;
            *file = nullptr;
        }
        if (!closed) { abandon(); return fail("Cannot close preceding output lanes."); }
        next_meta->Set("output_phase", "awaiting_resume");
        if (!next_meta->Flush()) { abandon(); return fail("Cannot publish restored output provenance."); }
        frames_f_ = files[0]; audio_f_ = files[1]; mem_f_ = files[2]; pad_f_ = files[3];
        files.fill(nullptr);
        meta_ = std::move(next_meta);
        audio_ = std::move(next_audio);
        leg_dir_ = directory.string();
        output_segment_ = segment;
        segment_boundary_ = n_boundaries_;
        segment_restore_frame_ = static_cast<uint32_t>(restored_frame);
        pad_ = {};
        pad_seen_ = false;
        pad_last_vf_ = pad_flushed_vf_ = 0;
        pad_lines_ = pad_segment_ = 0;
        pad_name_ = "input.padlog";
        return true;
    } catch (const std::exception& error) {
        abandon();
        return fail(error.what());
    }
}

} // namespace ps2r::verify

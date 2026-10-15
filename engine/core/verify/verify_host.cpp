// core/verify/verify_host.cpp
#include "engine/core/verify/verify_host.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>

#include "engine/core/config/config.h"
#include "engine/core/hash/hash.h"
#include "engine/core/verify/frame_hasher.h"

namespace ps2r::verify {

VerifyHost::~VerifyHost() {
    if (frames_f_) std::fclose(frames_f_);
    if (audio_f_) std::fclose(audio_f_);
    if (mem_f_) std::fclose(mem_f_);
    if (pad_f_) std::fclose(pad_f_);
}

std::string VerifyHost::LegPath(const char* name) const {
    return leg_dir_.empty() ? std::string() : leg_dir_ + "/" + name;
}

uint32_t VerifyHost::PresentKey(uint32_t boot_restore_frame, uint64_t boundary) const {
    if (OutputFailed()) throw std::runtime_error(output_error_);
    if (output_segment_) {
        if (boundary < segment_boundary_)
            throw std::runtime_error("Presentation belongs to a preceding output segment.");
        const uint64_t local = boundary - segment_boundary_;
        if (local >= uint64_t{UINT32_MAX} - segment_restore_frame_)
            throw std::runtime_error("Restored presentation exceeds the frame key grammar.");
        return segment_restore_frame_ + static_cast<uint32_t>(local) + 1;
    }
    // Existing boot anchors keep key 1 for their first present, then
    // boot_restore_frame + boundary. Cold boot uses boundary + 1 throughout.
    if (boot_restore_frame && boundary) {
        if (boundary > uint64_t{UINT32_MAX} - boot_restore_frame)
            throw std::runtime_error("Boot presentation exceeds the frame key grammar.");
        return boot_restore_frame + static_cast<uint32_t>(boundary);
    }
    if (boundary >= UINT32_MAX) throw std::runtime_error("Presentation exceeds the frame key grammar.");
    return static_cast<uint32_t>(boundary) + 1;
}

std::FILE* VerifyHost::OpenLeg(const char* name) const {
    const std::string p = LegPath(name);
    return p.empty() ? nullptr : std::fopen(p.c_str(), "w");
}

bool VerifyHost::Arm(const Config& cfg, const std::string& build_id, const std::string& title, std::string* err,
                     const std::vector<FingerprintEntry>& extra) {
    if (cfg.harness.out_dir.empty()) return true;   // harness off
    leg_dir_ = cfg.harness.out_dir;
    output_root_ = leg_dir_;
    std::error_code ec;
    std::filesystem::create_directories(leg_dir_, ec);
    if (ec && !std::filesystem::is_directory(leg_dir_)) {
        if (err) *err = "cannot create harness leg dir " + leg_dir_ + ": " + ec.message();
        return false;
    }
    title_ = title;
    record_input_ = cfg.harness.record_input;
    if (record_input_) {
        if (!cfg.harness.record_input_path.empty()) {
            // An explicit destination never overwrites an earlier session's
            // recording: exclusive create, parent directory created on demand.
            const std::filesystem::path explicit_path(cfg.harness.record_input_path);
            if (explicit_path.has_parent_path()) std::filesystem::create_directories(explicit_path.parent_path(), ec);
            pad_name_ = explicit_path.string();
            if (pad_name_.find('/') == std::string::npos) pad_name_ = "./" + pad_name_;
            pad_f_ = std::fopen(pad_name_.c_str(), "wx");
        } else {
            pad_f_ = OpenLeg("input.padlog");
        }
        if (!pad_f_) {
            if (err) *err = "cannot create input recording " + PadPath() + " (an existing file is never overwritten)";
            return false;
        }
    }
    RunMeta::Identity id;
    id.build_id = build_id;
    id.tree = "new";
    id.profile = cfg.meta.profile;
    id.env_ps2 = cfg.Fingerprint();
    id.env_hash = cfg.FingerprintHash();
    // The extra entries continue the same fold (`key=value` then ";") so a
    // leg from a different decoder build fails the same-engine env_hash gate.
    for (const FingerprintEntry& e : extra) {
        const std::string entry = e.key + "=" + e.value;
        id.env_hash = hash::Fnv1a64(entry.data(), entry.size(), id.env_hash);
        id.env_hash = hash::Fnv1a64(";", 1, id.env_hash);
    }
    meta_ = std::make_unique<RunMeta>(LegPath("run.meta"), id);
    for (const FingerprintEntry& e : extra) meta_->Set(e.key, e.value);
    if (record_input_) {
        meta_->Set("pad_record_path", pad_name_);
        meta_->Set("pad_record_clock", triage::InputLog::ClockToken(kRecordClock));
    }
    frames_f_ = OpenLeg("frames.fhash");
    audio_f_ = OpenLeg("audio.ahash");
    mem_f_ = OpenLeg("mem.mhash");
    audio_ = std::make_unique<AudioHasher>([this](uint64_t win, uint64_t h) {
        if (audio_f_) {
            std::fprintf(audio_f_, "%llu,0x%016llx\n", (unsigned long long)win, (unsigned long long)h);
            std::fflush(audio_f_);
        }
    });
    armed_ = true;
    return true;
}

void VerifyHost::OnPresent(uint32_t vf, const uint8_t* rgba_bottomup, int pw, int ph, int scale,
                           const char* renderer) {
    if (OutputFailed()) throw std::runtime_error(output_error_);
    if (!armed_) return;
    OnPresentHashed(vf, FrameHasher::HashNativeScale(rgba_bottomup, pw, ph, scale), renderer);
}

void VerifyHost::OnPresentHashed(uint32_t vf, uint64_t hash, const char* renderer) {
    if (OutputFailed()) throw std::runtime_error(output_error_);
    if (!armed_) return;
    if (output_segment_ && meta_->Frames() == 0) meta_->Set("output_phase", "active");
    meta_->OnPresent(renderer);
    if (frames_f_) {
        std::fprintf(frames_f_, "%u,0x%016llx\n", vf, (unsigned long long)hash);
        std::fflush(frames_f_);
    }
}

uint64_t VerifyHost::OnBoundary(const float* mix, int mix_frames, const std::vector<MemorySpan>& spans) {
    if (OutputFailed()) throw std::runtime_error(output_error_);
    const uint64_t n = n_boundaries_++;
    if (!armed_) return n;
    if (mix && mix_frames > 0) audio_->Push(mix, mix_frames);
    uint64_t mh = 0;
    const uint64_t local = n - segment_boundary_;
    if (mem_lane_ && mem_.MaybeHash((uint32_t)local, spans, &mh) && mem_f_) {
        std::fprintf(mem_f_, "%llu,0x%016llx\n", (unsigned long long)local, (unsigned long long)mh);
        std::fflush(mem_f_);
    }
    return n;
}

void VerifyHost::SetMemLane(bool on) {
    mem_lane_ = on;
    if (meta_) meta_->Set("mem_lane", on ? "on" : "off");
}

void VerifyHost::OnPadServe(uint32_t vf, uint16_t wire_buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry) {
    if (OutputFailed()) throw std::runtime_error(output_error_);
    if (!armed_ || !record_input_ || finalized_) return;
    std::lock_guard<std::mutex> lk(pad_mu_);
    if (!pad_f_) throw std::runtime_error("input recorder has no writable segment");
    if (pad_seen_ && vf < pad_last_vf_) {
        throw std::runtime_error("input recording clock moved backwards; start a new recording after loading a state");
    }
    if (!pad_seen_) {
        if (std::fprintf(pad_f_, "%s\n", triage::InputLog::HeaderLine(vf, title_, kRecordClock).c_str()) < 0 ||
            std::fflush(pad_f_) != 0)
            throw std::runtime_error("cannot write input recording header");
        pad_flushed_vf_ = vf;
        pad_seen_ = true;
        meta_->Set("pad_record_start", std::to_string(vf));
    }
    const uint16_t active_high = (uint16_t)~wire_buttons;
    const uint8_t axes[4] = {lx, ly, rx, ry};
    std::vector<std::string> lines;
    pad_.Feed(vf, active_high, axes, &lines);
    // Bound the unflushed held-input tail of an interrupted play session.
    if (vf - pad_flushed_vf_ >= 60) {
        pad_.Flush(vf, &lines);
        pad_flushed_vf_ = vf;
    }
    pad_last_vf_ = vf;
    WriteInputLines(lines);
}

void VerifyHost::WriteInputLines(const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
        if (std::fprintf(pad_f_, "%s\n", line.c_str()) < 0)
            throw std::runtime_error("cannot write input recording");
    }
    if (!lines.empty() && std::fflush(pad_f_) != 0)
        throw std::runtime_error("cannot flush input recording");
    pad_lines_ += lines.size();
    meta_->Set("pad_record_lines", std::to_string(pad_lines_));
    meta_->Set("pad_record_last", std::to_string(pad_last_vf_));
}

void VerifyHost::FlushInput() {
    if (OutputFailed()) throw std::runtime_error(output_error_);
    if (!armed_ || !record_input_) return;
    std::lock_guard<std::mutex> lk(pad_mu_);
    if (!pad_seen_ || !pad_f_) return;
    std::vector<std::string> lines;
    pad_.Flush(pad_last_vf_, &lines);
    pad_flushed_vf_ = pad_last_vf_;
    WriteInputLines(lines);
}

void VerifyHost::Finalize() {
    if (!armed_ || finalized_) return;
    finalized_ = true;
    if (OutputFailed()) {
        // Do not retry a partial tail or write through streams that the
        // failed transition already closed. The failure remains observable.
        meta_->Flush();
        return;
    }
    audio_->Finish();
    FlushInput();
    meta_->Finalize();
}

}  // namespace ps2r::verify

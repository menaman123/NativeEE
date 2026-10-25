#include "app/host/checkpoint_capture.h"

#include <cstdio>
#include <filesystem>
#include <stdexcept>

#include "app/host/session_clock.h"
#include "engine/core/savestate/quiescence.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/savestate/state_restorer.h"

namespace ps2r::app {
namespace {
void WriteFile(const std::string& path, const uint8_t* data, size_t size) {
    const std::string temporary = path + ".tmp";
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) throw std::runtime_error("Cannot create boundary capture: " + path);
    const bool written = std::fwrite(data, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;
    std::error_code ec;   // std::filesystem::rename replaces an existing file on Windows too; std::rename does not
    if (written && closed) std::filesystem::rename(temporary, path, ec);
    if (!written || !closed || ec) {
        std::remove(temporary.c_str());
        throw std::runtime_error("Cannot publish boundary capture: " + path);
    }
}
}  // namespace

void CheckpointCapture::Write(const std::string& directory, uint64_t boundary, uint64_t game_frame,
                              const MainMemory& memory, const StateRestorer& restorer,
                              savestate::Quiescence& quiescence) {
    const std::string suffix = "_b" + std::to_string(boundary);
    // Capture on the game owner immediately after its memory hash, before any
    // save preparation can drain work or otherwise change the inspected data.
    WriteFile(directory + "/memory" + suffix + ".bin", memory.Data(), memory.Size());
    std::string owner, reason;
    const std::string state_path = directory + "/state" + suffix + ".ps2st";
    const std::string refusal_path = directory + "/state" + suffix + ".refused";
    savestate::FileMeta meta;
    const bool frame_valid = SessionClock::SetFrame(meta, game_frame, reason);
    if (!frame_valid) owner = "logical clock";
    if (!frame_valid || !quiescence.Check(&owner, &reason)) {
        // A reused harness directory must never expose an older successful
        // snapshot under the current refused boundary's filename.
        std::remove(state_path.c_str());
        const std::string text = owner + ": " + reason + "\n";
        WriteFile(refusal_path, reinterpret_cast<const uint8_t*>(text.data()), text.size());
        return;
    }
    const auto image = restorer.SaveImage(meta);
    WriteFile(state_path, image.data(), image.size());
    std::remove(refusal_path.c_str());
}

}  // namespace ps2r::app

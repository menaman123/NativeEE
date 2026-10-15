#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace ps2r::app {

// Product output files, used by the serialized game owner or joined
// supervisor. Hash values remain process-wide diagnostics; this owner changes
// destinations only, never resets or executes a graphics producer.
class SessionFiles {
public:
    explicit SessionFiles(std::string frame_directory);
    // Select the actual VerifyHost output epoch before its first frame.
    // Fresh epochs reserve exclusive files/directories. Failure is terminal;
    // earlier artifacts are preserved and further writes throw.
    void Select(const std::string& leg_directory, uint64_t segment);
    void WriteHashes(uint64_t boundary, uint64_t operations, uint64_t draws);
    const std::string& FrameDirectory() const { return frame_directory_; }

private:
    using File = std::unique_ptr<FILE, int (*)(FILE*)>;
    [[noreturn]] void Fail(const std::string& error);
    const std::string frame_root_;
    std::string frame_directory_, leg_directory_, error_;
    uint64_t segment_ = 0;
    bool selected_ = false;
    File operations_{nullptr, std::fclose}, draws_{nullptr, std::fclose};
};

} // namespace ps2r::app

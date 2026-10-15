#pragma once
// Iso9660Image -- a synthetic ISO9660 (ECMA-119 level 1) volume writer for
// tests: a flat root directory of files (plus optional one-level
// subdirectories), written as a plain 2048-byte-per-sector image or as a raw
// 2352-byte-per-sector (.bin, Mode 1) image. Never a real disc: the Root pin
// RootFirstRunExtract and the DiscVerify unit vectors build their fake
// SLUS_123.45 with it, so no test ever reads the user's disc image.
#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::testlib {

class Iso9660Image {
public:
    // `path` is "NAME.EXT" (root) or "DIR/NAME.EXT" (one level; the directory
    // is created on first use). Identifiers are upper-cased as recorded;
    // files get the ";1" version suffix.
    void AddFile(const std::string& path, const std::vector<uint8_t>& bytes);
    void SetVolumeId(const std::string& id) { volume_id_ = id; }

    // Writes the image. raw2352 = every sector carries a Mode 1 raw header
    // (sync, MSF, mode byte) and a zero EDC/ECC trailer. False on I/O error.
    bool Write(const std::string& out_path, bool raw2352, std::string* error = nullptr) const;

    // The logical image bytes (sectors * 2048) a Write would produce.
    uint64_t LogicalBytes() const;

private:
    struct File {
        std::string dir;    // "" for the root
        std::string name;   // as recorded, without ";1"
        std::vector<uint8_t> bytes;
    };
    std::vector<File> files_;
    std::string volume_id_ = "SYNTHETIC";
    std::vector<uint8_t> Build() const;
};

}  // namespace ps2r::testlib

// core/hash/sha256.h -- SHA-256 (FIPS 180-4), our own implementation.
//
// The disc verification hash (CHARTER 2 C2): the title ELF read out of the
// user's disc image is hashed and compared with the ELF the build recompiled
// (TitleProfile::elf_sha256). Streaming: Update over any number of chunks,
// then Finish; Hex() renders the 32 digest bytes as 64 lower-case hex digits.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ps2r {
namespace hash {

class Sha256 {
public:
    static constexpr size_t kDigestBytes = 32;

    Sha256();
    void Update(const void* data, size_t len);
    // Finalizes into `digest` (32 bytes). The object is consumed; call Reset to reuse.
    void Finish(uint8_t digest[kDigestBytes]);
    void Reset();

    // Convenience: the hex digest of one buffer.
    static std::string HexOf(const void* data, size_t len);
    static std::string Hex(const uint8_t digest[kDigestBytes]);

private:
    void Block(const uint8_t block[64]);
    uint32_t state_[8];
    uint8_t buffer_[64];
    size_t buffered_ = 0;
    uint64_t total_ = 0;
};

}  // namespace hash
}  // namespace ps2r

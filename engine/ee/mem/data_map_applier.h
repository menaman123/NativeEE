#pragma once
// Applies a title's initial-RAM data map onto main memory. Grammar (one entry
// per line): `<hex address>:<0x-prefixed hex value>` with optional `#`
// comments; the value's hex digits are the BIG-endian spelling of the bytes,
// written LITTLE-endian from the address (0x002F3678 at A stores 78 36 2F 00),
// so an entry's byte count is half its digit count. Bytes past the end of RAM
// are skipped. The same grammar serves the post-load patch list.
#include <cstddef>
#include <cstdint>
#include <string>

class MainMemory;

class DataMapApplier {
public:
    struct Entry {
        uint32_t address;
        uint32_t byte_count;
        uint64_t value;  // little-endian numeric value of the bytes
    };

    struct Result {
        size_t entries = 0;
        size_t bytes_written = 0;
        size_t malformed_lines = 0;
        uint32_t lowest_address = 0;
        uint32_t highest_address_end = 0;  // exclusive
    };

    // Parse one map line. Returns false for blank/comment/malformed lines
    // (`*malformed` tells the two apart).
    static bool ParseLine(const std::string& line, Entry& out, bool* malformed);

    // Apply every entry of `text` (the file contents) to RAM.
    static Result ApplyText(const std::string& text, MainMemory& ram);

    // Apply the file at `path`; false when it cannot be read.
    static bool ApplyFile(const std::string& path, MainMemory& ram, Result& out, std::string* err);
};

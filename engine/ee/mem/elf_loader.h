#pragma once
// Loads a PS2 EE executable (ELF32, little-endian, EM_MIPS) into main RAM the
// way the BIOS loader does: every PT_LOAD segment's p_filesz bytes are copied
// to p_vaddr and the remaining p_memsz - p_filesz bytes are zero-filled. The
// entry point and the segment ranges are reported; nothing about the title
// (paths, addresses, section names) is known here.
#include <cstdint>
#include <string>
#include <vector>

class MainMemory;

class ElfLoader {
public:
    struct Segment {
        uint32_t vaddr;
        uint32_t file_offset;
        uint32_t filesz;
        uint32_t memsz;
        uint32_t flags;
    };

    struct Image {
        uint32_t entry = 0;
        std::vector<Segment> segments;  // PT_LOAD only, file order
        uint32_t lowest_vaddr = 0;
        uint32_t highest_vaddr_end = 0;  // exclusive
    };

    // Parse the program headers of an ELF image without touching memory.
    static bool Parse(const std::vector<uint8_t>& file, Image& out, std::string* err);

    // Parse + copy the PT_LOAD segments into RAM. Fails (and copies nothing)
    // when any segment falls outside the 32 MB.
    static bool Load(const std::vector<uint8_t>& file, MainMemory& ram, Image& out, std::string* err);

    static bool ReadFile(const std::string& path, std::vector<uint8_t>& out, std::string* err);
};

#include "engine/ee/mem/elf_loader.h"

#include <cstring>
#include <fstream>

#include "engine/ee/mem/main_memory.h"

namespace {

// ELF32 little-endian field offsets (System V ABI, Elf32_Ehdr / Elf32_Phdr).
constexpr size_t kEhdrSize = 52;
constexpr size_t kPhdrSize = 32;
constexpr uint32_t kPtLoad = 1;
constexpr uint16_t kEmMips = 8;

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool Fail(std::string* err, const char* what) {
    if (err) *err = what;
    return false;
}

}  // namespace

bool ElfLoader::ReadFile(const std::string& path, std::vector<uint8_t>& out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Fail(err, "cannot open ELF");
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n <= 0) return Fail(err, "empty ELF");
    out.resize(static_cast<size_t>(n));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<bool>(f) || Fail(err, "short read");
}

bool ElfLoader::Parse(const std::vector<uint8_t>& file, Image& out, std::string* err) {
    out = Image{};
    if (file.size() < kEhdrSize) return Fail(err, "file shorter than an ELF header");
    const uint8_t* e = file.data();
    if (std::memcmp(e, "\x7f"
                       "ELF",
                    4) != 0)
        return Fail(err, "bad ELF magic");
    if (e[4] != 1) return Fail(err, "not ELFCLASS32");
    if (e[5] != 1) return Fail(err, "not little-endian");
    if (Le16(e + 18) != kEmMips) return Fail(err, "not EM_MIPS");
    out.entry = Le32(e + 24);
    const uint32_t phoff = Le32(e + 28);
    const uint16_t phentsize = Le16(e + 42);
    const uint16_t phnum = Le16(e + 44);
    if (phentsize < kPhdrSize) return Fail(err, "program header entry too small");
    if (static_cast<uint64_t>(phoff) + static_cast<uint64_t>(phnum) * phentsize > file.size())
        return Fail(err, "program header table outside the file");

    bool any = false;
    for (uint16_t i = 0; i < phnum; ++i) {
        const uint8_t* p = e + phoff + static_cast<size_t>(i) * phentsize;
        if (Le32(p + 0) != kPtLoad) continue;
        Segment s{};
        s.file_offset = Le32(p + 4);
        s.vaddr = Le32(p + 8);
        s.filesz = Le32(p + 16);
        s.memsz = Le32(p + 20);
        s.flags = Le32(p + 24);
        if (s.filesz > s.memsz) return Fail(err, "PT_LOAD filesz > memsz");
        if (static_cast<uint64_t>(s.file_offset) + s.filesz > file.size()) return Fail(err, "PT_LOAD outside the file");
        const uint32_t end = s.vaddr + s.memsz;
        if (!any || s.vaddr < out.lowest_vaddr) out.lowest_vaddr = s.vaddr;
        if (!any || end > out.highest_vaddr_end) out.highest_vaddr_end = end;
        any = true;
        out.segments.push_back(s);
    }
    if (!any) return Fail(err, "no PT_LOAD segment");
    return true;
}

bool ElfLoader::Load(const std::vector<uint8_t>& file, MainMemory& ram, Image& out, std::string* err) {
    if (!Parse(file, out, err)) return false;
    for (const Segment& s : out.segments) {
        if (!ram.Pointer(MainMemory::Phys(s.vaddr), s.memsz)) return Fail(err, "PT_LOAD outside main RAM");
    }
    for (const Segment& s : out.segments) {
        uint8_t* dst = ram.Pointer(MainMemory::Phys(s.vaddr), s.memsz);
        std::memcpy(dst, file.data() + s.file_offset, s.filesz);
        std::memset(dst + s.filesz, 0, s.memsz - s.filesz);
    }
    return true;
}

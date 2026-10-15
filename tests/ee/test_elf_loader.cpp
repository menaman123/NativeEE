// ElfLoaderTitle: loads the frozen original's SLUS_123.45 read-only,
// checks the entry point and the PT_LOAD ranges, applies the title data map
// on top, and runs the RAM-image probe: for every data-map entry, do the
// ELF's own bytes at that address equal the map value? (PHASE3_PLAN 3.4.3)
// usage: test_elf_loader <elf> <ram_data_map> <post_load_patches>
// Exit 77 = SKIP (ELF absent).
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/ee/mem/data_map_applier.h"
#include "engine/ee/mem/elf_loader.h"
#include "engine/ee/mem/main_memory.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

// The title facts under test (runtime-environment.md; OLD memory.cpp:193-199).
constexpr uint32_t kEntry = 0x00100008u;
constexpr uint32_t kLoadLo = 0x00100000u;
constexpr uint32_t kFileHiEnd = 0x0030A0B8u;  // vaddr + p_filesz (the .sdata end)
constexpr uint32_t kMemHiEnd = 0x003DAE80u;   // vaddr + p_memsz (.bss zero-filled)
constexpr size_t kElfSize = 2151940;
constexpr uint32_t kMapLo = 0x002E6B80u;
constexpr uint32_t kMapHiEnd = 0x0030A0BBu;  // 3 four-byte entries at 0x30A0B5..B7 run past .sdata
constexpr size_t kMapEntries = 19281;

bool ReadText(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: test_elf_loader <elf> <ram_data_map> <post_load_patches>\n");
        return 2;
    }
    std::vector<uint8_t> file;
    std::string err;
    if (!ElfLoader::ReadFile(argv[1], file, &err)) {
        std::printf("SKIP: %s (%s)\n", argv[1], err.c_str());
        return 77;
    }
    CHECK(file.size() == kElfSize);

    // Parse + load.
    MainMemory ram;
    ElfLoader::Image img;
    CHECK(ElfLoader::Load(file, ram, img, &err));
    if (g_fail) {
        std::printf("load error: %s\n", err.c_str());
        return 1;
    }
    CHECK(img.entry == kEntry);
    CHECK(img.lowest_vaddr == kLoadLo);
    CHECK(img.highest_vaddr_end == kMemHiEnd);
    CHECK(img.segments.size() == 1 && img.segments[0].vaddr + img.segments[0].filesz == kFileHiEnd);
    std::printf("ELF: entry=0x%08x segments=%zu range=[0x%08x,0x%08x)\n", img.entry, img.segments.size(),
                img.lowest_vaddr, img.highest_vaddr_end);
    for (const auto& s : img.segments) {
        std::printf("  PT_LOAD vaddr=0x%08x off=0x%08x filesz=0x%x memsz=0x%x flags=%u\n", s.vaddr, s.file_offset,
                    s.filesz, s.memsz, s.flags);
        CHECK(s.vaddr >= kLoadLo && s.vaddr + s.memsz <= kMemHiEnd);
    }
    // The entry instruction is inside a loaded segment and non-zero.
    uint32_t entry_word = 0;
    std::memcpy(&entry_word, ram.Data() + kEntry, 4);
    CHECK(entry_word != 0);
    // Bytes outside every segment stay zero.
    CHECK(ram.Data()[kLoadLo - 4] == 0 && ram.Data()[kMemHiEnd + 0x100] == 0);
    // .bss is zero-filled from the file-backed end to memsz.
    CHECK(ram.Data()[kFileHiEnd + 0x10] == 0 && ram.Data()[kMemHiEnd - 4] == 0);

    // Snapshot the pure-ELF image over the map range, then apply the map.
    std::string map_text, patch_text;
    CHECK(ReadText(argv[2], map_text));
    CHECK(ReadText(argv[3], patch_text));

    // Probe: compare the ELF's bytes against each map entry BEFORE applying.
    size_t entries = 0, covered = 0, mismatched_entries = 0, mismatched_bytes = 0, outside = 0;
    size_t printed = 0;
    {
        std::istringstream in(map_text);
        std::string line;
        while (std::getline(in, line)) {
            DataMapApplier::Entry e{};
            bool malformed = false;
            if (!DataMapApplier::ParseLine(line, e, &malformed)) {
                CHECK(!malformed);
                continue;
            }
            ++entries;
            bool in_elf = false;
            for (const auto& s : img.segments) {
                if (e.address >= s.vaddr && e.address + e.byte_count <= s.vaddr + s.filesz) in_elf = true;
            }
            if (!in_elf) {
                ++outside;
                continue;
            }
            ++covered;
            bool diff = false;
            for (uint32_t i = 0; i < e.byte_count; ++i) {
                const uint8_t mapb = static_cast<uint8_t>((e.value >> (8u * i)) & 0xFFu);
                if (ram.Data()[e.address + i] != mapb) {
                    diff = true;
                    ++mismatched_bytes;
                }
            }
            if (diff) {
                ++mismatched_entries;
                if (printed < 12) {
                    uint64_t elfv = 0;
                    for (uint32_t i = 0; i < e.byte_count; ++i)
                        elfv |= static_cast<uint64_t>(ram.Data()[e.address + i]) << (8u * i);
                    std::printf("  MISMATCH 0x%08x: elf=0x%0*llx map=0x%0*llx\n", e.address, e.byte_count * 2,
                                static_cast<unsigned long long>(elfv), e.byte_count * 2,
                                static_cast<unsigned long long>(e.value));
                    ++printed;
                }
            }
        }
    }
    CHECK(entries == kMapEntries);
    std::printf("RAM-IMAGE PROBE: entries=%zu covered_by_elf=%zu outside_elf_filesz=%zu mismatched_entries=%zu mismatched_bytes=%zu\n",
                entries, covered, outside, mismatched_entries, mismatched_bytes);

    // Post-load patches vs the ELF.
    {
        std::istringstream in(patch_text);
        std::string line;
        while (std::getline(in, line)) {
            DataMapApplier::Entry e{};
            bool malformed = false;
            if (!DataMapApplier::ParseLine(line, e, &malformed)) continue;
            uint64_t elfv = 0;
            for (uint32_t i = 0; i < e.byte_count; ++i) elfv |= static_cast<uint64_t>(ram.Data()[e.address + i]) << (8u * i);
            std::printf("  PATCH 0x%08x: elf=0x%08llx patch=0x%08llx %s\n", e.address, static_cast<unsigned long long>(elfv),
                        static_cast<unsigned long long>(e.value), elfv == e.value ? "(same)" : "(DIFFERS)");
        }
    }

    // Apply map + patches; the applier's accounting matches the OLD loader.
    DataMapApplier::Result mr = DataMapApplier::ApplyText(map_text, ram);
    CHECK(mr.entries == kMapEntries);
    CHECK(mr.malformed_lines == 0);
    CHECK(mr.lowest_address == kMapLo);
    CHECK(mr.highest_address_end == kMapHiEnd);
    std::printf("data map applied: entries=%zu bytes=%zu range=[0x%08x,0x%08x)\n", mr.entries, mr.bytes_written,
                mr.lowest_address, mr.highest_address_end);

    // Superset property: after the map, every map entry reads back as the map value.
    {
        std::istringstream in(map_text);
        std::string line;
        size_t bad = 0;
        while (std::getline(in, line)) {
            DataMapApplier::Entry e{};
            if (!DataMapApplier::ParseLine(line, e, nullptr)) continue;
            for (uint32_t i = 0; i < e.byte_count; ++i) {
                if (ram.Data()[e.address + i] != static_cast<uint8_t>((e.value >> (8u * i)) & 0xFFu)) ++bad;
            }
        }
        CHECK(bad == 0);
    }
    // Bytes the map never touched keep the ELF's value (superset, not replacement).
    CHECK(std::memcmp(ram.Data() + kLoadLo, file.data() + img.segments[0].file_offset, 64) == 0);
    // Post-load patches apply LAST and win over the map (OLD memory.cpp:310-312 order).
    DataMapApplier::Result pr = DataMapApplier::ApplyText(patch_text, ram);
    CHECK(pr.entries == 3);
    uint32_t patched = 0;
    std::memcpy(&patched, ram.Data() + 0x002E804Cu, 4);
    CHECK(patched == 0);
    std::printf("post-load patches applied: %zu\n", pr.entries);

    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS ElfLoaderTitle\n");
    return 0;
}

// ps2r_elf_embedded -- copy an IOP module (IRX: ELF32 MIPS) EMBEDDED in a title ELF (or in any
// container file: a ROM image such as the IOPRP reboot image) out to a file and, optionally,
// refuse it unless its SHA-256 matches.
//
// Some titles ship their IOP modules inside the EE executable (a data section of IRX images
// pushed with sceSifLoadModuleBuffer) instead of as disc files. The build uses this tool to
// pull such a module out of the title ELF into the BUILD directory, where the IOP recompiler
// runs over it (cmake/TitleInputs.cmake rows `<ELF>!<module name>` / `<ELF>@<offset>+<size>`).
//
//   ps2r_elf_embedded <container> <selector> <out-file> [--sha256 <64 hex digits>] [--list]
//
//   <selector>  !<name>           the embedded ELF whose .iopmod section names the module <name>
//                                 (the image spans from its ELF magic to the end of its last
//                                 section or section header, whichever is later)
//               @<offset>+<size>  exact bytes of the title ELF FILE (hex or decimal)
//   --list      print every embedded ELF found (file offset, size, .iopmod name) and exit 0
//
// Exit codes: 0 extracted (and verified); 2 usage; 3 the title ELF does not open / is not an
// ELF; 4 no embedded module matches the selector; 5 the output could not be written; 6 the
// SHA-256 does not match (the output file is removed).
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine/core/hash/sha256.h"
#include "engine/iop/iop_relocator.h"

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int Usage() {
    std::fprintf(stderr,
                 "usage: ps2r_elf_embedded <title-elf> <!name | @offset+size> <out-file> [--sha256 <hex>] [--list]\n"
                 "  copies the IOP module embedded in the title ELF out to <out-file>;\n"
                 "  with --sha256 the copy is kept only if its SHA-256 equals <hex>.\n");
    return 2;
}

struct Embedded {
    size_t offset = 0;
    size_t size = 0;
    std::string name;
    uint16_t version = 0;
};

// The byte extent of an ELF image starting at `p`: the end of its section header table or of
// its last non-NOBITS section, whichever is later (what the module's own headers describe).
size_t ElfExtent(const uint8_t* p, size_t avail) {
    std::vector<ps2r::iop::IopRelocator::Section> secs;
    if (!ps2r::iop::IopRelocator::ParseSections(p, avail, secs, nullptr)) return 0;
    const uint32_t shoff = static_cast<uint32_t>(p[0x20] | (p[0x21] << 8) | (p[0x22] << 16) | (p[0x23] << 24));
    const uint16_t shentsize = static_cast<uint16_t>(p[0x2E] | (p[0x2F] << 8));
    const uint16_t shnum = static_cast<uint16_t>(p[0x30] | (p[0x31] << 8));
    size_t end = static_cast<size_t>(shoff) + static_cast<size_t>(shentsize) * shnum;
    for (const auto& s : secs) {
        if (s.type == 8) continue;   // SHT_NOBITS
        const size_t e = static_cast<size_t>(s.offset) + s.size;
        if (e > end) end = e;
    }
    return end <= avail ? end : 0;
}

std::vector<Embedded> Scan(const std::vector<uint8_t>& elf) {
    std::vector<Embedded> out;
    // The whole file, 4-byte aligned: an embedded image is an ELF inside the title's data (or a
    // module inside a ROM image such as the IOPRP reboot image, whose first bytes are not an ELF).
    const bool container_is_elf = elf[0] == 0x7F && elf[1] == 'E' && elf[2] == 'L' && elf[3] == 'F';
    for (size_t o = container_is_elf ? 4 : 0; o + 0x34 <= elf.size(); o += 4) {
        if (elf[o] != 0x7F || elf[o + 1] != 'E' || elf[o + 2] != 'L' || elf[o + 3] != 'F') continue;
        const size_t ext = ElfExtent(elf.data() + o, elf.size() - o);
        if (ext == 0) continue;
        const ps2r::iop::IopRelocator::IopMod mod = ps2r::iop::IopRelocator::ReadIopMod(elf.data() + o, ext);
        Embedded e;
        e.offset = o;
        e.size = ext;
        e.name = mod.ok ? mod.name : std::string();
        e.version = mod.version;
        out.push_back(e);
        o += ext - 4;   // skip the body
        o &= ~static_cast<size_t>(3);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return Usage();
    const std::string elf_path = argv[1];
    const std::string selector = argv[2];
    std::string out_path;
    std::string expected;
    bool list = false;
    int next = 3;
    if (next < argc && argv[next][0] != '-') out_path = argv[next++];
    for (int i = next; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--sha256" && i + 1 < argc) expected = Lower(argv[++i]);
        else if (a == "--list") list = true;
        else return Usage();
    }
    if (!list && out_path.empty()) return Usage();
    if (!expected.empty() && expected.size() != 2 * ps2r::hash::Sha256::kDigestBytes) {
        std::fprintf(stderr, "ps2r_elf_embedded: --sha256 wants 64 hex digits, got %zu\n", expected.size());
        return 2;
    }

    std::vector<uint8_t> elf;
    {
        std::FILE* f = std::fopen(elf_path.c_str(), "rb");
        if (!f) {
            std::fprintf(stderr, "ps2r_elf_embedded: cannot open %s\n", elf_path.c_str());
            return 3;
        }
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) elf.insert(elf.end(), buf, buf + n);
        std::fclose(f);
    }
    if (elf.size() < 0x34) {
        std::fprintf(stderr, "ps2r_elf_embedded: %s is too small to hold an ELF\n", elf_path.c_str());
        return 3;
    }

    const std::vector<Embedded> found = Scan(elf);
    if (list) {
        for (const Embedded& e : found)
            std::printf("@0x%zx+0x%zx  %s  (version 0x%04x, sha256 %s)\n", e.offset, e.size,
                        e.name.empty() ? "(no .iopmod)" : e.name.c_str(), e.version,
                        ps2r::hash::Sha256::HexOf(elf.data() + e.offset, e.size).c_str());
        if (out_path.empty()) return 0;
    }

    size_t offset = 0, size = 0;
    if (!selector.empty() && selector[0] == '!') {
        const std::string name = selector.substr(1);
        for (const Embedded& e : found) {
            if (e.name == name) {
                offset = e.offset;
                size = e.size;
                break;
            }
        }
        if (size == 0) {
            std::fprintf(stderr, "ps2r_elf_embedded: %s embeds no IOP module named '%s' (%zu embedded ELF(s) found)\n",
                         elf_path.c_str(), name.c_str(), found.size());
            return 4;
        }
    } else if (!selector.empty() && selector[0] == '@') {
        const size_t plus = selector.find('+');
        if (plus == std::string::npos) return Usage();
        offset = static_cast<size_t>(std::strtoull(selector.c_str() + 1, nullptr, 0));
        size = static_cast<size_t>(std::strtoull(selector.c_str() + plus + 1, nullptr, 0));
        if (size == 0 || offset + size > elf.size()) {
            std::fprintf(stderr, "ps2r_elf_embedded: %s is outside %s (%zu bytes)\n", selector.c_str(),
                         elf_path.c_str(), elf.size());
            return 4;
        }
    } else {
        return Usage();
    }

    const std::string digest = ps2r::hash::Sha256::HexOf(elf.data() + offset, size);
    if (!expected.empty() && digest != expected) {
        std::fprintf(stderr,
                     "ps2r_elf_embedded: %s in %s is not the module this build was made from:\n"
                     "  SHA-256 %s (%zu bytes), expected %s.\n"
                     "  Another revision / region, a patched file, or a damaged image.\n",
                     selector.c_str(), elf_path.c_str(), digest.c_str(), size, expected.c_str());
        return 6;
    }
    std::FILE* out = std::fopen(out_path.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "ps2r_elf_embedded: cannot write %s\n", out_path.c_str());
        return 5;
    }
    const bool written = std::fwrite(elf.data() + offset, 1, size, out) == size;
    const bool closed = std::fclose(out) == 0;
    if (!written || !closed) {
        std::remove(out_path.c_str());
        std::fprintf(stderr, "ps2r_elf_embedded: short write to %s\n", out_path.c_str());
        return 5;
    }
    std::printf("ps2r_elf_embedded: %s%s -> %s (%zu bytes at 0x%zx, sha256 %s%s)\n", elf_path.c_str(), selector.c_str(),
                out_path.c_str(), size, offset, digest.c_str(), expected.empty() ? "" : ", verified");
    return 0;
}

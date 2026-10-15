// engine/iop/iop_loader.cpp -- IopLoader: ELF32 section walk into IOP RAM + entry run.
#include "engine/iop/iop_loader.h"

#include <cstring>

#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_memory.h"
#include "engine/iop/iop_module_table.h"

namespace ps2r::iop {

namespace {

struct Elf32Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
};

constexpr uint16_t kEmMips = 8;
constexpr uint8_t kElfClass32 = 1;

std::string SectionName(const uint8_t* strtab, uint32_t strtab_size, uint32_t offset) {
    std::string name;
    for (uint32_t i = offset; i < strtab_size && strtab[i] != '\0'; ++i) name.push_back(static_cast<char>(strtab[i]));
    return name;
}

}  // namespace

IopLoader::SectionReport IopLoader::LoadSections(const uint8_t* irx, size_t size) {
    SectionReport r;
    if (!irx || size < sizeof(Elf32Ehdr)) {
        r.error = "too small for an ELF header";
        return r;
    }
    Elf32Ehdr ehdr;
    std::memcpy(&ehdr, irx, sizeof(ehdr));
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        r.error = "not an ELF image";
        return r;
    }
    if (ehdr.e_ident[4] != kElfClass32) {
        r.error = "not ELFCLASS32";
        return r;
    }
    if (ehdr.e_machine != kEmMips) {
        r.error = "not EM_MIPS";
        return r;
    }
    const uint64_t shend = static_cast<uint64_t>(ehdr.e_shoff) + static_cast<uint64_t>(ehdr.e_shnum) * sizeof(Elf32Shdr);
    if (shend > size) {
        r.error = "section headers run past the image";
        return r;
    }
    std::vector<Elf32Shdr> shdrs(ehdr.e_shnum);
    std::memcpy(shdrs.data(), irx + ehdr.e_shoff, ehdr.e_shnum * sizeof(Elf32Shdr));

    const uint8_t* shstrtab = nullptr;
    uint32_t shstrtab_size = 0;
    if (ehdr.e_shstrndx < ehdr.e_shnum) {
        const Elf32Shdr& s = shdrs[ehdr.e_shstrndx];
        if (static_cast<uint64_t>(s.sh_offset) + s.sh_size <= size) {
            shstrtab = irx + s.sh_offset;
            shstrtab_size = s.sh_size;
        }
    }

    for (const Elf32Shdr& s : shdrs) {
        const std::string name = shstrtab ? SectionName(shstrtab, shstrtab_size, s.sh_name) : std::string();
        const bool is_text = name == ".text";
        const bool is_data = name == ".data";
        const bool is_rodata = name == ".rodata";
        const bool is_bss = name == ".bss";
        if (!is_text && !is_data && !is_rodata && !is_bss) continue;
        uint8_t* dest = mem_.Pointer(s.sh_addr, s.sh_size);
        if (!dest) {
            r.error = "section " + name + " does not fit IOP RAM";
            return r;
        }
        if (is_bss) {
            std::memset(dest, 0, s.sh_size);
            r.bss_bytes += s.sh_size;
            continue;
        }
        if (static_cast<uint64_t>(s.sh_offset) + s.sh_size > size) {
            r.error = "section " + name + " runs past the image";
            return r;
        }
        std::memcpy(dest, irx + s.sh_offset, s.sh_size);
        if (is_text) {
            r.found_text = true;
            r.text_bytes += s.sh_size;
        } else if (is_data) {
            r.data_bytes += s.sh_size;
        } else {
            r.rodata_bytes += s.sh_size;
        }
    }
    if (!r.found_text) {
        r.error = "no .text section";
        return r;
    }
    r.ok = true;
    return r;
}

bool IopLoader::RunEntry(const ModuleLayout& layout, const FunctionResolver& resolve, IopModuleTable& table) {
    GuestFunction entry = resolve ? resolve(layout.entry) : GuestFunction{};
    if (!entry) return false;
    IopContext ctx = IopContext::ForEntry(layout.stack_top - 0x10, layout.gp);
    ctx.pc = layout.entry;
    entry(ctx);
    int rpc_tid = layout.rpc_tid_addr ? static_cast<int>(mem_.Read<uint32_t>(layout.rpc_tid_addr)) : 0;
    if (rpc_tid <= 0 || rpc_tid >= IopKernel::kMaxThreads) rpc_tid = 1;
    last_rpc_tid_ = rpc_tid;
    table.OnModuleLoaded(rpc_tid);
    return true;
}

IopLoader::RebootReport IopLoader::RebootHandshake(const std::string& iop_image_path) {
    // The pre-reboot BIOS module set is never hosted: the handshake is accepted
    // as complete and the post-reboot (IOPRP) set is the module table's.
    RebootReport r;
    r.accepted = !iop_image_path.empty();
    r.image = iop_image_path;
    return r;
}

uint32_t IopLoader::LoadFileRequest(const std::string& path) {
    if (path.empty()) return 0;
    load_requests_.push_back(path);
    return static_cast<uint32_t>(load_requests_.size());
}

}  // namespace ps2r::iop

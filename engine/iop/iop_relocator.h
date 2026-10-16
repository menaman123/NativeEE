// engine/iop/iop_relocator.h -- IopRelocator: the IRX (ELF32 MIPS, ET_IRX 0xFF80)
// relocation the IOP kernel's loadcore applies when it places a module at its
// load address. An IRX links every section at address 0 and carries SHT_REL
// tables (.rel.text / .rel.rodata / .rel.data; R_MIPS_32, R_MIPS_26,
// R_MIPS_HI16 + R_MIPS_LO16 pairs, each HI16 immediately followed by its LO16
// as the SDK linker emits them). Relocate() applies them IN PLACE to the image
// bytes for a chosen base: afterwards the section bytes are what IOP RAM holds
// at sh_addr + base. Header-only (this .h + the .inl): the IOP recompiler
// (tools/iop_recompiler) and the runtime loader (IopLoader) include the SAME
// code, so the text the tool disassembled and the image the kernel hosts agree
// word for word. No engine dependency; no title constant.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::iop {

class IopRelocator {
public:
    static constexpr uint16_t kEtIrx = 0xFF80;
    static constexpr uint32_t kShtRel = 9;
    static constexpr uint32_t kShtIopMod = 0x70000080;
    static constexpr uint32_t kRMips32 = 2;
    static constexpr uint32_t kRMips26 = 4;
    static constexpr uint32_t kRMipsHi16 = 5;
    static constexpr uint32_t kRMipsLo16 = 6;

    struct Section {
        std::string name;
        uint32_t type = 0;
        uint32_t addr = 0;     // sh_addr (module-relative, as linked)
        uint32_t offset = 0;   // sh_offset in the image
        uint32_t size = 0;
        uint32_t info = 0;     // SHT_REL: the target section index
        uint32_t flags = 0;
    };

    struct Report {
        bool ok = false;
        uint32_t r32 = 0, r26 = 0, hi16 = 0, lo16 = 0;
        uint32_t unknown_types = 0;
        std::string error;
    };

    // The .iopmod section: {moduleinfo, entry, gp, text, data, bss, version:u16, name}.
    struct IopMod {
        bool ok = false;
        std::string name;
        uint16_t version = 0;
        uint32_t entry = 0, gp = 0, text = 0, data = 0, bss = 0, moduleinfo = 0;
    };

    // One R_MIPS_32 / HI16+LO16 address the relocation tables materialise (the
    // oracle for pointer-only function entries and switch tables): the module
    // address it points at (after relocation = absolute), and where it sits.
    struct Pointer {
        uint32_t target = 0;
        uint32_t site = 0;      // module address of the relocated word (HI16: the lui)
        uint32_t type = 0;      // kRMips32 or kRMipsHi16
    };

    // ELF32 section headers + names (false with `err` for a malformed image).
    static bool ParseSections(const uint8_t* image, size_t size, std::vector<Section>& out, std::string* err);
    static IopMod ReadIopMod(const uint8_t* image, size_t size);
    static uint32_t Entry(const uint8_t* image, size_t size);   // e_entry as linked

    // Applies every SHT_REL table to its target section's bytes for `base`.
    // base = 0 leaves the bytes unchanged but still validates the tables.
    static Report Relocate(uint8_t* image, size_t size, uint32_t base);
    static Report Relocate(std::vector<uint8_t>& image, uint32_t base);

    // The addresses the tables materialise, read from an ALREADY relocated image
    // (targets are absolute). Empty on a malformed image.
    static std::vector<Pointer> Pointers(const uint8_t* image, size_t size);

    // The load extent: the highest sh_addr + sh_size over the allocated sections
    // (.text/.rodata/.data/.bss and any other SHF_ALLOC one), module-relative.
    static uint32_t LoadExtent(const uint8_t* image, size_t size);

private:
    static uint32_t Rd32(const uint8_t* p);
    static void Wr32(uint8_t* p, uint32_t v);
    static const Section* TargetOf(const std::vector<Section>& secs, const Section& rel);
};

}  // namespace ps2r::iop

#include "engine/iop/iop_relocator.inl"

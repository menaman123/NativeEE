// engine/iop/iop_relocator.inl -- IopRelocator bodies (header-only sibling of iop_relocator.h).
#pragma once

#include <cstring>

namespace ps2r::iop {

inline uint32_t IopRelocator::Rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline void IopRelocator::Wr32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline bool IopRelocator::ParseSections(const uint8_t* image, size_t size, std::vector<Section>& out,
                                        std::string* err) {
    out.clear();
    auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    if (!image || size < 0x34) return fail("too small for an ELF header");
    if (image[0] != 0x7F || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') return fail("not an ELF image");
    if (image[4] != 1) return fail("not ELFCLASS32");
    const uint16_t machine = static_cast<uint16_t>(image[0x12] | (image[0x13] << 8));
    if (machine != 8) return fail("not EM_MIPS");
    const uint32_t shoff = Rd32(image + 0x20);
    const uint16_t shentsize = static_cast<uint16_t>(image[0x2E] | (image[0x2F] << 8));
    const uint16_t shnum = static_cast<uint16_t>(image[0x30] | (image[0x31] << 8));
    const uint16_t shstrndx = static_cast<uint16_t>(image[0x32] | (image[0x33] << 8));
    if (shentsize < 0x28) return fail("bad e_shentsize");
    if (static_cast<uint64_t>(shoff) + static_cast<uint64_t>(shnum) * shentsize > size)
        return fail("section headers run past the image");
    std::vector<Section> secs(shnum);
    for (uint16_t i = 0; i < shnum; ++i) {
        const uint8_t* sh = image + shoff + static_cast<size_t>(i) * shentsize;
        secs[i].type = Rd32(sh + 0x04);
        secs[i].flags = Rd32(sh + 0x08);
        secs[i].addr = Rd32(sh + 0x0C);
        secs[i].offset = Rd32(sh + 0x10);
        secs[i].size = Rd32(sh + 0x14);
        secs[i].info = Rd32(sh + 0x1C);
        secs[i].name.clear();
        const uint32_t name_off = Rd32(sh + 0x00);
        if (shstrndx < shnum) {
            const uint8_t* str_sh = image + shoff + static_cast<size_t>(shstrndx) * shentsize;
            const uint32_t str_off = Rd32(str_sh + 0x10), str_size = Rd32(str_sh + 0x14);
            if (static_cast<uint64_t>(str_off) + str_size <= size && name_off < str_size) {
                for (uint32_t n = name_off; n < str_size && image[str_off + n]; ++n)
                    secs[i].name.push_back(static_cast<char>(image[str_off + n]));
            }
        }
    }
    out = std::move(secs);
    return true;
}

inline IopRelocator::IopMod IopRelocator::ReadIopMod(const uint8_t* image, size_t size) {
    IopMod m;
    std::vector<Section> secs;
    if (!ParseSections(image, size, secs, nullptr)) return m;
    for (const Section& s : secs) {
        if (s.type != kShtIopMod) continue;
        if (static_cast<uint64_t>(s.offset) + s.size > size || s.size < 0x1B) return m;
        const uint8_t* p = image + s.offset;
        m.moduleinfo = Rd32(p + 0x00);
        m.entry = Rd32(p + 0x04);
        m.gp = Rd32(p + 0x08);
        m.text = Rd32(p + 0x0C);
        m.data = Rd32(p + 0x10);
        m.bss = Rd32(p + 0x14);
        m.version = static_cast<uint16_t>(p[0x18] | (p[0x19] << 8));
        for (uint32_t n = 0x1A; n < s.size && p[n]; ++n) m.name.push_back(static_cast<char>(p[n]));
        m.ok = true;
        return m;
    }
    return m;
}

inline uint32_t IopRelocator::Entry(const uint8_t* image, size_t size) {
    return (image && size >= 0x34) ? Rd32(image + 0x18) : 0;
}

inline const IopRelocator::Section* IopRelocator::TargetOf(const std::vector<Section>& secs, const Section& rel) {
    if (rel.info < secs.size()) return &secs[rel.info];
    return nullptr;
}

inline IopRelocator::Report IopRelocator::Relocate(std::vector<uint8_t>& image, uint32_t base) {
    return Relocate(image.data(), image.size(), base);
}

inline IopRelocator::Report IopRelocator::Relocate(uint8_t* image, size_t size, uint32_t base) {
    Report r;
    std::vector<Section> secs;
    if (!ParseSections(image, size, secs, &r.error)) return r;
    for (const Section& rel : secs) {
        if (rel.type != kShtRel) continue;
        const Section* target = TargetOf(secs, rel);
        if (!target || target->type == 8 /*NOBITS*/) {
            r.error = "relocation section " + rel.name + " has no loadable target";
            return r;
        }
        if (static_cast<uint64_t>(rel.offset) + rel.size > size ||
            static_cast<uint64_t>(target->offset) + target->size > size) {
            r.error = "relocation section " + rel.name + " runs past the image";
            return r;
        }
        const uint32_t count = rel.size / 8;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* e = image + rel.offset + static_cast<size_t>(i) * 8;
            const uint32_t r_offset = Rd32(e), r_info = Rd32(e + 4);
            const uint32_t type = r_info & 0xFF;
            if (r_offset < target->addr || r_offset + 4 > target->addr + target->size) {
                r.error = "relocation in " + rel.name + " outside its target section";
                return r;
            }
            uint8_t* w = image + target->offset + (r_offset - target->addr);
            const uint32_t word = Rd32(w);
            switch (type) {
                case kRMips32:
                    Wr32(w, word + base);
                    r.r32++;
                    break;
                case kRMips26: {
                    const uint32_t tgt = ((word & 0x03FFFFFFu) << 2) + base;
                    Wr32(w, (word & 0xFC000000u) | ((tgt >> 2) & 0x03FFFFFFu));
                    r.r26++;
                    break;
                }
                case kRMipsHi16: {
                    // The SDK linker pairs each HI16 with the LO16 that follows it in the table.
                    if (i + 1 >= count) {
                        r.error = "R_MIPS_HI16 without a following R_MIPS_LO16 in " + rel.name;
                        return r;
                    }
                    const uint8_t* e2 = image + rel.offset + static_cast<size_t>(i + 1) * 8;
                    const uint32_t lo_offset = Rd32(e2), lo_info = Rd32(e2 + 4);
                    if ((lo_info & 0xFF) != kRMipsLo16) {
                        r.error = "R_MIPS_HI16 not followed by R_MIPS_LO16 in " + rel.name;
                        return r;
                    }
                    if (lo_offset < target->addr || lo_offset + 4 > target->addr + target->size) {
                        r.error = "R_MIPS_LO16 outside its target section in " + rel.name;
                        return r;
                    }
                    uint8_t* lw = image + target->offset + (lo_offset - target->addr);
                    const uint32_t lo_word = Rd32(lw);
                    const uint32_t ahl = ((word & 0xFFFFu) << 16) +
                                         static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(lo_word & 0xFFFFu)));
                    const uint32_t val = ahl + base;
                    const uint32_t lo = val & 0xFFFFu;
                    const uint32_t hi = (val - static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(lo)))) >> 16;
                    Wr32(w, (word & 0xFFFF0000u) | (hi & 0xFFFFu));
                    Wr32(lw, (lo_word & 0xFFFF0000u) | lo);
                    r.hi16++;
                    r.lo16++;
                    ++i;   // the LO16 is consumed with its HI16
                    break;
                }
                case kRMipsLo16:
                    // An unpaired LO16 (its HI16 handled above would have consumed it).
                    r.error = "R_MIPS_LO16 without a preceding R_MIPS_HI16 in " + rel.name;
                    return r;
                default:
                    r.unknown_types++;
                    r.error = "unknown relocation type " + std::to_string(type) + " in " + rel.name;
                    return r;
            }
        }
    }
    r.ok = true;
    return r;
}

inline std::vector<IopRelocator::Pointer> IopRelocator::Pointers(const uint8_t* image, size_t size) {
    std::vector<Pointer> out;
    std::vector<Section> secs;
    if (!ParseSections(image, size, secs, nullptr)) return out;
    for (const Section& rel : secs) {
        if (rel.type != kShtRel) continue;
        const Section* target = TargetOf(secs, rel);
        if (!target || target->type == 8) continue;
        if (static_cast<uint64_t>(rel.offset) + rel.size > size ||
            static_cast<uint64_t>(target->offset) + target->size > size)
            continue;
        const uint32_t count = rel.size / 8;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* e = image + rel.offset + static_cast<size_t>(i) * 8;
            const uint32_t r_offset = Rd32(e), type = Rd32(e + 4) & 0xFF;
            if (r_offset < target->addr || r_offset + 4 > target->addr + target->size) continue;
            const uint8_t* w = image + target->offset + (r_offset - target->addr);
            if (type == kRMips32) {
                out.push_back(Pointer{Rd32(w), r_offset, kRMips32});
            } else if (type == kRMipsHi16 && i + 1 < count) {
                const uint8_t* e2 = image + rel.offset + static_cast<size_t>(i + 1) * 8;
                const uint32_t lo_offset = Rd32(e2), lo_type = Rd32(e2 + 4) & 0xFF;
                if (lo_type != kRMipsLo16 || lo_offset < target->addr || lo_offset + 4 > target->addr + target->size)
                    continue;
                const uint32_t hi_word = Rd32(w), lo_word = Rd32(image + target->offset + (lo_offset - target->addr));
                const uint32_t val = ((hi_word & 0xFFFFu) << 16) +
                                     static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(lo_word & 0xFFFFu)));
                out.push_back(Pointer{val, r_offset, kRMipsHi16});
                ++i;
            }
        }
    }
    return out;
}

inline uint32_t IopRelocator::LoadExtent(const uint8_t* image, size_t size) {
    std::vector<Section> secs;
    if (!ParseSections(image, size, secs, nullptr)) return 0;
    uint32_t extent = 0;
    for (const Section& s : secs) {
        if (!(s.flags & 0x2 /*SHF_ALLOC*/) || s.type == kShtIopMod) continue;
        if (s.addr + s.size > extent) extent = s.addr + s.size;
    }
    return extent;
}

}  // namespace ps2r::iop

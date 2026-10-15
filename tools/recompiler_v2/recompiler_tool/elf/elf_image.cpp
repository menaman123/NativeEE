//
// elf_image.cpp — RECOMP v2: the single ELF authority (design §6).
//
// [RECOMP-P2] Loads the EE ELF once via ELFIO and exposes .text plus every
// other PROGBITS section as SectionImage inputs for the discovery engine.
// Module rule: discovery/ reads only this module; translate/ and emit/
// consume discovery outputs and never touch the ELF directly.
//

#include "elf_image.h"

#include <elfio/elfio.hpp>

namespace recomp_v2 {

std::optional<ElfImage> ElfImage::load(const std::string& path) {
    ELFIO::elfio reader;
    if (!reader.load(path)) return std::nullopt;

    ElfImage img;
    img.entry_ = static_cast<uint32_t>(reader.get_entry());
    for (const auto& sec : reader.sections) {
        if (sec->get_type() != ELFIO::SHT_PROGBITS) continue;
        if (!sec->get_data() || sec->get_size() == 0) continue;
        SectionImage s;
        s.name = sec->get_name();
        s.base_va = static_cast<uint32_t>(sec->get_address());
        s.bytes.assign(
            reinterpret_cast<const uint8_t*>(sec->get_data()),
            reinterpret_cast<const uint8_t*>(sec->get_data()) + sec->get_size());
        if (s.name == ".text") img.text_ = std::move(s);
        else img.data_.push_back(std::move(s));
    }
    if (img.text_.bytes.empty()) return std::nullopt;
    return img;
}

} // namespace recomp_v2

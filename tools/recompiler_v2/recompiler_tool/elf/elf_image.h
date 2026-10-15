#pragma once
//
// elf_image.h — RECOMP v2: single ELF authority (P1 STUB API).
//
// [RECOMP-P1] Spec (RECOMP_P0_DESIGN.md §6): one place that loads the ELF,
// exposes the section map, VA->offset translation, and table reads. Replaces
// today's scattered ELFIO use. Module rule: discovery/ may read ONLY this
// module; translate/ and emit/ consume discovery outputs and never touch the
// ELF directly (single source of truth).
//
// P1 stub behavior: load() always fails. Tests that need real ELF bytes use
// their own test-side loader (tests/recompiler_v2/elf_text_loader.*) so the
// spec assertions stay independent of this stub.
//

#include "../discovery/discovery_types.h"

#include <optional>
#include <string>
#include <vector>

namespace recomp_v2 {

class ElfImage {
public:
    // Load an EE ELF from disk. P1: STUB — always returns std::nullopt.
    static std::optional<ElfImage> load(const std::string& path);

    // Executable code section (.text) and data sections (.data/.rodata/.sdata).
    const SectionImage& text() const { return text_; }
    const std::vector<SectionImage>& data_sections() const { return data_; }
    uint32_t entry_point() const { return entry_; }

private:
    SectionImage text_;
    std::vector<SectionImage> data_;
    uint32_t entry_ = 0;
};

} // namespace recomp_v2

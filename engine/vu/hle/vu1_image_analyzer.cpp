// Vu1ImageAnalyzer -- hashes, extent, family prefix, dispatch chain (rules in the header).
#include "engine/vu/hle/vu1_image_analyzer.h"

#include <cstdio>
#include <cstring>

#include "engine/vu/vu_image_hash.h"

Vu1ImageAnalyzer::Vu1ImageAnalyzer(const Vu1FamilySignature& signature) : sig_(signature) {}

Vu1ImageAnalyzer::Vu1ImageAnalyzer(const Vu1ProgramLibrary& library)
    : sig_(library.family), programs_(library.programs), program_count_(library.program_count) {}

std::string Vu1ImageAnalyzer::ShortHash(uint64_t h) {
    char b[24];
    std::snprintf(b, sizeof(b), "%08llx", (unsigned long long)(h >> 32));
    return b;
}

bool Vu1ImageAnalyzer::LooksLikeFamilyDispatcher(const uint8_t* micro) const {
    uint32_t lower0;
    std::memcpy(&lower0, micro + 0, 4);
    return lower0 == sig_.dispatcher_op0;
}

Vu1ImageInfo Vu1ImageAnalyzer::Analyze(const uint8_t* micro, const uint8_t* data) {
    Vu1ImageInfo info{};
    info.image_hash = VuImageHash::Fast(micro, kVu1HleMicroSize);

    uint32_t extent = 0;
    for (uint32_t i = kVu1HleMicroSize; i > 0; i -= 8) {
        uint64_t b;
        std::memcpy(&b, micro + i - 8, 8);
        if (b != 0) {
            extent = i;
            break;
        }
    }
    info.extent = extent;
    info.prefix_hash = VuImageHash::Fast(micro, sig_.variant_base);
    info.variant_hash = extent > sig_.variant_base
                            ? VuImageHash::Fast(micro + sig_.variant_base, extent - sig_.variant_base)
                            : 0;

    // The loaded-program layout from variant_base.
    info.loaded_end = sig_.variant_base;
    info.layout_hash = 0;
    if (programs_ != nullptr && program_count_ > 0) {
        uint32_t pos = sig_.variant_base;
        while (pos < extent) {
            bool matched = false;
            for (size_t i = 0; i < program_count_; i++) {
                const Vu1LoadedProgram& p = programs_[i];
                if (p.size_bytes == 0 || pos + p.size_bytes > kVu1HleMicroSize) continue;
                if (VuImageHash::Fast(micro + pos, p.size_bytes) != p.code_hash) continue;
                info.layout.push_back({(uint16_t)pos, (uint16_t)i});
                pos += p.size_bytes;
                matched = true;
                break;
            }
            if (!matched) break;
        }
        info.loaded_end = pos;
        if (pos > sig_.variant_base) info.layout_hash = VuImageHash::Fast(micro + sig_.variant_base, pos - sig_.variant_base);
    }

    const bool dispatcher = LooksLikeFamilyDispatcher(micro);
    if (dispatcher) {
        if (learned_prefix_ == 0) learned_prefix_ = info.prefix_hash;
        info.prefix_is_family = (info.prefix_hash == learned_prefix_);
    }

    info.dispatch_valid = false;
    if (data && dispatcher) {
        uint16_t qw;
        std::memcpy(&qw, data + 3 * 16, 2);
        for (int step = 0; step < 64 && qw < kVu1HleDataSize / 16; step++) {
            uint16_t sub, next;
            std::memcpy(&sub, data + qw * 16, 2);
            std::memcpy(&next, data + qw * 16 + 8, 2);
            const uint16_t pc = (uint16_t)(sub * 8);
            info.dispatch_chain.push_back(pc);
            if (pc == sig_.exit_stubs[0] || pc == sig_.exit_stubs[1]) {
                info.dispatch_valid = true;
                break;
            }
            if (pc >= kVu1HleMicroSize) break;
            qw = next;
        }
    }
    return info;
}

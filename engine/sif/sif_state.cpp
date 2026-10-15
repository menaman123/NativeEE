#include "engine/sif/sif_state.h"

#include "engine/ee/mem/mmio_router.h"

void SifState::Register(MmioRouter& router) { router.Register(kBase, kSize, *this, "SIF"); }

void SifState::Reset() {
    mscom_ = smcom_ = msflag_ = smflag_ = ctrl_ = 0;
}

uint32_t* SifState::Word(uint32_t offset) {
    switch (offset) {
        case kMscom: return &mscom_;
        case kSmcom: return &smcom_;
        case kMsflag: return &msflag_;
        case kSmflag: return &smflag_;
        case kCtrl: return &ctrl_;
        default: return nullptr;
    }
}

uint32_t SifState::WordValue(uint32_t offset) const {
    switch (offset) {
        case kMscom: return mscom_;
        case kSmcom: return smcom_;
        case kMsflag: return msflag_;
        case kSmflag: return smflag_;
        case kCtrl: return ctrl_;
        default: return 0;
    }
}

// Each register is one 32-bit word at a 16-byte stride; sub-word reads see
// the byte lanes of the word, a 64-bit read pairs the word with the (zero)
// word at +4.
uint64_t SifState::Read(uint32_t addr, unsigned size) {
    ++reads_;
    const uint32_t offset = addr - kBase;
    const uint32_t lo = WordValue(offset & ~3u);
    if (size <= 4) {
        const unsigned shift = (addr & 3u) * 8u;
        const uint64_t mask = size == 4 ? 0xFFFFFFFFull : ((1ull << (size * 8u)) - 1ull);
        return (static_cast<uint64_t>(lo) >> shift) & mask;
    }
    const uint32_t hi = WordValue((offset & ~7u) + 4u);
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// Plain stores (the original kept no set/clear semantics on either flag
// word): a 32-bit or wider write replaces the word; a narrower write merges
// its byte lanes.
void SifState::Write(uint32_t addr, unsigned size, uint64_t value) {
    ++writes_;
    const uint32_t offset = addr - kBase;
    uint32_t* word = Word(offset & ~3u);
    if (!word) return;
    if (size >= 4) {
        *word = static_cast<uint32_t>(value);
        return;
    }
    const unsigned shift = (addr & 3u) * 8u;
    const uint32_t mask = ((1u << (size * 8u)) - 1u) << shift;
    *word = (*word & ~mask) | ((static_cast<uint32_t>(value) << shift) & mask);
}

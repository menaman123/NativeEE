#pragma once
// GifsSection: the GIFS savestate section -- the GIF register file -- over
// GifRegs. REQUIRED (the reference loader reads it unconditionally).
//
// Wire layout (13 bytes, little-endian), the reference writer's section:
//   +0x00 u32 ctrl          GIF_CTRL as stored (PSE latched; RST never stored)
//   +0x04 u32 mode          GIF_MODE (M3R, IMT, bit 1)
//   +0x08 u32 stat          the reference kept a GIF_STAT mirror that nothing
//                           wrote after reset -- GIF_STAT reads were composed
//                           from MODE / CTRL / the VIF mask / the transfer, as
//                           they are here -- so the word is 0 in every
//                           reference image. Written as 0; ignored on restore.
//   +0x0C u8  path3_masked  1 = PATH3 masked by VIF1 MSKPATH3 (STAT.M3P)
// Restore writes CTRL, MODE and the VIF mask and clears the in-flight transfer
// and the FIFO count (the reference cleared its DMA buffer, FIFO and
// in_transfer); the optional callback lets the root clear the parser's buffered
// tail, which this codec cannot reach.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class GifRegs;

class GifsSection {
public:
    using OnRestored = std::function<void()>;

    static constexpr size_t kPayloadBytes = 4 + 4 + 4 + 1;

    explicit GifsSection(GifRegs& regs);

    void SetOnRestored(OnRestored fn) { on_restored_ = std::move(fn); }

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    void Serialize(std::vector<uint8_t>& out) const;
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

    unsigned RestoredCount() const { return restored_; }

private:
    GifRegs& regs_;
    OnRestored on_restored_;
    unsigned restored_ = 0;
};

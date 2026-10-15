// engine/ipu/hle/guest_memory_adapter.h -- GuestMemoryAdapter: the GuestMemory
// face over the composition root's guest address translation. The adapter owns
// no memory: the root hands it the EE address map's translate (RAM mirrors and
// the scratchpad window), and the 32-bit word accesses are little-endian loads
// and stores through the same translation, so a word in an unmapped range reads
// 0 and a store there is dropped -- the behaviour of a libmpeg store into
// unbacked guest memory. engine/ipu keeps no link to engine/ee this way.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "engine/ipu/hle/guest_memory.h"

namespace ps2r::ipu {

class GuestMemoryAdapter : public GuestMemory {
public:
    // Host pointer to `len` guest bytes at `addr`, or nullptr when unbacked.
    using Translator = std::function<uint8_t*(uint32_t addr, size_t len)>;

    explicit GuestMemoryAdapter(Translator translate);

    uint8_t* Translate(uint32_t addr, size_t len) override;
    uint32_t Read32(uint32_t addr) override;
    void Write32(uint32_t addr, uint32_t value) override;

    // Word accesses that found no backing (evidence for run.meta).
    uint64_t UnbackedWords() const { return unbacked_; }

private:
    Translator translate_;
    uint64_t unbacked_ = 0;
};

}  // namespace ps2r::ipu

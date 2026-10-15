// engine/iop/iop_memory.h -- the IOP's 2 MB RAM (R3000 address space
// 0x00000000..0x001FFFFF, separate from EE main memory). Read/write of
// sizeof(T) bytes with the 2 MB mask; a masked access that would run off the
// end reads 0 / drops the write and counts as unrouted. The storage lives in
// the .cpp-constructed vector so the object is the IOPM savestate section's
// byte source. There is no bus window above RAM: the title's one custom module
// reaches the SPU2 only through its libsd imports (the HLE module over
// SpuCore), never by a raw register access -- the window that once served
// 0x1F900000 was deleted at zero hits in every arm (M12 ledger D1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2r::iop {

class IopMemory {
public:
    static constexpr uint32_t kSize = 2u * 1024u * 1024u;
    static constexpr uint32_t kMask = kSize - 1;

    IopMemory();

    template <typename T> T Read(uint32_t addr) const;
    template <typename T> void Write(uint32_t addr, T value);

    // Host pointer to `size` bytes at `addr` (masked), or nullptr when the
    // range runs past the end of RAM.
    uint8_t* Pointer(uint32_t addr, size_t size);
    const uint8_t* Pointer(uint32_t addr, size_t size) const;

    uint8_t* Data() { return bytes_.data(); }
    const uint8_t* Data() const { return bytes_.data(); }
    size_t Size() const { return bytes_.size(); }
    void Clear();

    // Accesses that fell outside RAM (the iop_unrouted census key).
    uint64_t UnroutedCount() const { return unrouted_; }

private:
    std::vector<uint8_t> bytes_;
    uint64_t unrouted_ = 0;
};

}  // namespace ps2r::iop

#include "engine/iop/iop_memory.inl"

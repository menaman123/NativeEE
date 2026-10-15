// gs/state/gs_mmio_handler.cpp -- GsMmioHandler (see gs_mmio_handler.h).
#include "engine/gs/state/gs_mmio_handler.h"

#include "engine/gs/state/gs_state.h"

namespace ps2r {
namespace gs {

namespace {
constexpr uint32_t kRegMask = ~7u;  // the 64-bit register an access falls in
}  // namespace

// The 32-bit half of the register `addr` lies in (upper when bit 2 is set).
uint32_t GsMmioHandler::ReadHalf(uint32_t addr) const {
    const uint64_t full = gs_.ReadPrivileged(addr & kRegMask);
    return (addr & 4u) ? static_cast<uint32_t>(full >> 32) : static_cast<uint32_t>(full);
}

void GsMmioHandler::WriteHalf(uint32_t addr, uint32_t value) {
    if (addr & 4u)
        gs_.Priv().WriteUpper(addr & kRegMask, value);
    else
        gs_.Priv().WriteLower(addr & kRegMask, value);
}

uint64_t GsMmioHandler::Read(uint32_t addr, unsigned size) {
    ++reads_;
    if (size == 8) return gs_.ReadPrivileged(addr & kRegMask);
    const uint32_t half = ReadHalf(addr);
    if (size == 4) return half;
    if (size == 2) return (half >> ((addr & 2u) * 8u)) & 0xFFFFu;
    return (half >> ((addr & 3u) * 8u)) & 0xFFu;
}

void GsMmioHandler::Write(uint32_t addr, unsigned size, uint64_t value) {
    ++writes_;
    if (size == 8) {
        gs_.WritePrivileged(addr & kRegMask, value);
        return;
    }
    if (size == 4) {
        WriteHalf(addr, static_cast<uint32_t>(value));
        return;
    }
    // Sub-word store: merge the byte lanes into the enclosing half.
    const uint32_t shift = (addr & 3u) * 8u;
    const uint32_t mask = (size == 2 ? 0xFFFFu : 0xFFu) << shift;
    const uint32_t cur = ReadHalf(addr);
    WriteHalf(addr, (cur & ~mask) | ((static_cast<uint32_t>(value) << shift) & mask));
}

}  // namespace gs
}  // namespace ps2r

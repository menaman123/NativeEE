// gs/state/gs_mmio_handler.h -- GsMmioHandler: the EE-side MmioHandler for the
// GS privileged register page [0x12000000, 0x12002000) over a GsState. The
// registers are 64-bit at a 16-byte stride (GS User's Manual chapter 6); the EE
// reaches them with 64-bit stores/loads or as two 32-bit halves (lower at
// offset 0, upper at offset 4 of the register). A 64-bit access goes to
// GsState::WritePrivileged / ReadPrivileged; a 32-bit access merges its half
// through GsPrivRegs::WriteLower / WriteUpper (and reads the half back); a
// 16/8-bit access is widened to the enclosing 32-bit half (byte lanes merged on
// write, extracted on read). Reads are counted for the composition root's census.
#pragma once

#include <cstdint>

#include "engine/ee/mem/mmio_handler.h"

namespace ps2r {
namespace gs {

class GsState;

class GsMmioHandler : public MmioHandler {
public:
    explicit GsMmioHandler(GsState& gs) : gs_(gs) {}

    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    uint64_t Reads() const { return reads_; }
    uint64_t Writes() const { return writes_; }  // MMIO accesses (GsPrivRegs::Writes counts register writes)

    // The E425 hazard census: what the EE reads of the GS event state. CSR reads
    // split by the event bits the returned word carried (FINISH bit 1, SIGNAL bit 0),
    // SIGLBLID reads, and the CSR W1C writes that cleared FINISH / SIGNAL.
    struct ReadCensus {
        uint64_t csr = 0, csr_finish_set = 0, csr_signal_set = 0, siglblid = 0, other = 0;
        uint64_t csr_clear_finish = 0, csr_clear_signal = 0;
    };
    const ReadCensus& Census() const { return census_; }

private:
    uint32_t ReadHalf(uint32_t addr) const;
    void WriteHalf(uint32_t addr, uint32_t value);
    void NoteRead(uint32_t addr, uint64_t word);
    void NoteWrite(uint32_t addr, uint64_t word);

    GsState& gs_;
    uint64_t reads_ = 0;
    uint64_t writes_ = 0;
    ReadCensus census_;
};

}  // namespace gs
}  // namespace ps2r

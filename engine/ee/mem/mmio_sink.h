#pragma once
// The default MMIO handler: every register range no subsystem has claimed
// yet reads its documented IDLE value and accepts (drops) writes. The idle
// values are the ones the original runtime's register read path returned with
// every device at rest (memory.cpp:539-981, dmac.cpp:1045-1131, gif.cpp:221-246);
// the table in mmio_sink.cpp cites each row. A boot leg may read any register
// and get its idle value; it may not depend on a completion the sink cannot
// produce -- that dependency is the park (stopping rule R17).
#include <cstdint>
#include <vector>

#include "engine/ee/mem/mmio_handler.h"

class MmioSink : public MmioHandler {
public:
    struct IdleRow {
        uint32_t base;
        uint32_t size;
        uint32_t value;  // 32-bit register view; wider reads replicate it per word
        const char* name;
    };

    MmioSink();

    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    // The 32-bit idle value of the register containing `addr`.
    uint32_t IdleValue(uint32_t addr) const;

    static const std::vector<IdleRow>& IdleTable();

    uint64_t ReadCount() const { return reads_; }
    uint64_t WriteCount() const { return writes_; }

private:
    uint64_t reads_ = 0;
    uint64_t writes_ = 0;
};

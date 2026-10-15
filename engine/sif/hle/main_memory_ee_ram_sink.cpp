#include "engine/sif/hle/main_memory_ee_ram_sink.h"

#include <cstring>

#include "engine/ee/mem/main_memory.h"

bool MainMemoryEeRamSink::WriteEeRam(uint32_t ee_addr, const uint8_t* src, size_t size) {
    const uint32_t phys = ee_addr & 0x1FFFFFFFu;
    uint8_t* dst = ram_.Pointer(phys, size);
    if (!dst) return false;
    std::memcpy(dst, src, size);
    bytes_ += size;
    return true;
}

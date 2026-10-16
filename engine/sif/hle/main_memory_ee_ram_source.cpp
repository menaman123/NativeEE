#include "engine/sif/hle/main_memory_ee_ram_source.h"

#include <cstring>

#include "engine/ee/mem/main_memory.h"

bool MainMemoryEeRamSource::ReadEeRam(uint32_t ee_addr, uint8_t* dst, size_t size) {
    const uint32_t phys = MainMemory::Phys(ee_addr);
    const uint8_t* src = ram_.Pointer(phys, size);
    if (!src) return false;
    if (size) std::memcpy(dst, src, size);
    bytes_ += size;
    return true;
}

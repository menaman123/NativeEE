// tests/iop/vector_ee_ram_sink.cpp -- VectorEeRamSink: bounded copy into the byte array.
#include "tests/iop/vector_ee_ram_sink.h"

#include <cstring>

bool VectorEeRamSink::WriteEeRam(uint32_t ee_addr, const uint8_t* src, size_t size) {
    if (ee_addr + size > sizeof(bytes)) return false;
    std::memcpy(bytes + ee_addr, src, size);
    return true;
}

// tests/ipu/file_fmv_sector_source.cpp -- FileFmvSectorSource bodies.
#include "file_fmv_sector_source.h"

#include <cstring>

FileFmvSectorSource::~FileFmvSectorSource() {
    if (file_) std::fclose(file_);
}

bool FileFmvSectorSource::Open(const std::string& path) {
    if (file_) std::fclose(file_);
    file_ = std::fopen(path.c_str(), "rb");
    sectors_read_ = 0;
    pump_calls_ = 0;
    return file_ != nullptr;
}

uint32_t FileFmvSectorSource::PumpSectors(uint8_t* dst, uint32_t max_sectors) {
    ++pump_calls_;
    if (!file_ || !dst || max_sectors == 0) return 0;
    uint32_t got = 0;
    while (got < max_sectors) {
        uint8_t* sector = dst + static_cast<size_t>(got) * kSectorBytes;
        const size_t n = std::fread(sector, 1, kSectorBytes, file_);
        if (n == 0) break;
        if (n < kSectorBytes) std::memset(sector + n, 0, kSectorBytes - n);
        ++got;
        if (n < kSectorBytes) break;
    }
    sectors_read_ += got;
    return got;
}

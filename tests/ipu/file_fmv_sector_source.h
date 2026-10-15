// tests/ipu/file_fmv_sector_source.h -- FileFmvSectorSource: an FmvSectorSource that
// reads 2048-byte sectors sequentially from a file (a program stream on disk). A
// short final sector is zero-padded; past the end it returns 0.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "engine/ipu/hle/fmv_sector_source.h"

class FileFmvSectorSource : public ps2r::ipu::FmvSectorSource {
public:
    static constexpr uint32_t kSectorBytes = 2048;

    FileFmvSectorSource() = default;
    ~FileFmvSectorSource() override;

    bool Open(const std::string& path);
    bool IsOpen() const { return file_ != nullptr; }
    uint64_t SectorsRead() const { return sectors_read_; }
    uint64_t PumpCalls() const { return pump_calls_; }

    uint32_t PumpSectors(uint8_t* dst, uint32_t max_sectors) override;

private:
    std::FILE* file_ = nullptr;
    uint64_t sectors_read_ = 0;
    uint64_t pump_calls_ = 0;
};

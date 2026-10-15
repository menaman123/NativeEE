#include "engine/ipu/hle/fmv_sector_source_adapter.h"

#include <utility>

namespace ps2r::ipu {

FmvSectorSourceAdapter::FmvSectorSourceAdapter(Reader read) : read_(std::move(read)) {}

uint32_t FmvSectorSourceAdapter::PumpSectors(uint8_t* dst, uint32_t max_sectors) {
    ++pumps_;
    if (!read_ || !dst || max_sectors == 0) return 0;
    const uint32_t n = read_(max_sectors, dst);
    sectors_ += n;
    return n;
}

}  // namespace ps2r::ipu

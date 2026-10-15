// engine/ipu/hle/fmv_sector_source_adapter.h -- FmvSectorSourceAdapter: the
// FmvSectorSource over the libcdvd stream reader's sceCdStRead body. The movie
// player streams its program stream through the sceCdSt* ring; sceMpegGetPicture's
// blocking refill pulls more sectors from the SAME stream position, so the
// game's next sceCdStRead continues where the pump left off (the shared-cursor
// rule). The adapter holds the reader's read function (sectors, dst -> sectors
// delivered) so engine/ipu keeps no link to engine/cdvd; 0 sectors when the
// stream is inactive or the image is exhausted.
#pragma once

#include <cstdint>
#include <functional>

#include "engine/ipu/hle/fmv_sector_source.h"

namespace ps2r::ipu {

class FmvSectorSourceAdapter : public FmvSectorSource {
public:
    // sceCdStRead over the shared cursor: whole sectors into dst, count delivered.
    using Reader = std::function<uint32_t(uint32_t sectors, uint8_t* dst)>;

    explicit FmvSectorSourceAdapter(Reader read);

    uint32_t PumpSectors(uint8_t* dst, uint32_t max_sectors) override;

    uint64_t Pumps() const { return pumps_; }
    uint64_t SectorsPumped() const { return sectors_; }

private:
    Reader read_;
    uint64_t pumps_ = 0;
    uint64_t sectors_ = 0;
};

}  // namespace ps2r::ipu

// engine/ipu/hle/fmv_sector_source.h -- FmvSectorSource: the CD stream read-ahead
// face of the sceMpegGetPicture block pump. On hardware sceMpegGetPicture blocks
// until the IPU has a whole picture, refilling the bitstream ring from the disc
// stream when it runs dry; the HLE mirrors that by pulling 2048-byte sectors
// from the SAME stream cursor the game's sceCdStRead uses, so the game's next
// read continues where the pump left off (no double feed, no desync).
#pragma once

#include <cstdint>

namespace ps2r::ipu {

class FmvSectorSource {
public:
    virtual ~FmvSectorSource() = default;

    // Reads up to `max_sectors` consecutive 2048-byte sectors into `dst` and
    // advances the shared stream cursor by the number read. Returns the sector
    // count actually read: 0 at end of stream or when no stream is active.
    // Synchronous; never blocks on another thread.
    virtual uint32_t PumpSectors(uint8_t* dst, uint32_t max_sectors) = 0;
};

}  // namespace ps2r::ipu

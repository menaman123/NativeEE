// engine/ipu/ipu_settings.h -- module-local knobs of the FMV/IPU path. A POD
// with the proven defaults; the composition root may copy Config fields into
// it, nothing in engine/ipu reads Config or the environment.
#pragma once

#include <cstdint>

namespace ps2r::ipu {

struct IpuSettings {
    // Bounded decode-ahead: the video decoder is fed only while fewer than this
    // many decoded pictures wait in a context's FIFO; the rest of the elementary
    // stream stays pending and is drained as pictures are served. 0 would mean
    // eager decode of the whole stream (a multi-gigabyte transient) and is not
    // a supported path. The default is the value every baseline leg ran with
    // (finding P7-LEDGER-CAP: the flag PS2_FMV_DECODE_CAP is ledgered DELETE but
    // its default-on arm is the baseline behaviour, so the constant is kept).
    uint32_t decode_ahead_pictures = 64;

    // CD-ROM sectors requested from the FMV sector source per pump step while
    // waiting for a picture (16 sectors = 32 KiB of program stream per step).
    uint32_t sectors_per_pump = 16;

    // Upper bound of sectors one GetPicture call may pump before it gives up
    // waiting for a picture (4096 sectors = 8 MiB), so a stream that never
    // yields a picture cannot stall the caller indefinitely.
    uint32_t max_sector_budget = 4096;
};

}  // namespace ps2r::ipu

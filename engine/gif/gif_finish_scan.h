#pragma once
// GifFinishScan: a side-effect-free GIFtag walker over one GIF path's byte
// stream that COUNTS the A+D FINISH register writes (PACKED descriptor 0x0E
// with register address 0x61) the parser will perform when it reaches the same
// bytes. It mirrors GifParser::ParseSpan's unit rules exactly (a 16-byte tag
// when idle, NLOOP x NREG quadwords of PACKED / REGLIST / IMAGE payload, FLG 3
// consuming quadwords without advancing, NLOOP 0 completing at once) and keeps
// the in-transfer state across Feed calls, as the parser keeps its transfer
// state across packets of the same path. The E425 early FINISH: the EE counts
// the FINISH writes of a chain it has STAGED (PATH2 DIRECT payloads inside a
// VIF1 walk, PATH3 chains at the arbiter submit) and raises the GS interrupt
// then; the parser's later Finish() consumes one pre-raised count per FINISH
// instead of raising again. The scanner's only duty is to never report a FINISH
// the parser will not write (a false positive would leave a pre-raised count
// that swallows the next real FINISH); a FINISH it misses is raised by the
// parser as today.
#include <cstddef>
#include <cstdint>

#include "engine/gif/gif_tag.h"

class GifFinishScan {
public:
    // Scan `size` bytes (whole quadwords; a trailing partial quadword is kept
    // for the next call). Returns the FINISH register writes found.
    uint32_t Feed(const uint8_t* data, size_t size);
    // A tag is accepted and not complete (the parser's STAT.OPH view of this path).
    bool MidPacket() const { return in_transfer_; }
    // The accepted tag is an IMAGE transfer still owed quadwords.
    bool MidImage() const;
    // Back to the packet boundary (the parser's FinishPath3 / RestoreRegisters: a
    // state is captured between packets; a PATH3 pass end drops a non-IMAGE tail).
    void Reset();
    // The parser's bulk-lane setting (GifParserSettings::bulk_lanes): an NLOOP 0
    // PACKED tag without EOP eats one quadword on the generic lane and none on
    // the bulk lane -- the one place the lanes differ in bytes consumed.
    void SetBulkLanes(bool on) { bulk_lanes_ = on; }

private:
    uint32_t ScanUnit(const uint8_t* qw);

    bool in_transfer_ = false;
    bool bulk_lanes_ = true;
    GifTag tag_{};
    GifTagClass cls_{};
    uint32_t loop_ = 0;
    uint32_t reg_ = 0;
    uint8_t tail_[16] = {};
    size_t tail_fill_ = 0;
};

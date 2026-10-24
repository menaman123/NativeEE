#pragma once
// MpegCoeffVlc: the MPEG-2 DCT coefficient variable-length codes (ISO/IEC 13818-2
// Tables B-14 "dct_coefficients table zero" and B-15 "table one"), decoded one
// (run, level) pair at a time over a BitCursor. Pure table arithmetic, no state:
// the IPU's BDEC / IDEC block decode drives it, as the hardware's VLC unit does.
//
// Spec rules carried here:
//   * EOB is "10" in table zero, "0110" in table one.
//   * Table zero's first coefficient of a NON-intra block reads "1s" as run 0 /
//     level +-1 (EOB cannot occur there); every later coefficient reads "11s".
//   * ESCAPE "000001" carries a 6-bit run and, in MPEG-2, a 12-bit two's-complement
//     level (MPEG-1 streams use the 8/16-bit form; the MP1 flag selects it).
//   * The sign bit follows every table code: level = s ? -level : level.
#include <cstdint>

#include "engine/ipu/mpeg_decode.h"

namespace ps2r::ipu {

class MpegCoeffVlc {
public:
    struct Symbol {
        bool eob = false;
        int run = 0;
        int level = 0;
    };
    // Reads one symbol. `table_one` selects B-15 (intra blocks with IVF = 1);
    // `first_nonintra` applies table zero's first-coefficient rule. Returns false
    // when no code matches or the bits run out (the cursor is left where it was).
    static bool Decode(BitCursor& c, bool table_one, bool first_nonintra, bool mpeg1, Symbol& out);
};

}  // namespace ps2r::ipu

// gs/hle/ir/direct_ops.h -- DirectOps: the register-op form of one GIF packet.
//
// One Op is the semantic payload of ONE GIF stream item, pre-split by the mode
// the GS decodes it in (GS User's Manual 7.2 GIFtag, 7.3 data formats):
//   reg 0x00..0x0F       PACKED-layout item: lo/hi are the quadword halves; only
//                        the descriptors the PACKED decoder treats specially occur
//                        (PRIM RGBAQ ST UV XYZF2 XYZ2 FOG XYZF3 XYZ3).
//   reg kModeReglist|a   64-bit register write of `lo` to GS register `a` -- the
//                        A+D descriptor (0x0E: address in the high quadword byte),
//                        the PACKED descriptors decoded as raw registers (TEX0_1/2,
//                        CLAMP_1/2, 0x0B) and every REGLIST item.
//   reg kOpFlush         end of a GIFtag whose EOP bit is set: the draw flush.
//   reg kOpSrcId         producer identity: lo = image hash,
//                        hi = handler<<32 | kick_ord<<16 | (entry_pc & 0x3FFF)
//   reg kOpSrcName       lo = same-process pointer to the producer's static name
//
// PacketToOps walks ONE complete packet exactly as the GIF parser would: PRE ->
// a PRIM item at tag position; NLOOP=0 -> EOP only; PACKED one quadword per
// register item; REGLIST two 64-bit items per quadword, padded to a quadword;
// EOP -> kOpFlush after the payload. It returns false on anything the op form
// cannot express losslessly: IMAGE/DISABLE tags, truncated payloads, trailing
// bytes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2r {
namespace gs {

class DirectOps {
public:
    struct Op {
        uint64_t lo = 0;
        uint64_t hi = 0;
        uint16_t reg = 0;
    };

    static constexpr uint16_t kModeReglist = 0x100;
    static constexpr uint16_t kOpFlush = 0xFFFF;
    static constexpr uint16_t kOpSrcId = 0xFFFE;
    static constexpr uint16_t kOpSrcName = 0xFFFD;

    static bool PacketToOps(const uint8_t* p, size_t n, std::vector<Op>& out);
};

}  // namespace gs
}  // namespace ps2r

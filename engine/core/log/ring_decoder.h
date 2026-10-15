// core/log/ring_decoder.h -- .rlog -> greppable text decoder. The ONE
// implementation of the text form (in-process .txt twins and the offline
// ring_decode tool share it). NOT signal-safe (fopen/malloc): crash paths use
// RingLog's signal-safe binary writer + text tail instead.
//
// TEXT FORM (frozen):
//   RLHDR file=<path> reason=<name> frame=<N> rings=<K> drops=<D> wall_ms=<W> slots_exhausted=<S>
//   RLRING ring=<name> emitted=<N> retained=<M> dropped=<D>
//   RL f=<frame> <ring> <CHAN> <TAG> seq=<S> a0=0x<hex> a1=0x<hex>[ V][ <pretty>]
// Events are merged across rings, ordered by (frame, ring, seq).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/core/log/ring_format.h"

namespace ps2r {
namespace ringlog {

struct DecodedRing {
    std::string        name;
    uint64_t           total_emitted = 0;
    std::vector<Event> events;   // oldest first
};

struct DecodedFile {
    FileHeader               hdr{};
    std::vector<DecodedRing> rings;
};

class RingDecoder {
public:
    static const char* ChannelName(uint8_t ch);
    static const char* ReasonName(uint32_t reason);

    // Per-tag pretty decoding of the packed args (additive to the raw hex).
    static void DecodePretty(const Event& e, char* out, size_t out_sz);

    // Read + validate an .rlog. Returns false with *err set on malformed input
    // (truncated files still decode the complete sections read so far when
    // allow_partial).
    static bool ReadRlog(const char* path, DecodedFile& out, std::string* err,
                         bool allow_partial = true);

    // Decode to a FILE* in the documented text form. Returns the event count.
    static uint64_t WriteDecodedText(const DecodedFile& df, const char* src_path,
                                     std::FILE* out);

    // Convenience: ReadRlog + WriteDecodedText to a path.
    static bool DecodeFileToText(const char* rlog_path, const char* txt_path);
};

}  // namespace ringlog
}  // namespace ps2r

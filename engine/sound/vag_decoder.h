#pragma once
// VagDecoder: the SPU2 ADPCM ("VAG") block decoder -- pure logic, no host
// dependency. The SPU2 stores every sample as 16-byte ADPCM blocks (the PS1
// SPU scheme; ps2tek "SPU2"):
//
//   byte 0    : low nibble = shift (0..12), high nibble = filter (0..4)
//   byte 1    : flag bits (kFlag*)
//   bytes 2-15: 28 nibbles, low nibble first = the earlier sample
//
// Per nibble: sign-extend the 4-bit value, scale by (12 - shift), add the
// two-tap predictor (h1 * F0[filter] + h2 * F1[filter]) >> 6 over the two
// previous decoded samples (the history carries ACROSS blocks), clamp to s16.
//
// Flag semantics are the voice engine's (engine/sound/voice): LOOP_END jumps
// NAX to LSAX and raises ENDX; LOOP_REPEAT with LOOP_END keeps the envelope
// alive (without it the voice is muted); LOOP_START latches the block's
// address into LSAX when read.
//
// Malformed fields (filter > 4, shift > 12) do not occur in well-formed data;
// the policy is filter -> 0 (no prediction), shift -> 12, pinned by the test.
#include <cstdint>

class VagDecoder {
public:
    static constexpr int kBlockBytes = 16;
    static constexpr int kBlockSamples = 28;
    static constexpr uint8_t kFlagLoopEnd = 0x01;
    static constexpr uint8_t kFlagLoopRepeat = 0x02;
    static constexpr uint8_t kFlagLoopStart = 0x04;
    static constexpr uint32_t kMaxShift = 12;
    static constexpr uint32_t kFilterCount = 5;

    // The last two decoded samples (h1 = most recent).
    struct History {
        int32_t h1 = 0;
        int32_t h2 = 0;
    };

    // Decodes one block into 28 signed 16-bit samples, advancing `hist` so
    // consecutive blocks decode seamlessly. Returns the block's flags byte.
    static uint8_t DecodeBlock(const uint8_t block[kBlockBytes], History& hist, int16_t out[kBlockSamples]);
};

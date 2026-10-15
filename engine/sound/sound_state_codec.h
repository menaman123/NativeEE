#pragma once
// SoundStateCodec: the two sound sections of a .ps2st file, owned here and
// registered with the SectionRegistry (both OPTIONAL: a file without them
// stays a v2/v3 file and the core boots silent).
//
//   SNDC (file version 4) -- the host sound core: u32 SPU RAM size + the 2 MB
//   SPU RAM, u32 voice count + 48 VoiceState mirrors, 2 ENDX latches, the
//   48x5 parameter shadows, the 48x2 address shadows, 2+2 core MVOL gains
//   (float), the 2x32 core-parameter shadows, then the 2+2 transfer-handler
//   {addr, arg} pairs. Serialize drains the command ring into the voices first
//   so the file carries what the next pump would have applied. Restore
//   discards anything queued, zeroes the eager key-on holds, imports every
//   field, then republishes NAX/ENVX/VOLX from the imported voices -- the
//   restored core answers the module's polls from the file's voice state,
//   never from a hold left by the pre-load boot.
//
//   SDRX -- the libsdr virtual SPU2 block transfers: per channel {u8 active,
//   u32 buffer index, u64 elapsed} where elapsed is the mixer frame count since
//   Arm (the domain's audio clock).
//
// The codec runs at a boundary on the EE thread with the IOP host thread
// quiescent; it never touches the mixer's voices while a pump is in flight.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

namespace ps2r::sound {
class SpuCore;
namespace hle {
class SdBlockTrans;
}

class SoundStateCodec {
public:
    SoundStateCodec(SpuCore& core, hle::SdBlockTrans& block);

    // Both sections, SDRX then SNDC (the reference's capture order).
    void Register(savestate::SectionRegistry& registry);
    savestate::SectionSpec SndcSpec();
    savestate::SectionSpec SdrxSpec();

    // SNDC
    void SerializeSndc(std::vector<uint8_t>& out);
    static bool ValidateSndc(const std::vector<uint8_t>& payload, std::string* err);
    bool RestoreSndc(const std::vector<uint8_t>& payload, std::string* err);
    // The SNDC payload's exact size (a well-formed section is exactly this long).
    static size_t SndcBytes();

    // SDRX
    void SerializeSdrx(std::vector<uint8_t>& out) const;
    static bool ValidateSdrx(const std::vector<uint8_t>& payload, std::string* err);
    bool RestoreSdrx(const std::vector<uint8_t>& payload, std::string* err);
    static size_t SdrxBytes();

private:
    SpuCore& core_;
    hle::SdBlockTrans& block_;
};

}  // namespace ps2r::sound

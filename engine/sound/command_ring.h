#pragma once
// CommandRing: the typed command queue between the libsd/libsdr producers
// (the IOP host thread running the libsd bodies under the REALTIME dispatch
// policy, the EE thread's sceSdRemote rows) and the ONE consumer, the mixer,
// which applies the queued commands at the start of its next pump. Ordering
// is the PS2 rule being kept: a parameter write, an SSA write and the KON
// that follows them land on the voice in issue order, all of them before the
// next block of samples is produced.
//
// Built on core/threads CmdRing: producers serialise among themselves on a
// producer-only mutex the consumer never touches, the consumer never blocks,
// and an overflow drops the OLDEST command (the SPU2 register file only ever
// holds the latest write) and counts it.
#include <cstddef>
#include <cstdint>

#include "engine/core/threads/cmd_ring.h"

namespace ps2r::sound {

enum class SoundOp : uint8_t {
    SetParam,     // entry, value
    SetAddr,      // entry, value
    Kon,          // core in entry, voice mask in value
    Koff,         // core in entry, voice mask in value
    SilenceAll,   // SdInit / savestate restore
};

struct SoundCommand {
    SoundOp op;
    uint32_t entry;
    uint32_t value;
};

class CommandRing {
public:
    static constexpr size_t kCapacity = 4096;

    // Producer side (any thread). Never fails: when full the oldest command is
    // dropped and counted.
    void Push(const SoundCommand& cmd);
    // Consumer side (the mixer thread only). False when empty.
    bool Pop(SoundCommand& out);
    // Consumer side: discard everything queued.
    void Clear();
    size_t Size() const { return static_cast<size_t>(ring_.SizeApprox()); }
    uint64_t Dropped() const { return ring_.Dropped(); }

private:
    ps2r::CmdRing<SoundCommand, kCapacity> ring_;
};

}  // namespace ps2r::sound

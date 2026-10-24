#pragma once
// VoiceTrace: a passive text record of what the SPU2 voice model is asked to do
// and what it then does -- key-on (with the voice's programmed registers), key-off,
// loop-end, the natural end of a release, the register writes to a keyed voice,
// the voice-RAM transfers, the switch / core-attribute / effect writes the model
// does not act on, and a per-second level census of every keyed voice. Lines go to
// the file the owner opened; timestamps are the core's mixed-frame count (48 kHz
// frames since the first pump, so 48000 = one second of SPU time). Attached to a
// SpuCore by its owner; detached, every hook is one null-pointer branch. The trace
// decides nothing and is never read back by the model.
#include <cstdint>
#include <cstdio>

namespace ps2r::sound {

class Voice;

class VoiceTrace {
public:
    VoiceTrace() = default;
    ~VoiceTrace() { Close(); }
    VoiceTrace(const VoiceTrace&) = delete;
    VoiceTrace& operator=(const VoiceTrace&) = delete;

    // Opens `path` for writing (truncating); false = not opened (nothing is recorded).
    bool Open(const char* path);
    void Close();
    bool IsOpen() const { return f_ != nullptr; }

    // Event lines (the register values are the raw 16-bit programming the key-on saw).
    void KeyOn(uint64_t frame, int vi, uint32_t ssa, uint32_t lsax, uint32_t pitch, uint32_t adsr1, uint32_t adsr2,
               uint32_t voll, uint32_t volr, uint8_t first_block_flags);
    void KeyOff(uint64_t frame, int vi, float env);
    void LoopEnd(uint64_t frame, int vi, uint32_t nax, uint32_t lsax, bool repeat, float env);
    void ReleaseEnd(uint64_t frame, int vi);
    void Param(uint64_t frame, int vi, int func, uint32_t value, bool keyed);
    void Addr(uint64_t frame, int vi, int func, uint32_t value, bool keyed);
    void Transfer(uint64_t frame, uint32_t mode, uint32_t spu_addr, uint32_t size);
    void Switch(uint64_t frame, int core, int func, uint32_t value, bool read);
    // A core attribute (SdSetCoreAttr) or, with `param`, a core parameter (MVOL / EVOL / AVOL / BVOL).
    void CoreAttr(uint64_t frame, int core, int func, uint32_t value, bool param);
    void Unmodelled(uint64_t frame, const char* what, uint32_t a, uint32_t b);
    // One line per keyed voice per census period: its RMS (dBFS, pre master) and envelope.
    void Level(uint64_t frame, int vi, float rms_l, float rms_r, float env, uint32_t nax, int phase);

    uint64_t Lines() const { return lines_; }

private:
    std::FILE* f_ = nullptr;
    uint64_t lines_ = 0;
};

}  // namespace ps2r::sound

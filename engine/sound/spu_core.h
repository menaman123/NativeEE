#pragma once
// SpuCore: the SPU2 voice model behind the libsd / libsdr HLE boundary -- a
// 2 MB byte-addressed SPU RAM, 48 voices (2 cores x 24), the core volumes,
// the ENDX latches, the parameter/address shadows the IOP code reads back,
// and the per-core transfer-completion handler registry. Commands go through
// the CommandRing and are applied by MixInto (the tick-clocked mixer pump);
// readbacks (NAX/ENVX/VOLX/ENDX) are refreshed after every 128-frame chunk.
//
// Threading: the producers (the IOP host thread running the libsd bodies
// under the REALTIME dispatch policy, the EE thread's sceSdRemote rows) and
// the one consumer (the mixer pump) share only the ring and the atomics
// below -- the voices belong to the consumer. Every published field is an
// atomic so a poll never reads a torn or racing value. NAX, ENVX and the
// key-on hold count (KONs queued but not yet applied) of a voice live in ONE
// 64-bit word: the key-on raises the hold and publishes the eager NAX/ENVX
// with a single CAS, and the pump republishes the voice's values with a CAS
// that only succeeds while the word still shows no hold. A poll that reads
// the hold up therefore always finds the eager values beside it; the pump
// can never interpose the stale pre-KON model (ENVX 0, the SPU2 driver's
// watchdog-abort value) between a key-on and its poll.
//
// Entry codes are libsd's register-addressing scheme (the same the module's
// decompilation uses):
//   voice params  (entry>>8): 0 VOLL, 1 VOLR, 2 PITCH, 3 ADSR1, 4 ADSR2,
//                             5 ENVX (read), 6 VOLXL, 7 VOLXR (read);
//                             low byte = voice<<1 | core.
//   core params   (bit 7 set): 9 MVOLL, 0xA MVOLR, 0xB EVOLL, 0xC EVOLR,
//                             0xF BVOLL, 0x10 BVOLR (stored; MVOL audible).
//   addresses     (entry>>8): 0x1D EEA (ignored), 0x20 SSA, 0x21 LSAX,
//                             0x22 NAX (read).
//   switches      (entry>>8): 0x15 KON, 0x16 KOFF, 0x17 ENDX (read latch;
//                             any write clears the core's latch).
//
// A key-on publishes its readbacks eagerly (NAX = the SSA shadow, ENVX = the
// first attack step) so a poll between the KON and the next pump sees the
// voice started -- the SPU2 starts the voice within one sample tick, long
// before the next 800-frame pump.
#include <atomic>
#include <cstdint>
#include <memory>

#include "engine/sound/command_ring.h"
#include "engine/sound/voice.h"

namespace ps2r::sound {

class FmvPcmInput;
class PcmSource;
class VoiceSource;

class SpuCore {
public:
    static constexpr int kCores = 2;
    static constexpr int kVoicesPerCore = 24;
    static constexpr int kVoices = kCores * kVoicesPerCore;
    static constexpr int kCoreParams = 0x20;
    static constexpr uint32_t kSilenceBlockAddr = 0x5000;   // libsd's silence block
    static constexpr int kBvoll = 0xF;    // core-attribute shadow index: external input volume L
    static constexpr int kBvolr = 0x10;   // external input volume R

    SpuCore();

    // libsd SdInit: the silence block (LOOP_END|LOOP_REPEAT, zero samples) is
    // written at 0x5000 and every voice is silenced.
    void SdInit();
    void SetParam(uint32_t entry, uint32_t value);
    uint32_t GetParam(uint32_t entry) const;
    void SetSwitch(uint32_t entry, uint32_t value);
    uint32_t GetSwitch(uint32_t entry) const;
    void SetAddr(uint32_t entry, uint32_t value);
    uint32_t GetAddr(uint32_t entry) const;
    void SetCoreAttr(uint32_t entry, uint32_t value);
    uint32_t CoreAttr(int core, int func) const {
        return core_param_shadow_[core & 1][func & 0x1F].load(std::memory_order_relaxed);
    }
    // The core-attribute entry's index (SD_CORE_* codes: attribute<<1 | core).
    static int CoreAttrIndex(uint32_t entry) { return static_cast<int>((entry >> 1) & 0x1F); }

    // Voice transfer (SPU DMA): mode&3 = 0/3 write host->SPU RAM, 1 read back,
    // 2 stop. Completes synchronously; returns the byte count moved. The
    // caller fires the registered completion handler afterwards.
    uint32_t VoiceTrans(int chan, uint32_t mode, uint8_t* host, uint32_t spu_addr, uint32_t size);
    int VoiceTransStatus(int chan, int flag) const;
    void SetTransIntrHandler(int chan, uint32_t handler, uint32_t arg);
    bool GetTransIntrHandler(int chan, uint32_t& handler, uint32_t& arg) const;

    // Mix `frames` interleaved stereo float frames: drain the ring, advance the
    // voices, latch ENDX, refresh the readbacks. Consumer thread only. With an
    // FMV PCM input attached its frames are added AFTER the voice mix at the
    // core-0 BVOLL/BVOLR gain (the SPU2's external-input volume, which the
    // title programs to 0x7FFF at FMV start) and before the master gain --
    // the SPU2 mixes the core's external audio input into its output beside
    // the voices. Consumption runs whenever the gate is open (a muted core
    // still drains the queue), so the movie's A/V clock keeps flowing.
    void MixInto(float* stereo_interleaved, int frames, VoiceSource* voices = nullptr, PcmSource* pcm = nullptr);
    uint64_t MixCalls() const { return mix_calls_; }
    // Attach / detach the FMV PCM source (nullptr = none; the default).
    void SetFmvInput(FmvPcmInput* input) { fmv_input_ = input; }
    FmvPcmInput* FmvInput() const { return fmv_input_; }
    // The BVOLL/BVOLR fixed-volume gain: bit 15 (a sweep mode, never used for
    // BVOL by the titles this models) falls back to unity, else the 15-bit
    // linear value over 0x7FFF.
    static float ExternalInputGain(uint32_t bvol_raw) {
        return (bvol_raw & 0x8000) ? 1.0f : static_cast<float>(bvol_raw & 0x7FFF) / 32767.0f;
    }
    float FmvGainL() const { return ExternalInputGain(core_param_shadow_[0][kBvoll].load(std::memory_order_relaxed)); }
    float FmvGainR() const { return ExternalInputGain(core_param_shadow_[0][kBvolr].load(std::memory_order_relaxed)); }
    uint64_t FmvFramesMixed() const { return fmv_frames_mixed_; }
    // Actual retained-input consumption attempts. The software provider does
    // not read the BVOL shadows or call the retained source on this path.
    uint64_t FmvModelPops() const { return fmv_model_pops_; }
    // FMV frames mixed while the core-0 BVOL gain was non-zero (audible ones).
    uint64_t FmvFramesMixedAudible() const { return fmv_frames_mixed_audible_; }
    // The core-0 BVOLL/BVOLR programming census: every SetParam on either
    // register, those with a non-zero value, and the largest value seen.
    uint64_t BvolSets() const { return bvol_sets_.load(std::memory_order_relaxed); }
    uint64_t BvolSetsNonZero() const { return bvol_sets_nonzero_.load(std::memory_order_relaxed); }
    uint32_t BvolPeak() const { return bvol_peak_.load(std::memory_order_relaxed); }
    // Apply every queued command to the voices without mixing (the savestate
    // capture: the file carries what the next pump would have applied).
    void ApplyQueued();

    uint8_t* SpuRam() { return spu_ram_.get(); }
    const uint8_t* SpuRam() const { return spu_ram_.get(); }
    Voice& VoiceAt(int index) { return voices_[index]; }
    const Voice& VoiceAt(int index) const { return voices_[index]; }
    uint32_t Endx(int core) const { return endx_[core & 1].load(std::memory_order_relaxed); }
    void SetEndx(int core, uint32_t v) { endx_[core & 1].store(v, std::memory_order_relaxed); }
    float MasterVolume() const { return master_.load(std::memory_order_relaxed); }
    void SetMasterVolume(float v) { master_.store(v, std::memory_order_relaxed); }
    int ActiveVoices() const { return active_voices_.load(std::memory_order_relaxed); }
    uint64_t KonCount() const { return kon_count_.load(std::memory_order_relaxed); }
    float LastRms() const { return last_rms_.load(std::memory_order_relaxed); }
    CommandRing& Ring() { return ring_; }
    uint32_t ParamShadow(int vi, int func) const { return param_shadow_[vi][func].load(std::memory_order_relaxed); }
    uint32_t AddrShadow(int vi, int which) const { return addr_shadow_[vi][which].load(std::memory_order_relaxed); }
    void SetParamShadow(int vi, int func, uint32_t v) { param_shadow_[vi][func].store(v, std::memory_order_relaxed); }
    void SetAddrShadow(int vi, int which, uint32_t v) { addr_shadow_[vi][which].store(v, std::memory_order_relaxed); }
    void SetCoreParamShadow(int core, int func, uint32_t v);
    // The savestate codec's raw fields: the core gains and the attribute
    // shadow as stored (SetCoreParamShadow would re-derive the gains).
    float MvolL(int core) const { return mvol_l_[core & 1].load(std::memory_order_relaxed); }
    float MvolR(int core) const { return mvol_r_[core & 1].load(std::memory_order_relaxed); }
    void SetMvolL(int core, float g) { mvol_l_[core & 1].store(g, std::memory_order_relaxed); }
    void SetMvolR(int core, float g) { mvol_r_[core & 1].store(g, std::memory_order_relaxed); }
    void SetCoreAttrShadow(int core, int func, uint32_t v) {
        core_param_shadow_[core & 1][func & 0x1F].store(v, std::memory_order_relaxed);
    }
    uint32_t EagerHold(int vi) const { return ReadbackHold(readback_[vi].load(std::memory_order_relaxed)); }
    void ClearEagerHolds();
    // Refresh NAX/ENVX/VOLX from the voices (after a savestate restore).
    void RepublishReadbacks();

    static int EntryCore(uint32_t e) { return static_cast<int>(e & 1); }
    static int EntryFunc(uint32_t e) { return static_cast<int>((e >> 8) & 0xFF); }
    static bool EntryIsCoreParam(uint32_t e) { return (e & 0x80) != 0; }
    static int EntryVoiceIndex(uint32_t e);

private:
    // The per-voice readback word: NAX in the low 32 bits, ENVX above it,
    // the key-on hold count (saturating) in the top 16 bits.
    static constexpr int kEnvxShift = 32;
    static constexpr int kHoldShift = 48;
    static constexpr uint64_t kHoldMax = 0xFFFF;
    static uint64_t PackReadback(uint32_t hold, uint32_t envx, uint32_t nax) {
        return (static_cast<uint64_t>(hold & kHoldMax) << kHoldShift) |
               (static_cast<uint64_t>(envx & 0xFFFF) << kEnvxShift) | nax;
    }
    static uint32_t ReadbackHold(uint64_t w) { return static_cast<uint32_t>(w >> kHoldShift); }
    static uint32_t ReadbackEnvx(uint64_t w) { return static_cast<uint32_t>((w >> kEnvxShift) & 0xFFFF); }
    static uint32_t ReadbackNax(uint64_t w) { return static_cast<uint32_t>(w); }

    void ApplyCommand(const SoundCommand& cmd);
    void PublishKeyOn(int vi);
    void ReleaseKeyOnHold(int vi);
    uint32_t EagerEnvx0(int vi) const;

    std::unique_ptr<uint8_t[]> spu_ram_;
    CommandRing ring_;
    Voice voices_[kVoices];
    std::atomic<uint64_t> readback_[kVoices] = {};
    std::atomic<uint32_t> volx_l_[kVoices] = {};
    std::atomic<uint32_t> volx_r_[kVoices] = {};
    std::atomic<uint32_t> endx_[kCores] = {};
    std::atomic<uint32_t> param_shadow_[kVoices][5] = {};
    std::atomic<uint32_t> addr_shadow_[kVoices][2] = {};
    std::atomic<float> mvol_l_[kCores] = {};
    std::atomic<float> mvol_r_[kCores] = {};
    std::atomic<uint32_t> core_param_shadow_[kCores][kCoreParams] = {};
    std::atomic<uint32_t> handler_addr_[kCores] = {};
    std::atomic<uint32_t> handler_arg_[kCores] = {};
    std::atomic<float> master_{1.0f};
    std::atomic<int> active_voices_{0};
    std::atomic<uint64_t> kon_count_{0};
    std::atomic<float> last_rms_{0.0f};
    FmvPcmInput* fmv_input_ = nullptr;
    uint64_t fmv_frames_mixed_ = 0;
    uint64_t fmv_model_pops_ = 0;
    uint64_t fmv_frames_mixed_audible_ = 0;
    std::atomic<uint64_t> bvol_sets_{0};
    std::atomic<uint64_t> bvol_sets_nonzero_{0};
    std::atomic<uint32_t> bvol_peak_{0};
    uint64_t mix_calls_ = 0; // execution census, deliberately not snapshot state
};

}  // namespace ps2r::sound

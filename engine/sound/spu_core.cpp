#include "engine/sound/spu_core.h"
#include "engine/sound/voice_source.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/sound/fmv_pcm_input.h"
#include "engine/sound/pcm_source.h"
#include "engine/sound/voice_trace.h"

namespace ps2r::sound {

namespace {
constexpr auto kRelaxed = std::memory_order_relaxed;
constexpr auto kSeqCst = std::memory_order_seq_cst;
}  // namespace

SpuCore::SpuCore() : spu_ram_(new uint8_t[kSpuRamSize]) {
    std::memset(spu_ram_.get(), 0, kSpuRamSize);
    ResetSwitchShadows();
    ResetRoutingDefaults();
}

// libsd's SdInit programs core 1's external input (core 0's output) at full scale so
// core 0 is audible; every other core gain starts at 0 until the title writes it.
void SpuCore::ResetRoutingDefaults() {
    core_param_shadow_[1][kAvoll].store(0x7FFF, kRelaxed);
    core_param_shadow_[1][kAvolr].store(0x7FFF, kRelaxed);
}

// The register file after SdInit (libsd's reset): KON/KOFF/PMON/NON clear, every
// voice's dry and wet mix enables on.
void SpuCore::ResetSwitchShadows() {
    for (int c = 0; c < kCores; c++)
        for (int f = kSwitchFirst; f <= kSwitchLast; f++)
            switch_shadow_[c][f - kSwitchFirst].store((f >= kSwVmixl && f <= kSwVmixer) ? 0xFFFFFFu : 0u, kRelaxed);
}

int SpuCore::EntryVoiceIndex(uint32_t e) {
    const int v = static_cast<int>((e >> 1) & 0x1F);
    if (v >= kVoicesPerCore) return -1;
    return EntryCore(e) * kVoicesPerCore + v;
}

void SpuCore::SdInit() {
    uint8_t* blk = spu_ram_.get() + kSilenceBlockAddr;
    std::memset(blk, 0, VagDecoder::kBlockBytes);
    blk[1] = VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat;
    ResetSwitchShadows();
    ResetRoutingDefaults();
    ring_.Push(SoundCommand{SoundOp::SilenceAll, 0, 0});
}

void SpuCore::SetCoreParamShadow(int core, int func, uint32_t value) {
    core &= 1;
    func &= 0x1F;
    core_param_shadow_[core][func].store(value, kRelaxed);
    if (!(value & 0x8000)) {
        // A fixed MVOL write: the level << 1 as a signed 16-bit value (0x3FFF = full scale).
        if (func == kMvoll) mvol_l_[core].store(MasterRegisterGain(value), kRelaxed);
        if (func == kMvolr) mvol_r_[core].store(MasterRegisterGain(value), kRelaxed);
    }
    if (core == 0 && (func == kBvoll || func == kBvolr)) {
        bvol_sets_.fetch_add(1, kRelaxed);
        if (value != 0) bvol_sets_nonzero_.fetch_add(1, kRelaxed);
        uint32_t peak = bvol_peak_.load(kRelaxed);
        while (value > peak && !bvol_peak_.compare_exchange_weak(peak, value, kRelaxed, kRelaxed)) {}
    }
}

void SpuCore::SetParam(uint32_t entry, uint32_t value) {
    if (EntryIsCoreParam(entry)) {
        SetCoreParamShadow(EntryCore(entry), EntryFunc(entry), value);
        if (trace_) ring_.Push(SoundCommand{SoundOp::TraceCoreAttr, static_cast<uint32_t>(EntryCore(entry)) | ((EntryFunc(entry) & 0xFF) << 8) | 0x10000u, value});
        return;
    }
    const int vi = EntryVoiceIndex(entry);
    const int func = EntryFunc(entry);
    if (vi < 0 || func > 4) return;   // ENVX and beyond are not writable
    param_shadow_[vi][func].store(value, kRelaxed);
    ring_.Push(SoundCommand{SoundOp::SetParam, entry, value});
}

uint32_t SpuCore::EagerEnvx0(int vi) const {
    // The envelope one sample after the key-on: the attack's first rate step from 0
    // (the SPU2 starts the voice within a sample tick; a poll before the next pump
    // sees it moving, never the stale pre-KON 0 a driver's watchdog reads as dead).
    const uint16_t adsr1 = static_cast<uint16_t>(param_shadow_[vi][3].load(kRelaxed));
    uint32_t counter = 0;
    const int32_t env = Voice::RateStep((adsr1 >> 15) & 1, false, (adsr1 >> 10) & 0x1F, 7 - ((adsr1 >> 8) & 0x3), 0, counter, 0, Voice::kEnvMax);
    return static_cast<uint32_t>(env);
}

uint32_t SpuCore::GetParam(uint32_t entry) const {
    if (EntryIsCoreParam(entry)) return core_param_shadow_[EntryCore(entry)][EntryFunc(entry) & 0x1F].load(kRelaxed);
    const int vi = EntryVoiceIndex(entry);
    if (vi < 0) return 0;
    const int func = EntryFunc(entry);
    if (func == 0x5) return ReadbackEnvx(readback_[vi].load(kRelaxed));
    if (func == 0x6) return volx_l_[vi].load(kRelaxed);
    if (func == 0x7) return volx_r_[vi].load(kRelaxed);
    if (func <= 4) return param_shadow_[vi][func].load(kRelaxed);
    return 0;
}

void SpuCore::PublishKeyOn(int vi) {
    // One CAS raises the hold and publishes the eager NAX (the SSA shadow)
    // and ENVX (the first attack step) together: no observer can see the
    // hold up beside anything but these values. The hold saturates; a
    // dropped Kon (ring overflow) leaves it raised until SilenceAll.
    const uint32_t nax = addr_shadow_[vi][0].load(kRelaxed);
    const uint32_t envx = EagerEnvx0(vi);
    uint64_t cur = readback_[vi].load(kRelaxed);
    for (;;) {
        const uint32_t hold = ReadbackHold(cur);
        const uint64_t next = PackReadback(hold < kHoldMax ? hold + 1 : hold, envx, nax);
        if (readback_[vi].compare_exchange_weak(cur, next, kSeqCst, kRelaxed)) return;
    }
}

void SpuCore::ReleaseKeyOnHold(int vi) {
    uint64_t cur = readback_[vi].load(kRelaxed);
    for (;;) {
        const uint32_t hold = ReadbackHold(cur);
        if (hold == 0) return;
        const uint64_t next = PackReadback(hold - 1, ReadbackEnvx(cur), ReadbackNax(cur));
        if (readback_[vi].compare_exchange_weak(cur, next, kSeqCst, kRelaxed)) return;
    }
}

void SpuCore::SetSwitch(uint32_t entry, uint32_t value) {
    const int func = EntryFunc(entry);
    const int core = EntryCore(entry);
    if (trace_) ring_.Push(SoundCommand{SoundOp::TraceSwitch, static_cast<uint32_t>(core) | (static_cast<uint32_t>(func) << 8), value});
    if (func >= kSwitchFirst && func <= kSwitchLast) switch_shadow_[core][func - kSwitchFirst].store(value, kRelaxed);
    switch (func) {
        case 0x15: {   // KON: voice bit mask for this core
            for (int v = 0; v < kVoicesPerCore; v++) {
                if (!(value & (1u << v))) continue;
                PublishKeyOn(core * kVoicesPerCore + v);
                kon_count_.fetch_add(1, kRelaxed);
            }
            ring_.Push(SoundCommand{SoundOp::Kon, static_cast<uint32_t>(core), value});
            break;
        }
        case 0x16:
            ring_.Push(SoundCommand{SoundOp::Koff, static_cast<uint32_t>(core), value});
            break;
        case 0x17:   // ENDX: any write clears the core's latch
            endx_[core].store(0, kRelaxed);
            break;
        default:   // PMON / NON / the wet gates: stored above (readback), not acted on; VMIXL/VMIXR gate at the mix
            break;
    }
}

uint32_t SpuCore::GetSwitch(uint32_t entry) const {
    const uint32_t v = EntryFunc(entry) == 0x17 ? endx_[EntryCore(entry)].load(kRelaxed) : SwitchShadow(EntryCore(entry), EntryFunc(entry));
    if (trace_) ring_.Push(SoundCommand{SoundOp::TraceSwitch, static_cast<uint32_t>(EntryCore(entry)) | (static_cast<uint32_t>(EntryFunc(entry)) << 8) | 0x10000u, v});
    return v;
}

void SpuCore::SetAddr(uint32_t entry, uint32_t value) {
    const int func = EntryFunc(entry);
    if (func == 0x1D) return;   // effect-area end address: no reverb
    const int vi = EntryVoiceIndex(entry);
    if (vi < 0) return;
    if (func == 0x20) addr_shadow_[vi][0].store(value, kRelaxed);
    if (func == 0x21) addr_shadow_[vi][1].store(value, kRelaxed);
    if (func == 0x20 || func == 0x21) ring_.Push(SoundCommand{SoundOp::SetAddr, entry, value});
}

uint32_t SpuCore::GetAddr(uint32_t entry) const {
    const int func = EntryFunc(entry);
    const int vi = EntryVoiceIndex(entry);
    if (vi < 0) return 0;
    if (func == 0x22) return ReadbackNax(readback_[vi].load(kRelaxed));
    if (func == 0x20) return addr_shadow_[vi][0].load(kRelaxed);
    if (func == 0x21) return addr_shadow_[vi][1].load(kRelaxed);
    return 0;
}

void SpuCore::SetCoreAttr(uint32_t entry, uint32_t value) {
    core_param_shadow_[EntryCore(entry)][(entry >> 1) & 0x1F].store(value, kRelaxed);
    if (trace_) ring_.Push(SoundCommand{SoundOp::TraceCoreAttr, static_cast<uint32_t>(EntryCore(entry)) | (((entry >> 1) & 0x1F) << 8), value});
}

uint32_t SpuCore::VoiceTrans(int chan, uint32_t mode, uint8_t* host, uint32_t spu_addr, uint32_t size) {
    (void)chan;
    if (!host || size == 0) return 0;
    const uint32_t dir = mode & 0x3;
    if (dir == 0x2) return 0;   // STOP: nothing in flight (transfers are synchronous)
    spu_addr &= kSpuRamSize - 1;
    const uint32_t n = std::min<uint32_t>(size, kSpuRamSize - spu_addr);
    if (dir == 0x1)
        std::memcpy(host, spu_ram_.get() + spu_addr, n);
    else
        std::memcpy(spu_ram_.get() + spu_addr, host, n);
    if (trace_) ring_.Push(SoundCommand{SoundOp::TraceTransfer, spu_addr, (n & 0xFFFFFFu) | (dir << 24)});
    return n;
}

int SpuCore::VoiceTransStatus(int chan, int flag) const {
    (void)chan;
    (void)flag;
    return 1;   // idle: every transfer completed synchronously
}

void SpuCore::SetTransIntrHandler(int chan, uint32_t handler, uint32_t arg) {
    handler_addr_[chan & 1].store(handler, kRelaxed);
    handler_arg_[chan & 1].store(arg, kRelaxed);
}

bool SpuCore::GetTransIntrHandler(int chan, uint32_t& handler, uint32_t& arg) const {
    handler = handler_addr_[chan & 1].load(kRelaxed);
    arg = handler_arg_[chan & 1].load(kRelaxed);
    return handler != 0;
}

void SpuCore::ClearEagerHolds() {
    for (int vi = 0; vi < kVoices; vi++) {
        uint64_t cur = readback_[vi].load(kRelaxed);
        while (!readback_[vi].compare_exchange_weak(cur, PackReadback(0, ReadbackEnvx(cur), ReadbackNax(cur)), kSeqCst, kRelaxed)) {
        }
    }
}

void SpuCore::ApplyCommand(const SoundCommand& cmd) {
    switch (cmd.op) {
        case SoundOp::SetParam: {
            const int vi = EntryVoiceIndex(cmd.entry);
            if (vi < 0) break;
            Voice& v = voices_[vi];
            if (trace_) trace_->Param(frames_mixed_, vi, EntryFunc(cmd.entry), cmd.value, v.Keyed());
            switch (EntryFunc(cmd.entry)) {
                case 0x0: v.SetVolL(static_cast<uint16_t>(cmd.value)); break;
                case 0x1: v.SetVolR(static_cast<uint16_t>(cmd.value)); break;
                case 0x2: v.SetPitch(static_cast<uint16_t>(cmd.value)); break;
                case 0x3: v.SetAdsr1(static_cast<uint16_t>(cmd.value)); break;
                case 0x4: v.SetAdsr2(static_cast<uint16_t>(cmd.value)); break;
                default: break;
            }
            break;
        }
        case SoundOp::SetAddr: {
            const int vi = EntryVoiceIndex(cmd.entry);
            if (vi < 0) break;
            if (trace_) trace_->Addr(frames_mixed_, vi, EntryFunc(cmd.entry), cmd.value, voices_[vi].Keyed());
            if (EntryFunc(cmd.entry) == 0x20) voices_[vi].SetSsa(cmd.value);
            if (EntryFunc(cmd.entry) == 0x21) voices_[vi].SetLsax(cmd.value);
            break;
        }
        case SoundOp::Kon:
            for (int v = 0; v < kVoicesPerCore; v++) {
                if (!(cmd.value & (1u << v))) continue;
                const int vi = static_cast<int>(cmd.entry & 1) * kVoicesPerCore + v;
                voices_[vi].KeyOn(spu_ram_.get());
                // The voice now carries the truth: release the eager hold.
                ReleaseKeyOnHold(vi);
                if (trace_)
                    trace_->KeyOn(frames_mixed_, vi, addr_shadow_[vi][0].load(kRelaxed), voices_[vi].Lsax(),
                                  param_shadow_[vi][2].load(kRelaxed), param_shadow_[vi][3].load(kRelaxed),
                                  param_shadow_[vi][4].load(kRelaxed), param_shadow_[vi][0].load(kRelaxed),
                                  param_shadow_[vi][1].load(kRelaxed), voices_[vi].BlockFlags());
            }
            break;
        case SoundOp::Koff:
            for (int v = 0; v < kVoicesPerCore; v++) {
                if (!(cmd.value & (1u << v))) continue;
                const int vi = static_cast<int>(cmd.entry & 1) * kVoicesPerCore + v;
                if (trace_) trace_->KeyOff(frames_mixed_, vi, voices_[vi].Env());
                voices_[vi].KeyOff();
            }
            break;
        case SoundOp::SilenceAll:
            // SdInit / savestate reset: the natural re-zero point for holds a
            // dropped Kon (overflow) could have left behind.
            for (auto& v : voices_) v.Silence();
            for (int i = 0; i < kCores; i++) endx_[i].store(0, kRelaxed);
            ClearEagerHolds();
            if (trace_) trace_->Unmodelled(frames_mixed_, "silence_all", 0, 0);
            break;
        case SoundOp::TraceSwitch:
            if (trace_) trace_->Switch(frames_mixed_, static_cast<int>(cmd.entry & 1), static_cast<int>((cmd.entry >> 8) & 0xFF), cmd.value, (cmd.entry & 0x10000u) != 0);
            break;
        case SoundOp::TraceCoreAttr:
            if (trace_) trace_->CoreAttr(frames_mixed_, static_cast<int>(cmd.entry & 1), static_cast<int>((cmd.entry >> 8) & 0xFF), cmd.value, (cmd.entry & 0x10000u) != 0);
            break;
        case SoundOp::TraceTransfer:
            if (trace_) trace_->Transfer(frames_mixed_, cmd.value >> 24, cmd.entry, cmd.value & 0xFFFFFFu);
            break;
        case SoundOp::TraceNote:
            if (trace_) trace_->Unmodelled(frames_mixed_, "libsd_ordinal", cmd.entry, cmd.value);
            break;
    }
}

// One sample of the MVOL registers: a fixed write (bit 15 clear) holds the gain the
// shadow derived; a sweep (bit 15 set: bit 14 exponential, bit 13 decrease, bit 12
// negative phase, bits 6-2 shift, bits 1-0 step) ramps the live level on the SPU2
// rate engine from wherever it stands.
void SpuCore::StepMasterSweeps() {
    for (int c = 0; c < kCores; c++) {
        for (int ch = 0; ch < 2; ch++) {
            const uint32_t reg = core_param_shadow_[c][ch == 0 ? kMvoll : kMvolr].load(kRelaxed);
            if (!(reg & 0x8000)) {
                const float g = (ch == 0 ? mvol_l_[c] : mvol_r_[c]).load(kRelaxed);
                mvol_level_[c][ch] = static_cast<int32_t>(g * 32768.0f);
                mvol_counter_[c][ch] = 0;
                continue;
            }
            const bool exp = (reg & 0x4000) != 0, decr = (reg & 0x2000) != 0, neg = (reg & 0x1000) != 0;
            const int shift = static_cast<int>((reg >> 2) & 0x1F), step_field = static_cast<int>(reg & 0x3);
            int32_t step = 7 - step_field;
            if (decr) step = ~step;
            if (!(exp && decr) && neg) step = -step;
            int32_t lo = INT16_MIN, hi = INT16_MAX;
            if (decr && !exp) {
                lo = neg ? INT16_MIN : 0;
                hi = neg ? 0 : INT16_MAX;
            } else if (decr) {
                lo = 0;
            }
            mvol_level_[c][ch] = Voice::RateStep(exp, decr, shift, step, mvol_level_[c][ch], mvol_counter_[c][ch], lo, hi);
            (ch == 0 ? mvol_l_[c] : mvol_r_[c]).store(static_cast<float>(mvol_level_[c][ch]) / 32768.0f, kRelaxed);
        }
    }
}

void SpuCore::RepublishReadbacks() {
    for (int vi = 0; vi < kVoices; vi++) {
        // A voice whose KON is still in the ring holds the stale pre-KON
        // model; the eager values published by the key-on stand until the
        // Kon is applied. The CAS publishes the model only if the word still
        // shows no hold: a key-on that lands after the load changes the hold
        // field, the CAS fails, and the eager values stay. No transient stale
        // value is ever visible beside a raised hold.
        uint64_t cur = readback_[vi].load(kSeqCst);
        if (ReadbackHold(cur) == 0)
            readback_[vi].compare_exchange_strong(cur, PackReadback(0, voices_[vi].Envx(), voices_[vi].Nax()), kSeqCst, kRelaxed);
        volx_l_[vi].store(voices_[vi].VolxL(), kRelaxed);
        volx_r_[vi].store(voices_[vi].VolxR(), kRelaxed);
    }
}

void SpuCore::ApplyQueued() {
    SoundCommand cmd;
    while (ring_.Pop(cmd)) ApplyCommand(cmd);
}

void SpuCore::MixInto(float* out, int frames, VoiceSource* source, PcmSource* pcm) {
    ++mix_calls_;
    ApplyQueued();
    const uint8_t* ram = spu_ram_.get();
    int active = 0;
    for (int vi = 0; vi < kVoices; vi++)
        if (voices_[vi].Audible()) active++;
    const float master = master_.load(kRelaxed);
    // Read the selected software intent or retained register gains once.
    const auto gains = pcm ? pcm->Gains() : std::array<float, 2>{FmvGainL(), FmvGainR()};
    const float fmv_gl = gains[0], fmv_gr = gains[1];
    // Core 1's external input gain (core 0's output arrives there) and the dry gates.
    const float avol_l = ExternalInputGain(core_param_shadow_[1][kAvoll].load(kRelaxed));
    const float avol_r = ExternalInputGain(core_param_shadow_[1][kAvolr].load(kRelaxed));
    uint32_t vmixl[kCores], vmixr[kCores];
    for (int c = 0; c < kCores; c++) {
        vmixl[c] = SwitchShadow(c, kSwVmixl);
        vmixr[c] = SwitchShadow(c, kSwVmixr);
    }
    double sum_sq = 0.0;
    constexpr int kChunk = 128;   // readbacks refresh every ~2.7 ms of samples
    int16_t fmv_buf[kChunk * 2];
    for (int base = 0; base < frames; base += kChunk) {
        const int n = std::min(kChunk, frames - base);
        // The FMV frames for this chunk (0 while the gate is closed or the queue is
        // empty); popped before the voices step so a muted core still consumes.
        int fmv_n = 0;
        if (pcm) fmv_n = pcm->Pop(fmv_buf, n);
        else if (fmv_input_) {
            ++fmv_model_pops_;
            fmv_n = fmv_input_->Pop(fmv_buf, n);
        }
        fmv_frames_mixed_ += static_cast<uint64_t>(fmv_n);
        if (fmv_gl != 0.0f || fmv_gr != 0.0f) fmv_frames_mixed_audible_ += static_cast<uint64_t>(fmv_n);
        for (int i = 0; i < n; i++) {
            float core_l[kCores] = {0.0f, 0.0f}, core_r[kCores] = {0.0f, 0.0f};
            for (int vi = 0; vi < kVoices; vi++) {
                float l = 0, r = 0;
                const int core = vi / kVoicesPerCore;
                const uint32_t bit = 1u << (vi % kVoicesPerCore);
                if (!source || !source->StepVoice(static_cast<uint32_t>(vi), l, r)) {
                    Voice& v = voices_[vi];
                    if (!v.Keyed()) continue;
                    const bool end = v.Step(ram, l, r);
                    if (end) endx_[core].fetch_or(1u << (vi % kVoicesPerCore), kRelaxed);
                    if (trace_) {
                        if (end) trace_->LoopEnd(frames_mixed_ + base + i, vi, v.Nax(), v.Lsax(), v.LastEndRepeat(), v.Env());
                        if (!v.Keyed()) trace_->ReleaseEnd(frames_mixed_ + base + i, vi);
                        trace_sq_l_[vi] += static_cast<double>(l) * l;
                        trace_sq_r_[vi] += static_cast<double>(r) * r;
                    }
                }
                if (vmixl[core] & bit) core_l[core] += l;
                if (vmixr[core] & bit) core_r[core] += r;
            }
            // The external audio input enters core 0's mix at the BVOL gain beside its voices.
            if (i < fmv_n) {
                core_l[0] += static_cast<float>(fmv_buf[i * 2]) / 32768.0f * fmv_gl;
                core_r[0] += static_cast<float>(fmv_buf[i * 2 + 1]) / 32768.0f * fmv_gr;
            }
            StepMasterSweeps();
            const float m0l = static_cast<float>(mvol_level_[0][0]) / 32768.0f, m0r = static_cast<float>(mvol_level_[0][1]) / 32768.0f;
            const float m1l = static_cast<float>(mvol_level_[1][0]) / 32768.0f, m1r = static_cast<float>(mvol_level_[1][1]) / 32768.0f;
            // Core 0 -> (MVOL0) -> core 1's external input (AVOL1) -> + core 1 -> (MVOL1).
            const float ext_l = std::clamp(core_l[0], -1.0f, 1.0f) * m0l * avol_l;
            const float ext_r = std::clamp(core_r[0], -1.0f, 1.0f) * m0r * avol_r;
            const float mix_l = std::clamp(core_l[1] + ext_l, -1.0f, 1.0f) * m1l;
            const float mix_r = std::clamp(core_r[1] + ext_r, -1.0f, 1.0f) * m1r;
            const float ol = std::clamp(mix_l * master, -1.0f, 1.0f);
            const float orr = std::clamp(mix_r * master, -1.0f, 1.0f);
            out[(base + i) * 2] = ol;
            out[(base + i) * 2 + 1] = orr;
            sum_sq += static_cast<double>(ol) * ol + static_cast<double>(orr) * orr;
        }
        RepublishReadbacks();
    }
    frames_mixed_ += static_cast<uint64_t>(frames);
    if (trace_ && frames_mixed_ >= trace_level_due_) {
        constexpr uint64_t kPeriod = 48000;   // one SPU second
        const double n = static_cast<double>(trace_level_due_ == 0 ? frames_mixed_ : kPeriod + (frames_mixed_ - trace_level_due_));
        for (int vi = 0; vi < kVoices; vi++) {
            if (voices_[vi].Keyed() || trace_sq_l_[vi] > 0.0 || trace_sq_r_[vi] > 0.0)
                trace_->Level(frames_mixed_, vi, static_cast<float>(std::sqrt(trace_sq_l_[vi] / n)), static_cast<float>(std::sqrt(trace_sq_r_[vi] / n)),
                              voices_[vi].Env(), voices_[vi].Nax(), static_cast<int>(voices_[vi].Phase()));
            trace_sq_l_[vi] = trace_sq_r_[vi] = 0.0;
        }
        trace_level_due_ = frames_mixed_ + kPeriod;
    }
    active_voices_.store(active, kRelaxed);
    last_rms_.store(frames > 0 ? static_cast<float>(std::sqrt(sum_sq / (2.0 * frames))) : 0.0f, kRelaxed);
}

}  // namespace ps2r::sound

#include "engine/sound/spu_core.h"
#include "engine/sound/voice_source.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/sound/fmv_pcm_input.h"
#include "engine/sound/pcm_source.h"

namespace ps2r::sound {

namespace {
constexpr auto kRelaxed = std::memory_order_relaxed;
constexpr auto kSeqCst = std::memory_order_seq_cst;
}  // namespace

SpuCore::SpuCore() : spu_ram_(new uint8_t[kSpuRamSize]) { std::memset(spu_ram_.get(), 0, kSpuRamSize); }

int SpuCore::EntryVoiceIndex(uint32_t e) {
    const int v = static_cast<int>((e >> 1) & 0x1F);
    if (v >= kVoicesPerCore) return -1;
    return EntryCore(e) * kVoicesPerCore + v;
}

void SpuCore::SdInit() {
    uint8_t* blk = spu_ram_.get() + kSilenceBlockAddr;
    std::memset(blk, 0, VagDecoder::kBlockBytes);
    blk[1] = VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat;
    ring_.Push(SoundCommand{SoundOp::SilenceAll, 0, 0});
}

void SpuCore::SetCoreParamShadow(int core, int func, uint32_t value) {
    core &= 1;
    func &= 0x1F;
    core_param_shadow_[core][func].store(value, kRelaxed);
    if (!(value & 0x8000)) {
        const float g = static_cast<float>(value & 0x7FFF) / 32767.0f;
        if (func == 0x9) mvol_l_[core].store(g, kRelaxed);
        if (func == 0xA) mvol_r_[core].store(g, kRelaxed);
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
        return;
    }
    const int vi = EntryVoiceIndex(entry);
    const int func = EntryFunc(entry);
    if (vi < 0 || func > 4) return;   // ENVX and beyond are not writable
    param_shadow_[vi][func].store(value, kRelaxed);
    ring_.Push(SoundCommand{SoundOp::SetParam, entry, value});
}

uint32_t SpuCore::EagerEnvx0(int vi) const {
    const uint32_t adsr1 = param_shadow_[vi][3].load(kRelaxed);
    const uint32_t ar = (adsr1 >> 8) & 0x7F;
    const float step = 1.0f / (4.0f * std::pow(2.0f, static_cast<float>(ar) / 2.0f));
    const float env0 = step > 1.0f ? 1.0f : step;
    return static_cast<uint32_t>(env0 * 32767.0f);
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
        default:
            break;   // PMON/NON/VMIX families: not used by this title
    }
}

uint32_t SpuCore::GetSwitch(uint32_t entry) const {
    if (EntryFunc(entry) == 0x17) return endx_[EntryCore(entry)].load(kRelaxed);
    return 0;
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
            }
            break;
        case SoundOp::Koff:
            for (int v = 0; v < kVoicesPerCore; v++)
                if (cmd.value & (1u << v)) voices_[static_cast<int>(cmd.entry & 1) * kVoicesPerCore + v].KeyOff();
            break;
        case SoundOp::SilenceAll:
            // SdInit / savestate reset: the natural re-zero point for holds a
            // dropped Kon (overflow) could have left behind.
            for (auto& v : voices_) v.Silence();
            for (int i = 0; i < kCores; i++) endx_[i].store(0, kRelaxed);
            ClearEagerHolds();
            break;
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
    float mvol_l[kCores], mvol_r[kCores];
    for (int c = 0; c < kCores; c++) {
        mvol_l[c] = mvol_l_[c].load(kRelaxed);
        mvol_r[c] = mvol_r_[c].load(kRelaxed);
    }
    // Read the selected software intent or retained register gains once.
    const auto gains = pcm ? pcm->Gains() : std::array<float, 2>{FmvGainL(), FmvGainR()};
    const float fmv_gl = gains[0], fmv_gr = gains[1];
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
            float mix_l = 0.0f, mix_r = 0.0f;
            for (int vi = 0; vi < kVoices; vi++) {
                float l = 0, r = 0;
                const int core = vi / kVoicesPerCore;
                if (!source || !source->StepVoice(static_cast<uint32_t>(vi), l, r)) {
                    Voice& v = voices_[vi];
                    if (!v.Keyed()) continue;
                    if (v.Step(ram, l, r)) endx_[core].fetch_or(1u << (vi % kVoicesPerCore), kRelaxed);
                }
                mix_l += l * mvol_l[core];
                mix_r += r * mvol_r[core];
            }
            // The external input adds after the voice mix at the BVOL gain; the
            // master gain and the clamp apply to the sum below as usual.
            if (i < fmv_n) {
                mix_l += static_cast<float>(fmv_buf[i * 2]) / 32768.0f * fmv_gl;
                mix_r += static_cast<float>(fmv_buf[i * 2 + 1]) / 32768.0f * fmv_gr;
            }
            const float ol = std::clamp(mix_l * master, -1.0f, 1.0f);
            const float orr = std::clamp(mix_r * master, -1.0f, 1.0f);
            out[(base + i) * 2] = ol;
            out[(base + i) * 2 + 1] = orr;
            sum_sq += static_cast<double>(ol) * ol + static_cast<double>(orr) * orr;
        }
        RepublishReadbacks();
    }
    active_voices_.store(active, kRelaxed);
    last_rms_.store(frames > 0 ? static_cast<float>(std::sqrt(sum_sq / (2.0 * frames))) : 0.0f, kRelaxed);
}

}  // namespace ps2r::sound

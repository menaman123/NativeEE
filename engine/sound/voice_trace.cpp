#include "engine/sound/voice_trace.h"

#include <cmath>

namespace ps2r::sound {

namespace {
float Dbfs(float rms) { return rms > 1e-9f ? 20.0f * std::log10(rms) : -180.0f; }
}  // namespace

bool VoiceTrace::Open(const char* path) {
    Close();
    if (!path || !*path) return false;
    f_ = std::fopen(path, "w");
    if (f_) std::fprintf(f_, "# voice trace: frame kind voice fields (frame = 48 kHz SPU frames since the first pump)\n");
    return f_ != nullptr;
}

void VoiceTrace::Close() {
    if (!f_) return;
    std::fclose(f_);
    f_ = nullptr;
}

void VoiceTrace::KeyOn(uint64_t frame, int vi, uint32_t ssa, uint32_t lsax, uint32_t pitch, uint32_t adsr1, uint32_t adsr2,
                       uint32_t voll, uint32_t volr, uint8_t first_block_flags) {
    if (!f_) return;
    std::fprintf(f_, "%llu KON v%d ssa=%06x lsax=%06x pitch=%04x adsr1=%04x adsr2=%04x voll=%04x volr=%04x flags=%02x\n",
                 static_cast<unsigned long long>(frame), vi, ssa, lsax, pitch, adsr1, adsr2, voll, volr, first_block_flags);
    ++lines_;
}

void VoiceTrace::KeyOff(uint64_t frame, int vi, float env) {
    if (!f_) return;
    std::fprintf(f_, "%llu KOFF v%d env=%.4f\n", static_cast<unsigned long long>(frame), vi, env);
    ++lines_;
}

void VoiceTrace::LoopEnd(uint64_t frame, int vi, uint32_t nax, uint32_t lsax, bool repeat, float env) {
    if (!f_) return;
    std::fprintf(f_, "%llu %s v%d nax=%06x lsax=%06x env=%.4f\n", static_cast<unsigned long long>(frame),
                 repeat ? "LOOP" : "END", vi, nax, lsax, env);
    ++lines_;
}

void VoiceTrace::ReleaseEnd(uint64_t frame, int vi) {
    if (!f_) return;
    std::fprintf(f_, "%llu OFF v%d\n", static_cast<unsigned long long>(frame), vi);
    ++lines_;
}

void VoiceTrace::Param(uint64_t frame, int vi, int func, uint32_t value, bool keyed) {
    if (!f_) return;
    static const char* const kNames[] = {"voll", "volr", "pitch", "adsr1", "adsr2"};
    const char* name = (func >= 0 && func <= 4) ? kNames[func] : "param?";
    std::fprintf(f_, "%llu PRM v%d %s=%04x%s\n", static_cast<unsigned long long>(frame), vi, name, value, keyed ? " keyed" : "");
    ++lines_;
}

void VoiceTrace::Addr(uint64_t frame, int vi, int func, uint32_t value, bool keyed) {
    if (!f_) return;
    std::fprintf(f_, "%llu ADR v%d %s=%06x%s\n", static_cast<unsigned long long>(frame), vi, func == 0x20 ? "ssa" : "lsax", value,
                 keyed ? " keyed" : "");
    ++lines_;
}

void VoiceTrace::Transfer(uint64_t frame, uint32_t mode, uint32_t spu_addr, uint32_t size) {
    if (!f_) return;
    std::fprintf(f_, "%llu XFER mode=%x spu=%06x size=%x\n", static_cast<unsigned long long>(frame), mode, spu_addr, size);
    ++lines_;
}

void VoiceTrace::Switch(uint64_t frame, int core, int func, uint32_t value, bool read) {
    if (!f_) return;
    std::fprintf(f_, "%llu %s c%d func=%02x value=%08x\n", static_cast<unsigned long long>(frame), read ? "SWR" : "SW", core, func, value);
    ++lines_;
}

void VoiceTrace::CoreAttr(uint64_t frame, int core, int func, uint32_t value, bool param) {
    if (!f_) return;
    std::fprintf(f_, "%llu %s c%d %s=%02x value=%08x\n", static_cast<unsigned long long>(frame), param ? "CPRM" : "CORE", core,
                 param ? "param" : "attr", func, value);
    ++lines_;
}

void VoiceTrace::Unmodelled(uint64_t frame, const char* what, uint32_t a, uint32_t b) {
    if (!f_) return;
    std::fprintf(f_, "%llu UNMODELLED %s %08x %08x\n", static_cast<unsigned long long>(frame), what, a, b);
    ++lines_;
}

void VoiceTrace::Level(uint64_t frame, int vi, float rms_l, float rms_r, float env, uint32_t nax, int phase) {
    if (!f_) return;
    std::fprintf(f_, "%llu LVL v%d l=%.1f r=%.1f env=%.4f nax=%06x phase=%d\n", static_cast<unsigned long long>(frame), vi,
                 Dbfs(rms_l), Dbfs(rms_r), env, nax, phase);
    ++lines_;
}

}  // namespace ps2r::sound

// gs/state/gs_priv_regs.cpp -- GsPrivRegs (see gs_priv_regs.h).
#include "engine/gs/state/gs_priv_regs.h"

#include <cstring>

namespace ps2r {
namespace gs {

void GsPrivRegs::SetJobContext(uint32_t* seq_counter, const uint32_t* src_frame, std::function<uint64_t()> vram_epoch) {
    seq_ = seq_counter;
    src_frame_ = src_frame;
    vram_epoch_ = std::move(vram_epoch);
}

uint32_t GsPrivRegs::TakeInterrupts() {
    const uint32_t n = interrupts_;
    interrupts_ = 0;
    return n;
}

namespace {
int SlotIndex(uint32_t a) {
    switch (a & 0xFFFF) {
        case GsPrivRegs::kPmode: return 0; case GsPrivRegs::kSmode1: return 1; case GsPrivRegs::kSmode2: return 2;
        case GsPrivRegs::kSrfsh: return 3; case GsPrivRegs::kSynch1: return 4; case GsPrivRegs::kSynch2: return 5;
        case GsPrivRegs::kSyncv: return 6; case GsPrivRegs::kDispfb1: return 7; case GsPrivRegs::kDisplay1: return 8;
        case GsPrivRegs::kDispfb2: return 9; case GsPrivRegs::kDisplay2: return 10; case GsPrivRegs::kExtbuf: return 11;
        case GsPrivRegs::kExtdata: return 12; case GsPrivRegs::kExtwrite: return 13; case GsPrivRegs::kBgcolor: return 14;
        case GsPrivRegs::kImr: return 16; case GsPrivRegs::kBusdir: return 17; case GsPrivRegs::kSiglblid: return 18;
        default: return -1;
    }
}
}  // namespace

GsPrivRegs::GsPrivRegs() { Reset(); }

// The DISPLAY window latches (window_[]) are change detectors for the presenter,
// not register state: a reset does not clear them, so a DISPLAY rewrite of the
// same geometry after CSR.RESET emits no SetWindow job.
void GsPrivRegs::Reset() {
    std::memset(regs_, 0, sizeof(regs_));
    csr_ = kCsrAfterReset;
    interlaced_ = false;
    frame_mode_ = false;
}

uint64_t& GsPrivRegs::Slot(uint32_t addr) {
    const int i = SlotIndex(addr);
    return i < 0 ? none_ : regs_[i];
}

void GsPrivRegs::PushJob(JobKind kind, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    if (!sink_) return;
    JobRec j{};
    j.type = static_cast<uint32_t>(kind);
    j.seq = seq_ ? (*seq_)++ : 0;
    j.src_frame = src_frame_ ? *src_frame_ : 0;
    j.arg1 = a1; j.arg2 = a2; j.arg3 = a3; j.arg4 = a4;
    j.vram_epoch = vram_epoch_ ? vram_epoch_() : 0;
    sink_->OnJob(j);
}

uint64_t GsPrivRegs::Read(uint32_t addr) const {
    if ((addr & 0xFFFF) == kCsr) return csr_;
    const int i = SlotIndex(addr);
    return i < 0 ? 0 : regs_[i];
}

void GsPrivRegs::WriteLower(uint32_t addr, uint32_t value) {
    Write(addr, (Read(addr) & 0xFFFFFFFF00000000ull) | value);
}

void GsPrivRegs::WriteUpper(uint32_t addr, uint32_t value) {
    Write(addr, (Read(addr) & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32));
}

void GsPrivRegs::Write(uint32_t addr, uint64_t v) {
    ++writes_;  // the half writes (WriteLower / WriteUpper) arrive here, so each counts once
    switch (addr & 0xFFFF) {
        case kSmode2: {  // INT 0 FFMD 1 DPMS 2-3
            Slot(addr) = v;
            interlaced_ = (v & 1) != 0;
            const bool frame_mode = (v & 2) != 0;
            if (frame_mode != frame_mode_) {
                // The buffer line count of both circuits' windows changed (see Display):
                // re-derive them from the latched DISPLAY values.
                frame_mode_ = frame_mode;
                EmitWindow(kDisplay1);
                EmitWindow(kDisplay2);
            }
            return;
        }
        case kDispfb1: case kDispfb2: Dispfb(addr, v); return;
        case kDisplay1: case kDisplay2: Display(addr, v); return;
        case kBgcolor:  // R 0-7 G 8-15 B 16-23
            if (Slot(addr) == v) return;
            Slot(addr) = v;
            PushJob(JobKind::SetClearColor, v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, 0);
            return;
        case kCsr:
            if (v & (1u << 9)) {  // RESET
                Reset();
                csr_ = 0;
                return;
            }
            csr_ &= ~(v & 0x1F);  // write-1-to-clear SIGNAL FINISH HSINT VSINT EDWINT; FLUSH (bit 8) no-op
            return;
        default:
            Slot(addr) = v;
            return;
    }
}

// DISPFB (6.x): FBP 0-8 (2048 words) FBW 9-14 (64 px) PSM 15-19 DBX 32-42 DBY 43-53.
void GsPrivRegs::Dispfb(uint32_t addr, uint64_t v) {
    if (Slot(addr) == v) return;  // idempotent
    Slot(addr) = v;
    const uint32_t fbp = v & 0x1FF, fbw = (v >> 9) & 0x3F, psm = (v >> 15) & 0x1F;
    PushJob(JobKind::SetFrontBuffer, fbp * 8192, fbw * 64, psm, 0);
}

// DISPLAY (6.x): DX 0-11 DY 12-22 MAGH 23-26 MAGV 27-28 DW 32-43 DH 44-54. The
// window is the raster size (DW+1)/(MAGH+1) x (DH+1)/(MAGV+1) clamped to [1,1024];
// the content is the frame-buffer rectangle the CRTC reads for it: the same, except
// in interlaced FRAME mode (SMODE2 INT=1 FFMD=1, GS User's Manual 6.3 / 3.5: every
// line of the buffer is read in EACH field, so a field's (DH+1)/(MAGV+1) raster lines
// come from half as many buffer lines -- a 448-line display scans a 224-line
// buffer, line-doubled) where the content height is half the raster height. FIELD
// mode (FFMD=0) and non-interlaced displays read one buffer line per raster line.
// The job carries window<<16 | content per axis and the VCK dims; a SMODE2 change
// of the mode re-derives both circuits' windows from the latched DISPLAY values.
void GsPrivRegs::Display(uint32_t addr, uint64_t v) {
    if (Slot(addr) == v) return;
    Slot(addr) = v;
    EmitWindow(addr);
}

void GsPrivRegs::EmitWindow(uint32_t addr) {
    const uint64_t v = Slot(addr);
    if (v == 0) return;   // no DISPLAY value latched for this circuit
    const int circuit = ((addr & 0xFFFF) == kDisplay1) ? 1 : 2;
    const int magh = (v >> 23) & 0xF, magv = (v >> 27) & 3;
    const int dw = (v >> 32) & 0xFFF, dh = (v >> 44) & 0x7FF;
    int content_w = (dw + 1) / (magh + 1);
    int content_h = (dh + 1) / (magv + 1);
    if (content_w <= 0 || content_h <= 0) {
        content_w = dw + 1;
        content_h = dh + 1;
    }
    auto clampw = [](int x) { return x < 1 ? 1 : (x > 1024 ? 1024 : x); };
    const int window_w = clampw(content_w), window_h = clampw(content_h);
    if (interlaced_ && frame_mode_) content_h = (content_h + 1) / 2;   // frame mode: half the buffer lines per field
    const uint32_t packed_w = (static_cast<uint32_t>(window_w) << 16) | (content_w & 0xFFFF);
    const uint32_t packed_h = (static_cast<uint32_t>(window_h) << 16) | (content_h & 0xFFFF);
    const uint32_t packed_vck = ((static_cast<uint32_t>(dw + 1) & 0xFFFF) << 16) | ((dh + 1) & 0xFFFF);
    Window& last = window_[circuit - 1];
    if (last.valid && last.w == packed_w && last.h == packed_h && last.vck == packed_vck) return;
    last = Window{packed_w, packed_h, packed_vck, true};
    PushJob(JobKind::SetWindow, circuit, packed_w, packed_h, packed_vck);
}

// SIGNAL (5.x): ID 0-31 IDMSK 32-63 -> SIGLBLID low half; CSR.SIGNAL; GS interrupt.
void GsPrivRegs::Signal(uint64_t value) {
    const uint64_t id = value & 0xFFFFFFFF, idmsk = value >> 32;
    uint64_t& s = regs_[SlotIndex(kSiglblid)];
    s = (s & ~idmsk) | (id & idmsk);
    csr_ |= kCsrSignal;
    interrupts_++;
    if (interrupt_observer_) interrupt_observer_(kCsrSignal);
}

void GsPrivRegs::Finish() {
    csr_ |= kCsrFinish;
    interrupts_++;
    if (interrupt_observer_) interrupt_observer_(kCsrFinish);
}

// LABEL: ID 0-31 IDMSK 32-63 -> SIGLBLID high half; no interrupt.
void GsPrivRegs::Label(uint64_t value) {
    const uint64_t id = value & 0xFFFFFFFF, idmsk = value >> 32;
    uint64_t& s = regs_[SlotIndex(kSiglblid)];
    s = (s & ~(idmsk << 32)) | ((id & idmsk) << 32);
}

}  // namespace gs
}  // namespace ps2r

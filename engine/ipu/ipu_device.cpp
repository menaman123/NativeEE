// engine/ipu/ipu_device.cpp -- see ipu_device.h.
#include "engine/ipu/ipu_device.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "engine/core/log/log.h"
#include "engine/ipu/ipu_color.h"
#include "engine/ipu/ipu_vlc.h"
#include "engine/ipu/mpeg_decode.h"

namespace ps2r::ipu {

IpuDevice::IpuDevice() {
    memcpy(intra_iq_, MpegDecode::DefaultIntraQuantMatrix(), kQuantMatrixBytes);
    memset(non_intra_iq_, 16, kQuantMatrixBytes);  // default non-intra matrix: flat 16
}

// ---------------------------------------------------------------------------
// MmioHandler: the EE bus presents 32-bit register words. A sub-word read sees
// the byte lanes of its aligned word; a sub-word write stores the whole value
// into the aligned word; a 64-bit access is the two consecutive words (the
// upper word of CMD/TOP carries BUSY at bit 31; CMD upper writes are ignored).
// ---------------------------------------------------------------------------
uint64_t IpuDevice::Read(uint32_t addr, unsigned size) {
    const uint32_t offset = addr & (kWindowBytes - 1);
    if (size <= 4) {
        const uint32_t word = ReadWord(offset & ~3u);
        const unsigned shift = (offset & 3u) * 8u;
        const uint64_t mask = size == 4 ? 0xFFFFFFFFull : ((1ull << (size * 8u)) - 1ull);
        return (static_cast<uint64_t>(word) >> shift) & mask;
    }
    const uint64_t lo = ReadWord(offset);
    const uint64_t hi = ReadWord((offset + 4) & (kWindowBytes - 1));
    return (hi << 32) | lo;
}

void IpuDevice::Write(uint32_t addr, unsigned size, uint64_t value) {
    const uint32_t offset = addr & (kWindowBytes - 1);
    if (size <= 4) {
        WriteWord(offset & ~3u, static_cast<uint32_t>(value));
        return;
    }
    WriteWord(offset, static_cast<uint32_t>(value));
    WriteWord((offset + 4) & (kWindowBytes - 1), static_cast<uint32_t>(value >> 32));
}

uint32_t IpuDevice::ReadWord(uint32_t offset) {
    switch (offset) {
        case kRegCmd: return ReadCmd();
        case kRegCmd + 4: return 0;  // synchronous VDEC/FDEC
        case kRegCtrl: return ReadCtrl();
        case kRegBp: return ReadBp();
        case kRegTop: return ReadTop();
        case kRegTop + 4: return busy_ ? kCtrlBusy : 0;
        default: return 0;
    }
}

void IpuDevice::WriteWord(uint32_t offset, uint32_t value) {
    switch (offset) {
        case kRegCmd: WriteCmd(value); return;
        case kRegCmd + 4: return;  // only the low command word is effective
        case kRegCtrl: WriteCtrl(value); return;
        default:
            ++unhandled_writes_;
            PS2R_LOG(IPU, Warn, "unhandled register write: offset=0x%02x value=0x%08x", offset, value);
            return;
    }
}

// ---------------------------------------------------------------------------
// Register face
// ---------------------------------------------------------------------------
void IpuDevice::WriteCmd(uint32_t value) {
    const uint8_t cmd = static_cast<uint8_t>((value >> 28) & 0xF);
    const size_t previous_output = output_fifo_.size();
    current_command_ = value;
    current_unsupported_ = false;
    ++command_counts_[cmd];

    // Issuing a command clears the previous command's error/start-code flags.
    ecd_ = false;
    scd_ = false;

    switch (static_cast<IpuCommand>(cmd)) {
        case IpuCommand::Bclr: ExecuteBclr(value); break;
        case IpuCommand::Idec: ExecuteIdec(value); break;
        case IpuCommand::Bdec: ExecuteBdec(value); break;
        case IpuCommand::Fdec: ExecuteFdec(value); break;
        case IpuCommand::SetIq: ExecuteSetIq(value); break;
        case IpuCommand::Csc: ExecuteCsc(value); break;
        case IpuCommand::SetTh: ExecuteSetTh(value); break;
        case IpuCommand::Vdec: ExecuteVdec(value); break;
        case IpuCommand::SetVq: ExecuteSetVq(); break;
        case IpuCommand::Pack: ExecutePack(value); break;
        default: ExecuteDivergent("unknown", value); break;
    }
    RecomputeInputCount();
    RecomputeOutputCount();
    if (output_fifo_.size() > previous_output && output_ready_) output_ready_();
}

uint64_t IpuDevice::CommandCount(uint8_t code) const {
    return code < 16 ? command_counts_[code] : 0;
}

uint64_t IpuDevice::UnsupportedCommandCount(uint8_t code) const {
    return code < 16 ? unsupported_counts_[code] : 0;
}

uint32_t IpuDevice::LastUnsupportedCommand(uint8_t code) const {
    return code < 16 ? last_unsupported_[code] : 0;
}

uint32_t IpuDevice::UnsupportedVariantMask(uint8_t code) const {
    return code < 16 ? unsupported_masks_[code] : 0;
}

void IpuDevice::MarkUnsupported(uint32_t reason) {
    const uint8_t code = static_cast<uint8_t>(current_command_ >> 28);
    unsupported_masks_[code] |= reason;
    last_unsupported_[code] = current_command_;
    if (!current_unsupported_) {
        current_unsupported_ = true;
        ++unsupported_counts_[code];
        ++divergent_commands_;
    }
}

// The DATA word contains all 32 result bits. BUSY belongs in the upper word.
uint32_t IpuDevice::ReadCmd() { return fdec_result_; }

// Writable control fields latch independently of the read-only status fields.
void IpuDevice::WriteCtrl(uint32_t value) {
    control_ = value & kCtrlWritable;
    idp_ = static_cast<uint8_t>((control_ >> 16) & 3u);
    if (value & kCtrlRst) {
        PS2R_LOG(IPU, Narrate, "RESET");
        input_fifo_.clear();
        output_fifo_.clear();
        bp_ = 0;
        busy_ = false;
        ifc_ = ofc_ = cbp_ = 0;
        ecd_ = scd_ = false;
        fdec_result_ = 0;
        memset(dc_pred_, 0, sizeof(dc_pred_));
    }
}

// Counts are updated by every command, reset and FIFO transfer.
uint32_t IpuDevice::ReadCtrl() {
    uint32_t val = 0;
    val |= (ifc_ & 0xFu);
    val |= (static_cast<uint32_t>(ofc_ & 0xFu) << 4);
    val |= (static_cast<uint32_t>(cbp_ & 0x3Fu) << 8);
    if (ecd_) val |= (1u << 14);
    if (scd_) val |= (1u << 15);
    val |= control_;
    if (busy_) val |= kCtrlBusy;

    return val;
}

// IPU_BP: bit position within the FIFO (7 bits) and IFC in bits 11:8.
uint32_t IpuDevice::ReadBp() {
    uint32_t val = 0;
    val |= (bp_ & 0x7Fu);
    val |= (static_cast<uint32_t>(ifc_ & 0xFu) << 8);
    return val;
}

// IPU_TOP: the next 32 bits of the bitstream, 0 when fewer remain.
uint32_t IpuDevice::ReadTop() {
    const BitCursor c{input_fifo_.data(), input_fifo_.size(), bp_};
    if (!MpegDecode::HasBits(c, 32)) return 0;
    return MpegDecode::PeekBits(c, 32);
}

// ---------------------------------------------------------------------------
// DMAC FIFO face
// ---------------------------------------------------------------------------
void IpuDevice::SetOutputReady(std::function<void()> callback) {
    output_ready_ = std::move(callback);
}

void IpuDevice::FeedInputFifo(const uint8_t* data, size_t size) {
    if (size) input_fifo_.insert(input_fifo_.end(), data, data + size);
    RecomputeInputCount();
    PS2R_LOG(IPU, Narrate, "FeedInput: %zu bytes, total=%zu", size, input_fifo_.size());
}

size_t IpuDevice::DrainOutputFifo(uint8_t* dest, size_t max_size) {
    const size_t to_copy = std::min(max_size, output_fifo_.size());
    if (to_copy > 0) {
        memcpy(dest, output_fifo_.data(), to_copy);
        output_fifo_.erase(output_fifo_.begin(),
                           output_fifo_.begin() + static_cast<std::ptrdiff_t>(to_copy));
    }
    RecomputeOutputCount();
    return to_copy;
}

void IpuDevice::RecomputeInputCount() {
    const size_t consumed_qwords = bp_ / 128;
    const size_t supplied_qwords = input_fifo_.size() / 16;
    ifc_ = static_cast<uint8_t>(std::min<size_t>(
        supplied_qwords > consumed_qwords ? supplied_qwords - consumed_qwords : 0, 8));
}

void IpuDevice::RecomputeOutputCount() {
    ofc_ = static_cast<uint8_t>(std::min<size_t>(output_fifo_.size() / 16, 8));
}

// ---------------------------------------------------------------------------
// BCLR: empty the input FIFO and set the bit pointer from bits 6:0.
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteBclr(uint32_t cmd) {
    const uint8_t new_bp = static_cast<uint8_t>(cmd & 0x7F);
    input_fifo_.clear();
    bp_ = new_bp;
    ifc_ = 0;
    busy_ = false;
    PS2R_LOG(IPU, Narrate, "BCLR: bp=%u", static_cast<unsigned>(new_bp));
}

// ---------------------------------------------------------------------------
// SETIQ: skip FB, then load 64 matrix bytes, including non-byte-aligned data.
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteSetIq(uint32_t cmd) {
    const bool non_intra = ((cmd >> 27) & 1u) != 0;
    BitCursor cursor{input_fifo_.data(), input_fifo_.size(), bp_};
    const uint32_t fb = cmd & 0x3Fu;
    if (MpegDecode::HasBits(cursor, static_cast<int>(fb + kQuantMatrixBytes * 8))) {
        bp_ += fb;
        uint8_t bytes[kQuantMatrixBytes];
        ReadInputBytes(bytes, sizeof(bytes));
        uint8_t* matrix = non_intra ? non_intra_iq_ : intra_iq_;
        const uint8_t* zigzag = MpegDecode::Zigzag();
        for (size_t i = 0; i < kQuantMatrixBytes; i++) {
            matrix[zigzag[i]] = bytes[i];
        }
        PS2R_LOG(IPU, Narrate, "SETIQ: %s matrix loaded", non_intra ? "non-intra" : "intra");
    } else {
        MarkUnsupported(kUnsupportedInput);
    }
    busy_ = false;
}

// ---------------------------------------------------------------------------
// SETTH: the ninth bit permits thresholds above every RGB component.
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteSetTh(uint32_t cmd) {
    th0_ = static_cast<uint16_t>(cmd & 0x1FF);
    th1_ = static_cast<uint16_t>((cmd >> 16) & 0x1FF);
    busy_ = false;
    PS2R_LOG(IPU, Narrate, "SETTH: th0=%u th1=%u", static_cast<unsigned>(th0_),
             static_cast<unsigned>(th1_));
}

// ---------------------------------------------------------------------------
// FDEC: skip FB bits (5:0), then the next 32 bits become the CMD result (0
// when the FIFO holds fewer).
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteFdec(uint32_t cmd) {
    const uint8_t fb = static_cast<uint8_t>(cmd & 0x3F);
    BitCursor c{input_fifo_.data(), input_fifo_.size(), bp_};
    MpegDecode::SkipBits(c, fb);
    bp_ = c.bit_pos;

    if (MpegDecode::HasBits(c, 32)) {
        fdec_result_ = MpegDecode::PeekBits(c, 32);
    } else {
        fdec_result_ = 0;
        MarkUnsupported(kUnsupportedInput);
    }
    busy_ = false;
    PS2R_LOG(IPU, Narrate, "FDEC: fb=%u result=0x%08x", static_cast<unsigned>(fb), fdec_result_);
}

// ---------------------------------------------------------------------------
// BDEC (block decode)
// diverges: no decode is performed; the command completes with no output.
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteBdec(uint32_t cmd) {
    ExecuteDivergent("BDEC", cmd);
    busy_ = false;
}

// diverges: BDEC and undefined command codes are not modelled.
void IpuDevice::ExecuteDivergent(const char* name, uint32_t cmd) {
    MarkUnsupported((cmd >> 28) == 2 ? kUnsupportedDecode : kUnsupportedOpcode);
    PS2R_LOG(IPU, Warn, "%s: command 0x%08x not modelled", name, cmd);
}

bool IpuDevice::ReadInputBytes(uint8_t* dest, size_t size) {
    BitCursor cursor{input_fifo_.data(), input_fifo_.size(), bp_};
    if (!MpegDecode::HasBits(cursor, static_cast<int>(size * 8))) {
        MarkUnsupported(kUnsupportedInput);
        return false;
    }
    for (size_t i = 0; i < size; ++i)
        dest[i] = static_cast<uint8_t>(MpegDecode::ReadBits(cursor, 8));
    bp_ = cursor.bit_pos;
    return true;
}

void IpuDevice::ExecuteSetVq() {
    uint8_t bytes[32];
    if (!ReadInputBytes(bytes, sizeof(bytes))) return;
    for (size_t i = 0; i < 16; ++i)
        vq_clut_[i] = static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
}

void IpuDevice::ExecuteVdec(uint32_t cmd) {
    BitCursor cursor{input_fifo_.data(), input_fifo_.size(), bp_};
    MpegDecode::SkipBits(cursor, cmd & 0x3fu);
    bp_ = cursor.bit_pos;
    if (!MpegDecode::HasBits(cursor, 32)) {
        fdec_result_ = 0;
        MarkUnsupported(kUnsupportedInput);
        return;
    }
    fdec_result_ = IpuVlc::Decode(MpegDecode::PeekBits(cursor, 32), (cmd >> 26) & 3u,
                                   (control_ >> 24) & 7u, (control_ & (1u << 23)) != 0);
    ecd_ = fdec_result_ == 0;
    bp_ += fdec_result_ >> 16;
}

void IpuDevice::AppendPixel(uint32_t rgba, unsigned pixel, bool rgb16, bool dither) {
    const uint32_t packed = rgb16 ? IpuColor::Pack(rgba, pixel % 16, pixel / 16, dither) : rgba;
    for (unsigned byte = 0; byte < (rgb16 ? 2u : 4u); ++byte)
        output_fifo_.push_back(static_cast<uint8_t>(packed >> (byte * 8)));
}

// ---------------------------------------------------------------------------
// CSC: planar RAW8 macroblocks to RGB32/RGB16, with transparency and dither.
// diverges: input starvation ends this synchronous command and is counted;
// it does not resume on a later feed. Only complete macroblocks are consumed.
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteCsc(uint32_t cmd) {
    const uint32_t mbc = cmd & 0x7FF;
    const bool rgb16 = (cmd & (1u << 27)) != 0;
    const bool dither = (cmd & (1u << 26)) != 0;

    for (uint32_t mb = 0; mb < mbc; mb++) {
        uint8_t bytes[kCscInputBytesPerMb];
        if (!ReadInputBytes(bytes, sizeof(bytes))) break;
        for (unsigned pixel = 0; pixel < 256; ++pixel) {
            const unsigned chroma = (pixel / 32) * 8 + (pixel % 16) / 2;
            const uint32_t rgba = IpuColor::Convert(bytes[pixel], bytes[256 + chroma], bytes[320 + chroma]);
            AppendPixel(IpuColor::Threshold(rgba, th0_, th1_), pixel, rgb16, dither);
        }
    }

    busy_ = false;
}

void IpuDevice::ExecutePack(uint32_t cmd) {
    const bool rgb16 = (cmd & (1u << 27)) != 0;
    const bool dither = (cmd & (1u << 26)) != 0;
    for (uint32_t mb = 0; mb < (cmd & 0x7ffu); ++mb) {
        uint8_t bytes[1024];
        if (!ReadInputBytes(bytes, sizeof(bytes))) break;
        uint8_t low_index = 0;
        for (unsigned pixel = 0; pixel < 256; ++pixel) {
            const uint8_t* p = bytes + pixel * 4;
            const uint32_t rgba = p[0] | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) |
                                  (uint32_t{p[3]} << 24);
            if (rgb16) {
                AppendPixel(rgba, pixel, true, dither);
            } else {
                const uint8_t index = IpuColor::Quantize(
                    IpuColor::Pack(rgba, pixel % 16, pixel / 16, dither), vq_clut_);
                if (pixel & 1) output_fifo_.push_back(static_cast<uint8_t>(low_index | (index << 4)));
                else low_index = index;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// IDEC: skip FB bits, reset the DC predictors for the intra DC precision, then
// decode intra macroblocks until a start code (0x000001) or the bitstream
// ends. QSC in bits 20:16, SGN in bit 25, OFM in bit 27.
// diverges: the MPEG parser is only a best-effort intra-block decoder. It
// lacks MBT/MBAI/DTD, full AC VLC and control-dependent decoding. Every IDEC
// issue is counted as unsupported, including apparently simple streams.
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteIdec(uint32_t cmd) {
    const uint8_t fb = static_cast<uint8_t>(cmd & 0x3F);
    const int qsc = static_cast<int>((cmd >> 16) & 0x1F);
    const bool sgn = ((cmd >> 25) & 1u) != 0;
    const uint8_t ofm = static_cast<uint8_t>((cmd >> 27) & 1u);
    const bool dither = (cmd & (1u << 26)) != 0;
    MarkUnsupported(kUnsupportedDecode);

    PS2R_LOG(IPU, Narrate, "IDEC: fb=%u qsc=%d sgn=%d ofm=%u input_size=%zu",
             static_cast<unsigned>(fb), qsc, sgn ? 1 : 0, static_cast<unsigned>(ofm),
             input_fifo_.size());

    BitCursor c{input_fifo_.data(), input_fifo_.size(), bp_};
    MpegDecode::SkipBits(c, fb);

    // DC predictor reset value: 128 << idp for 8/9/10-bit intra DC precision.
    int dc_reset = 128;
    if (idp_ == 1) dc_reset = 256;
    else if (idp_ != 0) dc_reset = 512;
    dc_pred_[0] = dc_pred_[1] = dc_pred_[2] = dc_reset;

    constexpr int kSafetyMacroblocks = 10000;
    int mb_count = 0;
    while (MpegDecode::HasBits(c, 32)) {
        const uint32_t peek = MpegDecode::PeekBits(c, 24);
        if (peek == 0x000001u) {
            scd_ = true;
            PS2R_LOG(IPU, Narrate, "IDEC: start code after %d MBs", mb_count);
            break;
        }

        std::vector<uint8_t> rgba;
        MpegDecode::DecodeIntraMacroblock(c, qsc, false, intra_iq_, dc_pred_, idp_, rgba);
        for (unsigned pixel = 0; pixel < rgba.size() / 4; ++pixel) {
            const uint8_t* p = rgba.data() + pixel * 4;
            uint32_t packed = p[0] | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16);
            packed = IpuColor::Threshold(packed, th0_, th1_);
            if (sgn) packed ^= 0x00808080u;
            AppendPixel(packed, pixel, ofm != 0, dither);
        }
        mb_count++;

        if (mb_count > kSafetyMacroblocks) {
            PS2R_LOG(IPU, Warn, "IDEC: safety limit reached");
            break;
        }
    }
    bp_ = c.bit_pos;

    PS2R_LOG(IPU, Narrate, "IDEC complete: %d macroblocks, %zu output bytes", mb_count,
             output_fifo_.size());
    busy_ = false;
}

}  // namespace ps2r::ipu

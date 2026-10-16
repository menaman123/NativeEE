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
        case kRegCmd + 4: return busy_ ? kCtrlBusy : 0;  // IPU_CMD bit 63: a command waiting for input
        case kRegCtrl: return ReadCtrl();
        case kRegBp: return ReadBp();
        case kRegTop: return ReadTop();
        case kRegTop + 4: return TopBusy() ? kCtrlBusy : 0;  // IPU_TOP bit 63: the 32 bits are not all there
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
    const size_t room_before = InputRoomBytes();
    current_command_ = value;
    current_unsupported_ = false;
    ++command_counts_[cmd];

    // Issuing a command clears the previous command's error/start-code flags.
    ecd_ = false;
    scd_ = false;
    if (!retrying_) busy_ = false;   // a new command supersedes a pending one

    switch (static_cast<IpuCommand>(cmd)) {
        case IpuCommand::Bclr: ExecuteBclr(value); break;
        case IpuCommand::Idec: ExecuteIdec(value); break;
        case IpuCommand::Bdec: ExecuteBdec(value); break;
        case IpuCommand::Fdec: ExecuteFdec(value); break;
        case IpuCommand::SetIq: ExecuteSetIq(value); break;
        case IpuCommand::Csc: ExecuteCsc(value); break;
        case IpuCommand::SetTh: ExecuteSetTh(value); break;
        case IpuCommand::Vdec: ExecuteVdec(value); break;
        case IpuCommand::SetVq: ExecuteSetVq(value); break;
        case IpuCommand::Pack: ExecutePack(value); break;
        default: ExecuteDivergent("unknown", value); break;
    }
    CompactInput();
    RecomputeInputCount();
    RecomputeOutputCount();
    if (InputRoomBytes() > room_before) NotifyInputRoom();
    if (output_fifo_.size() > previous_output && output_ready_) output_ready_();
}

uint32_t IpuDevice::ConsumedBitPosition() const {
    const uint32_t reach = busy_ ? std::max(bp_, pending_reach_bp_) : bp_;
    return static_cast<uint32_t>(std::min<uint64_t>(reach, static_cast<uint64_t>(input_fifo_.size()) * 8u));
}

size_t IpuDevice::InputRoomBytes() const {
    const size_t held = input_fifo_.size() - std::min(input_fifo_.size(), size_t{ConsumedBitPosition() / 128} * 16);
    return held >= kInputFifoBytes ? 0 : kInputFifoBytes - held;
}

size_t IpuDevice::UnreadInputQwords() const {
    const size_t supplied = input_fifo_.size() / 16;
    const size_t consumed = bp_ / 128;
    return supplied > consumed ? supplied - consumed : 0;
}

void IpuDevice::SetInputRoomReady(std::function<void()> callback) { input_room_ready_ = std::move(callback); }

// The callback feeds the FIFO, which may retry the pending command, which may free
// more room: that nested request is honoured once the callback has returned, so a
// transfer longer than the FIFO streams through without a kick from the guest.
void IpuDevice::NotifyInputRoom() {
    if (!input_room_ready_) return;
    if (in_room_ready_) {
        room_notify_pending_ = true;
        return;
    }
    in_room_ready_ = true;
    do {
        room_notify_pending_ = false;
        input_room_ready_();
    } while (room_notify_pending_ && InputRoomBytes() > 0);
    in_room_ready_ = false;
}

void IpuDevice::CompactInput() {
    const size_t consumed_qwords = bp_ / 128;
    if (consumed_qwords == 0) return;
    const size_t drop = std::min(consumed_qwords * 16, input_fifo_.size() & ~size_t{15});
    if (drop == 0) return;
    input_fifo_.erase(input_fifo_.begin(), input_fifo_.begin() + static_cast<std::ptrdiff_t>(drop));
    bp_ -= static_cast<uint32_t>(drop * 8);
    if (busy_) pending_reach_bp_ = pending_reach_bp_ > drop * 8 ? pending_reach_bp_ - static_cast<uint32_t>(drop * 8) : 0;
    dropped_bytes_ += drop;
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
        dropped_bytes_ = 0;
        busy_ = false;
        pending_command_ = 0;
        pending_reach_bp_ = 0;
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

// IPU_TOP: the next 32 bits of the bitstream from the bit pointer. When fewer
// remain, the ones there are MSB-aligned over zeros and bit 63 (TopBusy) says
// the word is incomplete -- libmpeg then takes 32 - (BP mod 32) of them as valid
// (the tail of the FIFO's last quadword) and FDECs for the rest.
uint32_t IpuDevice::ReadTop() {
    const BitCursor c{input_fifo_.data(), input_fifo_.size(), bp_};
    const uint64_t available_bits = static_cast<uint64_t>(input_fifo_.size()) * 8u;
    const int available = available_bits > bp_ ? static_cast<int>(std::min<uint64_t>(32, available_bits - bp_)) : 0;
    if (available == 0) return 0;
    return MpegDecode::PeekBits(c, available) << (32 - available);
}

// Bit 63 of IPU_TOP: a command in progress, or fewer than 32 bits past BP.
bool IpuDevice::TopBusy() const {
    const BitCursor c{input_fifo_.data(), input_fifo_.size(), bp_};
    return busy_ || !MpegDecode::HasBits(c, 32);
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
    if (busy_ && size) RetryPending();
}

void IpuDevice::DeferForInput(uint32_t cmd, uint32_t bp_at_issue, uint32_t reach) {
    bp_ = bp_at_issue;
    pending_command_ = cmd;
    pending_bp_ = bp_at_issue;
    pending_reach_bp_ = std::max(reach, bp_at_issue);
    busy_ = true;
    PS2R_LOG(IPU, Narrate, "command 0x%08x waits for input (bp=%u, reached=%u, fifo=%zu bytes)", cmd,
             bp_at_issue, pending_reach_bp_, input_fifo_.size());
}

void IpuDevice::RetryPending() {
    const uint32_t cmd = pending_command_;
    const size_t previous_output = output_fifo_.size();
    const size_t room_before = InputRoomBytes();
    retrying_ = true;
    busy_ = false;
    current_command_ = cmd;
    current_unsupported_ = false;
    switch (static_cast<IpuCommand>((cmd >> 28) & 0xF)) {
        case IpuCommand::Bdec: ExecuteBdec(cmd); break;
        case IpuCommand::Csc: ExecuteCsc(cmd); break;
        case IpuCommand::Pack: ExecutePack(cmd); break;
        case IpuCommand::Fdec: ExecuteFdec(cmd); break;
        case IpuCommand::Vdec: ExecuteVdec(cmd); break;
        case IpuCommand::SetIq: ExecuteSetIq(cmd); break;
        case IpuCommand::SetVq: ExecuteSetVq(cmd); break;
        default: break;
    }
    retrying_ = false;
    if (!busy_) ++pending_completions_;
    CompactInput();
    RecomputeInputCount();
    RecomputeOutputCount();
    if (InputRoomBytes() > room_before) NotifyInputRoom();
    if (output_fifo_.size() > previous_output && output_ready_) output_ready_();
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
    const size_t consumed_qwords = ConsumedBitPosition() / 128;
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
    dropped_bytes_ = 0;
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
        busy_ = false;
    } else {
        DeferForInput(cmd, bp_, bp_);
    }
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
    const uint32_t bp_at_issue = bp_;
    BitCursor c{input_fifo_.data(), input_fifo_.size(), bp_};
    MpegDecode::SkipBits(c, fb);

    if (!MpegDecode::HasBits(c, 32)) {
        DeferForInput(cmd, bp_at_issue, bp_at_issue);   // the hardware waits for the bits
        return;
    }
    bp_ = c.bit_pos;
    fdec_result_ = MpegDecode::PeekBits(c, 32);
    busy_ = false;
    PS2R_LOG(IPU, Narrate, "FDEC: fb=%u result=0x%08x", static_cast<unsigned>(fb), fdec_result_);
}

// ---------------------------------------------------------------------------
// BDEC (block decode): skip FB bits (5:0), then decode one macroblock's six
// 8x8 blocks (Y0 Y1 Y2 Y3 Cb Cr) with quantiser_scale_code QSC (20:16), DCT
// type DT (25: 1 = field DCT), DC predictor reset DCR (26) and MBI (27: 1 =
// intra macroblock, 0 = non-intra: a coded_block_pattern precedes the blocks
// and only its coded blocks are present; the pattern lands in CTRL.CBP).
// CTRL supplies the picture-level flags: AS (20) alternate scan, IVF (21) the
// intra VLC table, QST (22) the non-linear quantiser scale, MP1 (23) the MPEG-1
// escape. The output is one RAW16 macroblock. A VLC failure with the lookahead
// present is a bitstream error: CTRL.ECD is set and nothing is output; a
// failure for want of bits leaves the command pending until the next feed.
// diverges: the hardware decodes as the bits arrive; this model restarts the
// macroblock from the issue point on every feed (same result, same CTRL/BP
// face) and treats "no match with fewer than kBdecLookaheadBits left" as
// starvation rather than an error until those bits arrive.
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteBdec(uint32_t cmd) {
    const uint8_t fb = static_cast<uint8_t>(cmd & 0x3F);
    const int qsc = static_cast<int>((cmd >> 16) & 0x1F);
    const bool field_dct = ((cmd >> 25) & 1u) != 0;
    const bool dcr = ((cmd >> 26) & 1u) != 0;
    const bool intra = ((cmd >> 27) & 1u) != 0;
    const bool alt_scan = (control_ & (1u << 20)) != 0;
    const bool table_one = (control_ & (1u << 21)) != 0;
    const bool q_scale_type = (control_ & (1u << 22)) != 0;
    const bool mpeg1 = (control_ & (1u << 23)) != 0;
    const int qs = MpegDecode::QuantiserScale(qsc, q_scale_type);

    const uint32_t bp_at_issue = bp_;
    BitCursor c{input_fifo_.data(), input_fifo_.size(), bp_};
    MpegDecode::SkipBits(c, fb);

    // The predictors are worked on a copy: a deferred macroblock restarts from the
    // device state as it was at issue.
    int dc_pred[3] = {dc_pred_[0], dc_pred_[1], dc_pred_[2]};
    const int dc_reset = 128 << (idp_ > 2 ? 2 : idp_);
    if (dcr) dc_pred[0] = dc_pred[1] = dc_pred[2] = dc_reset;

    int16_t blocks[6][MpegDecode::kBlockCoeffs];
    static constexpr int kBlockComponent[6] = {0, 0, 0, 0, 1, 2};  // DC predictor per block
    int cbp = 0x3F;
    bool ok = true;
    if (intra) {
        for (int i = 0; i < 6 && ok; ++i)
            ok = MpegDecode::DecodeIntraBlock(c, blocks[i], kBlockComponent[i], qs, intra_iq_, dc_pred, idp_,
                                              table_one, mpeg1, alt_scan);
    } else {
        ok = MpegDecode::DecodeCodedBlockPattern(c, cbp);
        // 7.2.1: a non-intra macroblock resets the DC predictors.
        dc_pred[0] = dc_pred[1] = dc_pred[2] = dc_reset;
        for (int i = 0; i < 6 && ok; ++i) {
            if ((cbp >> (5 - i)) & 1)
                ok = MpegDecode::DecodeNonIntraBlock(c, blocks[i], qs, non_intra_iq_, mpeg1, alt_scan);
            else
                memset(blocks[i], 0, sizeof(blocks[i]));
        }
    }

    if (!ok) {
        if (!MpegDecode::HasBits(c, kBdecLookaheadBits)) {
            DeferForInput(cmd, bp_at_issue, c.bit_pos);   // the hardware waits for the bits
            return;
        }
        ecd_ = true;
        bp_ = c.bit_pos;
        busy_ = false;
        PS2R_LOG(IPU, Narrate, "BDEC: fb=%u qsc=%d %s cbp=0x%02x -> error code at bit %u",
                 static_cast<unsigned>(fb), qsc, intra ? "intra" : "non-intra", static_cast<unsigned>(cbp), bp_);
        return;
    }

    bp_ = c.bit_pos;
    dc_pred_[0] = dc_pred[0];
    dc_pred_[1] = dc_pred[1];
    dc_pred_[2] = dc_pred[2];
    cbp_ = static_cast<uint8_t>(cbp);
    busy_ = false;
    AppendRaw16Macroblock(blocks, field_dct);
    PS2R_LOG(IPU, Narrate, "BDEC: fb=%u qsc=%d (qs=%d) %s dt=%d dcr=%d cbp=0x%02x consumed=%u bits",
             static_cast<unsigned>(fb), qsc, qs, intra ? "intra" : "non-intra", field_dct ? 1 : 0, dcr ? 1 : 0,
             static_cast<unsigned>(cbp), bp_ - bp_at_issue);
}

// RAW16: Y as 16 rows of 16 little-endian int16 samples, then Cb and Cr as 8x8.
// Frame DCT: block b covers rows (b / 2) * 8 .. + 7, columns (b % 2) * 8 .. + 7.
// Field DCT (6.1.3, Figure 6-14): blocks 0/1 hold the top-field lines (rows
// 0, 2, ..., 14), blocks 2/3 the bottom-field lines (rows 1, 3, ..., 15).
void IpuDevice::AppendRaw16Macroblock(const int16_t blocks[6][MpegDecode::kBlockCoeffs], bool field_dct) {
    int16_t y[16 * 16];
    for (int b = 0; b < 4; ++b) {
        const int col0 = (b & 1) * 8;
        for (int r = 0; r < 8; ++r) {
            const int row = field_dct ? r * 2 + (b >> 1) : (b >> 1) * 8 + r;
            memcpy(&y[row * 16 + col0], &blocks[b][r * 8], 8 * sizeof(int16_t));
        }
    }
    auto append = [this](const int16_t* samples, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const uint16_t v = static_cast<uint16_t>(samples[i]);
            output_fifo_.push_back(static_cast<uint8_t>(v));
            output_fifo_.push_back(static_cast<uint8_t>(v >> 8));
        }
    };
    append(y, 16 * 16);
    append(blocks[4], MpegDecode::kBlockCoeffs);
    append(blocks[5], MpegDecode::kBlockCoeffs);
}

// diverges: undefined command codes are not modelled.
void IpuDevice::ExecuteDivergent(const char* name, uint32_t cmd) {
    MarkUnsupported(kUnsupportedOpcode);
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

void IpuDevice::ExecuteSetVq(uint32_t cmd) {
    BitCursor probe{input_fifo_.data(), input_fifo_.size(), bp_};
    if (!MpegDecode::HasBits(probe, 32 * 8)) {
        DeferForInput(cmd, bp_, bp_);
        return;
    }
    uint8_t bytes[32];
    if (!ReadInputBytes(bytes, sizeof(bytes))) return;
    busy_ = false;
    for (size_t i = 0; i < 16; ++i)
        vq_clut_[i] = static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
}

void IpuDevice::ExecuteVdec(uint32_t cmd) {
    const uint32_t bp_at_issue = bp_;
    BitCursor cursor{input_fifo_.data(), input_fifo_.size(), bp_};
    MpegDecode::SkipBits(cursor, cmd & 0x3fu);
    if (!MpegDecode::HasBits(cursor, 32)) {
        DeferForInput(cmd, bp_at_issue, bp_at_issue);
        return;
    }
    bp_ = cursor.bit_pos;
    busy_ = false;
    fdec_result_ = IpuVlc::Decode(MpegDecode::PeekBits(cursor, 32), (cmd >> 26) & 3u,
                                   (control_ >> 24) & 7u, (control_ & (1u << 23)) != 0);
    ecd_ = fdec_result_ == 0;
    bp_ += fdec_result_ >> 16;
    PS2R_LOG(IPU, Narrate, "VDEC: fb=%u tbl=%u result=0x%08x bp=%u", cmd & 0x3fu, (cmd >> 26) & 3u, fdec_result_,
             BitPosition());
}

void IpuDevice::AppendPixel(uint32_t rgba, unsigned pixel, bool rgb16, bool dither) {
    const uint32_t packed = rgb16 ? IpuColor::Pack(rgba, pixel % 16, pixel / 16, dither) : rgba;
    for (unsigned byte = 0; byte < (rgb16 ? 2u : 4u); ++byte)
        output_fifo_.push_back(static_cast<uint8_t>(packed >> (byte * 8)));
}

// ---------------------------------------------------------------------------
// CSC: planar RAW8 macroblocks to RGB32/RGB16, with transparency and dither.
// Each complete macroblock in the FIFO is converted and committed (BP, output);
// when the next one is not all there the command stays pending with the
// macroblocks still to do in its MBC field, and the bytes already buffered count
// as consumed (the converter holds them), so a 384-byte macroblock streams through
// the eight-quadword FIFO as it does on the hardware.
// ---------------------------------------------------------------------------
void IpuDevice::ExecuteCsc(uint32_t cmd) {
    uint32_t remaining = cmd & 0x7FF;
    const bool rgb16 = (cmd & (1u << 27)) != 0;
    const bool dither = (cmd & (1u << 26)) != 0;

    while (remaining) {
        const BitCursor probe{input_fifo_.data(), input_fifo_.size(), bp_};
        if (!MpegDecode::HasBits(probe, static_cast<int>(kCscInputBytesPerMb * 8))) {
            DeferForInput((cmd & ~0x7FFu) | remaining, bp_, static_cast<uint32_t>(input_fifo_.size() * 8));
            return;
        }
        uint8_t bytes[kCscInputBytesPerMb];
        ReadInputBytes(bytes, sizeof(bytes));
        for (unsigned pixel = 0; pixel < 256; ++pixel) {
            const unsigned chroma = (pixel / 32) * 8 + (pixel % 16) / 2;
            const uint32_t rgba = IpuColor::Convert(bytes[pixel], bytes[256 + chroma], bytes[320 + chroma]);
            AppendPixel(IpuColor::Threshold(rgba, th0_, th1_), pixel, rgb16, dither);
        }
        --remaining;
    }
    busy_ = false;
}

// PACK: RGB32 macroblocks (1024 bytes) to RGB16 or 4-bit indexed, the same
// per-macroblock streaming as CSC.
void IpuDevice::ExecutePack(uint32_t cmd) {
    uint32_t remaining = cmd & 0x7FF;
    const bool rgb16 = (cmd & (1u << 27)) != 0;
    const bool dither = (cmd & (1u << 26)) != 0;
    while (remaining) {
        const BitCursor probe{input_fifo_.data(), input_fifo_.size(), bp_};
        if (!MpegDecode::HasBits(probe, kPackInputBytesPerMb * 8)) {
            DeferForInput((cmd & ~0x7FFu) | remaining, bp_, static_cast<uint32_t>(input_fifo_.size() * 8));
            return;
        }
        uint8_t bytes[kPackInputBytesPerMb];
        ReadInputBytes(bytes, sizeof(bytes));
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
        --remaining;
    }
    busy_ = false;
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

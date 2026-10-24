// engine/ipu/ipu_device.h -- the EE's Image Processing Unit register model.
//
// The IPU exposes four registers on a 16-byte stride (EE User's Manual,
// IPU chapter): IPU_CMD at +0x00 (write = issue a command; read = the result of
// the last FDEC/VDEC with BUSY in bit 63), IPU_CTRL at +0x10 (RST in bit 30
// resets the unit; reads report IFC/OFC/CBP/ECD/SCD/IDP and BUSY in bit 31),
// IPU_BP at +0x20 (bit position in the input FIFO + IFC) and IPU_TOP at +0x30
// (the next 32 bits of the bitstream; bit 63 BUSY when they are not all there). The register window is 0x40 bytes; the
// class decodes the register from the low six bits of the address, so the
// composition root registers it over any 64-byte-aligned base (on the PS2 that
// base is the IPU's fixed I/O address) and this file names no absolute address.
//
// A command with enough input completes synchronously inside WriteCmd. An
// input-consuming command (FDEC / VDEC / BDEC / SETIQ / SETVQ / CSC / PACK) issued on
// a FIFO that holds fewer bits than it needs does what the hardware does: it stays PENDING
// with BUSY set (IPU_CMD bit 63, IPU_CTRL bit 31) until FeedInputFifo supplies
// the bits, then completes from the bit position it was issued at. That is the
// protocol libmpeg's sceIpuSync relies on: BUSY with a low IFC and no toIPU DMA in
// flight is its cue to run the title's NODATA callback, which feeds the FIFO. While
// a command is pending, the input it already parsed counts as consumed for IFC and
// for the FIFO room (the hardware's decoder has pulled those bits out of the FIFO),
// so a macroblock longer than the FIFO still streams through. The two DMAC FIFO
// channels reach the unit through FeedInputFifo (RAM -> IPU) and DrainOutputFifo
// (IPU -> RAM); the channel coupling itself lives with the DMAC. Scheduling,
// backpressure and the internal FP buffer are not modelled. IFC/OFC report the
// remaining buffered quadwords, capped at eight.
//
// BDEC (block decode) is the MPEG-2 macroblock decode of 7.2 - 7.5 (ISO/IEC 13818-2)
// over MpegDecode: the DC / AC coefficient VLCs, the coded_block_pattern of a
// non-intra macroblock (Table B-9; the decoded pattern is latched into CTRL.CBP),
// inverse quantisation with the SETIQ matrices and the command's quantiser_scale_code
// (CTRL.QST selects the non-linear scale, IVF the intra table, MP1 the MPEG-1 escape,
// AS the alternate scan) and the IDCT, output as one RAW16 macroblock. A code that
// does not match sets CTRL.ECD and the command completes without output.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "engine/ee/mem/mmio_handler.h"

namespace ps2r::ipu {

// IPU_CMD command codes (bits 31:28 of the command word).
enum class IpuCommand : uint8_t {
    Bclr = 0x0,   // clear the input FIFO, set the bit pointer
    Idec = 0x1,   // intra decode (slice of I-macroblocks -> RGB)
    Bdec = 0x2,   // block decode
    Vdec = 0x3,   // variable-length decode
    Fdec = 0x4,   // fixed-length decode: peek 32 bits
    SetIq = 0x5,  // load a quantiser matrix from the input FIFO
    SetVq = 0x6,  // load the vector-quantisation table
    Csc = 0x7,    // colour-space conversion of raw macroblocks
    Pack = 0x8,   // RGB32 -> indexed/RGB16 packing
    SetTh = 0x9,  // set the alpha thresholds TH0/TH1
};

class IpuDevice : public ::MmioHandler {
public:
    static constexpr uint32_t kRegCmd = 0x00;
    static constexpr uint32_t kRegCtrl = 0x10;
    static constexpr uint32_t kRegBp = 0x20;
    static constexpr uint32_t kRegTop = 0x30;
    static constexpr uint32_t kWindowBytes = 0x40;

    static constexpr uint32_t kCtrlRst = 1u << 30;
    static constexpr uint32_t kCtrlBusy = 1u << 31;
    static constexpr uint32_t kCtrlWritable = 0x07f30000u;

    static constexpr uint32_t kUnsupportedDecode = 1u << 0;
    static constexpr uint32_t kUnsupportedInput = 1u << 1;
    static constexpr uint32_t kUnsupportedOpcode = 1u << 2;

    static constexpr size_t kQuantMatrixBytes = 64;
    // A raw CSC macroblock: 256 Y + 64 Cb + 64 Cr samples.
    static constexpr size_t kCscInputBytesPerMb = 384;
    // A PACK macroblock: 256 RGB32 pixels.
    static constexpr int kPackInputBytesPerMb = 1024;
    // BDEC's RAW16 macroblock: the same planes as 16-bit signed samples (768 bytes =
    // 48 quadwords): Y as a 16x16 raster, then Cb 8x8, then Cr 8x8.
    static constexpr size_t kBdecOutputBytes = kCscInputBytesPerMb * 2;
    // The longest coefficient / CBP / DC code a block decode reads at once (ESCAPE =
    // 6 + 6 + 12 bits): a VLC failure with fewer bits than this left is starvation.
    static constexpr int kBdecLookaheadBits = 24;

    IpuDevice();

    // MmioHandler: `addr` is any address inside the registered 0x40-byte window.
    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    // Register face (32-bit words, as the EE bus presents them).
    void WriteCmd(uint32_t value);
    uint32_t ReadCmd();
    void WriteCtrl(uint32_t value);
    uint32_t ReadCtrl();
    uint32_t ReadBp();
    uint32_t ReadTop();
    // IPU_TOP's BUSY (bit 63 of the 64-bit read): the TOP word is incomplete -- a command
    // is in progress or the FIFO holds fewer than 32 bits past the bit pointer.
    bool TopBusy() const;

    // DMAC FIFO face.
    void FeedInputFifo(const uint8_t* data, size_t size);
    size_t DrainOutputFifo(uint8_t* dest, size_t max_size);
    // Called once after a command appends output. The host can retry an armed
    // FROM transfer; no DMA state or host callback is serialized by the IPU.
    void SetOutputReady(std::function<void()> callback);
    // The input FIFO is eight quadwords deep on the hardware; the toIPU DMA only moves
    // what fits and the remainder waits for the IPU to consume. InputRoomBytes() is
    // the free space (fully consumed leading quadwords are dropped after every command);
    // the room-ready callback fires once when a command freed space, so the host can
    // resume a toIPU channel left with QWC. Unbound (default) the FIFO is unbounded.
    static constexpr size_t kInputFifoBytes = 8 * 16;
    size_t InputRoomBytes() const;
    // Quadwords fed but not yet consumed (the current partially read one included).
    size_t UnreadInputQwords() const;
    void SetInputRoomReady(std::function<void()> callback);

    bool HasOutputData() const { return !output_fifo_.empty(); }
    size_t OutputSize() const { return output_fifo_.size(); }
    size_t InputSize() const { return input_fifo_.size(); }
    bool IsBusy() const { return busy_; }
    // A command waiting for input (BUSY): its word, or 0 when none is pending.
    uint32_t PendingCommand() const { return busy_ ? pending_command_ : 0; }
    uint64_t PendingCompletions() const { return pending_completions_; }  // commands finished by a later feed

    // Observers for tests and diagnostics. BitPosition is the absolute position in the
    // stream fed so far (consumed leading quadwords are dropped from the FIFO but counted).
    uint32_t BitPosition() const { return bp_ + static_cast<uint32_t>(dropped_bytes_ * 8); }
    const uint8_t* IntraQuantMatrix() const { return intra_iq_; }
    const uint8_t* NonIntraQuantMatrix() const { return non_intra_iq_; }
    uint16_t Threshold0() const { return th0_; }
    uint16_t Threshold1() const { return th1_; }
    const uint16_t* VqClut() const { return vq_clut_; }
    // Lifetime observer statistics, excluded from savestate. RST/BCLR never clear
    // these counters. Sample on the device's owning thread or after stopping it.
    // Out-of-range codes return zero. Unsupported counts count issues, not
    // reasons: one command with several omissions contributes exactly once.
    uint64_t CommandCount(uint8_t code) const;
    uint64_t UnsupportedCommandCount(uint8_t code) const;
    uint32_t LastUnsupportedCommand(uint8_t code) const;
    uint32_t UnsupportedVariantMask(uint8_t code) const;
    uint64_t DivergentCommandCount() const { return divergent_commands_; }
    // Writes to a register offset other than CMD/CTRL (dropped).
    uint64_t UnhandledWriteCount() const { return unhandled_writes_; }

private:
    uint32_t ReadWord(uint32_t offset);
    void WriteWord(uint32_t offset, uint32_t value);

    void ExecuteBclr(uint32_t cmd);
    void ExecuteIdec(uint32_t cmd);
    void ExecuteBdec(uint32_t cmd);
    void ExecuteVdec(uint32_t cmd);
    void ExecuteCsc(uint32_t cmd);
    void ExecutePack(uint32_t cmd);
    void ExecuteFdec(uint32_t cmd);
    void ExecuteSetIq(uint32_t cmd);
    void ExecuteSetVq(uint32_t cmd);
    void ExecuteSetTh(uint32_t cmd);
    void ExecuteDivergent(const char* name, uint32_t cmd);
    void MarkUnsupported(uint32_t reason);
    void AppendPixel(uint32_t rgba, unsigned pixel, bool rgb16, bool dither);
    // BDEC's RAW16 output: six decoded blocks (Y0 Y1 Y2 Y3 Cb Cr) as the planar
    // macroblock; `field_dct` interleaves the luminance block rows (6.1.3, field DCT).
    void AppendRaw16Macroblock(const int16_t blocks[6][64], bool field_dct);
    bool ReadInputBytes(uint8_t* dest, size_t size);

    void RecomputeInputCount();
    void RecomputeOutputCount();
    // The bit position the hardware's decoder has reached: bp_ when idle, else the point
    // where the pending command ran out of bits (the bits before it are out of the FIFO).
    uint32_t ConsumedBitPosition() const;
    // Drops the fully consumed leading quadwords (bp_ rebased); reports room when freed.
    void CompactInput();
    // Leaves `cmd` pending at `bp_at_issue`: BUSY until the FIFO has its bits. `reach` is
    // the bit position it stalled at (its input up to there counts as consumed).
    void DeferForInput(uint32_t cmd, uint32_t bp_at_issue, uint32_t reach);
    // Re-runs the pending command after a feed; clears BUSY when it completes.
    void RetryPending();
    // Fires the room-ready callback once (not reentrantly).
    void NotifyInputRoom();

    // CTRL fields.
    uint8_t ifc_ = 0;    // input FIFO quadwords (0..8)
    uint8_t ofc_ = 0;    // output FIFO quadwords (0..8)
    uint8_t cbp_ = 0;    // coded block pattern
    bool ecd_ = false;   // error code detected
    bool scd_ = false;   // start code detected
    uint8_t idp_ = 0;    // intra DC precision (0 = 8 bit, 1 = 9 bit, 2 = 10 bit)
    uint32_t control_ = 0;
    bool busy_ = false;
    uint32_t pending_command_ = 0;   // the BUSY command (valid while busy_)
    uint32_t pending_bp_ = 0;        // bp_ when it was issued (restored for the retry)
    uint32_t pending_reach_bp_ = 0;  // where it stalled: input before this is consumed
    bool retrying_ = false;          // inside RetryPending (a second deferral keeps waiting)
    uint64_t pending_completions_ = 0;

    std::vector<uint8_t> input_fifo_;   // raw bitstream bytes
    std::vector<uint8_t> output_fifo_;  // decoded pixel bytes
    std::function<void()> output_ready_;
    std::function<void()> input_room_ready_;
    bool in_room_ready_ = false;
    bool room_notify_pending_ = false;  // a nested room request to re-fire after the callback
    size_t dropped_bytes_ = 0;   // consumed leading bytes dropped from input_fifo_ (CompactInput)
    uint32_t bp_ = 0;                   // bit position in the input FIFO

    uint32_t fdec_result_ = 0;

    uint8_t intra_iq_[kQuantMatrixBytes];
    uint8_t non_intra_iq_[kQuantMatrixBytes];

    int dc_pred_[3] = {0, 0, 0};  // Y, Cb, Cr DC predictors

    uint16_t th0_ = 0;
    uint16_t th1_ = 0;
    uint16_t vq_clut_[16] = {};

    uint32_t current_command_ = 0;
    bool current_unsupported_ = false;
    uint64_t command_counts_[16] = {};
    uint64_t unsupported_counts_[16] = {};
    uint32_t last_unsupported_[16] = {};
    uint32_t unsupported_masks_[16] = {};
    uint64_t divergent_commands_ = 0;
    uint64_t unhandled_writes_ = 0;
};

}  // namespace ps2r::ipu

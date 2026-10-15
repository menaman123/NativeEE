#pragma once
// StreamReader: the libcdvd streaming API (sceCdSt*) as a ring-buffer state
// machine over the ReadEngine. The PS2 stream: sceCdStInit declares the ring
// (sectors per buffer, buffer count, EE address); sceCdStStart positions the
// drive at an LBA and begins filling; sceCdStRead pulls whole sectors from the
// current position into the caller's buffer and advances it; Seek repositions;
// Pause/Resume hold and release the fill; Stop ends the stream; Stat reports
// the sectors buffered and ready.
//
// Every command is answered inside the call (the original runtime's shape):
//   Init(ring, bufs, addr)  -> 1, stream idle at LBA 0
//   Start(lba)              -> 1, active from lba (a Start while active restarts)
//   Seek(lba)               -> 1, current position = lba
//   Stop()                  -> 1, inactive
//   Read(sectors, dst, err) -> sectors delivered: 0 with err=1 when not active
//                              or no image; 0 with err=0 past the image end;
//                              otherwise whole sectors up to the image end,
//                              err=0, position advanced
//   Stat()                  -> 0 when inactive; else the ring size in sectors,
//                              16 when Init declared 0 (see below)
//   Pause()/Resume()        -> 1 (a paused stream still answers Read: the data
//                              is already on the image)
//
// diverges: Stat reports an always-full ring (the whole ring, `16` when the
// ring size is unknown) instead of the sectors the drive has actually
// buffered since Start -- reads complete inside the call, so the ring is
// full by construction. Kept from the original runtime as a documented
// divergence (HACK_LEDGER cdvd_stream.cpp:268-276); the completion-visibility
// model (DeliveryMeter) is the future owner of a metered Stat.
#include <cstdint>

class ReadEngine;

class StreamReader {
public:
    static constexpr uint32_t kSectorBytes = 2048;
    static constexpr uint32_t kDefaultRingSectors = 16;   // Stat's answer for an undeclared ring
    // sceCdStream(lbn, sectors, buf, command, mode) command codes (the SDK's
    // internal dispatcher every sceCdSt* wrapper calls).
    enum Command : uint32_t {
        kCmdStart = 1, kCmdRead = 2, kCmdStop = 3, kCmdSeek = 4, kCmdInit = 5,
        kCmdStat = 6, kCmdPause = 7, kCmdResume = 8, kCmdSeekF = 9,
    };

    explicit StreamReader(ReadEngine* engine) : engine_(engine) {}
    void SetEngine(ReadEngine* engine) { engine_ = engine; }

    uint32_t Init(uint32_t ring_sectors, uint32_t num_buffers, uint32_t buf_addr);
    uint32_t Start(uint32_t lba);
    uint32_t Seek(uint32_t lba);
    uint32_t Stop();
    // Whole sectors into dst (sectors * 2048 bytes of room); *err as above.
    uint32_t Read(uint32_t sectors, uint8_t* dst, uint32_t* err);
    uint32_t Stat() const;
    uint32_t Pause();
    uint32_t Resume();

    bool Initialized() const { return initialized_; }
    bool Active() const { return active_; }
    bool Paused() const { return paused_; }
    uint32_t StartLba() const { return start_lba_; }
    uint32_t CurrentLba() const { return current_lba_; }
    uint32_t RingSectors() const { return ring_sectors_; }
    uint32_t NumBuffers() const { return num_buffers_; }
    uint32_t BufferAddress() const { return buf_addr_; }
    uint32_t TotalRead() const { return total_read_; }

private:
    ReadEngine* engine_;
    bool initialized_ = false;
    bool active_ = false;
    bool paused_ = false;
    uint32_t start_lba_ = 0;
    uint32_t current_lba_ = 0;
    uint32_t ring_sectors_ = 0;
    uint32_t num_buffers_ = 0;
    uint32_t buf_addr_ = 0;
    uint32_t total_read_ = 0;
};

#pragma once
// XGKICK: the VU1 -> GIF PATH1 transfer of a GIF packet out of VU1 data
// memory.
//
// PS2 rules: XGKICK arms a PATH1 transfer of the packet starting at data
// address VI[is]*16; the GIF reads VU1 memory as its drain pointer advances at
// 1 quadword per 2 VU cycles (stores issued after the kick but ahead of the
// pointer ARE transferred, which is why the bytes are copied at drain time,
// not at the kick); a second XGKICK while one is draining stalls the VU until
// the first completes; program end completes the pending kick without a
// stall charge; the packet length is discovered by walking GIFtags; a walk
// reaching 0x4000 bytes is invalid and cancels the kick. Addresses wrap at
// 16 KB. The drain is evaluated once per bundle in quanta of 32 cycles.
//
// `paced_drain == false` selects the immediate form: the whole packet is
// copied at the instruction (same tag walk, same submit); the harness
// certifies the paced form.
#include <cstddef>
#include <cstdint>
#include <vector>

class GifArbiter;

class XgKick {
public:
    static constexpr int64_t kQuantum = 32;                 // cycles between drain evaluations
    static constexpr uint32_t kMemMask = 0x3FFF;            // VU1 data memory wrap
    static constexpr uint32_t kMemSize = 0x4000;
    static constexpr uint32_t kPacketCancelBytes = 0x4000;  // a tag walk reaching this cancels the kick
    static constexpr uint32_t kRunawayBytes = 0x40000;      // staging past this drops the chain

    XgKick(GifArbiter& arbiter, bool paced_drain = true);

    // The XGKICK instruction: `vi_addr` is VI[is] (quadword address). A kick
    // still draining is completed first, charging 2 cycles per quadword left.
    void Arm(const uint8_t* data, uint32_t vi_addr, uint64_t& vu_cycle);
    // Once per bundle after the stalls: drain what the elapsed cycles allow.
    void Pace(const uint8_t* data, uint64_t& vu_cycle);
    // Complete the pending kick now; `add_stall_cycles` charges the VU clock.
    void Flush(const uint8_t* data, uint64_t& vu_cycle, bool add_stall_cycles);
    // Disarm and drop the staging (the unit's Reset).
    void Reset();

    // Walk the GIFtags at `offset`: one tag (paced) or the chain to EOP
    // (flush). Returns `bytes | eop << 31`, or 0 when the walk reaches 0x4000.
    static uint32_t PacketSize(const uint8_t* data, uint32_t offset, bool flush);

    bool armed() const { return enable_; }
    bool paced_drain() const { return paced_; }
    uint32_t addr() const { return addr_; }
    uint32_t size_remaining() const { return size_remaining_; }
    uint64_t next_due() const { return next_due_; }
    uint32_t cyclecount() const { return cyclecount_; }
    size_t staging_bytes() const { return staging_.size(); }

private:
    void Transfer(const uint8_t* data, uint64_t& vu_cycle, int64_t cycles, bool flush, bool add_stall_cycles);

    GifArbiter& arbiter_;
    bool paced_;
    bool enable_ = false;
    uint32_t addr_ = 0;            // drain pointer (byte address)
    uint32_t size_remaining_ = 0;  // bytes left of the current tag's span
    bool endpacket_ = false;       // the current span ends the packet (EOP)
    uint32_t cyclecount_ = 0;      // banked drain cycles (2 per quadword)
    uint64_t lastcycle_ = 0;       // VU cycle the bank was last settled at
    uint64_t next_due_ = 0;        // first cycle the next Pace acts
    std::vector<uint8_t> staging_;
};

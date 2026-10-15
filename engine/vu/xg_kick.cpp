// XgKick -- arm / pace / transfer / flush of the PATH1 packet. Rules in the
// header.
#include "engine/vu/xg_kick.h"

#include <cstring>

#include "engine/gif/gif_arbiter.h"

XgKick::XgKick(GifArbiter& arbiter, bool paced_drain) : arbiter_(arbiter), paced_(paced_drain) {}

uint32_t XgKick::PacketSize(const uint8_t* data, uint32_t offset, bool flush) {
    uint32_t cur_size = 0;
    for (;;) {
        uint64_t tag_lo;
        std::memcpy(&tag_lo, data + (offset & 0x3FF0u), sizeof(tag_lo));  // the low 64 bits of the GIFtag
        const uint32_t nloop = static_cast<uint32_t>(tag_lo & 0x7FFFu);
        const uint32_t eop = static_cast<uint32_t>((tag_lo >> 15) & 1u);
        const uint32_t flg = static_cast<uint32_t>((tag_lo >> 58) & 3u);
        uint32_t nregs = static_cast<uint32_t>((tag_lo >> 60) & 0xFu);
        if (nregs == 0) nregs = 16;
        uint32_t len;
        if (flg == 0) {
            len = nregs * nloop * 16;                    // PACKED: one quadword per register per loop
        } else if (flg == 1) {
            len = ((nregs * nloop + 1) / 2) * 16;        // REGLIST: two registers per quadword, odd padded
        } else {
            len = nloop * 16;                            // IMAGE / DISABLED: NLOOP quadwords
        }
        cur_size += 16 + len;
        offset = (offset + 16 + len) & kMemMask;
        if (cur_size >= kPacketCancelBytes) return 0;  // longer than VU memory: invalid, cancel
        if ((flush && eop) || !flush) return (cur_size & 0xFFFFu) | (eop << 31);
    }
}

void XgKick::Arm(const uint8_t* data, uint32_t vi_addr, uint64_t& vu_cycle) {
    const uint32_t mem_offset = (vi_addr * 16u) & kMemMask;
    if (enable_) Flush(data, vu_cycle, /*add_stall_cycles=*/true);  // second kick: finish the first, stalled
    enable_ = true;
    addr_ = mem_offset;
    size_remaining_ = 0;
    endpacket_ = false;
    lastcycle_ = vu_cycle;
    next_due_ = lastcycle_ + static_cast<uint64_t>(kQuantum) + 1;
    cyclecount_ = 1;  // the kick itself counts as one transfer cycle
    staging_.clear();
    if (staging_.capacity() < kMemSize) staging_.reserve(kMemSize);
    if (!paced_) Transfer(data, vu_cycle, 0, /*flush=*/true, /*add_stall_cycles=*/false);
}

void XgKick::Pace(const uint8_t* data, uint64_t& vu_cycle) {
    if (!enable_) return;
    if (vu_cycle < next_due_) return;
    const int64_t elapsed = static_cast<int64_t>(vu_cycle - lastcycle_) - 1;
    Transfer(data, vu_cycle, elapsed, /*flush=*/false, /*add_stall_cycles=*/false);
}

void XgKick::Flush(const uint8_t* data, uint64_t& vu_cycle, bool add_stall_cycles) {
    if (enable_) Transfer(data, vu_cycle, 0, /*flush=*/true, add_stall_cycles);
}

void XgKick::Reset() {
    enable_ = false;
    addr_ = 0;
    size_remaining_ = 0;
    endpacket_ = false;
    cyclecount_ = 0;
    lastcycle_ = 0;
    staging_.clear();
}

void XgKick::Transfer(const uint8_t* data, uint64_t& vu_cycle, int64_t cycles, bool flush, bool add_stall_cycles) {
    if (!enable_) return;
    if (cycles < 0) cycles = 0;
    cyclecount_ += static_cast<uint32_t>(cycles);
    lastcycle_ += static_cast<uint64_t>(cycles);
    next_due_ = lastcycle_ + static_cast<uint64_t>(kQuantum) + 1;

    while (enable_ && (flush || cyclecount_ >= 2)) {
        if (size_remaining_ == 0) {
            // Refill from the next tag in CURRENT data memory.
            const uint32_t size = PacketSize(data, addr_, flush);
            size_remaining_ = size & 0xFFFFu;
            endpacket_ = (size >> 31) != 0;
            if (size_remaining_ == 0) {
                enable_ = false;  // invalid / oversized packet: cancel (bytes staged so far still go out)
                break;
            }
        }
        if (staging_.size() > kRunawayBytes) {
            enable_ = false;  // runaway chain: drop everything
            staging_.clear();
            break;
        }
        const uint32_t diff_qw = (kMemSize - addr_) / 16;  // quadwords to the wrap edge
        uint32_t transfer_qw = size_remaining_ / 16;
        if (!flush && cyclecount_ / 2 < transfer_qw) transfer_qw = cyclecount_ / 2;
        if (diff_qw < transfer_qw) transfer_qw = diff_qw;
        if (transfer_qw != 0) {
            staging_.insert(staging_.end(), data + addr_, data + addr_ + transfer_qw * 16);
        }
        if (flush && add_stall_cycles) vu_cycle += static_cast<uint64_t>(transfer_qw) * 2;
        cyclecount_ -= transfer_qw * 2;  // may wrap in the flush arm; the next Arm resets it
        addr_ = (addr_ + transfer_qw * 16) & kMemMask;
        size_remaining_ -= transfer_qw * 16;
        if (size_remaining_ == 0 && endpacket_) enable_ = false;  // packet complete
    }

    if (!enable_ && !staging_.empty()) {
        std::vector<uint8_t> out = arbiter_.acquireBuffer();
        out.swap(staging_);
        arbiter_.submitOwned(GifPath::Path1, std::move(out));
    }
}

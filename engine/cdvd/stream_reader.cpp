// StreamReader implementation; the contract is in the header.
#include "engine/cdvd/stream_reader.h"

#include "engine/cdvd/read_engine.h"

uint32_t StreamReader::Init(uint32_t ring_sectors, uint32_t num_buffers, uint32_t buf_addr) {
    initialized_ = true;
    active_ = false;
    paused_ = false;
    ring_sectors_ = ring_sectors;
    num_buffers_ = num_buffers;
    buf_addr_ = buf_addr;
    current_lba_ = 0;
    total_read_ = 0;
    return 1;
}

uint32_t StreamReader::Start(uint32_t lba) {
    start_lba_ = lba;
    current_lba_ = lba;
    active_ = true;
    paused_ = false;
    total_read_ = 0;
    return 1;
}

uint32_t StreamReader::Seek(uint32_t lba) {
    current_lba_ = lba;
    return 1;
}

uint32_t StreamReader::Stop() {
    active_ = false;
    paused_ = false;
    return 1;
}

uint32_t StreamReader::Read(uint32_t sectors, uint8_t* dst, uint32_t* err) {
    if (!active_ || engine_ == nullptr || dst == nullptr) {
        if (err) *err = 1;
        return 0;
    }
    // Past the image end: nothing to deliver, not an error (the stream ran off the disc).
    const uint32_t got = sectors ? engine_->ReadSectors(current_lba_, sectors, dst) : 0;
    if (err) *err = 0;
    current_lba_ += got;
    total_read_ += got;
    return got;
}

uint32_t StreamReader::Stat() const {
    if (!active_) return 0;
    return ring_sectors_ ? ring_sectors_ : kDefaultRingSectors;   // diverges: always-full ring (header)
}

uint32_t StreamReader::Pause() {
    paused_ = true;
    return 1;
}

uint32_t StreamReader::Resume() {
    paused_ = false;
    return 1;
}

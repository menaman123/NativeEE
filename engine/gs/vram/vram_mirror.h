// gs/vram/vram_mirror.h -- VramMirror: the observer of a VramImage's EE-side
// mutations (VramImage::SetMirror). Each call carries exactly the arguments the
// image method received; spans are (start, size) byte pairs wrapping at 4 MiB.
// Implemented by VramOpLog (the pipelined GS queue's split image).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r {
namespace gs {

class VramMirror {
public:
    virtual ~VramMirror() = default;
    virtual void OnWriteStream(uint32_t byte_off, const uint8_t* src, size_t len) = 0;
    virtual void OnNoteWriteNoBytes(uint32_t byte_off, size_t len) = 0;
    virtual void OnNoteStreamWrite(uint32_t start, uint32_t size) = 0;
    virtual void OnSettleWrites() = 0;
    virtual void OnNoteCpuWrite(uint32_t start, uint32_t size) = 0;
};

}  // namespace gs
}  // namespace ps2r

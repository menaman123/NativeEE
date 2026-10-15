// core/savestate/payload_reader.h -- bounds-checked cursor over a section
// payload. Every Get returns false on overrun so a restore aborts instead of
// reading past a truncated or foreign section (SAVESTATE_SCHEMA: payloads are
// packed little-endian PODs, never raw struct dumps).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ps2r::savestate {

class PayloadReader {
public:
    explicit PayloadReader(const std::vector<uint8_t>& v) : p_(v.data()), len_(v.size()) {}
    PayloadReader(const uint8_t* p, size_t len) : p_(p), len_(len) {}

    template <typename T>
    bool Get(T* out);
    bool GetBytes(void* out, size_t n);
    bool Skip(size_t n);
    size_t Remaining() const { return len_ - off_; }
    size_t Offset() const { return off_; }

private:
    const uint8_t* p_;
    size_t len_;
    size_t off_ = 0;
};

}  // namespace ps2r::savestate

#include "engine/core/savestate/payload_reader.inl"

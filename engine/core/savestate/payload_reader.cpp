// core/savestate/payload_reader.cpp
#include "engine/core/savestate/payload_reader.h"

namespace ps2r::savestate {

bool PayloadReader::GetBytes(void* out, size_t n) {
    if (off_ + n > len_) return false;
    std::memcpy(out, p_ + off_, n);
    off_ += n;
    return true;
}

bool PayloadReader::Skip(size_t n) {
    if (off_ + n > len_) return false;
    off_ += n;
    return true;
}

}  // namespace ps2r::savestate

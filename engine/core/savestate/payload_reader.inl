// core/savestate/payload_reader.inl -- template sibling of payload_reader.h.
#pragma once

namespace ps2r::savestate {

template <typename T>
bool PayloadReader::Get(T* out) {
    if (off_ + sizeof(T) > len_) return false;
    std::memcpy(out, p_ + off_, sizeof(T));
    off_ += sizeof(T);
    return true;
}

}  // namespace ps2r::savestate

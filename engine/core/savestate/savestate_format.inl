// core/savestate/savestate_format.inl -- template sibling of savestate_format.h.
#pragma once

namespace ps2r::savestate {

template <typename T>
void PutPod(std::vector<uint8_t>& v, const T& x) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&x);
    v.insert(v.end(), p, p + sizeof(T));
}

}  // namespace ps2r::savestate

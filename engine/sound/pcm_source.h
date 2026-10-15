#pragma once

#include <array>
#include <cstdint>

namespace ps2r::sound {
// Software PCM input to the common voice/input/master mixing stage. The game
// execution owner supplies and consumes it; a device callback never drives it.
class PcmSource {
public:
    virtual ~PcmSource() = default;
    virtual std::array<float, 2> Gains() const = 0;
    virtual int Pop(int16_t* stereo, int frames) = 0;
};
}  // namespace ps2r::sound

#pragma once

namespace ps2r::sound {

// The game-clock output owner. One call begins one logical audio tick and
// writes the requested stereo frames; the playback device never calls it.
class MixSource {
public:
    virtual ~MixSource() = default;
    virtual void MixInto(float* stereo, int frames) = 0;
};

} // namespace ps2r::sound

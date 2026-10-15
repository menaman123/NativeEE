#include "tests/sound/pcm_source_fixture.h"

int PcmSourceFixture::Pop(int16_t* stereo, int n) {
    ++pops;
    const int popped = input_.Pop(stereo, n);
    frames += static_cast<uint64_t>(popped);
    return popped;
}

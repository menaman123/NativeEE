#include "engine/sound/command_ring.h"

namespace ps2r::sound {

void CommandRing::Push(const SoundCommand& cmd) { ring_.Push(cmd); }

bool CommandRing::Pop(SoundCommand& out) { return ring_.Pop(out); }

void CommandRing::Clear() {
    SoundCommand c;
    while (ring_.Pop(c)) {
    }
}

}  // namespace ps2r::sound

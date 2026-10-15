#pragma once

#include <cstdint>
#include <string>

class MainMemory;
class StateRestorer;
namespace ps2r::savestate { class Quiescence; }

namespace ps2r::app {

// Boundary diagnostics are independent of resumable state. An owned Graphics
// queue may prevent a runtime save without preventing inspection of game data.
class CheckpointCapture {
public:
    static void Write(const std::string& directory, uint64_t boundary, uint64_t game_frame,
                      const MainMemory& memory, const StateRestorer& restorer,
                      savestate::Quiescence& quiescence);
};

}  // namespace ps2r::app

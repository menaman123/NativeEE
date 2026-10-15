#pragma once
// Generated-code ABI forwarder (row 13): thread exit + main-thread lookup.
// Both are served by ThreadManager (engine/ee/hle); `ThreadInfo` is its
// record (the generated code reads `lookupThreadInfo(1)->gp`).
#include <memory>

#include "engine/ee/hle/thread_manager.h"

namespace ps2_threading {

using ::ThreadInfo;

void exitThread();
std::shared_ptr<ThreadInfo> lookupThreadInfo(int tid);

}  // namespace ps2_threading

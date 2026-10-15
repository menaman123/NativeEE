#pragma once
// GuestPark: the terminal exit of a guest thread that reached a body the runtime
// cannot execute yet (an absent IOP-side RPC, an unresolvable indirect jump,
// a VU microprogram before Phase 5). A park NEVER returns to the caller's
// flow: it records the reason, hands the record to the installed sink (the
// composition root's run.meta writer + run-token release, Phase 3F) and blocks
// the calling host thread forever. exit() is never called from here.
#include <cstdint>
#include <functional>

struct ParkRecord {
    const char* reason = nullptr;  // e.g. "sif_rpc_absent", "fatal_no_jump"
    uint32_t pc = 0;               // guest pc when known, else 0
    const char* detail = nullptr;  // optional: e.g. the SDK row "libgraph.a sceGsPutDrawEnv"
};

class GuestPark {
public:
    using Sink = std::function<void(const ParkRecord&)>;

    // Records `reason`/`pc`, calls the sink (if any) and blocks forever. A sink
    // that throws makes the park unwind instead of block (the unit test's path);
    // either way control never comes back to the caller.
    [[noreturn]] static void Forever(const char* reason, uint32_t pc = 0, const char* detail = nullptr);

    static void SetSink(Sink sink);
    static const ParkRecord& Last();
    static uint32_t Count();
};

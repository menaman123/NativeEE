#pragma once
// ParkSiteDump: the text of a fatal_no_jump park record. A `jr`/`jalr` whose
// target no dispatch tier resolves is terminal; the record then has to say
// WHERE the guest was (the shadow call-stack chain, innermost first) and WHAT it
// held (the parking thread's 32 GPRs, low word): every register equal to the
// target is a candidate jump source (the R5900 reads the target from one GPR),
// the other registers locate the object or table the word was loaded from, so
// the word's console-side writer can be named from the function's code alone.
#include <cstddef>
#include <cstdint>

struct EmotionEngineState;

class ParkSiteDump {
public:
    static constexpr uint32_t kMaxFrames = 12;  // shadow frames written (innermost first)

    // Formats `target=0x.. src=<reg,…> chain=<func(ra=..)<-…> gpr:<name=0x..…>`
    // into `out` (NUL-terminated, truncated at `cap`). `ctx` may be null (the
    // register half is then `gpr:none`). Returns the length written.
    static size_t Format(char* out, size_t cap, uint32_t target, const EmotionEngineState* ctx);

    // The process-lived record text for the park (a park never returns, so one
    // buffer serves every caller; a second caller overwrites it).
    static const char* FatalNoJump(uint32_t target, const EmotionEngineState* ctx);

    // The R5900 register name for a GPR index (0..31), e.g. 25 -> "t9".
    static const char* GprName(uint32_t index);
};

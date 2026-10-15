#pragma once
// BootEnv: the kernel's boot environment for the main thread -- the argc/argv
// block the PS2 kernel writes for crt0, and the InitMainThread / InitHeap /
// EndOfHeap answers (PS2 kernel: InitMainThread returns the initial $sp,
// InitHeap returns the heap limit, EndOfHeap returns the top of RAM).
//
// This class holds ONLY the mechanism. Every value that is specific to a title
// (argv[0] string, the fixed argv block address crt0 reads, the sbrk-break word
// libc expects the kernel to have initialised, the heap-limit cap OLD applied)
// arrives through BootParams from the title's TitleProfile.
//
// Argv block layout (kernel rule, shared by the crt0 pre-seed and the
// InitMainThread write): base+0x00 argc (u32); base+0x04.. argv pointer array,
// NULL-terminated, 7 slots (6 entries max); base+0x20.. packed NUL-terminated
// strings.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct BootParams {
    std::vector<std::string> argv;  // retail: exactly one entry
    uint32_t argv_block = 0;        // crt0's fixed argc/argv block; 0 = none
    uint32_t sbrk_break_word = 0;   // libc break pointer seeded at InitHeap; 0 = none
    uint32_t heap_limit_cap = 0;    // InitHeap clamp (0 = no cap)
    uint32_t default_heap_base = 0; // InitHeap(-1) before any base is known (kernel default, title-supplied)
};

// Byte-exact write plan for an argv block based at `base`.
struct ArgvLayout {
    uint32_t argc = 0;
    std::vector<std::pair<uint32_t, uint32_t>> word_writes;  // argc, pointers, NULL
    std::vector<std::pair<uint32_t, uint8_t>> byte_writes;   // packed strings + NULs
};

class BootEnv {
public:
    static BootEnv& Instance();

    void Configure(const BootParams& params);
    const BootParams& Params() const { return params_; }

    // Pure: the layout the kernel writes for `argv` at `base`.
    static ArgvLayout BuildLayout(const std::vector<std::string>& argv, uint32_t base);

    // Writes the argv block at `base` through guest memory (bytes, then words).
    void WriteArgv(uint32_t base) const;
    // crt0 pre-seed at the title's fixed block (no-op when argv_block == 0).
    void PreSeed() const;

    // Kernel answers. InitMainThread: sp = (stack == -1 ? top - size : stack + size) & ~0xF.
    static uint32_t MainThreadSp(uint32_t stack, int32_t stack_size);
    static uint32_t MainThreadStackBase(uint32_t stack, int32_t stack_size);
    // InitHeap limit: size == -1 -> caller's $sp (clamped to top) & ~0xF, else base + size;
    // clamped to top, then to the title cap when one is set.
    uint32_t HeapLimit(uint32_t heap_base, int32_t heap_size, uint32_t caller_sp) const;
    static constexpr uint32_t kRamTop = 0x02000000;      // EndOfHeap / GetMemorySize

    static constexpr uint32_t kPtrArrayOffset = 0x04;
    static constexpr uint32_t kStringsOffset = 0x20;
    static constexpr size_t kMaxArgs = 6;

private:
    BootEnv() = default;
    BootParams params_;
};

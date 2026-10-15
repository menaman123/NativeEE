#include "engine/ee/hle/boot_env.h"

#include "engine/ee/recomp_abi/host_app/memory.h"

BootEnv& BootEnv::Instance() {
    static BootEnv inst;
    return inst;
}

void BootEnv::Configure(const BootParams& params) { params_ = params; }

ArgvLayout BootEnv::BuildLayout(const std::vector<std::string>& argv_in, uint32_t base) {
    ArgvLayout lay;
    std::vector<std::string> argv = argv_in;
    if (argv.size() > kMaxArgs) argv.resize(kMaxArgs);  // 7 pointer slots: 6 entries + NULL

    uint32_t soff = base + kStringsOffset;
    std::vector<uint32_t> ptrs;
    ptrs.reserve(argv.size());
    for (const std::string& t : argv) {
        ptrs.push_back(soff);
        for (char c : t) lay.byte_writes.emplace_back(soff++, static_cast<uint8_t>(c));
        lay.byte_writes.emplace_back(soff++, 0);
    }
    lay.argc = static_cast<uint32_t>(argv.size());
    lay.word_writes.emplace_back(base, lay.argc);
    for (size_t i = 0; i < ptrs.size(); ++i)
        lay.word_writes.emplace_back(base + kPtrArrayOffset + static_cast<uint32_t>(4 * i), ptrs[i]);
    lay.word_writes.emplace_back(base + kPtrArrayOffset + static_cast<uint32_t>(4 * ptrs.size()), 0u);
    return lay;
}

void BootEnv::WriteArgv(uint32_t base) const {
    const ArgvLayout lay = BuildLayout(params_.argv, base);
    for (const auto& [addr, b] : lay.byte_writes) memory::write<uint8_t>(addr, b);
    for (const auto& [addr, w] : lay.word_writes) memory::write<uint32_t>(addr, w);
}

void BootEnv::PreSeed() const {
    if (params_.argv_block != 0) WriteArgv(params_.argv_block);
}

uint32_t BootEnv::MainThreadStackBase(uint32_t stack, int32_t stack_size) {
    return (stack == 0xFFFFFFFFu) ? (kRamTop - static_cast<uint32_t>(stack_size)) : stack;
}

uint32_t BootEnv::MainThreadSp(uint32_t stack, int32_t stack_size) {
    const uint32_t sp = (stack == 0xFFFFFFFFu) ? (kRamTop - static_cast<uint32_t>(stack_size))
                                               : (stack + static_cast<uint32_t>(stack_size));
    return sp & ~0xFu;
}

uint32_t BootEnv::HeapLimit(uint32_t heap_base, int32_t heap_size, uint32_t caller_sp) const {
    uint32_t limit;
    if (heap_size == -1) {
        uint32_t sp = caller_sp;
        if (sp > kRamTop) sp = kRamTop;
        limit = sp & ~0xFu;
    } else {
        limit = heap_base + static_cast<uint32_t>(heap_size);
    }
    if (limit > kRamTop) limit = kRamTop;
    if (params_.heap_limit_cap != 0 && limit > params_.heap_limit_cap) limit = params_.heap_limit_cap;
    return limit;
}

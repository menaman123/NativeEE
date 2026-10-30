// FnSwapHotSwap -- the tier-2 hot swap over the generated dispatch (engine/ee/cpu/fn_swap.h), on the
// title's generated code with real guest memory, no boot. The subject is the generated leaf
// FUN_001ae150 (arena_free_bytes in the Sly 2 knowledge base: v0 = [0x2f6388] - [0x2f6384], no call, no
// store). Arms:
//   identity   the generated body itself installed THROUGH the swap (FnSwap::GeneratedBody): the same
//              v0, the same memory hash on every "frame" (= one call), the shadow stack balanced, the
//              DispatchTable proof (CanResumeAt) kept, previous:"generated" then "swapped";
//   one byte   a body that computes the same v0 and writes one byte the generated body never writes
//              (tests/ee/fn_swap_candidate.cpp, dlopened from argv[1]): installed before "frame" 3 of
//              6, the per-frame memory hashes differ FIRST at frame 3 and the difference is that byte;
//   remove     the generated body dispatched again (hash equal to the pre-swap run), the raw-fast-path
//              flag as before;
//   refusals   not an entry, dlopen of a missing library, dlsym of a missing symbol, a function on the
//              calling thread's shadow stack.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/hash/hash.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/fn_swap.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"

namespace {
int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

// The rig (the file's one primary type): bound guest memory, one call of the leaf per "frame", the
// memory hash after it.
class TestFnSwap {
public:
    static constexpr uint32_t kFn = 0x001ae150;
    static constexpr uint32_t kLo = 0x002f6384, kHi = 0x002f6388;
    static constexpr uint32_t kSp = 0x00500000, kRa = 0x001ae3a0;
    static constexpr uint32_t kExtraByte = 0x00300040;   // what the one-byte candidate writes

    void BindMemory() {
        static MainMemory ram;
        static Scratchpad spad;
        static AddressMap map(ram, spad);
        static MmioSink sink;
        static MmioRouter mmio(sink);
        static FastMem fm(ram, map, mmio);
        ram_ = &ram;
        RecompAbi::BindMemory(&fm);
    }
    uint64_t RamHash() const { return ps2r::hash::Fnv1a64(ram_->Data(), ram_->Size(), ps2r::hash::kFnv1a64Offset); }
    // One "frame": the call with lo/hi = (frame+1)*16 / 0x2000; returns v0.
    uint32_t Frame(uint32_t frame) {
        memory::write<uint32_t>(kLo, (frame + 1) * 16);
        memory::write<uint32_t>(kHi, 0x2000);
        CpuContext ctx{};
        std::memset(&ctx, 0, sizeof ctx);
        ctx.cpuRegs.GPR.r[29].UL[0] = kSp;
        ctx.cpuRegs.GPR.r[31].UL[0] = kRa;
        DispatchTable::Invoke(kFn, ctx);
        return ctx.cpuRegs.GPR.r[2].UL[0];
    }
    // frames hashes h[k] after frame k, k = 0..n-1.
    std::vector<uint64_t> Run(uint32_t n, std::vector<uint32_t>* v0s) {
        std::vector<uint64_t> h;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t v0 = Frame(k);
            if (v0s) v0s->push_back(v0);
            h.push_back(RamHash());
        }
        return h;
    }
    static int FirstDiff(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) if (a[i] != b[i]) return static_cast<int>(i);
        return -1;
    }
private:
    MainMemory* ram_ = nullptr;
};
}  // namespace

int main(int argc, char** argv) {
    const std::string candidate = argc > 1 ? argv[1] : "";
    TestFnSwap rig;
    rig.BindMemory();
    DispatchTable::Initialize();
    const uint32_t id = DispatchTable::IdOf(TestFnSwap::kFn);
    CHECK(id != UINT32_MAX, "0x1ae150 is a generated entry");
    CHECK(FnSwap::GeneratedBody(TestFnSwap::kFn) != nullptr, "GeneratedBody(0x1ae150) is the raw generated body");
    CHECK(FnSwap::GeneratedBody(TestFnSwap::kFn + 4) == nullptr, "GeneratedBody off an entry is null");
    const bool hooked_before = DispatchTable::IsHooked(TestFnSwap::kFn);
    const bool resumable_before = DispatchTable::CanResumeAt(TestFnSwap::kFn, 0);
    CHECK(resumable_before, "the generated body is proven resumable at its entry before any swap");

    // Reference run: 6 frames with the generated body.
    std::vector<uint32_t> v0_ref;
    const std::vector<uint64_t> h_ref = rig.Run(6, &v0_ref);
    CHECK(v0_ref[0] == 0x2000 - 16 && v0_ref[5] == 0x2000 - 96, "generated body: v0 = hi - lo");
    CHECK(ShadowCallStack::Depth() == 0, "shadow stack empty after the reference run");

    // Identity: the generated body through the swap.
    std::string prev, err;
    CHECK(FnSwap::InstallBody(TestFnSwap::kFn, FnSwap::GeneratedBody(TestFnSwap::kFn), true, &prev, &err), "identity swap installs");
    CHECK(prev == "generated", "first swap reports previous:generated");
    CHECK(FnSwap::IsSwapped(TestFnSwap::kFn), "IsSwapped after the install");
    CHECK(DispatchTable::IsHooked(TestFnSwap::kFn), "a swapped id is hooked (classic dispatch chain)");
    CHECK(DispatchTable::CanResumeAt(TestFnSwap::kFn, 0) == resumable_before, "the proof is inherited (CanResumeAt unchanged)");
    std::vector<uint32_t> v0_id;
    const std::vector<uint64_t> h_id = rig.Run(6, &v0_id);
    CHECK(v0_id == v0_ref, "identity swap: the same v0 on every frame");
    CHECK(TestFnSwap::FirstDiff(h_ref, h_id) == -1, "identity swap: identical memory hash stream (6/6)");
    CHECK(ShadowCallStack::Depth() == 0, "shadow stack balanced across the swapped calls");
    {
        const std::vector<FnSwap::Info> list = FnSwap::List();
        CHECK(list.size() == 1 && list[0].addr == TestFnSwap::kFn && list[0].calls == 6, "List: one swap, 6 calls counted");
    }
    CHECK(FnSwap::InstallBody(TestFnSwap::kFn, FnSwap::GeneratedBody(TestFnSwap::kFn), true, &prev, &err) && prev == "swapped",
          "a second swap reports previous:swapped");

    // Refusals.
    CHECK(!FnSwap::InstallBody(TestFnSwap::kFn + 4, FnSwap::GeneratedBody(TestFnSwap::kFn), true, &prev, &err) && err.find("entry") != std::string::npos,
          "install off an entry is refused");
    CHECK(!FnSwap::Install(TestFnSwap::kFn, "/nonexistent/candidate.dylib", "recomp_candidate", true, &prev, &err) && err.find("dlopen") == 0,
          "a missing library is refused with the dlopen error");
    CHECK(!FnSwap::Remove(TestFnSwap::kFn + 4, &err) && err == "the function is not swapped", "remove of a non-swapped address is refused");
    CHECK(ShadowCallStack::Push(TestFnSwap::kFn, TestFnSwap::kRa), "push a frame of the function (a call in flight)");
    CHECK(!FnSwap::InstallBody(TestFnSwap::kFn, FnSwap::GeneratedBody(TestFnSwap::kFn), true, &prev, &err) && err.find("in flight") != std::string::npos,
          "a swap of a function on the calling thread's shadow stack is refused");
    CHECK(!FnSwap::Remove(TestFnSwap::kFn, &err) && err.find("in flight") != std::string::npos, "so is its removal");
    ShadowCallStack::Pop();

    // One byte differently, from the dlopened candidate, installed before frame 3 of 6.
    if (candidate.empty()) {
        std::printf("SKIP the dlopen arm (no candidate library on the command line)\n");
    } else {
        CHECK(!FnSwap::Install(TestFnSwap::kFn, candidate, "no_such_symbol", true, &prev, &err) && err.find("dlsym") == 0,
              "a missing symbol is refused with the dlsym error");
        // The format's address export: the module says recomp_candidate_addr = 0x001ae150; another generated
        // entry (memset, 0x2333b4) must refuse it before anything is installed.
        CHECK(!FnSwap::Install(0x002333b4u, candidate, FnSwap::kDefaultSymbol, true, &prev, &err) &&
                  err.find("built for 0x001ae150") != std::string::npos && !FnSwap::IsSwapped(0x002333b4u),
              "a library built for another address is refused by recomp_candidate_addr");
        std::vector<uint64_t> h_wrong;
        std::vector<uint32_t> v0_wrong;
        memory::write<uint8_t>(TestFnSwap::kExtraByte, 0);
        for (uint32_t k = 0; k < 6; ++k) {
            if (k == 3) {
                CHECK(FnSwap::Install(TestFnSwap::kFn, candidate, FnSwap::kDefaultSymbol, true, &prev, &err), "the candidate library swaps in");
                CHECK(prev == "swapped", "replacing the identity swap reports previous:swapped");
            }
            v0_wrong.push_back(rig.Frame(k));
            h_wrong.push_back(rig.RamHash());
        }
        CHECK(v0_wrong == v0_ref, "one-byte candidate: v0 equal on every frame (the result is right)");
        const int first = TestFnSwap::FirstDiff(h_ref, h_wrong);
        std::printf("first differing frame: %d (lane mem)\n", first);
        CHECK(first == 3, "one-byte candidate: the first differing frame is the swap frame (3)");
        CHECK(memory::read<uint8_t>(TestFnSwap::kExtraByte) == 0x5a, "the difference is the one byte the candidate wrote");
        CHECK(ShadowCallStack::Depth() == 0, "shadow stack balanced across the dlopened body");
        const std::vector<FnSwap::Info> list = FnSwap::List();
        CHECK(list.size() == 1 && list[0].library == candidate && list[0].symbol == FnSwap::kDefaultSymbol && list[0].calls == 3,
              "List names the library, the symbol and 3 calls");
    }

    // Remove: the generated body again, the flag as before.
    CHECK(FnSwap::Remove(TestFnSwap::kFn, &err), "remove restores");
    CHECK(!FnSwap::IsSwapped(TestFnSwap::kFn) && FnSwap::List().empty(), "nothing swapped after the remove");
    CHECK(DispatchTable::IsHooked(TestFnSwap::kFn) == hooked_before, "the raw-fast-path flag is restored");
    CHECK(DispatchTable::CanResumeAt(TestFnSwap::kFn, 0) == resumable_before, "the generated proof is back");
    memory::write<uint8_t>(TestFnSwap::kExtraByte, 0);
    std::vector<uint32_t> v0_back;
    const std::vector<uint64_t> h_back = rig.Run(6, &v0_back);
    CHECK(v0_back == v0_ref && TestFnSwap::FirstDiff(h_ref, h_back) == -1, "after the remove: the reference stream again (6/6)");

    if (g_fail) { std::printf("FAIL FnSwapHotSwap: %d checks failed\n", g_fail); return 1; }
    std::printf("PASS FnSwapHotSwap\n");
    return 0;
}

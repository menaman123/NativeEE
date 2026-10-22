// tests/platform/test_host_platform.cpp -- the host shim header's contract, run on every
// arm: the same assertions pass on the POSIX arm (macOS, Linux) and the Windows arm, so a
// green run on the dev host plus a green CI run on Windows is the identity of the two.
#include "engine/platform/host/platform.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                             \
        }                                                                             \
    } while (0)

void BitScans() {
    using namespace ps2r::host;
    CHECK(Ctz64(0) == 64);
    CHECK(Clz64(0) == 64);
    CHECK(Ctz64(1) == 0);
    CHECK(Ctz64(0x8000000000000000ull) == 63);
    CHECK(Clz64(1) == 63);
    CHECK(Clz64(0x8000000000000000ull) == 0);
    CHECK(Ctz64(0x0000000000F00000ull) == 20);
    CHECK(Popcount64(0) == 0);
    CHECK(Popcount64(~0ull) == 64);
    CHECK(Popcount64(0x00000000F0F0F0F0ull) == 16);
}

void WideMultiply() {
    using namespace ps2r::host;
    uint64_t hi = 0, lo = 0;
    Mul64x64(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, &hi, &lo);
    CHECK(hi == 0xFFFFFFFFFFFFFFFEull && lo == 1);
    Mul64x64(0x100000000ull, 0x100000000ull, &hi, &lo);
    CHECK(hi == 1 && lo == 0);
    Mul64x64(12345, 6789, &hi, &lo);
    CHECK(hi == 0 && lo == 12345ull * 6789ull);
    // The EE's 64-bit MULT/DMULT pair: the low half is the plain wrapped product.
    Mul64x64(0xDEADBEEFCAFEBABEull, 0x0123456789ABCDEFull, &hi, &lo);
    CHECK(lo == 0xDEADBEEFCAFEBABEull * 0x0123456789ABCDEFull);
}

void AlignedHeap() {
    using namespace ps2r::host;
    for (size_t align : {size_t{16}, size_t{64}, size_t{4096}}) {
        void* p = AlignedAlloc(align, 1000);
        CHECK(p != nullptr);
        CHECK((reinterpret_cast<uintptr_t>(p) % align) == 0);
        std::memset(p, 0xAB, 1000);
        AlignedFree(p);
    }
    CHECK(ps2r::host::FrameAddress() != nullptr);
}

void FileDescriptors(const std::string& scratch) {
    using namespace ps2r::host;
    const std::string dir = scratch + "/host_platform";
    CHECK(MakeDirectory(dir.c_str()));
    CHECK(MakeDirectory(dir.c_str()));   // idempotent
    CHECK(IsDirectory(dir.c_str()));
    CHECK(!IsDirectory((dir + "/absent").c_str()));

    const std::string path = dir + "/fd.bin";
    int fd = FdOpen(path.c_str(), kOpenWrite | kOpenCreate | kOpenTruncate);
    CHECK(fd >= 0);
    const char payload[] = "0123456789abcdef";
    CHECK(FdWrite(fd, payload, 16) == 16);
    CHECK(FdSync(fd) == 0);
    CHECK(FdClose(fd) == 0);
    CHECK(DirectorySync(dir.c_str()) == 0);

    fd = FdOpen(path.c_str(), kOpenRead);
    CHECK(fd >= 0);
    char buf[8] = {};
    CHECK(FdPread(fd, buf, 4, 10) == 4);
    CHECK(std::memcmp(buf, "abcd", 4) == 0);
    CHECK(FdPread(fd, buf, 4, 0) == 4);   // positioned: independent of the previous read
    CHECK(std::memcmp(buf, "0123", 4) == 0);
    CHECK(FdRead(fd, buf, 4) == 4);       // sequential position is the arm's own business
    CHECK(FdPread(fd, buf, 8, 16) == 0);  // at EOF: 0, not an error
    CHECK(FdClose(fd) == 0);

    fd = FdOpen(path.c_str(), kOpenWrite | kOpenAppend);
    CHECK(fd >= 0);
    CHECK(FdWrite(fd, "XY", 2) == 2);
    CHECK(FdClose(fd) == 0);
    std::FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        CHECK(FdOfStream(f) >= 0);
        CHECK(FileSeek64(f, 0, SEEK_END) == 0);
        CHECK(FileTell64(f) == 18);
        CHECK(FileSeek64(f, 16, SEEK_SET) == 0);
        char tail[2] = {};
        CHECK(std::fread(tail, 1, 2, f) == 2 && tail[0] == 'X' && tail[1] == 'Y');
        std::fclose(f);
    }
    CHECK(FdOpen((dir + "/absent/none").c_str(), kOpenRead) == -1);
}

std::atomic<int> g_thread_depth{0};
int DeepRecursion(int n, volatile char* sink) {
    volatile char pad[64 * 1024];   // 64 KiB per level: 256 levels = 16 MiB, past a default stack
    pad[0] = static_cast<char>(n);
    pad[sizeof pad - 1] = static_cast<char>(n);
    if (sink) *sink = pad[0] + pad[sizeof pad - 1];
    if (n == 0) return 0;
    return DeepRecursion(n - 1, sink) + 1;
}
void BigStackThread(void* arg) {
    volatile char sink = 0;
    *static_cast<int*>(arg) = DeepRecursion(256, &sink);
    ps2r::host::SetCurrentThreadName("ps2r-host-test");
    ps2r::host::PinCurrentThreadInteractive();
    g_thread_depth.store(1);
}

void Threads() {
    using namespace ps2r::host;
    int depth = -1;
    NativeThread t;
    CHECK(ThreadCreate(&t, &BigStackThread, &depth, size_t{64} * 1024 * 1024));
    ThreadJoin(t);
    CHECK(depth == 256);
    CHECK(g_thread_depth.load() == 1);
}

void Clocks() {
    using namespace ps2r::host;
    const uint64_t a = MonotonicNowNs();
    volatile uint64_t spin = 0;
    for (int i = 0; i < 100000; ++i) spin += static_cast<uint64_t>(i);
    const uint64_t b = MonotonicNowNs();
    CHECK(b >= a);
    CHECK(b - a < 10ull * 1000 * 1000 * 1000);   // under ten seconds for a short spin
    // PreciseSleepUntilNs: never early; a passed deadline returns at once; a 2 ms deadline
    // lands within the host's timer slop (the pin is loose -- 3 ms -- so a loaded CI box
    // passes; the precision claim is measured by the pacing judge, not here). Twenty
    // 500 us sleeps in a row keep the deadline chain: >= 10 ms total, no sleep early.
    {
        const uint64_t t0 = MonotonicNowNs();
        PreciseSleepUntilNs(t0 - 1);   // already passed
        CHECK(MonotonicNowNs() - t0 < 1000ull * 1000);
        const uint64_t deadline = MonotonicNowNs() + 2000ull * 1000;
        PreciseSleepUntilNs(deadline);
        const uint64_t woke = MonotonicNowNs();
        CHECK(woke >= deadline);
        CHECK(woke - deadline < 3000ull * 1000);
        uint64_t chain = MonotonicNowNs();
        const uint64_t chain0 = chain;
        for (int i = 0; i < 20; ++i) {
            chain += 500ull * 1000;
            PreciseSleepUntilNs(chain);
            CHECK(MonotonicNowNs() >= chain);
        }
        CHECK(MonotonicNowNs() - chain0 >= 10000ull * 1000);
    }
}

void Process(const std::string& scratch) {
    using namespace ps2r::host;
    CHECK(ProcessId() > 0);
    // Footprint is platform-keyed in magnitude; the contract is only "a running process is
    // charged something" (the peak_mb census row never reads 0 on a live arm).
    CHECK(ProcessFootprintBytes() > 0);
    CHECK(ProcessPeakFootprintBytes() >= ProcessFootprintBytes() / 2);

    void* frames[32];
    const int n = Backtrace(frames, 32);
    CHECK(n > 0);
    const std::string path = scratch + "/host_platform/backtrace.txt";
    const int fd = FdOpen(path.c_str(), kOpenWrite | kOpenCreate | kOpenTruncate);
    CHECK(fd >= 0);
    BacktraceSymbolsFd(frames, n, fd);
    FdClose(fd);
    std::FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        CHECK(FileSeek64(f, 0, SEEK_END) == 0);
        CHECK(FileTell64(f) > 0);   // one line per frame was written
        std::fclose(f);
    }
    // The symboliser may legitimately return "" for a stripped binary; it must not fault.
    (void)SymbolForAddress(frames[0]);
}

// Coroutines: ping-pong between the host thread and one coroutine three times.
ps2r::host::Coroutine g_main_co;
int g_co_hops = 0;
void CoroutineBody(void*) {
    for (;;) {
        ++g_co_hops;
        ps2r::host::CoroutineSwitch(g_main_co);
    }
}
void Coroutines() {
    using namespace ps2r::host;
    g_main_co = CoroutineCurrent();
    CHECK(g_main_co.handle != nullptr);
    Coroutine co;
    CHECK(CoroutineCreate(&co, &CoroutineBody, nullptr, size_t{256} * 1024));
    for (int i = 0; i < 3; ++i) CoroutineSwitch(co);
    CHECK(g_co_hops == 3);
    CoroutineDestroy(co);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scratch = argc > 1 ? argv[1] : ".";
    ps2r::host::MakeDirectory(scratch.c_str());
    BitScans();
    WideMultiply();
    AlignedHeap();
    FileDescriptors(scratch);
    Threads();
    Clocks();
    Process(scratch);
    Coroutines();
    std::printf("host platform arm=%s failures=%d\n", ps2r::host::HostArmName(), g_failures);
    return g_failures == 0 ? 0 : 1;
}

// platform/host/host_windows.cpp -- the Windows arm of engine/platform/host/platform.h
// (owner: Track B2). Compiled on _WIN32 only, with clang-cl or MSVC. Win32/UCRT calls
// behind the POSIX-named API; what the arm cannot provide is spelled out in the header's
// section 3 and degrades knowingly (never a silent zero).
//
// This file is not compiled on the dev host (macOS); the Windows CI job is its compiler
// (.github/workflows/windows.yml). Keep every call here to the documented Win32/UCRT
// surface so the first CI run is a compile check, not a redesign.
#include "engine/platform/host/platform.h"
#include "engine/platform/host/gl_windows.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#include <dbghelp.h>
#include <direct.h>
#include <fcntl.h>
#include <intrin.h>
#include <io.h>
#include <malloc.h>
#include <process.h>
#include <psapi.h>
#include <share.h>
#include <sys/stat.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

namespace ps2r::host {

// ---- section 1: compiler shims (MSVC intrinsics; clang-cl provides the same names) -----
uint32_t Ctz64(uint64_t v) {
    unsigned long idx = 0;
    return _BitScanForward64(&idx, v) ? static_cast<uint32_t>(idx) : 64u;
}
uint32_t Clz64(uint64_t v) {
    unsigned long idx = 0;
    return _BitScanReverse64(&idx, v) ? static_cast<uint32_t>(63u - idx) : 64u;
}
uint32_t Popcount64(uint64_t v) {
    // Portable fold: POPCNT is not in the x86-64 baseline and __popcnt64 faults without it.
    v = v - ((v >> 1) & 0x5555555555555555ull);
    v = (v & 0x3333333333333333ull) + ((v >> 2) & 0x3333333333333333ull);
    v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return static_cast<uint32_t>((v * 0x0101010101010101ull) >> 56);
}

void Mul64x64(uint64_t a, uint64_t b, uint64_t* hi, uint64_t* lo) {
#if defined(_M_X64) || defined(_M_AMD64)
    *lo = _umul128(a, b, hi);
#elif defined(_M_ARM64)
    *lo = a * b;
    *hi = __umulh(a, b);
#else
    const uint64_t a_lo = a & 0xFFFFFFFFull, a_hi = a >> 32, b_lo = b & 0xFFFFFFFFull, b_hi = b >> 32;
    const uint64_t p0 = a_lo * b_lo, p1 = a_lo * b_hi, p2 = a_hi * b_lo, p3 = a_hi * b_hi;
    const uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFull) + (p2 & 0xFFFFFFFFull);
    *lo = (p0 & 0xFFFFFFFFull) | (mid << 32);
    *hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
#endif
}

void* FrameAddress() {
    // The address of the return slot sits just above the saved frame pointer; the walkers
    // only need a pointer into the caller's frame, which this is.
    return _AddressOfReturnAddress();
}

void* AlignedAlloc(size_t alignment, size_t bytes) { return _aligned_malloc(bytes ? bytes : 1, alignment); }
void AlignedFree(void* p) { _aligned_free(p); }

// ---- section 2: host API ----------------------------------------------------------------
int FdOpen(const char* path, uint32_t flags) {
    int o = _O_BINARY;
    const bool rd = flags & kOpenRead, wr = flags & kOpenWrite;
    o |= (rd && wr) ? _O_RDWR : (wr ? _O_WRONLY : _O_RDONLY);
    if (flags & kOpenCreate) o |= _O_CREAT;
    if (flags & kOpenTruncate) o |= _O_TRUNC;
    if (flags & kOpenAppend) o |= _O_APPEND;
    int fd = -1;
    if (_sopen_s(&fd, path, o, _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0) return -1;
    return fd;
}
SSize FdRead(int fd, void* buf, size_t n) { return _read(fd, buf, static_cast<unsigned>(n)); }
SSize FdWrite(int fd, const void* buf, size_t n) { return _write(fd, buf, static_cast<unsigned>(n)); }
SSize FdPread(int fd, void* buf, size_t n, uint64_t offset) {
    // One positioned ReadFile through an OVERLAPPED offset: the read cannot be split by
    // another thread's seek (the pread guarantee). On a synchronous handle this may move
    // the handle's own file pointer; the engine's positioned and sequential readers of one
    // file never share a descriptor across threads.
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE) return -1;
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFull);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD got = 0;
    if (!ReadFile(h, buf, static_cast<DWORD>(n), &got, &ov)) {
        return (GetLastError() == ERROR_HANDLE_EOF) ? 0 : -1;
    }
    return static_cast<SSize>(got);
}
int FdSync(int fd) { return _commit(fd); }
int FdClose(int fd) { return _close(fd); }
int FdOfStream(std::FILE* f) { return _fileno(f); }
int DirectorySync(const char* directory) {
    // NTFS journals directory metadata; there is no directory fsync. FlushFileBuffers on a
    // directory handle needs backup semantics and is still a no-op for the rename case, so
    // the arm reports success after confirming the directory exists.
    return IsDirectory(directory) ? 0 : -1;
}

int StrCaseCmp(const char* a, const char* b) { return _stricmp(a, b); }
int StrNCaseCmp(const char* a, const char* b, size_t n) { return _strnicmp(a, b, n); }

bool MakeDirectory(const char* path) {
    if (_mkdir(path) == 0) return true;
    return errno == EEXIST && IsDirectory(path);
}
bool IsDirectory(const char* path) {
    const DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
int64_t FileSeek64(std::FILE* f, int64_t offset, int whence) { return _fseeki64(f, offset, whence); }
int64_t FileTell64(std::FILE* f) { return _ftelli64(f); }

int ProcessId() { return static_cast<int>(GetCurrentProcessId()); }
int ExecReplaceProcess(const char* image, char* const argv[]) {
    // No execv on Win32: run the new image to completion and exit with its status. The
    // caller's own pid survives only as the parent; the relaunch gate must not pin it.
    const char* exe = (image && *image) ? image : argv[0];
    const intptr_t status = _spawnv(_P_WAIT, exe, const_cast<const char* const*>(argv));
    if (status == -1) return -1;
    std::exit(static_cast<int>(status));
}

int ProcessRun(const char* image, char* const argv[], bool search_path, std::string* output) {
    // The CRT's spawn family: _P_WAIT returns the child's exit status; the PATH search is
    // _spawnvp's. Output capture: the child's stdout/stderr are redirected into a temporary
    // file through the inheritable handles of _dup2 (the CRT has no pipe-to-string spawn).
    const char* exe = (image && *image) ? image : argv[0];
    int saved_out = -1, saved_err = -1, tmp_fd = -1;
    char tmp_path[MAX_PATH] = {};
    if (output != nullptr) {
        char tmp_dir[MAX_PATH] = {};
        if (GetTempPathA(MAX_PATH, tmp_dir) == 0 || GetTempFileNameA(tmp_dir, "ps2", 0, tmp_path) == 0) return -1;
        tmp_fd = _open(tmp_path, _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (tmp_fd < 0) return -1;
        saved_out = _dup(_fileno(stdout));
        saved_err = _dup(_fileno(stderr));
        _dup2(tmp_fd, _fileno(stdout));
        _dup2(tmp_fd, _fileno(stderr));
    }
    const intptr_t status = search_path ? _spawnvp(_P_WAIT, exe, const_cast<const char* const*>(argv))
                                        : _spawnv(_P_WAIT, exe, const_cast<const char* const*>(argv));
    if (output != nullptr) {
        _dup2(saved_out, _fileno(stdout));
        _dup2(saved_err, _fileno(stderr));
        _close(saved_out);
        _close(saved_err);
        _lseek(tmp_fd, 0, SEEK_SET);
        char buf[4096];
        for (;;) {
            const int n = _read(tmp_fd, buf, sizeof buf);
            if (n <= 0) break;
            output->append(buf, static_cast<size_t>(n));
        }
        _close(tmp_fd);
        DeleteFileA(tmp_path);
    }
    if (status == -1) return -1;
    return static_cast<int>(status) & 0xFF;
}

bool DynamicLibraryOpen(DynamicLibrary* out, const char* path, std::string* err) {
    HMODULE h = LoadLibraryA(path);
    if (h == nullptr) {
        if (err) *err = "LoadLibrary failed: " + std::to_string(GetLastError());
        return false;
    }
    out->handle = reinterpret_cast<void*>(h);
    return true;
}
void* DynamicLibrarySymbol(DynamicLibrary lib, const char* name) {
    return lib.handle ? reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib.handle), name)) : nullptr;
}
void DynamicLibraryClose(DynamicLibrary lib) {
    if (lib.handle) FreeLibrary(reinterpret_cast<HMODULE>(lib.handle));
}
const char* DynamicLibraryExtension() { return "dll"; }

uint64_t ProcessFootprintBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) return 0;
    return static_cast<uint64_t>(pmc.PrivateUsage);
}
uint64_t ProcessPeakFootprintBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) return 0;
    return static_cast<uint64_t>(pmc.PeakPagefileUsage);
}
std::string ExecutableImagePath(std::string* err) {
    // GetModuleFileNameW of the main module; grown until it fits (MAX_PATH is not a limit
    // with long paths enabled), converted to UTF-8 for the one std::string path type.
    std::wstring wide(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, wide.data(), static_cast<DWORD>(wide.size()));
        if (n == 0) {
            if (err) *err = "GetModuleFileNameW failed: " + std::to_string(GetLastError());
            return std::string();
        }
        if (n < wide.size()) { wide.resize(n); break; }
        wide.resize(wide.size() * 2);
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        if (err) *err = "WideCharToMultiByte failed on the executable path";
        return std::string();
    }
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

namespace {
using StartFn = void (*)(void*);
struct ThreadStart { StartFn fn; void* arg; };
unsigned __stdcall ThreadTrampoline(void* p) {
    ThreadStart s = *static_cast<ThreadStart*>(p);
    delete static_cast<ThreadStart*>(p);
    s.fn(s.arg);
    return 0;
}
}  // namespace

bool ThreadCreate(NativeThread* out, void (*fn)(void*), void* arg, size_t stack_bytes) {
    auto* s = new ThreadStart{fn, arg};
    // _beginthreadex commits the stack lazily; a 1 GiB request only reserves address space.
    const uintptr_t h = _beginthreadex(nullptr, static_cast<unsigned>(stack_bytes), &ThreadTrampoline, s,
                                       STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (h == 0) { delete s; out->handle = nullptr; return false; }
    out->handle = reinterpret_cast<void*>(h);
    return true;
}
void ThreadJoin(NativeThread t) {
    if (!t.handle) return;
    HANDLE h = reinterpret_cast<HANDLE>(t.handle);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
}
void ThreadDetach(NativeThread t) {
    if (t.handle) CloseHandle(reinterpret_cast<HANDLE>(t.handle));
}
void SetCurrentThreadName(const char* name) {
    if (!name || !*name) return;
    // SetThreadDescription (Windows 10 1607+); resolved at run time so older loaders start.
    using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
    if (!fn) return;
    wchar_t wide[64];
    const int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 63);
    if (n <= 0) return;
    wide[63] = 0;
    fn(GetCurrentThread(), wide);
}
void PinCurrentThreadInteractive() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
}

uint64_t MonotonicNowNs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // Split to avoid overflowing the 64-bit product for long uptimes.
    const uint64_t secs = static_cast<uint64_t>(now.QuadPart) / static_cast<uint64_t>(freq.QuadPart);
    const uint64_t rem = static_cast<uint64_t>(now.QuadPart) % static_cast<uint64_t>(freq.QuadPart);
    return secs * 1000000000ull + (rem * 1000000000ull) / static_cast<uint64_t>(freq.QuadPart);
}

uint64_t FastNowNs() { return MonotonicNowNs(); }   // QueryPerformanceCounter is the cheap clock already

bool GmTimeUtc(int64_t unix_seconds, struct tm* out) {
    const __time64_t t = static_cast<__time64_t>(unix_seconds);
    return _gmtime64_s(out, &t) == 0;
}

namespace {
std::mutex g_gl_load_mutex;
bool g_gl_loaded = false;
void* (*g_gl_resolve)(const char*) = nullptr;

void* GlResolveOne(void* (*resolve)(const char*), HMODULE gl32, const char* name) {
    void* p = resolve ? resolve(name) : nullptr;
    // wglGetProcAddress's documented failure values besides null.
    const intptr_t v = reinterpret_cast<intptr_t>(p);
    if (v == 1 || v == 2 || v == 3 || v == -1) p = nullptr;
    // wglGetProcAddress (what SDL_GL_GetProcAddress wraps) knows only the names beyond GL 1.1;
    // the 1.1 set (glClear, glTexImage2D, glGetError, ...) is opengl32.dll's own export table.
    if (!p && gl32) p = reinterpret_cast<void*>(GetProcAddress(gl32, name));
    return p;
}
}  // namespace

bool GlLoadEntryPoints(void* (*resolve)(const char*), std::string* missing) {
    std::lock_guard<std::mutex> lock(g_gl_load_mutex);
    if (missing) missing->clear();
    if (resolve) g_gl_resolve = resolve;
    if (g_gl_loaded) return true;
    HMODULE gl32 = GetModuleHandleW(L"opengl32.dll");
    if (!gl32) gl32 = LoadLibraryW(L"opengl32.dll");
    bool ok = true;
    for (int i = 0; i < gl_windows::kEntryPointCount; ++i) {
        const gl_windows::EntryPoint& e = gl_windows::kEntryPoints[i];
        void* p = GlResolveOne(resolve, gl32, e.name);
        *e.slot = p;
        if (!p) {
            ok = false;
            if (missing) { if (!missing->empty()) *missing += ' '; *missing += e.name; }
        }
    }
    g_gl_loaded = ok;
    return ok;
}

void* GlGetProcAddress(const char* name) {
    if (!name) return nullptr;
    std::lock_guard<std::mutex> lock(g_gl_load_mutex);
    return GlResolveOne(g_gl_resolve, GetModuleHandleW(L"opengl32.dll"), name);
}

namespace {
std::once_flag g_sym_once;
bool g_sym_ready = false;
void SymInitOnce() {
    std::call_once(g_sym_once, [] {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        g_sym_ready = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    });
}
}  // namespace

int Backtrace(void** frames, int max_frames) {
    if (max_frames <= 0) return 0;
    // Skip this frame itself so frame 0 is the caller, as backtrace() reports it.
    return static_cast<int>(CaptureStackBackTrace(1, static_cast<DWORD>(max_frames), frames, nullptr));
}
void BacktraceSymbolsFd(void* const* frames, int count, int fd) {
    SymInitOnce();
    char line[512];
    alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + 256];
    for (int i = 0; i < count; ++i) {
        const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        int n = 0;
        if (g_sym_ready) {
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;
            DWORD64 disp = 0;
            HMODULE mod = nullptr;
            char mod_name[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(frames[i]), &mod)) {
                GetModuleFileNameA(mod, mod_name, MAX_PATH);
            }
            if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym)) {
                n = std::snprintf(line, sizeof line, "%d  %s  %s + %llu\n", i, mod_name, sym->Name,
                                  static_cast<unsigned long long>(disp));
            } else {
                n = std::snprintf(line, sizeof line, "%d  %s  0x%016llx\n", i, mod_name, static_cast<unsigned long long>(addr));
            }
        } else {
            n = std::snprintf(line, sizeof line, "%d  0x%016llx\n", i, static_cast<unsigned long long>(addr));
        }
        if (n > 0) (void)_write(fd, line, static_cast<unsigned>(n));
    }
}
std::string SymbolForAddress(const void* pc) {
    SymInitOnce();
    if (!g_sym_ready) return std::string();
    alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + 256];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    if (!SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(pc), &disp, sym)) return std::string();
    return std::string(sym->Name);
}

namespace {
struct FiberStart { StartFn fn; void* arg; };
void WINAPI FiberTrampoline(void* p) {
    FiberStart s = *static_cast<FiberStart*>(p);
    delete static_cast<FiberStart*>(p);
    s.fn(s.arg);   // never returns (the caller's trampoline loops); returning would end the thread
}
}  // namespace

bool CoroutineCreate(Coroutine* out, void (*fn)(void*), void* arg, size_t stack_bytes) {
    auto* s = new FiberStart{fn, arg};
    void* f = CreateFiberEx(stack_bytes, stack_bytes, FIBER_FLAG_FLOAT_SWITCH, &FiberTrampoline, s);
    if (!f) { delete s; out->handle = nullptr; return false; }
    out->handle = f;
    return true;
}
Coroutine CoroutineCurrent() {
    // The first call on a host thread converts it into a fiber so a coroutine can switch
    // back to it (the implicit per-thread record the POSIX arm keeps as well).
    void* cur = GetCurrentFiber();
    if (!cur || cur == reinterpret_cast<void*>(0x1E00)) cur = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    return Coroutine{cur};
}
void CoroutineSwitch(Coroutine to) { if (to.handle) SwitchToFiber(to.handle); }
void CoroutineDestroy(Coroutine c) { if (c.handle) DeleteFiber(c.handle); }

const char* HostArmName() { return "windows"; }

}  // namespace ps2r::host

// platform/host/platform.h -- the ONE host-platform / compiler shim header (ARCHITECTURE
// section 16: no #ifdef on the operating system or the compiler exists outside
// engine/platform/). Two owners write disjoint sections of this one file:
//
//   A  TARGET-FOLDER API  (ps2r::platform::host, Track B1)  -- what the application and the
//      tests used to reach through a macOS-specific call; every target folder (macos/, linux/,
//      windows/) implements the same interface through host/platform.cpp:
//
//        macOS call                                  host:: entry                   Linux equivalent
//        PlatformMacos::RegisterSelf()               RegisterSelf()                 PlatformLinux::RegisterSelf()
//        pthread_set_qos_class_self_np(USER_INTER.)  PinCurrentThreadInteractive()  thread name only (no QoS classes;
//                                                                                   CFS has no performance-core pin)
//        task_info(TASK_VM_INFO).phys_footprint      PhysFootprintBytes()           /proc/self/statm resident pages
//        _NSGetExecutablePath                        ExecutablePath()               readlink(/proc/self/exe)
//        std::chrono::steady_clock (both)            NowNs()                        the same
//        execinfo backtrace (both)                   WriteBacktrace()               the same (glibc)
//        <OpenGL/gl3.h>                              #include PS2R_GL_HEADER        <GL/glcorearb.h> + GL_GLEXT_PROTOTYPES
//                                                                                   (Mesa / libglvnd export the core
//                                                                                   entry points; no loader library)
//        #pragma STDC FENV_ACCESS ON (clang)         PS2R_FENV_ACCESS_ON            GCC: optimize("rounding-math")
//
//   B  COMPILER SHIMS + HOST API  (ps2r::host, Track B2)  -- everything the engine and the
//      composition root used to take straight from <unistd.h>/<pthread.h>/<mach/*.h>/
//      <execinfo.h>/<dlfcn.h> or from GNU __builtin_* / __attribute__ spellings has exactly one
//      portable name here, defined by ONE of two arms:
//        section 1  COMPILER SHIMS   (keyed on the compiler, not the OS; shared)
//        section 2  HOST API         (the declarations both arms implement; shared)
//        section 3  WINDOWS ARM      host_windows.cpp  -- owner: Track B2 (Windows)
//        section 4  POSIX ARM        host_posix.cpp    -- owner: Track B1 (Linux + macOS)
//      A Windows-only or POSIX-only detail belongs in the arm's .cpp, never in a caller.
//
// 128-bit: the EE register file is engine/core/types.h's u128/s128 (two 64-bit halves),
// which every compiler builds; the generated code performs no __int128 arithmetic. The
// one place a 128-bit product is wanted (64x64 -> 128) is Mul64x64 below.
//
// The deterministic lanes are platform-neutral by construction: nothing here feeds the
// game's time domain or its arithmetic; the only per-OS behaviour is scheduling (QoS),
// diagnostics (footprint, exe path, backtrace, GL header) and the file/thread/fiber calls.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "engine/platform/platform.h"

// --- the GL header the render backend and the windowed tests include --------
// `#include PS2R_GL_HEADER` is the one spelling; the macro resolves per OS here.
#if defined(__APPLE__)
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION 1
#endif
#define PS2R_GL_HEADER <OpenGL/gl3.h>
#define PS2R_HOST_MACOS 1
#define PS2R_HOST_LINUX 0
#elif defined(__linux__)
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#define PS2R_GL_HEADER <GL/glcorearb.h>
#define PS2R_HOST_MACOS 0
#define PS2R_HOST_LINUX 1
#elif defined(_WIN32)
// The Windows target (engine/platform/windows/): opengl32.dll exports GL 1.1 only, so the GL
// header is the tree's own generated loader header (tools/build/gen_gl_windows.py: the enums
// and entry points the GL arm uses, each name a macro over a function pointer that
// host::GlLoadEntryPoints fills once a context is current -- platform/sdl/gl_context.cpp).
#define PS2R_GL_HEADER <engine/platform/host/gl_windows.h>
#define PS2R_HOST_MACOS 0
#define PS2R_HOST_LINUX 0
#else
#error "engine/platform/host: no host target for this operating system (add engine/platform/<target>/)"
#endif

// --- strict floating-point environment per TU ------------------------------
// The title's rounding-mode sections (fesetround(FE_TOWARDZERO) around the
// PS2's chop arithmetic) need the compiler to treat every FP operation as
// depending on the dynamic rounding mode: no folding under round-to-nearest, no
// hoisting across the fesetround call. Clang: `PS2R_FENV_ACCESS_ON`.
// GCC does not implement that pragma (and warns on it); its per-TU equivalent is
// the -frounding-math optimisation attribute for the functions that follow.
// Spelled once as PS2R_FENV_ACCESS_ON after the includes of each such TU.
#if defined(__clang__)
#define PS2R_FENV_ACCESS_ON _Pragma("STDC FENV_ACCESS ON")
#elif defined(__GNUC__)
#define PS2R_FENV_ACCESS_ON _Pragma("GCC optimize (\"rounding-math\")")
#else
#define PS2R_FENV_ACCESS_ON
#endif

// ---------------------------------------------------------------------------------------
// A  TARGET-FOLDER API  (Track B1) -- host/platform.cpp picks the target folder.
// ---------------------------------------------------------------------------------------
namespace ps2r::platform::host {

// The host target's filled-in Platform instance (static storage) and its
// registration with platform::Register. Idempotent.
const Platform& Instance();
void RegisterSelf();

// The target's name as the Platform caps report it ("macos", "linux").
const char* Name();

// Pin the calling thread to the interactive scheduling class the emulation
// threads share and name it (<= 15 characters survive on both kernels).
// macOS: pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE) -- the
// scheduler keeps the thread off the efficiency cores. Linux: the name only;
// CFS has no QoS class and the default policy already schedules evenly.
// Idempotent, first call wins per thread; a null name keeps the current one.
void PinCurrentThreadInteractive(const char* name);

// The process's physical footprint in bytes as the OS charges it (macOS:
// task_info phys_footprint; Linux: resident set from /proc/self/statm).
// 0 when the kernel refuses the query. Diagnostics only (run.meta footprint=).
uint64_t PhysFootprintBytes();

// The absolute, symlink-resolved path of the running executable image
// (macOS: _NSGetExecutablePath; Linux: /proc/self/exe). Empty string and
// *err set on failure. The F8 live-restore relaunch and the build-id check
// read it.
std::string ExecutablePath(std::string* err);

// Monotonic nanoseconds (std::chrono::steady_clock on both targets): the
// single TimeDomain source behind Platform::now_ns.
uint64_t NowNs();

// Signal-safe: the current native backtrace to `fd`, skipping the innermost
// `skip_frames` (execinfo on both targets).
void WriteBacktrace(int fd, int skip_frames);

}  // namespace ps2r::platform::host

// ---------------------------------------------------------------------------------------
// section 1  COMPILER SHIMS
// ---------------------------------------------------------------------------------------
#if defined(_MSC_VER) && !defined(__clang__)
#define PS2R_COMPILER_MSVC 1
#else
#define PS2R_COMPILER_MSVC 0
#endif

// printf-style format checking: a GNU attribute (clang, gcc, clang-cl); MSVC has only the
// SAL annotations, which need the declaration rewritten, so it gets no check there.
#if PS2R_COMPILER_MSVC
#define PS2R_PRINTF_FORMAT(fmt_index, first_arg_index)
#else
#define PS2R_PRINTF_FORMAT(fmt_index, first_arg_index) __attribute__((format(printf, fmt_index, first_arg_index)))
#endif

#if PS2R_COMPILER_MSVC
#define PS2R_FORCE_INLINE __forceinline
#define PS2R_NOINLINE __declspec(noinline)
#define PS2R_UNREACHABLE() __assume(0)
#define PS2R_LIKELY(x) (x)
#define PS2R_UNLIKELY(x) (x)
#else
#define PS2R_FORCE_INLINE inline __attribute__((always_inline))
#define PS2R_NOINLINE __attribute__((noinline))
#define PS2R_UNREACHABLE() __builtin_unreachable()
#define PS2R_LIKELY(x) __builtin_expect(!!(x), 1)
#define PS2R_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// Alignment is standard C++ (alignas(16) on the register unions, the VU memories and
// QuadWord); the heap side is not: 16-byte aligned blocks come from AlignedAlloc/
// AlignedFree below (posix_memalign vs _aligned_malloc, which must pair with its own free).

namespace ps2r::host {

// Bit scans. The GNU builtins take an unsigned long long and are undefined at 0; the MSVC
// intrinsics return a found flag. Both arms define the 0 case: Ctz64(0) == Clz64(0) == 64.
uint32_t Ctz64(uint64_t v);
uint32_t Clz64(uint64_t v);
uint32_t Popcount64(uint64_t v);

// 64 x 64 -> 128 unsigned product (hi:lo). __int128 where the compiler has it, _umul128
// on MSVC x64, a 32-bit-limb multiply elsewhere.
void Mul64x64(uint64_t a, uint64_t b, uint64_t* hi, uint64_t* lo);

// The caller's frame pointer (what __builtin_frame_address(0) returns); the stack-walk
// diagnostics start from it. Null where the compiler cannot provide it.
void* FrameAddress();

// 16-byte (or larger power-of-two) aligned heap blocks; AlignedFree only.
void* AlignedAlloc(size_t alignment, size_t bytes);
void AlignedFree(void* p);

// ---------------------------------------------------------------------------------------
// section 2  HOST API  (declarations; one arm's .cpp defines each)
// ---------------------------------------------------------------------------------------

// A signed byte count: ssize_t on POSIX, long long on Windows (LLP64 has no ssize_t).
using SSize = long long;

// --- file descriptors (the low-level writers: ring log, bench report, triage bundle,
//     save storage). Flags are the host's own; the names are POSIX's. ---
enum FileOpenFlags : uint32_t {
    kOpenRead = 1u << 0,
    kOpenWrite = 1u << 1,
    kOpenCreate = 1u << 2,
    kOpenTruncate = 1u << 3,
    kOpenAppend = 1u << 4,
};
int FdOpen(const char* path, uint32_t flags);   // -1 on failure (errno set)
SSize FdRead(int fd, void* buf, size_t n);
SSize FdWrite(int fd, const void* buf, size_t n);
// Positioned read: the offset comes from the argument, never from a shared file position,
// so no other thread's seek can land between a seek and this read (the pread guarantee).
SSize FdPread(int fd, void* buf, size_t n, uint64_t offset);
int FdSync(int fd);          // fsync / _commit
int FdClose(int fd);
int FdOfStream(std::FILE* f);   // fileno / _fileno
// Directory sync: POSIX fsyncs the directory fd after a rename; NTFS has no directory
// fsync, so the Windows arm flushes the volume's view of the parent and reports success.
int DirectorySync(const char* directory);

// --- C library spellings that differ (POSIX <strings.h> vs the UCRT's underscore names) ---
int StrCaseCmp(const char* a, const char* b);               // strcasecmp / _stricmp
int StrNCaseCmp(const char* a, const char* b, size_t n);    // strncasecmp / _strnicmp

// --- paths ---
bool MakeDirectory(const char* path);   // true if created or already a directory
bool IsDirectory(const char* path);
// A fresh, uniquely named directory under the host's temp root (mkdtemp / GetTempPathW +
// CreateDirectoryW with a random suffix): "<tmp>/<prefix>XXXXXX". Empty + *err on failure.
// The tests' scratch directories (nothing of the game writes here).
std::string MakeTempDirectory(const char* prefix, std::string* err);
int64_t FileSeek64(std::FILE* f, int64_t offset, int whence);   // fseeko / _fseeki64
int64_t FileTell64(std::FILE* f);                                // ftello / _ftelli64

// --- process ---
int ProcessId();
// Replace the running image (the F8 restore relaunch): execv on POSIX; on Windows the
// child is spawned and the parent exits with its status, which is the nearest the OS has.
// Returns only on failure.
int ExecReplaceProcess(const char* image, char* const argv[]);
// Run a child image to completion and wait for it: `argv` is the child's own vector
// (argv[0] = the image name); `search_path` resolves `image` on the PATH the way the shell
// would (posix_spawnp / the CRT's spawn search) -- the one way the engine reaches a tool it
// does not know the location of without reading the environment itself. The child's stdout
// and stderr are gathered into *output (when non-null). Returns the exit status (0..255),
// -1 when the child could not be started (errno-shaped failure), -2 when it died on a signal.
// Used by the VU1 AOT builder to drive the host compiler; nothing of the game runs through it.
int ProcessRun(const char* image, char* const argv[], bool search_path, std::string* output);
// Resident footprint in bytes as the platform's own memory tool reports it (the peak_mb
// census row): phys_footprint on Darwin, VmRSS on Linux, PagefileUsage+WorkingSet on Win32.
uint64_t ProcessFootprintBytes();
uint64_t ProcessPeakFootprintBytes();
// The absolute path of the running executable image (_NSGetExecutablePath / readlink
// /proc/self/exe / GetModuleFileNameW). Empty + *err on failure. The target folders'
// ExecutablePath() statics forward here; the F8 relaunch and the build-id check read it.
std::string ExecutableImagePath(std::string* err);

// --- threads ---
// A native thread with an explicit stack size (the session runtime's 1 GiB EE stack
// cannot be expressed with std::thread). The thread runs fn(arg); Join waits for it.
struct NativeThread { void* handle = nullptr; };
bool ThreadCreate(NativeThread* out, void (*fn)(void*), void* arg, size_t stack_bytes);
void ThreadJoin(NativeThread t);
// Abandon a thread that will not finish (the bounded shutdown join): its handle is
// released, the thread keeps running until the process exits. Never joined after.
void ThreadDetach(NativeThread t);
// Name the calling thread (<= 15 bytes are kept on Linux/Darwin; Win32 keeps all).
void SetCurrentThreadName(const char* name);
// Pin the calling thread to the OS's interactive class: USER_INTERACTIVE QoS on Darwin,
// SCHED_OTHER nice -5 where permitted on Linux (best effort), ABOVE_NORMAL on Win32.
void PinCurrentThreadInteractive();

// --- dynamic libraries (dlopen / LoadLibrary): the VU1 AOT builder's generated image
//     libraries. A handle stays open until Close; symbols are the library's C names. ---
struct DynamicLibrary { void* handle = nullptr; };
bool DynamicLibraryOpen(DynamicLibrary* out, const char* path, std::string* err);
void* DynamicLibrarySymbol(DynamicLibrary lib, const char* name);
void DynamicLibraryClose(DynamicLibrary lib);
// The platform's shared-library file extension ("dylib" | "so" | "dll").
const char* DynamicLibraryExtension();

// --- clocks ---
uint64_t MonotonicNowNs();   // the steady clock the TimeDomain samples
// The cheapest monotonic nanosecond clock the host has, for per-event ledgers
// on hot paths (the VU1 tier's per-kick run_ns: thousands of pairs a frame).
// macOS: mach_absolute_time scaled by the timebase (~5 ns a read; steady_clock
// is ~17 ns); elsewhere MonotonicNowNs. Its epoch is its own: never mix its
// values with MonotonicNowNs / NowNs, difference it against itself only.
uint64_t FastNowNs();
// CPU time the CALLING thread has consumed, in ns (user + kernel): clock_gettime
// CLOCK_THREAD_CPUTIME_ID on POSIX, GetThreadTimes on Win32 (100 ns units; the kernel's
// quantum-sampled accounting, so individual reads are coarse but sums over a second are
// right). 0 when the host cannot say. The app's clock census books per-thread CPU with it.
uint64_t ThreadCpuTimeNs();
// Ask the OS for the finest scheduler / timer granularity the process may have, once, at
// boot (the target folder's RegisterSelf calls it). POSIX: nothing to ask (nanosleep,
// condition-variable deadlines and mach_wait_until are already sub-millisecond). Win32: the
// default timer resolution is 15.625 ms and every Sleep / SleepConditionVariable /
// WaitForSingleObject timeout rounds UP to it -- a 60 Hz tick or a 4 ms pump wait cannot hold
// -- so: timeBeginPeriod(1) (per-process since Windows 10 2004) and the process power-throttling
// flag that keeps the 1 ms when the window is unfocused / minimized (Windows 11 otherwise
// reverts a background process to 15.6 ms). The pacing lever on top of this is the pacing
// track's PreciseSleepUntilNs (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION on Win32).
void RequestFineTimerResolution();
// Thread-safe UTC broken-down time (gmtime_r / gmtime_s); false when the conversion fails.
bool GmTimeUtc(int64_t unix_seconds, struct tm* out);

// --- OpenGL entry points. Windows' opengl32.dll exports GL 1.1 only; every core 3.3 / 4.1
//     name the backend calls is a per-context pointer from wglGetProcAddress. The Windows arm
//     fills the generated table (gl_windows.h / gl_windows.cpp, tools/build/gen_gl_windows.py)
//     through `resolve` (the SDL window layer passes SDL_GL_GetProcAddress, which falls back
//     to opengl32.dll's exports for the 1.1 names; the arm falls back the same way when
//     `resolve` returns null). Idempotent: the first successful load wins. On POSIX the names
//     are linked directly and this returns true without touching anything. `missing` (when
//     non-null) receives the space-joined names that resolved to null; false iff any did. ---
bool GlLoadEntryPoints(void* (*resolve)(const char* name), std::string* missing);
// One GL entry point by name, for the OPTIONAL extensions the backend probes after
// HasExtension (glTextureBarrierNV): the process's own GL symbols on POSIX (dlsym
// RTLD_DEFAULT -- the framework / libGL is linked in), the resolver GlLoadEntryPoints was
// given (then opengl32.dll's exports) on Windows. Null when absent or before a context exists.
void* GlGetProcAddress(const char* name);

// --- diagnostics: stack walking and symbolisation (crash handler, triage bundle,
//     word watch). Frames are return addresses, innermost first. ---
int Backtrace(void** frames, int max_frames);
// Append one "frame  module  symbol+offset" line per frame to fd (the crash path has no
// heap to spare, so this writes directly like backtrace_symbols_fd).
void BacktraceSymbolsFd(void* const* frames, int count, int fd);
// The symbol containing pc, or "" (dladdr / SymFromAddr).
std::string SymbolForAddress(const void* pc);

// (No environment accessors: the Config loader is the ONE reader of the environment --
//  CoreSingleGetenvSite -- and nothing in the engine writes it.)

// --- fibers: the EE thread model's coroutines. Win32 has native fibers (CreateFiber /
//     SwitchToFiber); the POSIX arm is the assembly switcher engine/platform/posix/
//     fiber_posix.cpp (FiberPosix) provides. ---
struct Coroutine { void* handle = nullptr; };
bool CoroutineCreate(Coroutine* out, void (*fn)(void*), void* arg, size_t stack_bytes);
Coroutine CoroutineCurrent();
void CoroutineSwitch(Coroutine to);
void CoroutineDestroy(Coroutine c);

// --- the arm's name, for the census and the crash report header ---
const char* HostArmName();   // "windows" | "posix"

}  // namespace ps2r::host

// ---------------------------------------------------------------------------------------
// section 3  WINDOWS ARM  -- declarations only here; bodies in host_windows.cpp (B2).
// ---------------------------------------------------------------------------------------
#if defined(_WIN32)
#define PS2R_HOST_WINDOWS 1
#define PS2R_HOST_POSIX 0
// What the Windows arm does NOT provide (and why), so callers degrade knowingly:
//   - no mach phys_footprint: ProcessFootprintBytes is the private working set
//     (PROCESS_MEMORY_COUNTERS_EX.PrivateUsage) -- the same order of magnitude, not the
//     same number; the peak_mb pins are platform-keyed.
//   - no execv: ExecReplaceProcess spawns the new image and exits this process with the
//     child's status; the parent pid changes (the relaunch gate must not pin the pid).
//   - no backtrace_symbols_fd: BacktraceSymbolsFd writes through DbgHelp when available
//     and raw addresses otherwise; symbols need the .pdb beside the exe.
#endif

// ---------------------------------------------------------------------------------------
// section 4  POSIX ARM  -- declarations only here; bodies in host_posix.cpp (B1).
// ---------------------------------------------------------------------------------------
#if !defined(_WIN32)
#define PS2R_HOST_WINDOWS 0
#define PS2R_HOST_POSIX 1
#endif

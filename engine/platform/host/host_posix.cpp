// platform/host/host_posix.cpp -- the POSIX arm of engine/platform/host/platform.h
// (owner: Track B1, Linux + macOS). Compiled on every non-Windows host. Each body is
// the direct POSIX call the engine used to make inline; the Darwin-only pieces (QoS,
// phys_footprint) are the one place the host keeps an #if on the OS.
#include "engine/platform/host/platform.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/qos.h>
#include <pthread/qos.h>
#endif
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h>
#include <dlfcn.h>
#define PS2R_HOST_HAVE_BACKTRACE 1
#else
#define PS2R_HOST_HAVE_BACKTRACE 0
#endif

#include "engine/platform/posix/fiber_posix.h"

extern char** environ;

namespace ps2r::host {

// ---- section 1: compiler shims (GNU builtins on this arm) ------------------------------
uint32_t Ctz64(uint64_t v) { return v ? static_cast<uint32_t>(__builtin_ctzll(v)) : 64u; }
uint32_t Clz64(uint64_t v) { return v ? static_cast<uint32_t>(__builtin_clzll(v)) : 64u; }
uint32_t Popcount64(uint64_t v) { return static_cast<uint32_t>(__builtin_popcountll(v)); }

void Mul64x64(uint64_t a, uint64_t b, uint64_t* hi, uint64_t* lo) {
    const unsigned __int128 p = static_cast<unsigned __int128>(a) * b;
    *lo = static_cast<uint64_t>(p);
    *hi = static_cast<uint64_t>(p >> 64);
}

void* FrameAddress() { return __builtin_frame_address(0); }

void* AlignedAlloc(size_t alignment, size_t bytes) {
    void* p = nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (posix_memalign(&p, alignment, bytes ? bytes : 1) != 0) return nullptr;
    return p;
}
void AlignedFree(void* p) { std::free(p); }

// ---- section 2: host API ----------------------------------------------------------------
int FdOpen(const char* path, uint32_t flags) {
    int o = 0;
    const bool rd = flags & kOpenRead, wr = flags & kOpenWrite;
    o |= (rd && wr) ? O_RDWR : (wr ? O_WRONLY : O_RDONLY);
    if (flags & kOpenCreate) o |= O_CREAT;
    if (flags & kOpenTruncate) o |= O_TRUNC;
    if (flags & kOpenAppend) o |= O_APPEND;
    return ::open(path, o, 0644);
}
SSize FdRead(int fd, void* buf, size_t n) { return ::read(fd, buf, n); }
SSize FdWrite(int fd, const void* buf, size_t n) { return ::write(fd, buf, n); }
SSize FdPread(int fd, void* buf, size_t n, uint64_t offset) {
    return ::pread(fd, buf, n, static_cast<off_t>(offset));
}
int FdSync(int fd) { return ::fsync(fd); }
int FdClose(int fd) { return ::close(fd); }
int FdOfStream(std::FILE* f) { return ::fileno(f); }
int DirectorySync(const char* directory) {
    const int fd = ::open(directory, O_RDONLY);
    if (fd < 0) return -1;
    const int r = ::fsync(fd);
    ::close(fd);
    return r;
}

int StrCaseCmp(const char* a, const char* b) { return ::strcasecmp(a, b); }
int StrNCaseCmp(const char* a, const char* b, size_t n) { return ::strncasecmp(a, b, n); }

bool MakeDirectory(const char* path) {
    if (::mkdir(path, 0755) == 0) return true;
    return errno == EEXIST && IsDirectory(path);
}
std::string MakeTempDirectory(const char* prefix, std::string* err) {
    std::error_code ec;
    std::filesystem::path root = std::filesystem::temp_directory_path(ec);
    if (ec) root = "/tmp";
    std::string templ = (root / (std::string(prefix ? prefix : "ps2r_") + "XXXXXX")).string();
    if (::mkdtemp(templ.data()) == nullptr) {
        if (err) *err = templ + ": " + std::strerror(errno);
        return std::string();
    }
    return templ;
}
bool IsDirectory(const char* path) {
    struct stat st{};
    return ::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
int64_t FileSeek64(std::FILE* f, int64_t offset, int whence) { return ::fseeko(f, offset, whence); }
int64_t FileTell64(std::FILE* f) { return ::ftello(f); }

int ProcessId() { return static_cast<int>(::getpid()); }
int ExecReplaceProcess(const char* image, char* const argv[]) {
    if (image && *image) ::execv(image, argv);
    return ::execvp(argv[0], argv);
}

int ProcessRun(const char* image, char* const argv[], bool search_path, std::string* output) {
    int pipefd[2] = {-1, -1};
    if (output != nullptr && ::pipe(pipefd) != 0) return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (output != nullptr) {
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipefd[0]);
        posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    }
    pid_t pid = 0;
    const char* exe = (image && *image) ? image : argv[0];
    const int rc = search_path ? ::posix_spawnp(&pid, exe, &actions, nullptr, argv, environ)
                               : ::posix_spawn(&pid, exe, &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (output != nullptr) ::close(pipefd[1]);
    if (rc != 0) {
        if (output != nullptr) ::close(pipefd[0]);
        errno = rc;
        return -1;
    }
    if (output != nullptr) {
        char buf[4096];
        for (;;) {
            const ssize_t n = ::read(pipefd[0], buf, sizeof buf);
            if (n > 0) output->append(buf, static_cast<size_t>(n));
            else if (n == 0 || errno != EINTR) break;
        }
        ::close(pipefd[0]);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -2;
}

bool DynamicLibraryOpen(DynamicLibrary* out, const char* path, std::string* err) {
    void* h = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) {
        if (err) {
            const char* e = ::dlerror();
            *err = e ? e : "dlopen failed";
        }
        return false;
    }
    out->handle = h;
    return true;
}
void* DynamicLibrarySymbol(DynamicLibrary lib, const char* name) { return lib.handle ? ::dlsym(lib.handle, name) : nullptr; }
void DynamicLibraryClose(DynamicLibrary lib) {
    if (lib.handle) ::dlclose(lib.handle);
}
const char* DynamicLibraryExtension() {
#if defined(__APPLE__)
    return "dylib";
#else
    return "so";
#endif
}

uint64_t ProcessFootprintBytes() {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return static_cast<uint64_t>(info.phys_footprint);
#else
    // Linux: VmRSS from /proc/self/statm (resident pages).
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long size = 0, resident = 0;
    const int got = std::fscanf(f, "%lu %lu", &size, &resident);
    std::fclose(f);
    if (got != 2) return 0;
    return static_cast<uint64_t>(resident) * static_cast<uint64_t>(::sysconf(_SC_PAGESIZE));
#endif
}
uint64_t ProcessPeakFootprintBytes() {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return static_cast<uint64_t>(info.ledger_phys_footprint_peak);
#else
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    uint64_t kb = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (std::strncmp(line, "VmHWM:", 6) == 0) { kb = std::strtoull(line + 6, nullptr, 10); break; }
    }
    std::fclose(f);
    return kb * 1024u;
#endif
}
std::string ExecutableImagePath(std::string* err) {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size + 1, '\0');
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0) {
        if (err) *err = "_NSGetExecutablePath failed";
        return std::string();
    }
    buffer.resize(std::strlen(buffer.c_str()));
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::canonical(buffer, ec);
    return ec ? buffer : canonical.string();
#else
    std::error_code ec;
    const std::filesystem::path target = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        if (err) *err = "/proc/self/exe: " + ec.message();
        return std::string();
    }
    return target.string();
#endif
}

namespace {
using StartFn = void (*)(void*);
struct ThreadStart { StartFn fn; void* arg; };
void* ThreadTrampoline(void* p) {
    ThreadStart s = *static_cast<ThreadStart*>(p);
    delete static_cast<ThreadStart*>(p);
    s.fn(s.arg);
    return nullptr;
}
}  // namespace

bool ThreadCreate(NativeThread* out, void (*fn)(void*), void* arg, size_t stack_bytes) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack_bytes) pthread_attr_setstacksize(&attr, stack_bytes);
    pthread_t tid{};
    auto* s = new ThreadStart{fn, arg};
    const int rc = pthread_create(&tid, &attr, &ThreadTrampoline, s);
    pthread_attr_destroy(&attr);
    if (rc != 0) { delete s; out->handle = nullptr; return false; }
    out->handle = reinterpret_cast<void*>(tid);
    return true;
}
void ThreadJoin(NativeThread t) {
    if (t.handle) pthread_join(reinterpret_cast<pthread_t>(t.handle), nullptr);
}
void ThreadDetach(NativeThread t) {
    if (t.handle) pthread_detach(reinterpret_cast<pthread_t>(t.handle));
}
void SetCurrentThreadName(const char* name) {
    if (!name || !*name) return;
#if defined(__APPLE__)
    pthread_setname_np(name);
#else
    char buf[16];
    std::strncpy(buf, name, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    pthread_setname_np(pthread_self(), buf);
#endif
}
void PinCurrentThreadInteractive() {
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#else
    // Best effort: a lower nice value needs CAP_SYS_NICE; failure is not an error
    // (glibc marks nice() warn_unused_result, which a void cast does not satisfy
    // under GCC -Werror, so the result is tested).
    if (::nice(-5) == -1) {
        // No capability: the thread keeps the default priority.
    }
#endif
}

uint64_t MonotonicNowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t FastNowNs() {
#if defined(__APPLE__)
    static const mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t{};
        mach_timebase_info(&t);
        if (t.denom == 0) { t.numer = 1; t.denom = 1; }
        return t;
    }();
    const uint64_t t = mach_absolute_time();
    if (tb.numer == tb.denom) return t;
    // Split to keep the product in 64 bits for long uptimes.
    return (t / tb.denom) * tb.numer + ((t % tb.denom) * tb.numer) / tb.denom;
#else
    return MonotonicNowNs();
#endif
}

uint64_t ThreadCpuTimeNs() {
    timespec ts{};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

bool GmTimeUtc(int64_t unix_seconds, struct tm* out) {
    const time_t t = static_cast<time_t>(unix_seconds);
    return ::gmtime_r(&t, out) != nullptr;
}

bool GlLoadEntryPoints(void* (*)(const char*), std::string* missing) {
    // The GL entry points are linked directly on macOS (OpenGL.framework) and Linux
    // (libOpenGL / libGL export the core set): nothing to load.
    if (missing) missing->clear();
    return true;
}

void* GlGetProcAddress(const char* name) {
    return name ? ::dlsym(RTLD_DEFAULT, name) : nullptr;
}

int Backtrace(void** frames, int max_frames) {
#if PS2R_HOST_HAVE_BACKTRACE
    return ::backtrace(frames, max_frames);
#else
    (void)frames; (void)max_frames;
    return 0;
#endif
}
void BacktraceSymbolsFd(void* const* frames, int count, int fd) {
#if PS2R_HOST_HAVE_BACKTRACE
    ::backtrace_symbols_fd(const_cast<void**>(frames), count, fd);
#else
    char line[32];
    for (int i = 0; i < count; ++i) {
        const int n = std::snprintf(line, sizeof line, "%p\n", frames[i]);
        if (n > 0) (void)!::write(fd, line, static_cast<size_t>(n));
    }
#endif
}
std::string SymbolForAddress(const void* pc) {
#if PS2R_HOST_HAVE_BACKTRACE
    Dl_info info{};
    if (!dladdr(pc, &info) || !info.dli_sname) return std::string();
    return std::string(info.dli_sname);
#else
    (void)pc;
    return std::string();
#endif
}

bool CoroutineCreate(Coroutine* out, void (*fn)(void*), void* arg, size_t stack_bytes) {
    const platform::Fiber f = platform::FiberPosix::Create(fn, arg, stack_bytes);
    out->handle = f.handle;
    return f.handle != nullptr;
}
Coroutine CoroutineCurrent() { return Coroutine{platform::FiberPosix::Current().handle}; }
void CoroutineSwitch(Coroutine to) { platform::FiberPosix::Switch(platform::Fiber{to.handle}); }
void CoroutineDestroy(Coroutine c) { platform::FiberPosix::Destroy(platform::Fiber{c.handle}); }

const char* HostArmName() { return "posix"; }

}  // namespace ps2r::host

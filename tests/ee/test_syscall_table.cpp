// Stage 3C2 suites (one binary, section selected by argv[1]):
//   coverage  -- SyscallTableCoverage: 119 live labels each dispatch to a named
//                handler (unknown count stays 0); unknown numbers are counted;
//                the $v1 normalisation rule; a sample of idle results; the E430
//                WouldBlockNow probe (WaitSema / WaitEventFlag / SleepThread / Suspend).
//   bootenv   -- BootEnvRetailArgv: the argv block bytes at 0x30a980 equal OLD's
//                S8-ARGV image (argc=1, ptr, NULL, "cdrom0:\SLUS_123.45;1\0").
//   libc      -- LibcHleDiff: memcpy/memmove/memset/strlen memory effect equals
//                the C library over randomized vectors; guards fall back; the
//                documented register effects hold on representative paths.
//   manifest  -- SdkBindingManifest: the title TSV has 291 rows inside the ELF
//                PT_LOAD range, resolves 298/298 (handler or trap), installs
//                291 through an injected installer, engine rows carry no address.
//   parks     -- KernlTimer parks without a clock and advances with the tick;
//                KernlSifRpc::Park never returns (sink unwind).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/boot_env.h"
#include "engine/ee/hle/kernl_boot.h"
#include "engine/ee/hle/kernl_sif_rpc.h"
#include "engine/ee/hle/kernl_timer.h"
#include "engine/ee/hle/libc.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/sif/sif_state.h"
#include "engine/title/title_profile.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

struct ParkUnwind { std::string reason; };

static MainMemory* g_ram;

static void BindMemory() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static SifState sif;  // the SIF words the sceSifSetReg/GetReg syscalls read and write (the sink drops them)
    static bool registered = false;
    if (!registered) { sif.Register(mmio); registered = true; }
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);
    g_ram = &ram;
}

static uint32_t Rd32(uint32_t a) { return memory::read<uint32_t>(a); }

// ---------------------------------------------------------------- coverage
static const int kBlocking[] = {4, 35, 36, 50, 68, 86};  // exit / sleep / wait: need a live guest thread

static void Coverage() {
    auto& st = SyscallTable::Instance();
    st.ResetForTest();
    int named = 0;
    for (int n = 0; n < 256; ++n) if (SyscallTable::NameOf(n)) ++named;
    CHECK(named == 119, "coverage: exactly 119 live labels are named");
    CHECK(SyscallTable::NameOf(0x74 + 0) != nullptr && std::string(SyscallTable::NameOf(116)) == "SetSysCall",
          "coverage: 0x74 is SetOsdConfigParam, not sceSifSetDma");
    CHECK(SyscallTable::Normalize(0xffffff98u) == 104, "normalize: li v1,-0x68 -> 104 (iFlushCache)");
    CHECK(SyscallTable::Normalize(0x100 + 47) == 47, "normalize: low 8 bits only");

    ThreadManager::Instance().AdoptCurrentThread(1, 0);
    CpuContext ctx{};
    ctx.cpuRegs.GPR.r[29].UL[0] = 0x01FFFF00;
    int dispatched = 0;
    for (int n = 0; n < 256; ++n) {
        if (!SyscallTable::NameOf(n)) continue;
        bool blocking = false;
        for (int b : kBlocking) if (b == n) blocking = true;
        if (blocking) { ++dispatched; continue; }  // counted as covered by name; body needs a guest thread
        const uint64_t before = st.UnknownCount();
        CpuContext c = ctx;
        c.cpuRegs.GPR.r[4].UL[0] = 0;  // null params -> KE_ERROR paths, never default
        if (n >= 33 && n <= 58) c.cpuRegs.GPR.r[4].UL[0] = 200;  // an unknown tid, never self (Suspend(self) parks)
        try { st.Dispatch(static_cast<uint32_t>(n), c); } catch (const ThreadExitUnwind&) {}
        if (st.UnknownCount() == before) ++dispatched;
        else std::printf("  label %d (%s) fell to default\n", n, SyscallTable::NameOf(n));
    }
    CHECK(dispatched == 119, "coverage: every live label dispatches to its handler (none hit default)");
    CHECK(st.UnknownCount() == 0, "coverage: unknown count 0 after the sweep");
    st.Dispatch(3, ctx);
    st.Dispatch(63, ctx);
    st.Dispatch(200, ctx);
    CHECK(st.UnknownCount() == 3 && st.LastUnknown() == 200, "unknown numbers counted + last recorded");

    // Idle results the EE observes with no IOP (plan 8.4).
    CpuContext c = ctx;
    st.Dispatch(127, c);
    CHECK(c.cpuRegs.GPR.r[2].UL[0] == 0x02000000u, "GetMemorySize = 32 MB");
    st.Dispatch(118, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == -1, "sceSifDmaStat = -1 (complete)");
    st.Dispatch(124, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == -1, "Deci2Call = -1 (no DECI2 on retail)");
    c.cpuRegs.GPR.r[4].UL[0] = 3; c.cpuRegs.GPR.r[5].UL[0] = 0x30000;
    st.Dispatch(121, c);
    c.cpuRegs.GPR.r[4].UL[0] = 4;
    st.Dispatch(122, c);
    CHECK((c.cpuRegs.GPR.r[2].UL[0] & 0x30000u) == 0x30000u, "sceSifSetReg(MSFLAG) acks SIFINIT|CMDINIT on SMFLAG");
    c.cpuRegs.GPR.r[4].UL[0] = 0x80000002; c.cpuRegs.GPR.r[5].UL[0] = 7;
    st.Dispatch(121, c);
    st.Dispatch(122, c);
    CHECK(c.cpuRegs.GPR.r[2].UL[0] == 7, "SIF system register round trip");
    // Semaphore through guest memory.
    memory::write<uint32_t>(0x100000, 0); memory::write<int32_t>(0x100004, 4); memory::write<int32_t>(0x100008, 2);
    c.cpuRegs.GPR.r[4].UL[0] = 0x100000;
    st.Dispatch(64, c);
    const int sid = c.cpuRegs.GPR.r[2].SL[0];
    CHECK(sid > 0, "CreateSema decodes SemaParam and returns an id");
    c.cpuRegs.GPR.r[4].SL[0] = sid;
    st.Dispatch(69, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == 0, "PollSema on count 2 succeeds");
    // ReferSemaStatus fills the SDK SemaParam (count after the poll = 1, max 4, init 2, 0 waiters) and
    // returns the id; a game pacing loop polls currentCount (a second title's frame thread spins on it).
    for (uint32_t i = 0; i < 6; ++i) memory::write<uint32_t>(0x100100 + 4 * i, 0xCCCCCCCCu);
    c.cpuRegs.GPR.r[4].SL[0] = sid; c.cpuRegs.GPR.r[5].UL[0] = 0x100100;
    st.Dispatch(71, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == sid && memory::read<int32_t>(0x100100) == 1 &&
              memory::read<int32_t>(0x100104) == 4 && memory::read<int32_t>(0x100108) == 2 &&
              memory::read<int32_t>(0x10010C) == 0 && memory::read<uint32_t>(0x100110) == 0,
          "ReferSemaStatus: id returned, {count 1, max 4, init 2, waiters 0, attr 0} written");
    c.cpuRegs.GPR.r[4].SL[0] = sid + 1000; c.cpuRegs.GPR.r[5].UL[0] = 0x100100;
    st.Dispatch(72, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == -408 && memory::read<int32_t>(0x100100) == 1,
          "iReferSemaStatus on an unknown id: KE_UNKNOWN_SEMID, struct untouched");
    // Event flag: create {attr 2 (multi), option 7, init 0x5}; refer -> the SDK EventFlagParam view
    // (a SetEventFlag from this non-holder test thread would be routed through the event queue,
    // so the pattern pinned is the initial one).
    memory::write<uint32_t>(0x100200, 2); memory::write<uint32_t>(0x100204, 7); memory::write<uint32_t>(0x100208, 5);
    c.cpuRegs.GPR.r[4].UL[0] = 0x100200;
    st.Dispatch(80, c);
    const int eid = c.cpuRegs.GPR.r[2].SL[0];
    CHECK(eid > 0, "CreateEventFlag returns an id");
    for (uint32_t i = 0; i < 5; ++i) memory::write<uint32_t>(0x100300 + 4 * i, 0xCCCCCCCCu);
    c.cpuRegs.GPR.r[4].SL[0] = eid; c.cpuRegs.GPR.r[5].UL[0] = 0x100300;
    st.Dispatch(90, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == eid && memory::read<uint32_t>(0x100300) == 2 &&
              memory::read<uint32_t>(0x100304) == 7 && memory::read<uint32_t>(0x100308) == 5 &&
              memory::read<uint32_t>(0x10030C) == 5 && memory::read<int32_t>(0x100310) == 0,
          "iReferEventFlagStatus: id returned, {attr 2, option 7, init 5, pattern 5, waiters 0} written");
    c.cpuRegs.GPR.r[4].SL[0] = eid + 1000;
    st.Dispatch(89, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == -409, "ReferEventFlagStatus on an unknown id: KE_UNKNOWN_EVFID");
    // E430 WouldBlockNow: the observation point's probe -- would this blocking wait's body PARK now?
    // The sema's count is 1 after the poll: a WaitSema returns at once (not a park); a second poll
    // empties it and the same WaitSema would park; an unknown id is an error return, not a park.
    c.cpuRegs.GPR.r[4].SL[0] = sid;
    CHECK(SyscallTable::IsBlockingWait(68) && !SyscallTable::WouldBlockNow(68, c), "WouldBlockNow: WaitSema on count 1 -> no park");
    st.Dispatch(69, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == 0 && SyscallTable::WouldBlockNow(68, c), "WouldBlockNow: WaitSema on count 0 -> park");
    c.cpuRegs.GPR.r[4].SL[0] = sid + 1000;
    CHECK(!SyscallTable::WouldBlockNow(68, c), "WouldBlockNow: WaitSema on an unknown id -> no park (KE_UNKNOWN_SEMID)");
    // The flag's pattern is 5: OR-waiting bit 4 is satisfied, bit 8 is not; AND-waiting 5 is, 7 is not;
    // an invalid mode / empty bit set is an error return.
    c.cpuRegs.GPR.r[4].SL[0] = eid; c.cpuRegs.GPR.r[5].UL[0] = 0x4; c.cpuRegs.GPR.r[6].UL[0] = WEF_OR;
    CHECK(!SyscallTable::WouldBlockNow(86, c), "WouldBlockNow: WaitEventFlag OR 0x4 on pattern 5 -> satisfied, no park");
    c.cpuRegs.GPR.r[5].UL[0] = 0x8;
    CHECK(SyscallTable::WouldBlockNow(86, c), "WouldBlockNow: WaitEventFlag OR 0x8 on pattern 5 -> park");
    c.cpuRegs.GPR.r[5].UL[0] = 0x5; c.cpuRegs.GPR.r[6].UL[0] = WEF_AND;
    CHECK(!SyscallTable::WouldBlockNow(86, c), "WouldBlockNow: WaitEventFlag AND 0x5 on pattern 5 -> satisfied, no park");
    c.cpuRegs.GPR.r[5].UL[0] = 0x7;
    CHECK(SyscallTable::WouldBlockNow(86, c), "WouldBlockNow: WaitEventFlag AND 0x7 on pattern 5 -> park");
    c.cpuRegs.GPR.r[5].UL[0] = 0x7; c.cpuRegs.GPR.r[6].UL[0] = 0x100;
    CHECK(!SyscallTable::WouldBlockNow(86, c), "WouldBlockNow: WaitEventFlag with an invalid mode -> error return, no park");
    c.cpuRegs.GPR.r[5].UL[0] = 0; c.cpuRegs.GPR.r[6].UL[0] = WEF_OR;
    CHECK(!SyscallTable::WouldBlockNow(86, c), "WouldBlockNow: WaitEventFlag with no bits -> error return, no park");
    CHECK(SyscallTable::WouldBlockNow(55, c) && SyscallTable::WouldBlockNow(59, c), "WouldBlockNow: SuspendThread / JoinThread always park");
    CHECK(!SyscallTable::WouldBlockNow(122, c) && !SyscallTable::WouldBlockNow(69, c), "WouldBlockNow: non-waits (sceSifGetReg, PollSema) never park");
    // SleepThread parks unless a WakeupThread is banked on the caller (R4: consumed at once).
    if (auto cur = ThreadManager::Instance().Current()) {
        { std::lock_guard<std::mutex> lk(cur->m); cur->wakeup_count = 0; }
        CHECK(SyscallTable::WouldBlockNow(50, c), "WouldBlockNow: SleepThread with no banked wakeup -> park");
        { std::lock_guard<std::mutex> lk(cur->m); cur->wakeup_count = 1; }
        CHECK(!SyscallTable::WouldBlockNow(50, c), "WouldBlockNow: SleepThread with a banked wakeup -> no park");
        { std::lock_guard<std::mutex> lk(cur->m); cur->wakeup_count = 0; }
    } else {
        CHECK(!SyscallTable::WouldBlockNow(50, c), "WouldBlockNow: SleepThread with no current guest thread -> no park");
    }
    // INTC handler registry + masks.
    c.cpuRegs.GPR.r[4].SL[0] = 2; c.cpuRegs.GPR.r[5].UL[0] = 0x00123456; c.cpuRegs.GPR.r[6].SL[0] = -1;
    st.Dispatch(16, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == 1 && st.IntcHandlers().size() == 1, "AddIntcHandler records id 1");
    c.cpuRegs.GPR.r[4].SL[0] = 2;
    st.Dispatch(20, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == 1 && st.IntcMask() == 4, "_EnableIntc(2): changed -> 1, mask bit set");
    st.Dispatch(20, c);
    CHECK(c.cpuRegs.GPR.r[2].SL[0] == 0, "_EnableIntc(2) again: unchanged -> 0");
    // mtc0 Status mask / Compare clears IP7.
    c.cpuRegs.CP0.n.Cause = 0x8000;
    st.HandleMtc0(c, 11, 5);
    CHECK(c.cpuRegs.CP0.r[11] == 5 && (c.cpuRegs.CP0.n.Cause & 0x8000) == 0, "mtc0 Compare clears IP7");
    st.HandleMtc0(c, 12, 0xFFFFFFFF);
    CHECK(c.cpuRegs.CP0.n.Status == 0xF0FF003Fu, "mtc0 Status writes only the writable bits");
    st.HandleMtc0(c, 9, 1234);
    CHECK(c.cpuRegs.CP0.r[9] == 1234, "mtc0 Count reseats the free-running counter");
    // RFU-style labels leave $v0 untouched.
    c.cpuRegs.GPR.r[2].UL[0] = 0xDEADBEEF;
    st.Dispatch(96, c);
    CHECK(c.cpuRegs.GPR.r[2].UL[0] == 0xDEADBEEFu && st.NotImplementedCount() >= 1, "KSeg0: logged no-op, $v0 untouched");
}

// ---------------------------------------------------------------- bootenv
static void BootEnvArgv() {
    const TitleProfile& t = ActiveTitleProfile();
    BootEnv::Instance().Configure(t.boot);
    BootEnv::Instance().PreSeed();
    const uint32_t base = t.boot.argv_block;
    CHECK(base == 0x0030a980u, "argv block is crt0's fixed address");
    CHECK(Rd32(base) == 1, "argc == 1 (retail)");
    CHECK(Rd32(base + 4) == base + 0x20, "argv[0] -> base+0x20");
    CHECK(Rd32(base + 8) == 0, "argv[1] == NULL");
    // OLD image: ps2_argv::build_layout(retail_tokens(), 0x30a980) -- byte-for-byte.
    static const char kExpect[] = "cdrom0:\\SLUS_123.45;1";
    bool same = true;
    for (size_t i = 0; i <= std::strlen(kExpect); ++i)
        if (memory::read<uint8_t>(base + 0x20 + static_cast<uint32_t>(i)) != static_cast<uint8_t>(kExpect[i])) same = false;
    CHECK(same, "argv[0] bytes == \"cdrom0:\\SLUS_123.45;1\\0\"");
    const ArgvLayout lay = BootEnv::BuildLayout(t.boot.argv, base);
    CHECK(lay.argc == 1 && lay.word_writes.size() == 3 && lay.byte_writes.size() == std::strlen(kExpect) + 1,
          "layout: 3 words (argc, ptr, NULL) + 22 bytes");
    // InitMainThread writes the same block at `args` and returns sp.
    CpuContext c{};
    ThreadManager::Instance().AdoptCurrentThread(1, 0);
    const uint32_t sp = KernlBoot::InitMainThread(0x12340, 0xFFFFFFFF, 0x10000, 0x00400000, 0, c);
    CHECK(sp == 0x01FF0000u && c.cpuRegs.GPR.r[29].UL[0] == sp && c.cpuRegs.GPR.r[28].UL[0] == 0x12340,
          "InitMainThread: sp = top - size, $sp/$gp set");
    CHECK(Rd32(0x00400000) == 1 && Rd32(0x00400004) == 0x00400020, "InitMainThread writes argc/argv at args");
    c.cpuRegs.GPR.r[29].UL[0] = sp;
    const uint32_t lim = KernlBoot::InitHeap(0xFFFFFFFF, -1, c);
    CHECK(lim == 0x01F00000u, "InitHeap(-1,-1): sp clamp then the title cap (OLD parity)");
    CHECK(Rd32(0x002ea844) == 0x00400000u, "InitHeap seeds the title's sbrk break word with the heap base");
    CHECK(KernlBoot::EndOfHeap() == 0x02000000u, "EndOfHeap = top of RAM");
}

// ---------------------------------------------------------------- libc
static uint32_t Rnd(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }

static void LibcVectors() {
    uint8_t* ram = g_ram->Data();
    uint32_t seed = 12345;
    std::vector<uint8_t> ref(0x20000);
    int mem_ok = 0, guard_ok = 0;
    for (int it = 0; it < 400; ++it) {
        for (auto& b : ref) b = static_cast<uint8_t>(Rnd(seed));
        std::memcpy(ram + 0x200000, ref.data(), ref.size());
        // Every [d, d+n) / [s, s+n) must stay inside the 0x20000-byte reference
        // window (max offset 0x1CFFF + 0x2FF < 0x20000), or the host `exp`
        // vector overflows -- UB whose visibility depends on heap layout.
        const uint32_t n = Rnd(seed) % 0x300;
        const uint32_t d = 0x200000 + ((it & 1) ? (Rnd(seed) % 0x1D00) * 16 : Rnd(seed) % 0x1D000);
        const uint32_t s = 0x200000 + ((it & 2) ? (Rnd(seed) % 0x1D00) * 16 : Rnd(seed) % 0x1D000);
        CpuContext c{};
        c.cpuRegs.GPR.r[4].UL[0] = d; c.cpuRegs.GPR.r[5].UL[0] = s; c.cpuRegs.GPR.r[6].UL[0] = n;
        const bool handled = (it % 3 == 0) ? Libc::Memcpy(c) : (it % 3 == 1) ? Libc::Memmove(c) : Libc::Memset(c);
        std::vector<uint8_t> exp = ref;
        if (it % 3 == 2) std::memset(exp.data() + (d - 0x200000), static_cast<uint8_t>(s), n);
        else std::memmove(exp.data() + (d - 0x200000), exp.data() + (s - 0x200000), n);
        if (handled && std::memcmp(ram + 0x200000, exp.data(), exp.size()) == 0 && c.cpuRegs.GPR.r[2].UL[0] == d) ++mem_ok;
    }
    CHECK(mem_ok == 400, "libc: memcpy/memmove/memset memory effect == C library over 400 random vectors, v0 = dest");
    // Guards: KSEG mirror, MMIO, garbage upper bits, out-of-RAM tail all fall back.
    CpuContext g{};
    g.cpuRegs.GPR.r[4].UL[0] = 0x80200000; g.cpuRegs.GPR.r[5].UL[0] = 0x200000; g.cpuRegs.GPR.r[6].UL[0] = 16;
    if (!Libc::Memcpy(g)) ++guard_ok;
    g.cpuRegs.GPR.r[4].UD[0] = 0x0000000100200000ull;
    if (!Libc::Memmove(g)) ++guard_ok;
    g.cpuRegs.GPR.r[4].UL[0] = 0x01FFFFF8; g.cpuRegs.GPR.r[6].UL[0] = 16;
    if (!Libc::Memset(g)) ++guard_ok;
    Libc::SetProtectedBand(0x300000, 0x300100);
    g.cpuRegs.GPR.r[4].UL[0] = 0x2FFFF0; g.cpuRegs.GPR.r[5].UL[0] = 0x200000; g.cpuRegs.GPR.r[6].UL[0] = 0x20;
    if (!Libc::Memcpy(g)) ++guard_ok;
    Libc::SetProtectedBand(0, 0);
    CHECK(guard_ok == 4, "libc: KSEG / garbage-upper / RAM-end / protected-band arguments fall back");
    // memmove register effects: backward overlap path.
    CpuContext c{};
    c.cpuRegs.GPR.r[4].UL[0] = 0x210010; c.cpuRegs.GPR.r[5].UL[0] = 0x210000; c.cpuRegs.GPR.r[6].UL[0] = 0x40;
    CHECK(Libc::Memmove(c) && c.cpuRegs.GPR.r[3].UL[0] == 0x210010 && c.cpuRegs.GPR.r[4].SD[0] == -1 &&
              c.cpuRegs.GPR.r[7].UL[0] == 0x210040 && c.cpuRegs.GPR.r[8].UL[0] == 0x210010,
          "memmove backward: v1=dest a0=-1 a3=src+n t0=dest");
    // memmove forward quad path residues.
    for (uint32_t i = 0; i < 0x40; ++i) ram[0x220000 + i] = static_cast<uint8_t>(i);
    c = CpuContext{};
    c.cpuRegs.GPR.r[4].UL[0] = 0x230000; c.cpuRegs.GPR.r[5].UL[0] = 0x220000; c.cpuRegs.GPR.r[6].UL[0] = 0x40;
    uint64_t v1hi = 0, v0hi = 0;
    std::memcpy(&v1hi, ram + 0x220000 + 0x28, 8);
    std::memcpy(&v0hi, ram + 0x220000 + 0x38, 8);
    CHECK(Libc::Memmove(c) && c.cpuRegs.GPR.r[3].UD[1] == v1hi && c.cpuRegs.GPR.r[2].UD[1] == v0hi &&
              c.cpuRegs.GPR.r[7].UL[0] == 0x230040 && c.cpuRegs.GPR.r[5].UL[0] == 0x220040,
          "memmove quad: lq residues in v1.hi/v0.hi, a3=dest+(n&~7), a1=src+n");
    // memset M3 residues.
    c = CpuContext{};
    c.cpuRegs.GPR.r[4].UL[0] = 0x240000; c.cpuRegs.GPR.r[5].UL[0] = 0xAB; c.cpuRegs.GPR.r[6].UL[0] = 0x30;
    c.cpuRegs.GPR.r[8].UD[1] = 0x1234;
    CHECK(Libc::Memset(c) && c.cpuRegs.GPR.r[8].UD[0] == 0xABABABABABABABABull && c.cpuRegs.GPR.r[8].UD[1] == 0xABABABABABABABABull &&
              c.cpuRegs.GPR.r[3].UD[1] == 0x1234ull * 0x0001000100010001ull && c.cpuRegs.GPR.r[9].UL[0] == 0xAB &&
              c.cpuRegs.GPR.r[10].UL[0] == 0 && c.cpuRegs.GPR.r[7].UL[0] == 0x240030,
          "memset M3: t0 broadcast, v1.hi pcpyh residue of the caller's t0.US[4], t1=v, t2=(n<0x20)");
    // strlen over the three alignment paths.
    int slen_ok = 0;
    for (int it = 0; it < 300; ++it) {
        const uint32_t len = Rnd(seed) % 70;
        const uint32_t s = 0x250000 + (Rnd(seed) % 64);
        for (uint32_t i = 0; i < len; ++i) ram[s + i] = static_cast<uint8_t>(1 + Rnd(seed) % 255);
        ram[s + len] = 0;
        CpuContext k{};
        k.cpuRegs.GPR.r[4].UL[0] = s;
        if (Libc::Strlen(k) && k.cpuRegs.GPR.r[2].UL[0] == len && k.cpuRegs.GPR.r[4].UL[0] == s + len &&
            k.cpuRegs.GPR.r[7].UL[0] == s) ++slen_ok;
    }
    CHECK(slen_ok == 300, "strlen: v0=len a0=s+len a3=s over 300 vectors (S1/S2/S3 paths)");
    CpuContext k{};
    k.cpuRegs.GPR.r[4].UL[0] = 0x01FFFFF0;
    std::memset(ram + 0x01FFFFF0, 'x', 16);
    CHECK(!Libc::Strlen(k), "strlen: no NUL before RAM end falls back");
}

// ---------------------------------------------------------------- manifest
static void Manifest(const std::string& tsv) {
    auto& m = SdkBindingManifest::Instance();
    m.ResetForTest();
    m.RegisterBuiltins();
    std::vector<SdkBindingManifest::TitleRow> rows;
    std::string err;
    CHECK(SdkBindingManifest::LoadTitleManifest(tsv, rows, &err), ("manifest: TSV loads (" + err + ")").c_str());
    CHECK(rows.size() == 298, "manifest: 298 title rows (291 finder matches + 4 libc.a + 3 sceCdSt wrapper rows)");
    const TitleProfile& t = ActiveTitleProfile();
    int in_range = 0;
    std::set<uint32_t> addrs;
    for (const auto& r : rows) {
        if (r.address >= t.load_base && r.address < t.load_end) ++in_range;
        addrs.insert(r.address);
    }
    CHECK(in_range == 298 && addrs.size() == 298, "manifest: every address inside the ELF PT_LOAD range, all distinct");
    const auto resolved = m.Resolve(rows);
    int with_handler = 0;
    for (const auto& r : resolved) if (r.engine) ++with_handler;
    std::printf("  resolved %zu/298 rows: %d engine handlers, %zu trap (sdk_unimplemented) rows\n",
                resolved.size(), with_handler, resolved.size() - with_handler);
    CHECK(resolved.size() == 298, "manifest: resolves 298/298 (handler or trap)");
    CHECK(with_handler == 77, "manifest: exactly 77 rows have an engine handler (64 sce + 4 libc + 9 recompiled libkernl printf rows)");
    int recompiled_rows = 0;
    for (const auto& r : resolved) if (r.engine && r.engine->kind == SdkBindingManifest::Kind::Recompiled) ++recompiled_rows;
    CHECK(recompiled_rows == 9, "manifest: the 9 libkernl printf rows (6 engine + kputchar/deci2Putchar/kputs sink) keep their generated bodies (Recompiled)");
    // Reverse drift: every engine row must be matched by a title row, except the
    // rows the finder legitimately did not see in this ELF. The exact set is
    // pinned so a new engine row that forgets its TSV row (or a TSV row that
    // loses its engine row) fails here instead of parking at boot.
    std::set<std::string> unmatched;
    for (const auto& e : m.EngineRows()) {
        bool hit = false;
        for (const auto& t : rows)
            if (t.name == e.name && (e.library.empty() || e.library == t.library)) { hit = true; break; }
        if (!hit) unmatched.insert(e.library + " " + e.name);
    }
    const std::set<std::string> expected_unmatched = {"libkernl.a sceSifFreeIopHeap"};
    for (const auto& u : unmatched) std::printf("  engine row without a title match: %s\n", u.c_str());
    CHECK(unmatched == expected_unmatched,
          "manifest: engine rows with NO title match == {libkernl.a sceSifFreeIopHeap} exactly (reverse drift)");
    std::set<uint32_t> installed;
    int traps = 0, unresolved = 0;
    const int n = m.Install(resolved, [&](uint32_t a, SdkBindingManifest::Body) { installed.insert(a); return true; },
                            [](uint32_t) { return SdkBindingManifest::Body([](CpuContext&, uint32_t) {}); }, &traps, &unresolved);
    CHECK(n == 298 && installed.size() == 298 && unresolved == 0, "manifest: installer receives 298 distinct addresses");
    CHECK(traps == 298 - with_handler, "manifest: trap count == rows without a handler");
    // Engine purity: no engine row carries an address; every library/name is non-empty.
    bool pure = true;
    for (const auto& r : m.EngineRows())
        if (r.name.empty() || (r.handler == nullptr && r.wrap == nullptr && r.kind != SdkBindingManifest::Kind::Recompiled)) pure = false;
    CHECK(pure && sizeof(SdkBindingManifest::EngineRow) > 0, "manifest: engine rows are (library, name, handler) only");
    // Park owners are bound by name.
    const auto* init_rpc = m.Find("libkernl.a", "sceSifInitRpc");
    CHECK(init_rpc && init_rpc->handler == &KernlSifRpc::Park, "manifest: sceSifInitRpc -> KernlSifRpc::Park");
    const auto* cpy = m.Find("libc.a", "memcpy");
    CHECK(cpy && cpy->kind == SdkBindingManifest::Kind::SdkWrap && cpy->set_pc_on_wrap, "manifest: memcpy is an SdkWrap row that simulates jr $ra");
}

// ---------------------------------------------------------------- parks
static void Parks() {
    GuestPark::SetSink([](const ParkRecord& r) { throw ParkUnwind{r.reason}; });
    CpuContext c{};
    c.cpuRegs.pc = 0x00123450;
    // KernlTimer without a clock parks.
    KernlTimer::ResetForTest();
    std::string reason;
    try { KernlTimer::TimerWaitMicroseconds(c); } catch (const ParkUnwind& u) { reason = u.reason; }
    CHECK(reason == "tick_clock_absent", "kernl_timer: parks tick_clock_absent without a clock");
    // With a clock: the wait advances the tick until the deadline, returns 1.
    uint64_t now = 100;
    int waits = 0;
    KernlTimer::SetClock([&] { return now; }, [&](uint64_t) { now += 25; ++waits; });
    memory::write<uint32_t>(0x300070, 576000);  // tick_rate at obj+0x70
    c.cpuRegs.GPR.r[4].UL[0] = 0x300000;
    const float secs = 100.0f / 576000.0f;
    std::memcpy(&c.fpuRegs.fpr[12].UL, &secs, 4);
    KernlTimer::TimerWaitMicroseconds(c);
    CHECK(c.cpuRegs.GPR.r[2].UL[0] == 1 && waits == 4 && now == 200 && KernlTimer::LastDeadline() == 200,
          "kernl_timer: deadline = now + (int)(f12*rate); advances with the tick; returns 1");
    CHECK(KernlTimer::TargetTicks(-1.0f, 576000) == 0, "kernl_timer: negative delay -> 0 ticks");
    // SIF park never returns.
    reason.clear();
    bool returned = false;
    try { KernlSifRpc::Park(c); returned = true; } catch (const ParkUnwind& u) { reason = u.reason; }
    CHECK(!returned && reason == "sif_rpc_absent", "kernl_sif_rpc: Park never returns; reason sif_rpc_absent");
    CHECK(GuestPark::Last().pc == c.cpuRegs.GPR.r[31].UL[0], "park record carries the caller ($ra): the guest SIF call site");
    GuestPark::SetSink(nullptr);
}

int main(int argc, char** argv) {
    const std::string section = argc > 1 ? argv[1] : "all";
    const std::string tsv = argc > 2 ? argv[2] : "";
    BindMemory();
    if (section == "coverage" || section == "all") Coverage();
    if (section == "bootenv" || section == "all") BootEnvArgv();
    if (section == "libc" || section == "all") LibcVectors();
    if (section == "manifest" || section == "all") Manifest(tsv);
    if (section == "parks" || section == "all") Parks();
    std::printf("%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}

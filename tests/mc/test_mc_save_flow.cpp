// LibMcSaveFlow: the libmc rows over McCommandModel + McHostStore against a
// fresh temp card root -- the port of the original tests/test_libmc.cpp save
// sequence (the title's FUN_002a6b68 / FUN_002a4b30 state machines):
//   phase 0  unbound gate (-100) and the idle sceMcSync (-1);
//   phase 1  the GetInfo watchdog: first probe -1 (a formatted card appeared),
//            later probes 0; mode-1 polls held busy for latency_polls, mode-0 not;
//   phase 2  the failing trail on an empty root: sceMcGetDir("/BASLUS-12345/*")
//            is -4 (the parent does not resolve), root listings are 0 entries;
//   phase 3  the new-save continuation: Mkdir 0 -> GetDir 1 (root, exact) ->
//            Chdir -> Open CREAT -> Write 17 -> Close -> the wildcard listing
//            of the save directory carries "." / ".." ahead of the file;
//   phase 4  reopen / read-back: Open RDONLY, Read 17 byte-exact;
//   plus the kick rules: a GetInfo kick never destroys an undelivered save-flow
//   completion; a save-flow kick replaces a pending GetInfo.
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/mc/hle/libmc_bindings.h"
#include "engine/mc/hle/mc_command_model.h"
#include "engine/mc/hle/mc_host_store.h"
#include "engine/mc/hle/mcserv_module.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

static CpuContext g_ctx;
static constexpr uint32_t kSp = 0x00300000, kStr = 0x00310000, kBuf = 0x00320000, kTable = 0x00330000;
static constexpr uint32_t kCmdPtr = 0x00340000, kResPtr = 0x00340004, kTypePtr = 0x00340010, kFreePtr = 0x00340014,
                          kFormatPtr = 0x00340018, kSentinelObj = 0x00340100;

static uint32_t Call(void (*fn)(CpuContext&), uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0,
                     uint32_t t0 = 0, uint32_t t1 = 0) {
    g_ctx.cpuRegs.GPR.r[4].UL[0] = a0; g_ctx.cpuRegs.GPR.r[5].UL[0] = a1;
    g_ctx.cpuRegs.GPR.r[6].UL[0] = a2; g_ctx.cpuRegs.GPR.r[7].UL[0] = a3;
    g_ctx.cpuRegs.GPR.r[8].UL[0] = t0; g_ctx.cpuRegs.GPR.r[9].UL[0] = t1;
    g_ctx.cpuRegs.GPR.r[29].UL[0] = kSp;
    g_ctx.cpuRegs.GPR.r[2].UD[0] = 0xDEADBEEFull;
    fn(g_ctx);
    return g_ctx.cpuRegs.GPR.r[2].UL[0];
}
static int32_t S(uint32_t v) { return static_cast<int32_t>(v); }
static void PutStr(const char* s) {
    uint32_t i = 0;
    for (; s[i]; i++) memory::write<uint8_t>(kStr + i, static_cast<uint8_t>(s[i]));
    memory::write<uint8_t>(kStr + i, 0);
}
static int32_t Rd(uint32_t a) { return memory::read<int32_t>(a); }
// sceMcSync(mode) -> ret; cmd/result through the out pointers.
static int32_t Sync(uint32_t mode, int32_t* cmd, int32_t* res) {
    const int32_t r = S(Call(&LibmcBindings::Sync, mode, kCmdPtr, kResPtr));
    if (cmd) *cmd = Rd(kCmdPtr);
    if (res) *res = Rd(kResPtr);
    return r;
}
static std::string EntryName(uint32_t table, int i) {
    std::string s;
    for (uint32_t k = 0; k < 32; k++) {
        const char c = static_cast<char>(memory::read<uint8_t>(table + i * 64 + 32 + k));
        if (!c) break;
        s.push_back(c);
    }
    return s;
}

int main() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);

    const std::string root = (std::filesystem::temp_directory_path() / ("ps2r_mc_" + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(root);
    McHostStore store(root);
    McCommandModel model(/*latency_polls*/ 2);
    std::vector<std::pair<uint32_t, uint32_t>> reply;
    McservModule mcserv(model, [&](uint32_t a, uint32_t v) { reply.push_back({a, v}); memory::write<uint32_t>(a, v); });

    SdkBindingManifest& manifest = SdkBindingManifest::Instance();
    const int rows = LibmcBindings::Bind(manifest, &model, &store);
    size_t names = 0;
    LibmcBindings::RowNames(&names);
    CHECK(rows == 15 && names == 15, "fifteen_libmc_rows_registered");
    std::vector<SdkBindingManifest::TitleRow> title = {{"libmc.a", "sceMcSync", 0x2c5820, ""}, {"libmc.a", "sceMcGetInfo", 0x2c5940, ""}};
    CHECK(manifest.Resolve(title).size() == 2, "rows_resolve_under_libmc_a");

    // The caller's stack word at sp+0x10 points at an object whose header word must survive
    // every GetInfo (the o32 stack read of the fifth argument would store `format` there).
    memory::write<uint32_t>(kSp + 0x10, kSentinelObj);
    memory::write<uint32_t>(kSentinelObj, 0xE004);
    memory::write<uint32_t>(kFormatPtr, 0xFFFFFFFF);

    // Phase 0: unbound.
    int32_t cmd = 99, res = 99;
    CHECK(Sync(1, &cmd, &res) == -1 && cmd == 0 && res == 0, "p0_idle_sync_is_minus_one");
    CHECK(S(Call(&LibmcBindings::GetInfo, 0, 0, kTypePtr, kFreePtr, kFormatPtr)) == -100, "p0_getinfo_unbound_is_minus_100");
    PutStr("/BASLUS-12345/*");
    CHECK(S(Call(&LibmcBindings::GetDir, 0, 0, kStr, 0, 4, kTable)) == -100, "p0_getdir_unbound_is_minus_100");
    CHECK(S(Call(&LibmcBindings::Open, 0, 0, kStr, 1)) == -100, "p0_open_unbound_is_minus_100");
    // The recompiled sceMcInit's RPC init command binds the subsystem.
    RpcRouter::CallRequest init;
    init.server_id = McservModule::kServerId; init.func = McservModule::kFnoInit; init.recv = kBuf; init.recv_size = 12;
    mcserv.Call(init);
    CHECK(model.Bound() && mcserv.InitCalls() == 1 && reply.size() == 3 && Rd(kBuf) == 0 && Rd(kBuf + 4) == McservModule::kMcservVersion && Rd(kBuf + 8) == McservModule::kMcmanVersion,
          "mcserv_init_binds_and_replies_versions");

    // Phase 1: the GetInfo watchdog. The fifth argument is register t0 (EE calling
    // convention: a0-a3, t0-t3); the stack word at sp+0x10 is a caller's saved register in
    // the title (its MC-manager object). With the t0 arm the row must not dereference it --
    // the sentinel object keeps its header word; with the inherited stack arm (the default,
    // the baselines' observable) the `format = 1` store lands in the sentinel (BUG-089).
    CHECK(!LibmcBindings::FormatInT0(), "p1_default_arm_is_the_stack_read");
    CHECK(S(Call(&LibmcBindings::GetInfo, 0, 0, kTypePtr, kFreePtr, kFormatPtr)) == 0, "p1_getinfo_kick_accepted");
    CHECK(Rd(kTypePtr) == 2 && Rd(kFreePtr) == 0x1F00 && Rd(kFormatPtr) == -1 && Rd(kSentinelObj) == 1,
          "p1_stack_arm_writes_format_through_the_stack_word_into_the_sentinel");
    memory::write<uint32_t>(kSentinelObj, 0xE004);
    LibmcBindings::SetFormatInT0(true);
    (void)Sync(0, &cmd, &res);   // deliver the first kick so the next is a fresh probe of the same card
    CHECK(S(Call(&LibmcBindings::GetInfo, 0, 0, kTypePtr, kFreePtr, kFormatPtr)) == 0, "p1_getinfo_kick_accepted_t0_arm");
    CHECK(Rd(kTypePtr) == 2 && Rd(kFreePtr) == 0x1F00 && Rd(kFormatPtr) == 1, "p1_type_2_free_1f00_format_1_written_now");
    CHECK(Rd(kSentinelObj) == 0xE004, "p1_format_not_written_through_the_stack_word");
    (void)Sync(0, &cmd, &res);
    model.SetSeen(0, 0, false);   // back to a first probe so the latency/edge checks below read as before
    CHECK(S(Call(&LibmcBindings::GetInfo, 0, 0, kTypePtr, kFreePtr, kFormatPtr)) == 0, "p1_getinfo_kick_accepted_again");
    CHECK(Sync(1, &cmd, &res) == 0 && cmd == 1 && res == 0, "p1_first_poll_busy");
    CHECK(Sync(1, &cmd, &res) == 0, "p1_second_poll_busy_latency_2");
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 1 && res == -1, "p1_third_poll_completes_with_minus_one_formatted_card_appeared");
    CHECK(Sync(1, &cmd, &res) == -1, "p1_then_idle");
    Call(&LibmcBindings::GetInfo, 0, 0, kTypePtr, kFreePtr, kFormatPtr);
    CHECK(Sync(0, &cmd, &res) == 1 && cmd == 1 && res == 0, "p1_blocking_sync_delivers_at_once_same_card_0");
    Call(&LibmcBindings::GetInfo, 1, 0, kTypePtr, kFreePtr, kFormatPtr);
    Sync(0, &cmd, &res);
    CHECK(res == -1, "p1_port_1_first_probe_is_its_own_edge");

    // Phase 2: the failing trail on an empty root.
    for (int i = 0; i < 7; i++) {
        PutStr("/BASLUS-12345/*");
        CHECK(S(Call(&LibmcBindings::GetDir, 0, 0, kStr, 0, 4, kTable)) == 0, "p2_getdir_kick_accepted");
        CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xD && res == -4, "p2_missing_save_dir_listing_is_minus_4");
    }
    PutStr("/*");
    Call(&LibmcBindings::GetDir, 0, 0, kStr, 0, 16, kTable);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xD && res == 0, "p2_root_wildcard_listing_empty_no_dot_entries");

    // Kick rules while a save-flow completion is undelivered.
    PutStr("/BASLUS-12345/*");
    Call(&LibmcBindings::GetDir, 0, 0, kStr, 0, 4, kTable);
    Call(&LibmcBindings::GetInfo, 0, 0, kTypePtr, kFreePtr, kFormatPtr);
    CHECK(model.PendingCmd() == 0xD && model.DroppedGetInfoKicks() == 1, "kick_getinfo_dropped_while_getdir_pending");
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xD && res == -4, "kick_pending_getdir_delivered_intact");
    Call(&LibmcBindings::GetInfo, 0, 0, kTypePtr, kFreePtr, kFormatPtr);
    PutStr("/BASLUS-12345");
    Call(&LibmcBindings::Mkdir, 0, 0, kStr);
    CHECK(model.PendingCmd() == 0xB && model.Clobbers() == 1, "kick_save_flow_replaces_pending_getinfo");
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xB && res == 0, "p3_mkdir_completes_0");

    // Phase 3: the new-save continuation.
    PutStr("/BASLUS-12345");
    Call(&LibmcBindings::GetDir, 0, 0, kStr, 0, 4, kTable);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xD && res == 1 && EntryName(kTable, 0) == "BASLUS-12345" &&
              memory::read<uint16_t>(kTable + 20) == 0x8427 && Rd(kTable + 16) == 0,
          "p3_exact_lookup_finds_the_directory_entry");
    // The count-only form (maxent -1, table NULL): the entry count, nothing written.
    PutStr("/BASLUS-12345");
    memory::write<uint32_t>(kTable, 0xDEADBEEF);
    Call(&LibmcBindings::GetDir, 0, 0, kStr, 0, 0xFFFFFFFFu, 0);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xD && res == 1 && static_cast<uint32_t>(Rd(kTable)) == 0xDEADBEEFu, "p3_count_only_getdir_finds_one");
    PutStr("/ABSENT-00000");
    Call(&LibmcBindings::GetDir, 0, 0, kStr, 0, 0xFFFFFFFFu, 0);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xD && res == 0, "p3_count_only_getdir_missing_is_0");
    PutStr("/BASLUS-12345");
    Call(&LibmcBindings::Chdir, 0, 0, kStr);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xC && res == 0 && store.Cwd() == "BASLUS-12345", "p3_chdir_into_the_save_dir");
    PutStr("/NOPE");
    Call(&LibmcBindings::Chdir, 0, 0, kStr);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xC && res == -4 && store.Cwd() == "BASLUS-12345", "p3_chdir_missing_is_minus_4_cwd_kept");
    PutStr("SAVE0.DAT");
    Call(&LibmcBindings::Open, 0, 0, kStr, 0x2 | 0x200);
    int32_t fd = 0;
    CHECK(Sync(1, &cmd, &fd) == 1 && cmd == 0x2 && fd >= 2, "p3_open_creat_returns_a_descriptor");
    const char payload[17] = "EXAMPLETTL-SAVE0";
    for (uint32_t i = 0; i < 17; i++) memory::write<uint8_t>(kBuf + i, static_cast<uint8_t>(payload[i]));
    Call(&LibmcBindings::Write, static_cast<uint32_t>(fd), kBuf, 17);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0x6 && res == 17, "p3_write_17_bytes");
    Call(&LibmcBindings::Close, static_cast<uint32_t>(fd));
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0x3 && res == 0 && store.OpenDescriptors() == 0, "p3_close_frees_the_descriptor");
    PutStr("/BASLUS-12345/*");
    for (uint32_t i = 0; i < 64 * 4; i++) memory::write<uint8_t>(kTable + i, 0);
    Call(&LibmcBindings::GetDir, 0, 0, kStr, 0, 4, kTable);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xD && res == 3 && EntryName(kTable, 0) == "." && EntryName(kTable, 1) == ".." &&
              EntryName(kTable, 2) == "SAVE0.DAT" && Rd(kTable + 2 * 64 + 16) == 17 && memory::read<uint16_t>(kTable + 2 * 64 + 20) == 0x8497,
          "p3_subdir_wildcard_listing_dot_dotdot_then_the_file");
    CHECK(memory::read<uint8_t>(kTable + 3) == 12 && memory::read<uint8_t>(kTable + 6) == static_cast<uint8_t>(2026 & 0xFF),
          "p3_entry_timestamp_is_the_fixed_stamp");

    // Phase 4: reopen / read-back.
    PutStr("/BASLUS-12345/SAVE0.DAT");
    Call(&LibmcBindings::Open, 0, 0, kStr, 0x1);
    Sync(1, &cmd, &fd);
    for (uint32_t i = 0; i < 32; i++) memory::write<uint8_t>(kBuf + i, 0xAA);
    Call(&LibmcBindings::Read, static_cast<uint32_t>(fd), kBuf, 32);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0x5 && res == 17, "p4_read_returns_17");
    bool same = true;
    for (uint32_t i = 0; i < 17; i++) same &= memory::read<uint8_t>(kBuf + i) == static_cast<uint8_t>(payload[i]);
    CHECK(same && memory::read<uint8_t>(kBuf + 17) == 0xAA, "p4_read_back_byte_exact_guard_intact");
    Call(&LibmcBindings::Read, static_cast<uint32_t>(fd), kBuf, 32);
    CHECK(Sync(1, &cmd, &res) == 1 && res == 0, "p4_second_read_at_eof_is_0");
    Call(&LibmcBindings::Close, static_cast<uint32_t>(fd));
    Sync(1, &cmd, &res);
    Call(&LibmcBindings::Read, 77, kBuf, 4);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0x5 && res == -5, "p4_bad_descriptor_is_minus_5");
    PutStr("/BASLUS-12345/SAVE0.DAT");
    Call(&LibmcBindings::Delete, 0, 0, kStr);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0xF && res == 0 && !std::filesystem::exists(root + "/mc0/BASLUS-12345/SAVE0.DAT"),
          "p4_delete_removes_the_host_file");
    Call(&LibmcBindings::Format, 0, 0);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0x10 && res == 0, "p4_format_completes_0");
    // sceMcGetEntSpace: the free entries of a directory (the fresh card's free clusters x 2), -4 when missing.
    PutStr("/");
    Call(&LibmcBindings::GetEntSpace, 0, 0, kStr);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0x12 && res == 0x3E00, "p4_getentspace_root_is_free_clusters_x2");
    PutStr("/BASLUS-12345");
    Call(&LibmcBindings::GetEntSpace, 0, 0, kStr);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0x12 && res == 0x3E00, "p4_getentspace_subdir");
    PutStr("/NOPE");
    Call(&LibmcBindings::GetEntSpace, 0, 0, kStr);
    CHECK(Sync(1, &cmd, &res) == 1 && cmd == 0x12 && res == -4, "p4_getentspace_missing_is_minus_4");
    CHECK(S(Call(&LibmcBindings::Internal)) == 0, "mce_internal_rows_return_0");
    CHECK(store.HostPath("/../../etc") == root + "/mc0/etc", "host_path_never_escapes_the_card");

    std::filesystem::remove_all(root);
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}

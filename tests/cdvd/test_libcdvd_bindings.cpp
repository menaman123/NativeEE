// LibcdvdBindings: the libcdvd rows over guest memory.
//   * every row registers under "libcdvd.a" (the title's rows resolve to them);
//   * sceCdReadClock writes the fixed RTC (deterministic profile) as BCD
//     {stat, sec, min, hour, pad, day, month, year} = 2005-06-15 12:00:00 ->
//     00 00 00 12 00 15 06 05, returns 1; a null pointer -> 0;
//   * sceCdDiskReady -> 2, sceCdInit/Mmode/Break -> 1, sceCdSync/SyncS/GetError -> 0,
//     sceCdGetDiskType -> 0x14, sceCdStatus -> 2;
//   * sceCdSearchFile fills sceCdlFILE {lsn,size,name[16],date[8]} from the ISO
//     (when the image is present: argv[1]) and returns 1; a miss zeroes it, 0;
//   * sceCdRead copies sectors into EE RAM byte for byte (guard intact), 1;
//   * the stream rows drive the StreamReader: StInit/Stream(START,STAT,SEEK,STOP)/StRead.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/cdvd/hle/libcdvd_bindings.h"
#include "engine/cdvd/iso_fs.h"
#include "engine/cdvd/read_engine.h"
#include "engine/cdvd/stream_reader.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/ee/timing/derived_clocks.h"
#include "engine/ee/timing/emu_domain.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

static uint32_t Call(void (*fn)(CpuContext&), uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    static CpuContext ctx;
    ctx.cpuRegs.GPR.r[4].UL[0] = a0; ctx.cpuRegs.GPR.r[5].UL[0] = a1;
    ctx.cpuRegs.GPR.r[6].UL[0] = a2; ctx.cpuRegs.GPR.r[7].UL[0] = a3;
    ctx.cpuRegs.GPR.r[2].UD[0] = 0xDEADBEEFull;
    fn(ctx);
    return ctx.cpuRegs.GPR.r[2].UL[0];
}

int main(int argc, char** argv) {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);

    EmuDomain domain;
    DerivedClocks clocks(domain);
    clocks.SetRtcFixed(true);
    IsoFs fs;
    const bool have_iso = argc > 1 && fs.Open(argv[1]);
    ReadEngine engine(fs);

    SdkBindingManifest& m = SdkBindingManifest::Instance();
    StreamReader stream(have_iso ? &engine : nullptr);
    const int n = LibcdvdBindings::Bind(m, have_iso ? &engine : nullptr, &clocks, &stream);
    size_t count = 0;
    const char* const* names = LibcdvdBindings::RowNames(&count);
    bool all = n == static_cast<int>(count);
    for (size_t i = 0; i < count; i++) all &= m.Find("libcdvd.a", names[i]) != nullptr;
    CHECK(all && m.Find("libcdvd.a", "sceCdReadClock") && m.Find("libcdvd.a", "sceCdDiskReady"), "every_row_registered_under_libcdvd");
    std::printf("  rows=%d\n", n);

    // RTC: the fixed epoch 2005-06-15 12:00:00 in BCD.
    const uint32_t clk = 0x00100000;
    for (uint32_t i = 0; i < 12; i++) memory::write<uint8_t>(clk + i, 0xAA);
    CHECK(Call(&LibcdvdBindings::ReadClock, clk) == 1, "read_clock_returns_1");
    const uint8_t expect[8] = {0x00, 0x00, 0x00, 0x12, 0x00, 0x15, 0x06, 0x05};
    uint8_t got[12];
    for (uint32_t i = 0; i < 12; i++) got[i] = memory::read<uint8_t>(clk + i);
    CHECK(std::memcmp(got, expect, 8) == 0 && got[8] == 0xAA, "read_clock_fixed_rtc_bcd_2005_06_15_12_00_00_guard_intact");
    std::printf("  sceCdCLOCK = %02x %02x %02x %02x %02x %02x %02x %02x\n", got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7]);
    CHECK(Call(&LibcdvdBindings::ReadClock, 0) == 0, "read_clock_null_pointer_returns_0");
    CHECK(LibcdvdBindings::ReadClockCalls() == 2, "read_clock_census");

    CHECK(Call(&LibcdvdBindings::DiskReady, 0) == 2 && Call(&LibcdvdBindings::DiskReady, 1) == 2, "disk_ready_is_2_both_modes");
    CHECK(Call(&LibcdvdBindings::Init, 0) == 1 && Call(&LibcdvdBindings::Mmode, 2) == 1 && Call(&LibcdvdBindings::Break) == 1, "init_mmode_break_return_1");
    CHECK(Call(&LibcdvdBindings::Sync, 0) == 0 && Call(&LibcdvdBindings::Sync, 1) == 0 && Call(&LibcdvdBindings::GetError) == 0, "sync_and_get_error_return_0");
    CHECK(Call(&LibcdvdBindings::GetDiskType) == 0x14 && Call(&LibcdvdBindings::Status) == 2, "disk_type_ps2dvd_status_pause");
    CHECK(Call(&LibcdvdBindings::CmdSemInit) == 0 && Call(&LibcdvdBindings::Prechk) == 1 && Call(&LibcdvdBindings::Exit) == 1 &&
              Call(&LibcdvdBindings::PoffIntr) == 0, "internal_rows");

    // SearchFile / Read against the disc image.
    const uint32_t fp = 0x00200000, name = 0x00200100, buf = 0x00300000;
    const char* path = "\\DATA\\SYS\\STREAM.IRX;1";
    for (uint32_t i = 0; i <= std::strlen(path); i++) memory::write<uint8_t>(name + i, static_cast<uint8_t>(path[i]));
    for (uint32_t i = 0; i < 40; i++) memory::write<uint8_t>(fp + i, 0xBB);
    if (!have_iso) {
        CHECK(Call(&LibcdvdBindings::SearchFile, fp, name) == 0 && memory::read<uint32_t>(fp) == 0, "search_without_disc_zeroes_and_misses");
        CHECK(Call(&LibcdvdBindings::Read, 100, 1, buf, 0) == 0, "read_without_disc_returns_0");
        std::printf("  (no disc image given: ISO-backed cases skipped)\n");
    } else {
        CHECK(Call(&LibcdvdBindings::SearchFile, fp, name) == 1, "search_file_hit");
        IsoFs::Entry e;
        fs.Find("DATA/SYS/STREAM.IRX", e);
        const uint32_t lsn = memory::read<uint32_t>(fp), size = memory::read<uint32_t>(fp + 4);
        char nm[17] = {};
        for (int i = 0; i < 16; i++) nm[i] = static_cast<char>(memory::read<uint8_t>(fp + 8 + i));
        CHECK(lsn == e.lba && size == e.size && std::string(nm) == e.name && memory::read<uint8_t>(fp + 32) == 0xBB,
              "search_file_record_lsn_size_name_guard");
        std::printf("  sceCdlFILE: lsn %u size %u name '%s' date %02x%02x%02x %02x%02x%02x\n", lsn, size, nm, memory::read<uint8_t>(fp + 31),
                    memory::read<uint8_t>(fp + 30), memory::read<uint8_t>(fp + 29), memory::read<uint8_t>(fp + 27), memory::read<uint8_t>(fp + 26),
                    memory::read<uint8_t>(fp + 25));
        // The sceCdlFILE record may live in the scratchpad (this title keeps its
        // FMV opener's record at 0x70003ec0): the row fills it there too. A
        // main-RAM-only gate here left the first FMV unopened (sceCdSearchFile
        // answered 0, the opener skipped sceCdStInit/sceCdStStart).
        const uint32_t spad_fp = 0x70003ec0;
        for (uint32_t i = 0; i < 40; i++) memory::write<uint8_t>(spad_fp + i, 0xBB);
        CHECK(Call(&LibcdvdBindings::SearchFile, spad_fp, name) == 1 && memory::read<uint32_t>(spad_fp) == e.lba &&
                  memory::read<uint32_t>(spad_fp + 4) == e.size && memory::read<uint8_t>(spad_fp + 32) == 0xBB,
              "search_file_record_in_scratchpad_filled");
        CHECK(Call(&LibcdvdBindings::SearchFile, 0x70003ff0, name) == 0, "search_file_record_past_scratchpad_end_rejected");
        CHECK(Call(&LibcdvdBindings::SearchFile, 0x10003000, name) == 0, "search_file_record_in_mmio_rejected");
        const char* miss = "\\DATA\\SYS\\NOPE.IRX;1";
        for (uint32_t i = 0; i <= std::strlen(miss); i++) memory::write<uint8_t>(name + i, static_cast<uint8_t>(miss[i]));
        CHECK(Call(&LibcdvdBindings::SearchFile, fp, name) == 0 && memory::read<uint32_t>(fp) == 0 && memory::read<uint32_t>(fp + 4) == 0,
              "search_file_miss_zeroes_record");
        // sceCdRead: 2 sectors of the file into EE RAM, byte-exact with the fd path.
        for (uint32_t i = 0; i < 2 * IsoFs::kSectorSize + 4; i++) memory::write<uint8_t>(buf + i, 0xCC);
        CHECK(Call(&LibcdvdBindings::Read, e.lba, 2, buf, 0) == 1, "read_two_sectors_returns_1");
        std::vector<uint8_t> want(2 * IsoFs::kSectorSize);
        const int32_t fd = engine.Open("DATA/SYS/STREAM.IRX", 0);
        engine.Read(fd, want.data(), static_cast<uint32_t>(want.size()));
        engine.Close(fd);
        bool same = memory::read<uint8_t>(buf + 2 * IsoFs::kSectorSize) == 0xCC;
        for (uint32_t i = 0; i < want.size() && same; i++) same = memory::read<uint8_t>(buf + i) == want[i];
        CHECK(same, "read_sectors_byte_exact_guard_intact");
        CHECK(Call(&LibcdvdBindings::Read, 0x7FFFFFF0u, 4, buf, 0) == 0 && Call(&LibcdvdBindings::Read, e.lba, 0, buf, 0) == 0,
              "read_off_image_or_zero_sectors_returns_0");
        CHECK(LibcdvdBindings::ReadCalls() == 3 && LibcdvdBindings::SearchCalls() == 5, "read_and_search_census");  // 2 main-RAM + 3 record-pointer cases
        // A buffer that runs past main RAM: no partial copy, 0.
        CHECK(Call(&LibcdvdBindings::Read, e.lba, 2, 0x01FFF800u, 0) == 0, "read_buffer_past_ram_returns_0");

        // The stream rows: sceCdStInit, sceCdStream(START/STAT/SEEK/STOP), sceCdStRead against the file.
        const uint32_t sbuf = 0x00400000, errp = 0x00402000;   // err word outside the data + guard
        CHECK(Call(&LibcdvdBindings::StInit, 0, 4, sbuf, 0) == 1 && stream.Initialized() && !stream.Active(), "st_init_row");
        CHECK(Call(&LibcdvdBindings::Stream, e.lba, 0, 0, StreamReader::kCmdStart) == 1 && stream.Active() && stream.CurrentLba() == e.lba,
              "st_stream_start_row");
        CHECK(Call(&LibcdvdBindings::Stream, 0, 0, 0, StreamReader::kCmdStat) == 16, "st_stream_stat_row_always_full_ring_16");
        for (uint32_t i = 0; i < 2 * IsoFs::kSectorSize + 4; i++) memory::write<uint8_t>(sbuf + i, 0xDD);
        memory::write<uint32_t>(errp, 0x77);
        CHECK(Call(&LibcdvdBindings::StRead, 2, sbuf, 0, errp) == 2 && memory::read<uint32_t>(errp) == 0 && stream.CurrentLba() == e.lba + 2,
              "st_read_row_two_sectors_err_0");
        same = memory::read<uint8_t>(sbuf + 2 * IsoFs::kSectorSize) == 0xDD;
        for (uint32_t i = 0; i < want.size() && same; i++) same = memory::read<uint8_t>(sbuf + i) == want[i];
        CHECK(same, "st_read_bytes_equal_file_start_guard_intact");
        CHECK(Call(&LibcdvdBindings::Stream, e.lba, 0, 0, StreamReader::kCmdSeek) == 1 && stream.CurrentLba() == e.lba, "st_stream_seek_row");
        CHECK(Call(&LibcdvdBindings::Stream, 0, 0, 0, StreamReader::kCmdStop) == 1 && !stream.Active(), "st_stream_stop_row");
        CHECK(Call(&LibcdvdBindings::StRead, 1, sbuf, 0, errp) == 0 && memory::read<uint32_t>(errp) == 1, "st_read_after_stop_0_err_1");
        CHECK(Call(&LibcdvdBindings::Stream, 0, 0, 0, 42) == 0, "st_stream_unknown_command_0");
        CHECK(LibcdvdBindings::StreamCalls() == 8, "stream_census");
    }
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}

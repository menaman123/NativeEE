// CdvdStreamReader: the sceCdSt* ring state machine over the ReadEngine.
//   * Init -> initialized, idle, ring geometry recorded, 1;
//   * Read before Start -> 0 with err=1 (not active);
//   * Start(lba) -> active from lba; Stat = the ring size (16 when Init declared 0 --
//     the documented always-full-ring divergence); Pause/Resume -> 1, paused flag;
//   * a read ring round trip against the ISO (argv[1]): sectors read through the
//     stream equal a direct ReadEngine::ReadSectors of the same LBAs, the position
//     advances, TotalRead accumulates, Seek repositions, Stop deactivates;
//   * past the image end -> 0 with err=0; without an engine -> 0 with err=1;
//   * sceCdStream dispatch codes carry the SDK's numbering.
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/cdvd/iso_fs.h"
#include "engine/cdvd/read_engine.h"
#include "engine/cdvd/stream_reader.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

int main(int argc, char** argv) {
    IsoFs fs;
    const bool have_iso = argc > 1 && fs.Open(argv[1]);
    ReadEngine engine(fs);
    StreamReader st(&engine);

    CHECK(!st.Initialized() && !st.Active(), "fresh_reader_idle");
    CHECK(st.Stat() == 0, "stat_inactive_is_0");
    CHECK(st.Init(0, 4, 0x00500000) == 1 && st.Initialized() && !st.Active() && st.NumBuffers() == 4 &&
              st.BufferAddress() == 0x00500000,
          "init_records_ring_and_stays_idle");
    std::vector<uint8_t> buf(4 * StreamReader::kSectorBytes, 0xEE);
    uint32_t err = 7;
    CHECK(st.Read(1, buf.data(), &err) == 0 && err == 1, "read_before_start_is_0_err_1");
    CHECK(st.Start(261) == 1 && st.Active() && st.StartLba() == 261 && st.CurrentLba() == 261 && st.TotalRead() == 0,
          "start_positions_the_stream");
    CHECK(st.Stat() == StreamReader::kDefaultRingSectors, "stat_active_undeclared_ring_is_16_documented_divergence");
    CHECK(st.Pause() == 1 && st.Paused() && st.Resume() == 1 && !st.Paused(), "pause_resume_flags");
    CHECK(st.Init(32, 2, 0x00600000) == 1 && !st.Active() && st.Start(261) == 1 && st.Stat() == 32,
          "stat_active_declared_ring_is_ring_size");
    CHECK(StreamReader::kCmdStart == 1 && StreamReader::kCmdRead == 2 && StreamReader::kCmdStop == 3 &&
              StreamReader::kCmdSeek == 4 && StreamReader::kCmdInit == 5 && StreamReader::kCmdStat == 6 &&
              StreamReader::kCmdPause == 7 && StreamReader::kCmdResume == 8 && StreamReader::kCmdSeekF == 9,
          "sceCdStream_command_codes");

    if (have_iso) {
        // Round trip: the root directory sector run (LBA 261..) through the stream vs a direct read.
        std::vector<uint8_t> direct(3 * StreamReader::kSectorBytes);
        CHECK(engine.ReadSectors(261, 3, direct.data()) == 3, "iso_direct_read_3_sectors");
        err = 7;
        CHECK(st.Read(2, buf.data(), &err) == 2 && err == 0 && st.CurrentLba() == 263 && st.TotalRead() == 2,
              "stream_read_2_sectors_advances");
        CHECK(std::memcmp(buf.data(), direct.data(), 2 * StreamReader::kSectorBytes) == 0, "stream_bytes_equal_direct_read");
        CHECK(st.Read(1, buf.data(), &err) == 1 && err == 0 && st.CurrentLba() == 264 &&
                  std::memcmp(buf.data(), direct.data() + 2 * StreamReader::kSectorBytes, StreamReader::kSectorBytes) == 0,
              "stream_continues_from_the_advanced_position");
        CHECK(st.Seek(262) == 1 && st.CurrentLba() == 262 && st.Read(1, buf.data(), &err) == 1 &&
                  std::memcmp(buf.data(), direct.data() + StreamReader::kSectorBytes, StreamReader::kSectorBytes) == 0,
              "seek_repositions");
        CHECK(st.Read(0, buf.data(), &err) == 0 && err == 0, "zero_sector_read_is_0_no_error");
        // Off the image: 0 delivered, err=0 (not an error), position unchanged.
        const uint32_t end_lba = static_cast<uint32_t>(fs.ImageBytes() / IsoFs::kSectorSize);
        CHECK(st.Seek(end_lba + 10) == 1 && st.Read(2, buf.data(), &err) == 0 && err == 0 && st.CurrentLba() == end_lba + 10,
              "past_image_end_is_0_err_0");
        // Short at the end: the last sector only.
        CHECK(st.Seek(end_lba - 1) == 1 && st.Read(4, buf.data(), &err) == 1 && err == 0 && st.CurrentLba() == end_lba,
              "short_read_at_image_end");
        std::printf("  total_read=%u current_lba=%u image_sectors=%u\n", st.TotalRead(), st.CurrentLba(), end_lba);
    } else {
        std::printf("  (no ISO: ring round trip skipped)\n");
    }
    CHECK(st.Stop() == 1 && !st.Active() && !st.Paused() && st.Stat() == 0, "stop_deactivates");
    err = 7;
    CHECK(st.Read(1, buf.data(), &err) == 0 && err == 1, "read_after_stop_is_0_err_1");

    StreamReader none(nullptr);
    CHECK(none.Init(16, 1, 0x00700000) == 1 && none.Start(100) == 1 && none.Read(1, buf.data(), &err) == 0 && err == 1,
          "no_engine_read_is_0_err_1");
    CHECK(none.Stat() == 16, "no_engine_stat_still_reports_the_ring");

    if (g_fail == 0) std::printf("ALL PASS\n");
    else std::printf("FAILURES: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

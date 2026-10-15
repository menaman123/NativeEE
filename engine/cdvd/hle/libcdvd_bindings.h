#pragma once
// LibcdvdBindings: the libcdvd SDK rows as real bodies, bound through the
// SdkBindingManifest (library "libcdvd.a"; the addresses are the title's
// sdk_bindings.tsv). The disc is always in the tray and every command
// completes inside the call (the original runtime's synchronous model):
//
//   sceCdInit(mode)        -> 1          sceCdMmode(media)      -> 1
//   sceCdDiskReady(mode)   -> 2 (SCECdComplete)
//   sceCdSync/SyncS(mode)  -> 0 (idle)   sceCdGetError()        -> 0 (SCECdErNO)
//   sceCdBreak()           -> 1          sceCdStatus()          -> 2 (SCECdStatPause)
//   sceCdGetDiskType()     -> 0x14 (SCECdPS2DVD)
//   sceCdReadClock(clk)    -> the RTC as the 8-byte sceCdCLOCK
//                             {stat, second, minute, hour, pad, day, month, year}
//                             in BCD from DerivedClocks::Rtc (fixed under the
//                             deterministic profile), 1
//   sceCdRead(lbn, n, buf, mode) -> n sectors copied into EE RAM, 1 (0 when
//                             the disc image has no such sectors)
//   sceCdSearchFile(fp, name) / sceCdLayerSearchFile(fp, name, layer)
//                          -> sceCdlFILE {lsn, size, name[16], date[8]} filled
//                             from the ISO9660 directory, 1 hit / 0 miss
//   cmd_sem_init()         -> 0          _sceCd_scmd_prechk/_sceCd_ncmd_prechk -> 1
//   cdvd_exit()            -> 1          _sceCd_Poff_Intr()     -> 0
//   sceCdStInit(bufmax, bankmax, buf, mode) -> StreamReader::Init, 1
//   sceCdStRead(sectors, buf, mode, err)    -> StreamReader::Read: sectors
//                             delivered into EE RAM, *err written
//   sceCdStream(lbn, sectors, buf, command, mode) -> the SDK's internal stream
//                             dispatcher (START 1 / READ 2 / STOP 3 / SEEK 4 /
//                             INIT 5 / STAT 6 / PAUSE 7 / RESUME 8 / SEEKF 9)
//                             onto the StreamReader; the title's sceCdStStart/
//                             Seek/Stop wrappers reach it this way
//   PowerOffCB is a title-internal body (writes title data words) and is NOT
//   an engine row.
//
// The original runtime zeroed the sceCdCLOCK struct; writing the RTC is the
// PS2 rule (the S-command returns the RTC) and is recorded as an accepted
// difference (deterministic profile: a fixed date).
#include <cstddef>
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;
class ReadEngine;
class DerivedClocks;
class StreamReader;

class LibcdvdBindings {
public:
    static constexpr uint32_t kDiskTypePs2Dvd = 0x14;
    static constexpr uint32_t kDiskReadyComplete = 2;
    static constexpr uint32_t kStatusPause = 2;
    static constexpr uint32_t kClockBytes = 8;
    static constexpr uint32_t kFileRecordBytes = 32;

    // Registers every row above; returns the number registered.
    static int Bind(SdkBindingManifest& m, ReadEngine* engine, const DerivedClocks* clocks, StreamReader* stream);
    static const char* const* RowNames(size_t* count);

    static void Init(CpuContext& ctx);
    static void Mmode(CpuContext& ctx);
    static void DiskReady(CpuContext& ctx);
    static void Sync(CpuContext& ctx);
    static void GetError(CpuContext& ctx);
    static void Break(CpuContext& ctx);
    static void Status(CpuContext& ctx);
    static void GetDiskType(CpuContext& ctx);
    static void ReadClock(CpuContext& ctx);
    static void Read(CpuContext& ctx);
    static void SearchFile(CpuContext& ctx);
    static void CmdSemInit(CpuContext& ctx);
    static void Prechk(CpuContext& ctx);
    static void Exit(CpuContext& ctx);
    static void PoffIntr(CpuContext& ctx);
    static void StInit(CpuContext& ctx);
    static void StRead(CpuContext& ctx);
    static void Stream(CpuContext& ctx);
    static void StStart(CpuContext& ctx);
    static void StSeek(CpuContext& ctx);
    static void StStop(CpuContext& ctx);

    // Census (tests / run.meta).
    static uint64_t ReadClockCalls();
    static uint64_t ReadCalls();
    static uint64_t SearchCalls();
    static uint64_t StreamCalls();
    static uint8_t ToBcd(unsigned v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }
};

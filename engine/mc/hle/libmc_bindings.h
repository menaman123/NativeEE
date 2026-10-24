#pragma once
// LibmcBindings: the libmc SDK rows as real bodies over McCommandModel +
// McHostStore, bound through the SdkBindingManifest (library "libmc.a"; the
// addresses are the title's sdk_bindings.tsv). sceMcInit is NOT a row: its
// recompiled body binds the mcserv RPC server and calls its init command,
// which McservFace serves (the model's Bound flag is raised there).
//
//   sceMcSync(mode, *cmd, *result)      -> model.Sync: 1 / 0 / -1; *cmd and
//                                          *result written when non-null
//   sceMcGetInfo(port, slot, *type, *free, *format)
//                                       -> type 2 / free 0x1F00 / format 1
//                                          written now; completion (1, -1|0)
//                                          queued; returns 0. The fifth
//                                          argument is register t0 (the EE
//                                          calling convention passes eight
//                                          arguments in a0-a3, t0-t3; nothing
//                                          of a 5-argument call is on the
//                                          stack), as sceMcGetDir's max/table
//                                          -- read there when FormatInT0 is
//                                          set (Config hle.libmc_format_in_t0);
//                                          otherwise the inherited sp+0x10 read,
//                                          the caller's spilled register
//                                          (BUG-089; the baselines' observable).
//   sceMcOpen(port, slot, name, mode)   -> queues (2, fd | -code), 0
//   sceMcClose(fd)                      -> queues (3, 0), 0
//   sceMcRead(fd, buf, size)            -> the bytes into EE RAM, queues (5, n), 0
//   sceMcWrite(fd, buf, size)           -> the bytes to the host file, queues (6, n), 0
//   sceMcMkdir(port, slot, path)        -> queues (0xB, 0 | -4), 0
//   sceMcChdir(port, slot, path)        -> queues (0xC, 0 | -4), 0
//   sceMcGetDir(port, slot, pattern, mode, max[t0], table[t1])
//                                       -> 64-byte sceMcTblGetDir entries
//                                          written, queues (0xD, count | -4), 0;
//                                          max -1 = count only (no table, which
//                                          may be NULL)
//   sceMcDelete(port, slot, name)       -> queues (0xF, 0 | -4), 0
//   sceMcFormat(port, slot)             -> queues (0x10, 0), 0
//   sceMcGetEntSpace(port, slot, path)  -> queues (0x12, free entries | -4), 0:
//                                          the fresh card's free clusters x 2
//                                          entries when `path` is a directory
//   mceIntrReadFixAlign / mceGetInfoApdx / mceStorePwd -> 0 (internal RPC
//                                          fragments; the work is in the rows)
// Every row but sceMcSync returns -100 while the model is not bound.
//
// sceMcTblGetDir (64 bytes): create[8], modify[8], u32 size, u16 attr,
// u16 res1, u32 res2, u32 pda, char name[32]; the timestamps are the
// deterministic profile's fixed date (the host store keeps none).
#include <cstddef>
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;
class McCommandModel;
class McHostStore;

class LibmcBindings {
public:
    static constexpr uint32_t kDirEntryBytes = 64;
    static constexpr uint32_t kMaxTransfer = 2u * 1024u * 1024u;  // 2 MiB per sceMcRead/Write call
    static constexpr uint32_t kMaxDirEntries = 512;
    static constexpr uint32_t kCountOnly = 0xFFFFFFFFu;   // sceMcGetDir maxent -1
    static constexpr size_t kMaxPath = 256;

    // Registers every row above; returns the number registered.
    static int Bind(SdkBindingManifest& m, McCommandModel* model, McHostStore* store);
    // Where sceMcGetInfo reads its fifth argument: true = t0 (the EE convention),
    // false = the stack word sp+0x10 (the inherited read). Default false.
    static void SetFormatInT0(bool on);
    static bool FormatInT0();
    static const char* const* RowNames(size_t* count);

    static void Sync(CpuContext& ctx);
    static void GetInfo(CpuContext& ctx);
    static void Open(CpuContext& ctx);
    static void Close(CpuContext& ctx);
    static void Read(CpuContext& ctx);
    static void Write(CpuContext& ctx);
    static void Mkdir(CpuContext& ctx);
    static void Chdir(CpuContext& ctx);
    static void GetDir(CpuContext& ctx);
    static void Delete(CpuContext& ctx);
    static void Format(CpuContext& ctx);
    static void GetEntSpace(CpuContext& ctx);
    static void Internal(CpuContext& ctx);

    // Census (tests / run.meta).
    static uint64_t SyncCalls();
    static uint64_t GetInfoCalls();
    static uint64_t GetDirCalls();
    static uint64_t OpenCalls();
};

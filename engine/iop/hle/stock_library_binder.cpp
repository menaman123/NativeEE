// engine/iop/hle/stock_library_binder.cpp -- the stock modules' export ordinals.
#include "engine/iop/hle/stock_library_binder.h"

#include "engine/iop/hle/cdvdman/cdvdman_module.h"
#include "engine/iop/hle/intrman/intrman_module.h"
#include "engine/iop/hle/ioman/ioman_module.h"
#include "engine/iop/hle/libsd/libsd_module.h"
#include "engine/iop/hle/loadcore/loadcore_module.h"
#include "engine/iop/hle/modload/modload_module.h"
#include "engine/iop/hle/sifcmd/sifcmd_module.h"
#include "engine/iop/hle/sifman/sifman_module.h"
#include "engine/iop/hle/stdio/stdio_module.h"
#include "engine/iop/hle/sysclib/sysclib_module.h"
#include "engine/iop/hle/sysmem/sysmem_module.h"
#include "engine/iop/hle/thbase/thbase_module.h"
#include "engine/iop/hle/thevent/thevent_module.h"
#include "engine/iop/hle/thmsgbx/thmsgbx_module.h"
#include "engine/iop/hle/thsemap/thsemap_module.h"
#include "engine/iop/hle/timrman/timrman_module.h"
#include "engine/iop/hle/usbd/usbd_module.h"
#include "engine/iop/iop_library_table.h"

namespace ps2r::iop::hle {

namespace {

// Registers `fn` as (lib, idx) bound to the module instance.
template <typename M>
int Reg(IopLibraryTable& libs, const char* lib, uint32_t version, M* m, const char* provider,
        std::initializer_list<std::pair<uint32_t, void (M::*)(IopContext&)>> entries) {
    if (!m) return 0;
    int n = 0;
    for (const auto& e : entries) {
        void (M::*fn)(IopContext&) = e.second;
        libs.RegisterHle(lib, version, e.first, [m, fn](IopContext& ctx) { (m->*fn)(ctx); }, provider);
        ++n;
    }
    return n;
}

}  // namespace

int StockLibraryBinder::Bind(IopLibraryTable& libs, const Modules& m) {
    int n = 0;
    n += Reg(libs, "sysmem", 0x102, m.sysmem, "hle:SysmemModule",
             {{4, &SysmemModule::AllocSysMemory}, {5, &SysmemModule::FreeSysMemory},
              {6, &SysmemModule::QueryMemSize}, {7, &SysmemModule::QueryMaxFreeMemSize},
              {8, &SysmemModule::QueryTotalFreeMemSize}, {9, &SysmemModule::QueryBlockTopAddress},
              {10, &SysmemModule::QueryBlockSize}, {14, &SysmemModule::Kprintf}});
    n += Reg(libs, "intrman", 0x102, m.intrman, "hle:IntrmanModule",
             {{8, &IntrmanModule::CpuDisableIntr}, {9, &IntrmanModule::CpuEnableIntr},
              {17, &IntrmanModule::CpuSuspendIntr}, {18, &IntrmanModule::CpuResumeIntr}});
    n += Reg(libs, "ioman", 0x104, m.ioman, "hle:IomanModule",
             {{4, &IomanModule::iop_open}, {5, &IomanModule::iop_close}, {6, &IomanModule::iop_read},
              {7, &IomanModule::iop_write}, {8, &IomanModule::iop_lseek}});
    if (m.ioman) {
        // ioman 1.04's remaining entries: ioctl 9, remove 10, mkdir 11, rmdir 12, dopen 13, dclose 14,
        // dread 15, getstat 16, chstat 17, format 18, AddDrv 20, DelDrv 21, rename 25, chdir 26,
        // sync 27, mount 28, umount 29, lseek64 30, devctl 31, symlink 32, readlink 33, ioctl2 34.
        static const uint32_t kUnsupported[] = {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34};
        for (uint32_t idx : kUnsupported) {
            IomanModule* mod = m.ioman;
            libs.RegisterHle("ioman", 0x104, idx, [mod](IopContext& ctx) { mod->iop_unsupported(ctx); }, "hle:IomanModule");
            ++n;
        }
    }
    n += Reg(libs, "loadcore", 0x103, m.loadcore, "hle:LoadcoreModule",
             {{5, &LoadcoreModule::FlushDcache}, {6, &LoadcoreModule::RegisterLibraryEntries},
              {7, &LoadcoreModule::ReleaseLibraryEntries}, {12, &LoadcoreModule::QueryBootMode}});
    n += Reg(libs, "stdio", 0x103, m.stdio, "hle:StdioModule", {{4, &StdioModule::printf}});
    if (m.sysclib) {
        const SysclibModule::OrdinalEntry* t = SysclibModule::OrdinalTable();
        for (size_t i = 0; i < SysclibModule::kOrdinalCount; ++i) {
            SysclibModule* mod = m.sysclib;
            SysclibModule::Stub stub = t[i].stub;
            libs.RegisterHle("sysclib", 0x104, t[i].ordinal, [mod, stub](IopContext& ctx) { (mod->*stub)(ctx); },
                             "hle:SysclibModule");
            ++n;
        }
    }
    n += Reg(libs, "thbase", 0x102, m.thbase, "hle:ThbaseModule",
             {{4, &ThbaseModule::CreateThread}, {5, &ThbaseModule::DeleteThread}, {6, &ThbaseModule::StartThread},
              {8, &ThbaseModule::ExitThread}, {9, &ThbaseModule::ExitDeleteThread}, {10, &ThbaseModule::TerminateThread},
              {14, &ThbaseModule::ChangeThreadPriority}, {16, &ThbaseModule::RotateThreadReadyQueue},
              {20, &ThbaseModule::GetThreadId}, {22, &ThbaseModule::ReferThreadStatus},
              {24, &ThbaseModule::SleepThread}, {25, &ThbaseModule::WakeupThread},
              {26, &ThbaseModule::iWakeupThread}, {33, &ThbaseModule::DelayThread}, {39, &ThbaseModule::USec2SysClock}});
    n += Reg(libs, "thsemap", 0x101, m.thsemap, "hle:ThsemapModule",
             {{4, &ThsemapModule::CreateSema}, {5, &ThsemapModule::DeleteSema}, {6, &ThsemapModule::SignalSema},
              {7, &ThsemapModule::iSignalSema}, {8, &ThsemapModule::WaitSema}, {9, &ThsemapModule::PollSema}});
    n += Reg(libs, "thevent", 0x101, m.thevent, "hle:TheventModule",
             {{4, &TheventModule::CreateEventFlag}, {5, &TheventModule::DeleteEventFlag},
              {6, &TheventModule::SetEventFlag}, {7, &TheventModule::iSetEventFlag},
              {8, &TheventModule::ClearEventFlag}, {9, &TheventModule::iClearEventFlag},
              {10, &TheventModule::WaitEventFlag}, {11, &TheventModule::PollEventFlag},
              {12, &TheventModule::ReferEventFlagStatus}});
    n += Reg(libs, "thmsgbx", 0x101, m.thmsgbx, "hle:ThmsgbxModule",
             {{4, &ThmsgbxModule::CreateMbx}, {5, &ThmsgbxModule::DeleteMbx}, {6, &ThmsgbxModule::SendMbx},
              {7, &ThmsgbxModule::iSendMbx}, {8, &ThmsgbxModule::ReceiveMbx}, {9, &ThmsgbxModule::PollMbx},
              {11, &ThmsgbxModule::ReferMbxStatus}});
    n += Reg(libs, "timrman", 0x103, m.timrman, "hle:TimrmanModule",
             {{4, &TimrmanModule::AllocHardTimer}, {6, &TimrmanModule::FreeHardTimer},
              {20, &TimrmanModule::SetTimerHandler}, {22, &TimrmanModule::SetupHardTimer},
              {23, &TimrmanModule::StartHardTimer}, {24, &TimrmanModule::StopHardTimer}});
    n += Reg(libs, "sifman", 0x101, m.sifman, "hle:SifmanModule",
             {{5, &SifmanModule::sceSifInit}, {7, &SifmanModule::sceSifSetDma}, {8, &SifmanModule::sceSifDmaStat},
              {29, &SifmanModule::sceSifCheckInit}, {32, &SifmanModule::sceSifSetDmaIntr}});
    n += Reg(libs, "sifcmd", 0x101, m.sifcmd, "hle:SifcmdModule",
             {{12, &SifcmdModule::sceSifSendCmd}, {13, &SifcmdModule::isceSifSendCmd},
              {14, &SifcmdModule::sceSifInitRpc}, {15, &SifcmdModule::sceSifBindRpc}, {16, &SifcmdModule::sceSifCallRpc},
              {17, &SifcmdModule::sceSifRegisterRpc}, {19, &SifcmdModule::sceSifSetRpcQueue},
              {22, &SifcmdModule::sceSifRpcLoop}, {23, &SifcmdModule::sceSifGetOtherData},
              {24, &SifcmdModule::sceSifRemoveRpc}, {25, &SifcmdModule::sceSifRemoveRpcQueue}});
    if (m.libsd) {
        // libsd export index -> LibsdModule::Call ordinal (the first title's 0..15 + 16 Note2Pitch).
        struct Pair { uint32_t idx; int ordinal; };
        static const Pair kLibsd[] = {{4, 0},   {5, 1},   {6, 2},   {7, 3},   {8, 4},   {9, 5},
                                      {10, 6},  {11, 7},  {13, 16}, {15, 8},  {17, 9},  {18, 10},
                                      {19, 11}, {20, 12}, {23, 13}, {25, 14}, {26, 15}};
        for (const Pair& p : kLibsd) {
            LibsdModule* mod = m.libsd;
            const int ordinal = p.ordinal;
            libs.RegisterHle("libsd", 0x105, p.idx, [mod, ordinal](IopContext& ctx) { mod->Call(ordinal, ctx); },
                             "hle:LibsdModule");
            ++n;
        }
    }
    n += Reg(libs, "cdvdman", 0x101, m.cdvdman, "hle:CdvdmanModule",
             {{4, &CdvdmanModule::sceCdInit}, {6, &CdvdmanModule::sceCdRead}, {8, &CdvdmanModule::sceCdGetError},
              {10, &CdvdmanModule::sceCdSearchFile}, {11, &CdvdmanModule::sceCdSync},
              {12, &CdvdmanModule::sceCdGetDiskType}, {13, &CdvdmanModule::sceCdDiskReady},
              {14, &CdvdmanModule::sceCdTrayReq}, {15, &CdvdmanModule::sceCdStop},
              {28, &CdvdmanModule::sceCdApplySCmd}, {37, &CdvdmanModule::sceCdCallback},
              {39, &CdvdmanModule::sceCdBreak}, {75, &CdvdmanModule::sceCdMmode}});
    n += Reg(libs, "modload", 0x107, m.modload, "hle:ModloadModule",
             {{17, &ModloadModule::ReferModuleStatus}, {18, &ModloadModule::GetModuleIdListByName}});
    n += Reg(libs, "usbd", 0x101, m.usbd, "hle:UsbdModule",
             {{4, &UsbdModule::sceUsbdRegisterLdd}, {5, &UsbdModule::sceUsbdUnregisterLdd},
              {6, &UsbdModule::sceUsbdScanStaticDescriptor}, {7, &UsbdModule::sceUsbdSetPrivateData},
              {8, &UsbdModule::sceUsbdGetPrivateData}, {9, &UsbdModule::sceUsbdOpenPipe},
              {10, &UsbdModule::sceUsbdClosePipe}, {11, &UsbdModule::sceUsbdTransferPipe},
              {12, &UsbdModule::sceUsbdOpenPipeAligned}, {13, &UsbdModule::sceUsbdGetDeviceLocation},
              {18, &UsbdModule::sceUsbdMultiIsochronousTransfer}});
    return n;
}

}  // namespace ps2r::iop::hle

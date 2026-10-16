// engine/iop/hle/stock_library_binder.h -- StockLibraryBinder: registers the
// stock Sony modules' HLE bodies in an IopLibraryTable under the real export
// ordinals (the index words in a module's import stubs), so a recompiled
// module's `import_call("thbase", 24, ctx)` reaches ThbaseModule::SleepThread.
// One place for the ordinal tables (sysmem, intrman, ioman, loadcore, stdio,
// sysclib, thbase, thsemap, thevent, thmsgbx, timrman, sifman, sifcmd, libsd,
// cdvdman, modload, usbd); a module instance left null is not registered. Nothing here
// is a title's: these are the IOP kernel's and Sony's library numbers.
#pragma once

#include <cstdint>

namespace ps2r::iop {
class IopLibraryTable;
}

namespace ps2r::iop::hle {

class SysmemModule;
class IntrmanModule;
class IomanModule;
class LoadcoreModule;
class StdioModule;
class SysclibModule;
class ThbaseModule;
class ThsemapModule;
class TheventModule;
class ThmsgbxModule;
class TimrmanModule;
class SifmanModule;
class SifcmdModule;
class LibsdModule;
class CdvdmanModule;
class ModloadModule;
class UsbdModule;

class StockLibraryBinder {
public:
    struct Modules {
        SysmemModule* sysmem = nullptr;
        IntrmanModule* intrman = nullptr;
        IomanModule* ioman = nullptr;
        LoadcoreModule* loadcore = nullptr;
        StdioModule* stdio = nullptr;
        SysclibModule* sysclib = nullptr;
        ThbaseModule* thbase = nullptr;
        ThsemapModule* thsemap = nullptr;
        TheventModule* thevent = nullptr;
        ThmsgbxModule* thmsgbx = nullptr;
        TimrmanModule* timrman = nullptr;
        SifmanModule* sifman = nullptr;
        SifcmdModule* sifcmd = nullptr;
        LibsdModule* libsd = nullptr;
        CdvdmanModule* cdvdman = nullptr;
        ModloadModule* modload = nullptr;
        UsbdModule* usbd = nullptr;
    };

    // Registers every bound module's entries; returns the number of entries registered.
    static int Bind(IopLibraryTable& libs, const Modules& m);
};

}  // namespace ps2r::iop::hle

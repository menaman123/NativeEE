#pragma once
// KernlBoot: the libkernl boot-family bindings and the kernel boot syscall
// bodies (InitMainThread / InitHeap / EndOfHeap) over BootEnv + ThreadManager.
//   InitMainThread(gp, stack, stack_size, args, root): sets $gp/$sp, adopts the
//     caller as thread 1 (RUN, prio 0 as the BIOS reports it), writes the
//     argc/argv block at `args` (kernel rule; contents from the title's
//     BootParams) and returns the initial $sp.
//   InitHeap(heap, size): heap base (-1 = keep / default) and limit per
//     BootEnv::HeapLimit; seeds the title's libc break word; returns the limit.
//   EndOfHeap: top of RAM.
//   InitTLB / InitTLB32MB / InitTLBFunctions: the BIOS programs the EE TLB;
//     NEW has a flat address map (ARCHITECTURE 4.11) so they return 0.
//   ContextRestore (thbase RFU-style return): restores $ra/$sp from the current
//     thread record and returns -- the kernel's context-switch tail.
//   EIntr/DIntr: CP0.Status.IE set / clear-and-report.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

class KernlBoot {
public:
    static void Register(SdkBindingManifest& m);

    // Syscall bodies (called by SyscallTable with decoded arguments).
    static uint32_t InitMainThread(uint32_t gp, uint32_t stack, int32_t stack_size, uint32_t args,
                                   int32_t root, CpuContext& ctx);
    static uint32_t InitHeap(uint32_t heap, int32_t heap_size, CpuContext& ctx);
    static uint32_t EndOfHeap();
    static uint32_t HeapBase() { return heap_base_; }
    static uint32_t HeapEnd() { return heap_end_; }

    // Generated-code seams (row 20).
    static void InitTLB(CpuContext& ctx);
    static void ContextRestore(CpuContext& ctx);

    static void ResetForTest();

private:
    static uint32_t heap_base_;
    static uint32_t heap_end_;
};

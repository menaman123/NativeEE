# tests/vif -- Phase 5 VIF suites. NEW-only unit tests against ps2r_vif (the
# UNPACK scalar reference, the command processor) plus, from 5E part 2, the
# ported vif_replay equivalence arms. macos-dev runs the SCALAR unpack lanes
# (PS2R_SIMD OFF); the NEON twins are proven from a bounded -DPS2R_SIMD=ON
# configure.

function(ps2r_vif_test name ctest_name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_vif ps2r_gs_ir)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O2)
    add_test(NAME ${ctest_name} COMMAND ${name} ${ARGN})
    set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endfunction()

# VifUnpackScalar: GetUnpackSize (num 0 -> 256, CL<WL filling count, byte
# rounding), the mask table, and the scalar UNPACK loop on hand-computed
# vectors: every format x USN, S replicate, V2 z=x w=y (+ V2-32 W=0 on odd
# phase), V3 W peek + the V3-32 parity rule, V4-5 expansion, mode 1/2/3 adds,
# every mask action with write_cycle wrapping at WL, CL>WL skip, CL<WL fill,
# 1024-QW wrap, TOPS on VIF1 only, VU0 masked-read wrap, partial payloads,
# and the bulk lanes byte-identical to the scalar loop on the same shapes.
ps2r_vif_test(test_vif_unpack_scalar VifUnpackScalar)

# VifCommands: register effects of STCYCL/OFFSET/BASE/ITOP/STMOD/MARK/STMASK/
# STROW/STCOL, MSCAL's TOP/ITOPS latch + DBF/TOPS swap driving a real micro
# through VuUnit (XTOP/XITOP), MSCNT resume, MPG upload + generation bump,
# DIRECT/DIRECTHL into GifArbiter -> GifParser -> RegisterOpStream (incl. a
# transfer spanning two feeds), MSKPATH3 -> GifRegs, the i-bit / MII rule,
# the MMIO register file, FBRST, the VIF1 FIFO window, and word-path vs
# whole-span parity on every quadword split of a mixed stream.
ps2r_vif_test(test_vif_commands VifCommands)

# VifMmioGate: the router-facing VIF surface (the reference's routed observable,
# P5-VIF-MMIO-OBSERVABLE): FBRST/ERR reads and writes forward on both units,
# every other VIF register reads 0 through the gate while Vif::Read of the
# same address is non-zero, non-FBRST/ERR writes leave both register files
# untouched, a VIF1 FIFO-window write reaches the unit exactly as a direct
# Vif::Write (latch state, submit on slot 3), the VIF0 FIFO address is dropped,
# and the ReadsZeroed / WritesDropped counters are exact.
ps2r_vif_test(test_vif_mmio_gate VifMmioGate)

# VifsSection: the VIFS savestate section -- 184 bytes = VIF0 then VIF1 VifRegs
# in field order, round trip into a fresh rig byte-equal, restore idles each
# unit's transient state (latch / FIFO / cursor / pending / DIRECT buffer) while
# keeping the restored registers (Vif::ResetTransient), wrong lengths refused,
# REQUIRED through the registry.
ps2r_vif_test(test_vifs_section VifsSection)

# ---- tools/vif_replay (5E part 2) ----
# VifUnpackBulkEquivalence(Fast): the ported OLD arms. 30 synthetic VIF1
# streams, each fed twice on a fresh rig (bulk lanes off / on) -> byte-identical
# 16 KB VU1 data memory, must-execute / must-decline lane census, parser idle;
# the no-flag row drives the word latch, --vif-fast the whole-span parser.
add_test(NAME VifUnpackBulkEquivalence COMMAND vif_replay --unpack-bulk)
add_test(NAME VifUnpackBulkEquivalenceFast COMMAND vif_replay --unpack-bulk --vif-fast)
set_tests_properties(VifUnpackBulkEquivalence VifUnpackBulkEquivalenceFast PROPERTIES TIMEOUT 120)

# ---- tools/vif_replay (5G part 2) ----
# VifMtvuSubmitEquivalence(Fast): the ported OLD --mtvu 20 arm. Per iteration
# three synthetic VIF1 DMA chains (MPG, absolute/TOPS/masked/mode UNPACKs
# spanning tags, MSCAL/MSCNT, DIRECT, FLUSH, TTE words) walked inline, through
# the MtvuWorker ring (staged Batch / per-span alternating, the EE's VIF1_STAT
# poll loop alongside) and through the ring with mid-stream eager drains and a
# deferred frame boundary per chain -> identical packet census+order,
# byte-identical VU1 data+code memory, identical VF/VI/TPC, position-exact
# boundaries; plus the fixed structural case. Serial: the overlap-liveness
# gate (some packets drained mid-wait over 20 iterations) is scheduler-shaped.
add_test(NAME VifMtvuSubmitEquivalence COMMAND vif_replay --mtvu 20)
add_test(NAME VifMtvuSubmitEquivalenceFast COMMAND vif_replay --mtvu 20 --vif-fast)
set_tests_properties(VifMtvuSubmitEquivalence VifMtvuSubmitEquivalenceFast
    PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)

# VifRunaheadBound(Off): the ported --runahead arms on the injected fake clock
# (A: depth 10 > cap 2 parks until the consumer presents at +25 ms; B: frozen
# depth released by the 100 ms valve; C: cap 0 never parks). Serial: the real
# park loop still waits its 500 us polls.
add_test(NAME VifRunaheadBound COMMAND vif_replay --runahead --depth 2)
add_test(NAME VifRunaheadBoundOff COMMAND vif_replay --runahead-off --depth 0)
set_tests_properties(VifRunaheadBound VifRunaheadBoundOff PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# VifArmsVsOld: every ported arm (--arms all: 2 UNPACK + 2 MTVU + 2 runahead)
# on this tree's tool versus the frozen OLD tool's arms -- the reference is
# re-made bounded from the OLD binary (cwd pinned under the build tree) and
# every line + exit code must be identical after `[VU-MODE]` is dropped and
# the timing tokens (busy views, eager-drained counts, iteration ratio, ms
# values) are blanked on both sides. SKIP 77 when the OLD tool is absent.
add_test(NAME VifArmsVsOld
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vif_replay/vif_arms_vs_old.py
            --new-bin $<TARGET_FILE:vif_replay>
            --old-bin ${PS2R_OG_ROOT}/build/tests/vif_replay
            --refresh-old
            --arms all
            --out ${CMAKE_BINARY_DIR}/tests/vif/vif_arms_vs_old)
set_tests_properties(VifArmsVsOld PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

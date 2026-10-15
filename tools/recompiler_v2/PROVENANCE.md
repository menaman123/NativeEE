# PROVENANCE -- `tools/recompiler_v2/` (byte-identical copies of OLD files; stage P8.8-T)

OLD = `$PS2R_OLD_ROOT` (read-only; HEAD `5e1b17feed02552a0aa15e71d03c40a12c20e6d9`, worktree clean for these paths at copy time — `git status --porcelain` shows only ` M include/ELFIO` / ` M include/rabbitizer`, which is an untracked `.DS_Store` inside each submodule, committed content unchanged: ELFIO `7d30a22`, rabbitizer `fb4ab5e`).
Every file below is an UNMODIFIED copy of the named OLD file (the md5 is of both) EXCEPT `host_app/common/Pcsx2Types.h`: OLD's file is a verbatim PCSX2 header (SPDX GPL-3.0+), which STAGE_RULES rule 5 forbids in NEW; it is replaced by a NEW-authored header of the same name exposing the same type names (`s8..u64`, `uptr/sptr/uint`, `u128`/`s128`), so `cpu_state.h` compiles unchanged (finding INT-P88T-PCSX2-GPL-HEADER, DP-23). The ctest `RecompToolProvenanceIntact` re-hashes this tree against this table.
Fixes to the tool are made HERE from P8.9-A on (DP-23: NEW owns the recompiler); a changed file gets a new row with its new md5 and a note naming the change.

**Diverged at P8.9-A (BUG-072, DP-18):** `recompiler_tool/discovery/discovery_types.h`, `recompiler_tool/discovery/functions.cpp`, `recompiler_tool/discovery/functions.h` (comment only), `recompiler_tool/main_v2.cpp` — rows below carry the note and OLD's md5 for reference; every other file is still byte-identical to OLD.

| File (relative) | OLD path | bytes | md5 |
|---|---|---|---|
| `continuations.py` | `NEW-authored: emitted continuation metadata and common dispatch/stack proof instrumentation; Charter 2 B1: DEFAULT also matches the compact "= &FUN_...;" registration` | 10809 | `f77c819837a24eecc44e5c6d37a3ed6e` |
| `continuation_audits.json` | `NEW-authored: exact-source hashes and rationale for diagnostic-only continuation cases` | 801 | `10c8233ab14d91f715b3e8222ee80c9d` |
| `EEAnalyze/DataFlowEngine.h` | `EEAnalyze/DataFlowEngine.h` | 1288 | `33e2974792b04aea1c0943f41de98b22` |
| `EEAnalyze/Function.h` | `EEAnalyze/Function.h` | 1529 | `1c61fb35ee20bb5063f987ec7e2dacba` |
| `EEAnalyze/analyze.cpp` | `EEAnalyze/analyze.cpp` | 12705 | `4ca7b634cd454d2f7a3a6c5915a3f02c` |
| `EEAnalyze/analyze.h` | `EEAnalyze/analyze.h` | 1127 | `3f08ca7775d4fe1c6602158298c98503` |
| `EEAnalyze/register_state.h` | `EEAnalyze/register_state.h` | 2865 | `d592426a3b0756187f2355790884fdfd` |
| `host_app/common/Pcsx2Types.h` | `NEW-authored (replaces OLD host_app/common/Pcsx2Types.h, md5 42e616dba6faa3eaa36bc4db341e2ad7 -- verbatim PCSX2 GPL-3.0+ header; STAGE_RULES rule 5); 2026-10-14: header comment renamed the product Ps2Recomp -> NativeEE (was md5 2dc773d28beba4d52328d0fa69fdbd41, 1894 bytes)` | 1883 | `437298731f5716fd83777afd1fb2cce6` |
| `host_app/cpu_state.h` | `host_app/cpu_state.h` | 11594 | `6af16a9548a3a312266d86790a9f844b` |
| `host_app/memory.h` | `host_app/memory.h` | 13638 | `c7fdea0f51a9ea14b2efbf556c11230e` |
| `include/json.hpp` | `include/json.hpp` | 919975 | `30b66ababceb852ec47ecf93fd370b8d` |
| `recompiler_tool/corpus/audit.cpp` | `recompiler_tool/corpus/audit.cpp` | 7117 | `9b42c1530f6fca71ca0f4e0710aaaec1` |
| `recompiler_tool/corpus/audit.h` | `recompiler_tool/corpus/audit.h` | 2636 | `61e807fca7ae674f5f87392b61e0fca1` |
| `recompiler_tool/corpus/recorder_gen.cpp` | `recompiler_tool/corpus/recorder_gen.cpp` | 3867 | `a7271cd08746942aa491013e65f09475` |
| `recompiler_tool/corpus/recorder_gen.h` | `recompiler_tool/corpus/recorder_gen.h` | 2122 | `17d489d2ff704bcfb525568a5f6578f1` |
| `recompiler_tool/discovery/discovery_types.h` | `diverged at P8.9-A (BUG-072): two diagnostic sets delay_slot_weak_rejected / delay_slot_strong_entries; OLD recompiler_tool/discovery/discovery_types.h md5 08867d4496caeb283d82e236a71a95ba, 6877 bytes` | 7694 | `6b1cee3d58b1ed1255f524ea943d32fe` |
| `recompiler_tool/discovery/functions.cpp` | `diverged at P8.9-A (BUG-072): the delay-slot rule (a weak seed in a branch delay slot is never a function start) + leaders for every externally-entered address (fall-through / return site / external table target of the CONTAINING function); OLD recompiler_tool/discovery/functions.cpp md5 84044779047a7f29815283049bee5d30, 25626 bytes` | 27811 | `515cb2fc1c512be25a15dee3ae5bfe41` |
| `recompiler_tool/discovery/functions.h` | `diverged at P8.9-A (BUG-072): header comment states the delay-slot rule and the containing-function leader rule; OLD recompiler_tool/discovery/functions.h md5 e4f753ef4253b468354ae835b05f03ae, 1775 bytes` | 2382 | `133fc331dbe7647041a9f0c83df0c9a5` |
| `recompiler_tool/discovery/jumptables.cpp` | `recompiler_tool/discovery/jumptables.cpp` | 23146 | `6dd7299c250083323a628f1d11031835` |
| `recompiler_tool/discovery/jumptables.h` | `recompiler_tool/discovery/jumptables.h` | 4701 | `a5395228c28be2172aee31a1d4d3a21d` |
| `recompiler_tool/discovery/leaders.cpp` | `recompiler_tool/discovery/leaders.cpp` | 2964 | `b7d841f33b9c900a1000e72c0b5959ca` |
| `recompiler_tool/discovery/leaders.h` | `recompiler_tool/discovery/leaders.h` | 1719 | `f6a103cea22b647ca386f5c94cad6ac7` |
| `recompiler_tool/discovery/report.cpp` | `recompiler_tool/discovery/report.cpp` | 2860 | `32a1a789f3822edf5da49fb1778c2952` |
| `recompiler_tool/discovery/report.h` | `recompiler_tool/discovery/report.h` | 1190 | `6affc1e2f54c228adf4ba91adcc86af7` |
| `recompiler_tool/discovery/rv2_decode.h` | `recompiler_tool/discovery/rv2_decode.h` | 6171 | `720e9fb7b3390350f661b25dda07e836` |
| `recompiler_tool/elf/elf_image.cpp` | `recompiler_tool/elf/elf_image.cpp` | 1332 | `b5ea67f70ed875edb6fde1f5b7b5c307` |
| `recompiler_tool/elf/elf_image.h` | `recompiler_tool/elf/elf_image.h` | 1326 | `5a5672751184e6747bcc28a9a4dd4e6c` |
| `recompiler_tool/emit/driver_v2.cpp` | `recompiler_tool/emit/driver_v2.cpp` | 12251 | `ffd659a671c544806ac9cca997b2dc98` |
| `recompiler_tool/emit/driver_v2.h` | `recompiler_tool/emit/driver_v2.h` | 1651 | `71b9a0efef786c18bf0df6dd64943dee` |
| `recompiler_tool/emit/v2_emission.h` | `recompiler_tool/emit/v2_emission.h` | 2625 | `ab981e89b6de2e2a939ffe14a34719c8` |
| `recompiler_tool/legacy/recomp_overrides_config.h` | `recompiler_tool/legacy/recomp_overrides_config.h` | 6119 | `22ba0e32d80fda3258e06cf6b915dc48` |
| `recompiler_tool/main_v2.cpp` | `diverged at P8.9-A (BUG-072): report line [v2] delay-slot rule: ... + the strong-entry-in-delay-slot list; at Charter 2 B1: the --init-parts flag; OLD recompiler_tool/main_v2.cpp md5 2834bd10bf248ed8e0b51d3d85c63851, 27826 bytes` | 28783 | `c0205d22ca4b81c8a53d15f8e8a9c204` |
| `recompiler_tool/recompiler.cpp` | `diverged at Charter 2 B1 (GCC lane): --init-parts emits the registration table as part functions with direct &FUN_ registrations (write_cpp_tail; inject_gen_hook_list accepts the compact default form); classic form byte-identical; OLD recompiler_tool/recompiler.cpp md5 570af65f25b8736d07d89f3d1a073ff8, 1593012 bytes` | 1597406 | `3b73b5a54565355e3990002077cb39e3` |
| `recompiler_tool/recompiler.h` | `diverged at Charter 2 B1 (GCC lane): set_init_parts / m_init_parts; OLD recompiler_tool/recompiler.h md5 37c399802eb3faede9c751f831090199, 8938 bytes` | 9997 | `3b305a0070b2417e209090cd2c14b184` |
| `recompiler_tool/translate/control_flow_v2.cpp` | `recompiler_tool/translate/control_flow_v2.cpp` | 5263 | `1112e29822ddff2d091d9a71cd113077` |

35 files.
# Continuation metadata post-pass (2026-10-07)

`continuations.py` is NEW-owned support for product reproduction saves.
`tools/regen_from_frozen.sh` runs it after emission, before output manifests.
It extracts exact entry switches and inert syscall-entry restart proofs from
the emitted body shards into `continuations.h`; it never modifies those
shards. It instruments common dispatch registration, installs, trace wrappers
and shadow-stack overflow so proof follows the actual installed callable.
Existing generated body semantics and hashes remain unchanged. Isolated
tests extract only the common stack ABI and execute synthetic callables,
never title code. Unknown special prologues and installed wrappers fail
closed; range containment and `IsHooked` are not continuation proof.

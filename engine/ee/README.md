# `engine/ee/`

**Phase:** 3  
**Purpose:** Emotion Engine: CPU context + dispatch seams, memory model, kernel/OS HLE, timing domain, INTC/timers, run token.

## Belongs here

- cpu/, mem/, hle/, timing/, intc/, sched/, mods/ (9.3)
- savestate/: the EE-side section owners over the frozen PS2SAVE1 payload layouts (`memory_sections` RAM0/SPAD/META,
  `thread_section` THRD + `thread_relauncher` (the chain walk), `kernel_sections` SEMA/EVFL, `intc_section`, `timer_section`,
  `heap_section` HEAP = the original HLE-heap grammar (skipped) + a tail codec the composition root supplies for the META
  arena's allocation bookkeeping -- BUG-085: the bytes of an arena are not a restore without the record of which cells
  are vended). The engine names no title address: title-tied codecs live under `games/<title>/`.

## Does not belong here

- recompiled OUTPUT (games/<title>/recomp/), title boot data (TitleProfile)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

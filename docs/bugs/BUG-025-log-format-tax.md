# BUG-025 — The log layer taxed the EE thread ~7.5 % formatting lines it then threw away

| Field | Content |
|---|---|
| **ID** | BUG-025 |
| **Title** | [MIDDLE_NATIVE M0] Sampling profiler pinned a flat EE-thread regression to `TagFilterBuf::put_locked` + ostream `push_back` — full iostream formatting ran for every log line BEFORE the denylist dropped it |
| **User-visible symptom** | Game-wide fps regression after middle-layer instrumentation grew (the M0-REGRESSION); no single subsystem hot — a flat tax across the EE thread. |
| **Scene / savestate** | `slowspot_f7260_s1788564002` uncapped defaults leg (P0 protocol); sampling profiler folds. |
| **Subsystem** | core-log |
| **Root cause** | The retrofit logging stack filtered AFTER formatting: every `g_logFile <<` site built its message through ostream (allocations, itoa, locale machinery) into `TagFilterBuf`, whose `put_locked` then consulted the tag denylist and DROPPED most lines. At ~6,869 raw log sites, the formatting cost of dead lines summed to ~7.5 % of the EE thread — plus the lock in `put_locked` serializing writers. |
| **PS2 rule violated** | None (host-side). The violated engineering rule: filter decisions must run BEFORE formatting; a disabled log line must cost at most one branch. |
| **Fix + hatch** | M0/M2 remediation: early-out the filter decision before formatting on the hot tags (and the M2 follow-ups reduced the raw-site population); made STRUCTURAL by the refactor — `core/log` compile-time categories where disabled paths compile to nothing (ARCH §9, "the M4A lesson, structural this time"). |
| **Regression test** | Bench fps on the slowspot anchor (the M0 regression metric); post-refactor: the M4A property test — byte-identical kept-log output on anchors while dead categories compile out (MIGRATION §1.5 log rewrite row). |
| **Source record** | `Ps2Recomp/docs/loop_state/MIDDLE_NATIVE_IMPL.md` ([M0B]/[M0R]/[M0B2] + M2 flat-profile note: "log-format tax (~7.5 % EE thread: TagFilterBuf::put_locked + ostream push_back on lines the denylist then drops)") |
| **Triage tips** | A regression with no hot function = a distributed tax: use a SAMPLING profiler and fold by module, not by leaf. Any logging API whose call site builds the string before asking "enabled?" is a perf bug at scale. When instrumentation itself regresses the system, measure the instrument first — probes are code too. |

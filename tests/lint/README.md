# `tests/lint/`

**Phase:** 0  
**Purpose:** CI lints, ctest-registered: LintOneClass (ARCH §13), LintEnginePurity (A6 §1.2 token set),
and the ARCH §11 budget lints (P8.36-L): LintSizeBudget, LintHeaderGlobals, LintSessionTags. `getenv` is
`CoreSingleGetenvSite` (tests/core).

## The §11 lints (`lint_size_budget.py`, `lint_header_globals.py`, `lint_session_tags.py`)

| ctest | measures | FAIL on | census 2026-10-06 (the lint's own tail line is authoritative; these go stale by construction) |
|---|---|---|---|
| `LintSizeBudget` (TIMEOUT 120) | file lines (soft 1,000 / hard 1,500 / fail 2,000), function lines (150 / 300 / 400; C++ brace-span heuristic + Python `def` spans, stated in the docstring), generated shard bytes (soft 3 MB / hard 4 MB; MB = decimal, 1,000,000 B — the §11 table's unit, not MiB) over `engine/ app/ games/ tools/ verify/ tests/` minus vendored/generated/frozen paths | an unallowlisted hard-fail | 4 offenders allowlisted (`ee_bootstrap.cpp` file + `Run`, shard 028, `diff_legs.py selftest`); WARNs = the refactor queue (files > 1,000, functions > 150, shards > 3 MB) |
| `LintHeaderGlobals` (TIMEOUT 60) | mutable variable DEFINITIONS in `.h .hpp` and the textual-include siblings `.inl .inc` under `engine/ app/ games/` (namespace-scope, `static`, `inline`, `static inline` members; `extern` and `static T n;` members are declarations and pass; `constexpr`/top-level `const` pass) | any | 0 |
| `LintSessionTags` (TIMEOUT 60) | `[TAG]` prose in comments (ARCH's regex, widened to `[SESSION-14 FIX]`), session-prefixed namespaces/identifiers (`r120gl::`, `s9_x`, `zk::`), session-NUMBERED `[TAG]`s in string literals — across `.cpp .h .inc .inl .py .sh .cmake CMakeLists.txt` of the whole hand-written tree | any unallowlisted occurrence | 39 pre-existing occurrences in 11 files allowlisted by literal (7 classes, below) |

Allowlist: `budget_allowlist.txt`, one `<relpath>:<kind>:<measured>` per entry with a dated reason above it
(`kind` = `file-lines`, `func-lines:<Name>`, `shard-bytes`, `header-global:<name>`, `session-tag:<text>`).
An entry silences the FAIL only while the measured value is ≤ the recorded one (growth fails again); an entry
with nothing to silence is reported stale. The thresholds live in the scripts, never in the allowlist. The
two lints whose docstrings/selftests quote the rejected shapes — `lint_session_tags.py`, `lint_one_class.py` — are
exempt from the tag scan by name; every other `tests/lint/*.py` is scanned.

Session-tag classes allowlisted (the refactor queue, PHASE8_IMPL P8.36-L): (A) the frozen bench_report.json
vocabulary below; (B) the `[GS-S9RDR]` log prefix of the shadow composite renderer, pinned by the classrdr
suite — rename with its pins; (C) `tools/vif_replay` prints OLD's tool grammar for the vs-OLD diff;
(D) FLAG_LEDGER flag names in `tools/flag_phase_counts.py`; (E) PHASE3 case labels `[T3]..[B4]` in
`tests/ee/test_thread_manager.cpp` — print case names; (F) the GS-corpus capture name `s57_walk`;
(G) the `[VU-MODE]` log prefix quoted in two test registrations.

## `lint_artifact_licenses.py` (ctest `LintArtifactLicenses`, Charter 2 C1)

The artifact's licence set: `tools/dist/licenses/` (SDL2 zlib; FFmpeg `LICENSE.md` + LGPL v2.1; Dear ImGui
MIT; the gl3w loader's public-domain dedication; GPLv3 + the GCC Runtime Library Exception for Linux's
static libstdc++/libgcc) pinned by SHA-256 in its `PROVENANCE.md`; `tools/ci/ffmpeg_static.sh
--print-configure` must name `--disable-gpl --disable-nonfree --disable-version3 --disable-everything` and
no `--enable-gpl|nonfree|version3|lib*`; `build.sh`, `build.ps1`, `README.md`'s Licence section and the
`macos-dist` / `linux-dist` presets must agree. With `--dist dist/<platform>` (the stagers run it, and
ctest does when the folder exists) the staged `licenses/` must hold every platform-required text byte-equal
to the committed copy plus the generated `FFmpeg-BUILD-CONFIGURATION.txt` (no GPL flag), and
`THIRD_PARTY_NOTICES.md` must not contradict the folder (static vs bundled, a `lib/` section iff `lib/`).

## Belongs here

- pure-python lints + their allowlists

## Does not belong here

- —

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

## Accepted exception: frozen report vocabulary (Review 1A finding 4)

`lint_one_class.py` scans comments and identifiers for session tags; it does not scan string
literals. Three OLD session-tag prefixes survive in `engine/core/diag/` **as string literals
only** because they are the frozen `bench_report.json` / census-line vocabulary that
`tools`/`verify` parse and `CoreBenchReportFormat` pins: `[R127-BENCH]` (`bench.cpp`,
`bench_report.cpp` summary line), `[R123-MTVUSYNC]` and `[R147-GFXRES]` (`bench_report.cpp`
census-line parsers). They are data, not narration: renaming them would break the frozen
report format. No other session-tag literal is accepted; a future lint pass over string
literals must carry exactly this allowlist (`tests/lint/purity_allowlist.txt` style).

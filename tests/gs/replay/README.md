# tests/gs/replay

ctest drivers for `tools/gs_replay` against the frozen OLD corpus (`PS2R_OG_ROOT/gs_corpus`,
SKIP 77 when absent): `GsReplayCensus` (census lines == the baseline replay's),
`GsClassdiffMatchesOld` (classdiff of a capture with itself is clean and its per-class totals
== the baseline CLASS lines), `GsReplayDeterminism` (two replays, one hash, one .gsrep) and
the 19 `GsCorpus_<cap>` legs (bounded replay; REPLAY_HASH == baseline or ALLOWLIST; .gsrep
byte-identical). Legs not yet hash-equal are registered DISABLED (see PHASE6_IMPL 6F) and run
by hand: `ctest --preset macos-dev -R GsCorpus_<cap>` after removing the DISABLED property, or
directly `bash tests/gs/replay/gs_corpus_leg.sh verify/run_bounded.sh build/tools/gs_replay/gs_replay
<corpus> <cap> 900 /tmp/x`.

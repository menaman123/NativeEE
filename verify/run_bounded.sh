#!/usr/bin/env bash
# run_bounded.sh -- the harness's bounded process runner (ours; verify/ is not frozen).
#
#   run_bounded.sh TIMEOUT_S LOGFILE -- cmd args...
#
# Runs `cmd args...` with stdout+stderr appended to LOGFILE and kills it (SIGKILL) when
#   * wall time exceeds TIMEOUT_S seconds                         -> exit 124
#   * resident set exceeds RB_RSS_KB (default 8388608 = 8 GiB)      -> exit 137
#   * RB_PROGRESS_FILE is set and its size has not grown for
#     RB_STALL_S seconds (default 300)                              -> exit 125
# Otherwise it exits with the command's own exit status. Polls every RB_POLL_S (default 5).
#
# Every OLD-oracle and every NEW-binary leg goes through this script (CONSTRUCTION_PLAN
# rule 7 / REFERENCE_FREEZE "Accepted side effects"). It never changes the working
# directory: callers that must protect the frozen OLD tree pin cwd themselves.
set -u
if [ "$#" -lt 3 ]; then
  echo "usage: run_bounded.sh TIMEOUT_S LOGFILE -- cmd args..." >&2
  exit 2
fi
TIMEOUT_S="$1"; LOGFILE="$2"; shift 2
[ "${1:-}" = "--" ] && shift

POLL_S="${RB_POLL_S:-5}"
RSS_LIMIT_KB="${RB_RSS_KB:-8388608}"
PROGRESS_FILE="${RB_PROGRESS_FILE:-}"
STALL_LIMIT_S="${RB_STALL_S:-300}"

mkdir -p "$(dirname "$LOGFILE")"
"$@" >>"$LOGFILE" 2>&1 &
CHILD=$!

# Wall time from the clock, not from the poll count: a loaded host stretches each
# poll iteration (sleep + the ps/stat forks) well past POLL_S, and a counted
# elapsed then fires the timeout minutes late (observed: 1300 s -> 41+ min).
started=$(date +%s)
elapsed=0
stalled=0
last_size=""
kill_with() {  # $1 = reason, $2 = exit code
  echo "[run_bounded] $1 -- killing pid $CHILD" >>"$LOGFILE"
  kill -9 "$CHILD" 2>/dev/null
  wait "$CHILD" 2>/dev/null
  exit "$2"
}

while kill -0 "$CHILD" 2>/dev/null; do
  sleep "$POLL_S"
  elapsed=$(( $(date +%s) - started ))
  rss_kb="$(ps -o rss= -p "$CHILD" 2>/dev/null | tr -d ' ')"
  if [ -n "$rss_kb" ] && [ "$rss_kb" -gt "$RSS_LIMIT_KB" ]; then
    kill_with "RSS ${rss_kb} KB > ${RSS_LIMIT_KB} KB" 137
  fi
  if [ -n "$PROGRESS_FILE" ]; then
    size="$(stat -f %z "$PROGRESS_FILE" 2>/dev/null || stat -c %s "$PROGRESS_FILE" 2>/dev/null || echo -1)"
    if [ "$size" != "$last_size" ]; then
      last_size="$size"; stalled=0
    else
      stalled=$((stalled + POLL_S))
      [ "$stalled" -ge "$STALL_LIMIT_S" ] && kill_with "no growth of $PROGRESS_FILE for ${stalled}s" 125
    fi
  fi
  [ "$elapsed" -ge "$TIMEOUT_S" ] && kill_with "wall timeout ${TIMEOUT_S}s" 124
done
wait "$CHILD"
exit $?

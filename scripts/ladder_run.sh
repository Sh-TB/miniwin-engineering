#!/bin/bash
# ladder_run.sh — standardized 3x evidence run for PROGRAM_LADDER rungs.
#
# Usage:
#   scripts/ladder_run.sh <LADDER-ID> <phase-label> -- <command...>
#
# Example:
#   scripts/ladder_run.sh LADDER-008 version -- \
#     ./minwin_loader samples/real/zstd/zstd-v1.5.6-win64/zstd.exe --version
#
# For each of 3 runs it records: command, exit code, stdout, stderr, and
# (if produced) the loader api_trace.json. It also hashes the target exe and
# every artifact left in the scratch workdir. Evidence dir:
#   evidence/master/ladder/<ID>_<ts>/
#
# Exit: 0 if all 3 runs produced the SAME exit code (consistency check),
#       1 otherwise or on usage error. Note: exit-code equality across runs is
#       enforced, but whether that code is "success" is judged by the caller
#       (status vocabulary stays honest: RUNS-PARTIAL vs RUNS-3x vs VERIFIED).

set -uo pipefail

REPO="${LADDER_REPO:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$REPO" || exit 1

if [ $# -lt 3 ]; then
    echo "usage: ladder_run.sh <LADDER-ID> <phase-label> -- <command...>" >&2
    exit 1
fi
ID="$1"; shift
PHASE="$1"; shift
[ "$1" = "--" ] && shift
if [ $# -eq 0 ]; then
    echo "usage: ladder_run.sh <LADDER-ID> <phase-label> -- <command...>" >&2
    exit 1
fi

TS=$(date -u +%Y%m%d_%H%M%S)
EV="$REPO/evidence/master/ladder/${ID}_${PHASE}_${TS}"
WORK=$(mktemp -d /tmp/ladder_${ID}_XXXXXX)
mkdir -p "$EV"

CMD="$*"
printf 'command=%s\nphase=%s\nworkdir=%s\ntimestamp_utc=%s\n' \
    "$CMD" "$PHASE" "$WORK" "$TS" > "$EV/command.txt"

# Resolve arguments: the run subshell starts in $WORK (scratch dir), so any
# argument that refers to an EXISTING path (seen from REPO) must be made
# absolute. Non-existent args (output files, flags, values) stay as-is so
# outputs are created inside $WORK and get hashed as round-trip artifacts.
ARGS=("$@")
for j in "${!ARGS[@]}"; do
    a="${ARGS[$j]}"
    case "$a" in
        -*) continue ;;
        /*) continue ;;
        *)  if [ -e "$REPO/$a" ]; then ARGS[$j]="$REPO/$a"; fi ;;
    esac
done
set -- "${ARGS[@]}"
TARGET="$1"
if [ -f "$TARGET" ]; then
    sha256sum "$TARGET" > "$EV/exe.sha256"
fi

for i in 1 2 3; do
    ( cd "$WORK" && REPO_ABS="$REPO" "$@" \
        > "$EV/r${i}.out" 2> "$EV/r${i}.err" ; echo $? > "$EV/r${i}.exit" )
    # api_trace.json is written by the loader into miniwin-results/<exe>/
    EXE_BASE=$(basename "$1" | sed 's/[":*?<>|]/_/g')
    TRACE="miniwin-results/${EXE_BASE}/api_trace.json"
    if [ -f "$TRACE" ]; then
        cp "$TRACE" "$EV/r${i}_api_trace.json" 2>/dev/null || true
    fi
done

# Artifacts produced in the scratch workdir (round-trip outputs etc.),
# plus a listing for the report. Includes dotfiles at top level only,
# recursion handled by find.
( cd "$WORK" && find . -type f -print0 | sort -z | xargs -0 -r sha256sum \
    > "$EV/artifacts.sha256" )
( cd "$WORK" && ls -la > "$EV/workdir.ls" )
cp "$EV/artifacts.sha256" "$WORK/.ladder_artifacts.sha256" 2>/dev/null || true

E1=$(cat "$EV/r1.exit" 2>/dev/null || echo MISSING)
E2=$(cat "$EV/r2.exit" 2>/dev/null || echo MISSING)
E3=$(cat "$EV/r3.exit" 2>/dev/null || echo MISSING)

printf 'LADDER %s phase=%s  exit codes: r1=%s r2=%s r3=%s\n' \
    "$ID" "$PHASE" "$E1" "$E2" "$E3"
if [ "$E1" = "$E2" ] && [ "$E2" = "$E3" ]; then
    printf 'consistency: 3/3 SAME exit code\n'
    printf 'evidence: %s\n' "$EV"
    exit 0
else
    printf 'consistency: DIVERGENT exit codes\n'
    printf 'evidence: %s\n' "$EV"
    exit 1
fi

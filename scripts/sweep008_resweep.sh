#!/bin/bash
# SWEEP-008: full re-sweep of the 74-program corpus after the named-stub
# batch (SW-555..561) + SWEEP-008b data-driven ring. Per program: exit code,
# named-stub call count, unresolved import count, wall seconds, and a delta
# column vs the SWEEP-50 baseline summary.tsv.
#
# Usage: scripts/sweep008_resweep.sh   (writes artifacts/sweep50/runs_stubfix/sweep008_summary.tsv)

set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
RUNS="artifacts/sweep50/runs"
OUT="artifacts/sweep50/runs_stubfix"
mkdir -p "$OUT"
TSV="$OUT/sweep008_summary.tsv"
BASE="artifacts/sweep50/runs/summary.tsv"

echo -e "name\texe\texit\tstubs\tunresolved\tsecs\tbaseline_exit" > "$TSV"

for d in "$RUNS"/*/; do
    tag="$(basename "$d")"
    [ -f "$d/meta.txt" ] || continue
    exe_rel="$(sed -n 's/^exe=//p' "$d/meta.txt")"
    exe="$REPO/artifacts/sweep50/dl/$exe_rel"
    [ -f "$exe" ] || { echo -e "$tag\t$exe_rel\tMISSING\t-\t-\t-\t-" >> "$TSV"; continue; }

    start=$(date +%s.%N)
    timeout 15 ./minwin_loader "$exe" > "$OUT/$tag.stdout" 2> "$OUT/$tag.stderr"
    ec=$?
    end=$(date +%s.%N)
    secs=$(echo "$end $start" | awk '{printf "%.1f", $1-$2}')
    stubs=$(grep -c "STUB\]" "$OUT/$tag.stderr" 2>/dev/null || true)
    unres=$(grep -c "unresolved:" "$OUT/$tag.stderr" 2>/dev/null || true)
    bexit=$(awk -F'\t' -v t="$tag" '$1==t{print $2}' "$BASE" 2>/dev/null)
    echo -e "$tag\t$exe_rel\t$ec\t$stubs\t$unres\t$secs\t${bexit:--}" >> "$TSV"
done

echo "== SWEEP-008 resweep complete: $TSV ="
echo "== improved (baseline>0 -> 0, or big delta) =="
awk -F'\t' 'NR>1 && $7!="-" && $3!=$7 {print $1": "$7" -> "$3}' "$TSV"

#!/bin/bash
# SWEEP-016 broad sweep: every program under artifacts/sweep50/dl/* x3,
# exe auto-resolved inside each program dir (BUG-072 lesson: never pass
# the directory itself). Records exit / stubs / unresolved / output size.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DL="$REPO/artifacts/sweep50/dl"
OUT="$REPO/artifacts/sweep016/sweep"
mkdir -p "$OUT"
TSV="$OUT/sweep016_sweep.tsv"
echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$TSV"

for dir in "$DL"/*/; do
    pname=$(basename "$dir")
    exe=""
    for cand in "$dir"*.exe; do
        [ -f "$cand" ] && exe="$cand" && break
    done
    [ -z "$exe" ] && { echo -e "$pname\tNO-EXE\t-\t-\t-\t-\t-\t-" >> "$TSV"; continue; }
    ename=$(basename "$exe")
    for run in 1 2 3; do
        d="$OUT/$pname/run$run"
        mkdir -p "$d"
        ( cd "$d" && MW_GUI_EVENTS=1 timeout 25 "$REPO/minwin_loader" "$exe" \
            >stdout.txt 2>stderr.txt )
        code=$?
        stubs=$(grep -c '\[STUB' "$d/stderr.txt" 2>/dev/null || echo 0)
        unres=$(grep -c 'unresolved:' "$d/stderr.txt" 2>/dev/null || echo 0)
        so=$(wc -c < "$d/stdout.txt" 2>/dev/null || echo 0)
        se=$(wc -c < "$d/stderr.txt" 2>/dev/null || echo 0)
        echo -e "$pname\t$ename\t$run\t$code\t$stubs\t$unres\t$so\t$se" >> "$TSV"
    done
done
column -t "$TSV"

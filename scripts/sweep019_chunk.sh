#!/bin/bash
# SWEEP-019 chunk runner: sweep ONE program x3 runs (pump-off) + append TSV.
# Usage: sweep019_chunk.sh <program_dir_name> [<more names>...]
# Chunked foreground execution: the 3 GB box kills big background batches;
# per-program chunks stay well inside tool time limits.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DL="$REPO/artifacts/sweep50/dl"
OUT="$REPO/artifacts/sweep019"
mkdir -p "$OUT/sweep"
TSV="$OUT/sweep/sweep019_sweep.tsv"
[ -f "$TSV" ] || echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$TSV"

for pname in "$@"; do
    dir="$DL/$pname/"
    exe=""
    for cand in "$dir"*.exe; do
        [ -f "$cand" ] && exe="$cand" && break
    done
    if [ -z "$exe" ]; then
        echo -e "$pname\tNO-EXE\t-\t-\t-\t-\t-\t-" >> "$TSV"
        continue
    fi
    ename=$(basename "$exe")
    for run in 1 2 3; do
        d="$OUT/sweep/$pname/run$run"
        mkdir -p "$d"
        ( cd "$d" && MW_GUI_EVENTS=0 timeout 15 "$REPO/minwin_loader" "$exe" \
            >stdout.txt 2>stderr.txt )
        code=$?
        stubs=$(grep -c '\[STUB' "$d/stderr.txt" 2>/dev/null); stubs=${stubs:-0}
        unres=$(grep -c 'unresolved:' "$d/stderr.txt" 2>/dev/null); unres=${unres:-0}
        so=$(wc -c < "$d/stdout.txt" 2>/dev/null || echo 0)
        se=$(wc -c < "$d/stderr.txt" 2>/dev/null || echo 0)
        echo -e "$pname\t$ename\t$run\t$code\t$stubs\t$unres\t$so\t$se" >> "$TSV"
    done
done
echo "chunk done: $*"

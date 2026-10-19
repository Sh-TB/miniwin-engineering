#!/bin/bash
# SWEEP-019 broad sweep: every program under artifacts/sweep50/dl/* x3 (pump-off,
# BUG-072 lesson: exe auto-resolve, never the dir) + produkey pump-on acceptance
# (SW-816..820 menu family) + GUI battery (calc/snake/breakout).
# Records exit / stubs / unresolved / stdout+stderr bytes per run.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DL="$REPO/artifacts/sweep50/dl"
OUT="$REPO/artifacts/sweep019"
mkdir -p "$OUT/sweep"
TSV="$OUT/sweep/sweep019_sweep.tsv"
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
        d="$OUT/sweep/$pname/run$run"
        mkdir -p "$d"
        ( cd "$d" && MW_GUI_EVENTS=0 timeout 25 "$REPO/minwin_loader" "$exe" \
            >stdout.txt 2>stderr.txt )
        code=$?
        stubs=$(grep -c '\[STUB' "$d/stderr.txt" 2>/dev/null); stubs=${stubs:-0}
        unres=$(grep -c 'unresolved:' "$d/stderr.txt" 2>/dev/null); unres=${unres:-0}
        so=$(wc -c < "$d/stdout.txt" 2>/dev/null || echo 0)
        se=$(wc -c < "$d/stderr.txt" 2>/dev/null || echo 0)
        echo -e "$pname\t$ename\t$run\t$code\t$stubs\t$unres\t$so\t$se" >> "$TSV"
    done
done

# produkey pump-on acceptance (menu family SW-816..820: exit0 WITH pump)
d="$OUT/produkey_pumpon/run1"
mkdir -p "$d"
exe=$(ls "$DL"/produkey/*.exe 2>/dev/null | head -1)
if [ -n "$exe" ]; then
    ( cd "$d" && MW_GUI_EVENTS=1 timeout 25 "$REPO/minwin_loader" "$exe" \
        >stdout.txt 2>stderr.txt )
    code=$?
    stubs=$(grep -c '\[STUB' "$d/stderr.txt" 2>/dev/null); stubs=${stubs:-0}
    unres=$(grep -c 'unresolved:' "$d/stderr.txt" 2>/dev/null); unres=${unres:-0}
    so=$(wc -c < "$d/stdout.txt" 2>/dev/null || echo 0)
    se=$(wc -c < "$d/stderr.txt" 2>/dev/null || echo 0)
    echo -e "produkey-pumpon\t$(basename "$exe")\t1\t$code\t$stubs\t$unres\t$so\t$se" >> "$TSV"
fi

# GUI battery: calc / snake / breakout x1 (determinism re-check on run2/3)
for g in calc snake breakout; do
    exe=""
    for cand in "$REPO/samples/$g"*.exe "$REPO/miniwin-results/$g.exe" \
                "$REPO/samples/$g/$g.exe"; do
        [ -f "$cand" ] && exe="$cand" && break
    done
    [ -z "$exe" ] && { echo -e "GUI-$g\tNOT-FOUND\t-\t-\t-\t-\t-\t-" >> "$TSV"; continue; }
    gname=$(basename "$exe")
    for run in 1 2 3; do
        d="$OUT/gui_$g/run$run"
        mkdir -p "$d"
        ( cd "$d" && MW_GUI_EVENTS=1 timeout 25 "$REPO/minwin_loader" "$exe" \
            >stdout.txt 2>stderr.txt )
        code=$?
        stubs=$(grep -c '\[STUB' "$d/stderr.txt" 2>/dev/null); stubs=${stubs:-0}
        unres=$(grep -c 'unresolved:' "$d/stderr.txt" 2>/dev/null); unres=${unres:-0}
        so=$(wc -c < "$d/stdout.txt" 2>/dev/null || echo 0)
        se=$(wc -c < "$d/stderr.txt" 2>/dev/null || echo 0)
        echo -e "GUI-$g\t$gname\t$run\t$code\t$stubs\t$unres\t$so\t$se" >> "$TSV"
    done
done

column -t "$TSV"

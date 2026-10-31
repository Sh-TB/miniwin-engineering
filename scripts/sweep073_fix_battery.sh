#!/bin/bash
# SWEEP-073 — post-fix regression battery (fixes SW-1316/_onexit+_initterm width,
# SW-1317 accel storage, SW-1318 key state, SW-1319 0F BA+shld/shrd, __p___initenv,
# SW-1320 named GetProcAddress thunks).
# Parity targets: the input-sensitive games + XP apps whose baselines are EXACT
# (winmine 82 / sol 38 / freecell 10 / calc 1 / breakout 48 / snake / notepad rc=0)
# plus the fixed guests (7zr rc=0, spider accel table, winmine accel table).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
G61=artifacts/sweep061/guests
EV=evidence/sweep073
mkdir -p "$EV"

GUESTS="winmine:300:$G61/winmine/winmine.exe
sol:300:$G61/sol/sol.exe
freecell:300:$G61/freecell/freecell.exe
spider:800:$G61/spider/spider.exe
calc:120:$G61/calc/calc.exe
breakout:120:artifacts/sweep006/breakout.exe
snake:120:artifacts/sweep005/snake.exe
notepad:120:$G61/notepad/notepad.exe
wordpad:120:$G61/wordpad/wordpad.exe
mspaint:150:$G61/mspaint/mspaint.exe"

echo "$GUESTS" | xargs -P 5 -I{} bash -c '
    set -u; cd /home/z/my-project/workspace/miniwin-engineering || exit 1
    IFS=: read -r p b e <<< "{}"
    [ -f "$e" ] || { echo "$p MISSING $e"; exit 0; }
    rm -rf "miniwin-results/$(basename "$e")"
    START=$(date +%s.%N)
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$b" timeout "$b" ./minwin_loader "$e" > "/tmp/sw73_$p.log" 2>&1
    RC=$?
    END=$(date +%s.%N)
    FRAMES=$(ls miniwin-results/"$(basename "$e")"/screenshots/ 2>/dev/null | wc -l)
    STEPS=$(grep -o "steps=[0-9]*" "/tmp/sw73_$p.log" | tail -1 | cut -d= -f2)
    CRASH=$(grep -c "\[CRASH\]" "/tmp/sw73_$p.log")
    DUR=$(echo "$END $START" | awk "{printf \"%.1f\", \$1-\$2}")
    echo -e "$p\trc=$RC\tframes=$FRAMES\tsteps=${STEPS:-?}\tcrash=$CRASH\t${DUR}s"
'
echo "=== battery done ==="

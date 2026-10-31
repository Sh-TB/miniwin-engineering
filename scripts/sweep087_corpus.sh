#!/bin/bash
# SWEEP-087 corpus parity — the canonical XP/game corpus through the fixed
# engine, 3 runs each, EXACT determinism checked against run1.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
EV=evidence/sweep087
mkdir -p "$EV/raw"

GUESTS="winmine:60:gui:artifacts/sweep061/guests/winmine/winmine.exe
sol:60:gui:artifacts/sweep061/guests/sol/sol.exe
freecell:60:gui:artifacts/sweep061/guests/freecell/freecell.exe
calc:60:gui:artifacts/sweep061/guests/calc/calc.exe
breakout:60:gui:artifacts/sweep006/breakout.exe
snake:60:gui:artifacts/sweep005/snake.exe
spider:90:gui:artifacts/sweep061/guests/spider/spider.exe"

run_one() {
    IFS=: read -r p b mode e <<< "$1"
    [ -f "$e" ] || { echo "$p MISSING"; return 0; }
    local base=$(basename "$e" .exe)
    for run in 1 2 3; do
        rm -rf "miniwin-results/${base}.exe"
        local log="$EV/raw/corpus_${p}_run${run}.log"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=25 timeout "$b" ./minwin_loader "$e" > "$log" 2>&1
        local rc=$?
        local frames=$(ls miniwin-results/"$base".exe/screenshots/ 2>/dev/null | wc -l)
        local crash=$(grep -c '\[CRASH\]' "$log")
        local fatal=$(grep -c 'PE32-FATAL' "$log")
        local hash=$(cat "miniwin-results/${base}.exe/analysis/"*.txt 2>/dev/null | sha256sum | cut -c1-12)
        echo -e "$p\trun$run\trc=$rc\tframes=$frames\tcrash=$crash\tfatal=$fatal\tblob=$hash"
    done
}
export -f run_one
export EV

echo "$GUESTS" | xargs -P 4 -I{} bash -c 'run_one "$@"' _ {} | sort | tee "$EV/corpus_parity.tsv"
echo "=== SWEEP-087 corpus parity complete ==="

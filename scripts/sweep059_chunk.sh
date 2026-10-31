#!/bin/bash
# chunked battery runner: sweep059_chunk.sh <start> <end> (1-based, over guests dirs)
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
G=artifacts/sweep059/guests
SHOTS=artifacts/sweep059/dl_shots
PNG=evidence/sweep059/dl_shots_png
CEN=artifacts/sweep059/census_dl.tsv
[ -f "$CEN" ] || echo -e "guest\tarch\tbytes\trc\tframes\tshot" > "$CEN"
S=$1; E=$2
i=0
for gd in "$G"/*/; do
    i=$((i+1))
    [ $i -lt $S ] && continue
    [ $i -gt $E ] && break
    gdname=$(basename "$gd")
    grep -q "^$gdname	" "$CEN" 2>/dev/null && { echo "SKIP $gdname"; continue; }
    exe=$(ls "$gd"/*.exe 2>/dev/null | head -1)
    [ -z "$exe" ] && continue
    name=$(basename "$exe" .exe)
    arch=$(file -b "$exe" | rg -o "x86-64|x86" | head -1)
    bytes=$(stat -c%s "$exe")
    rm -rf "miniwin-results/$name.exe"
    env MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=150 MINWIN_TRACE_MAX_LINES=200000 \
        timeout 30 ./minwin_loader "$exe" > "$SHOTS/${name}_stdout.txt" 2> "$SHOTS/${name}_stderr.txt"
    rc=$?
    frames=$(ls "miniwin-results/$name.exe/screenshots/"*.bmp 2>/dev/null | wc -l)
    shot="-"
    last=$(ls "miniwin-results/$name.exe/screenshots/"frame_*.bmp 2>/dev/null | sort | tail -1)
    if [ -n "$last" ]; then
        python3 - "$last" "$PNG/${name}.png" <<'PYEOF' 2>/dev/null && shot=saved
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
PYEOF
    fi
    mb=$(ls "miniwin-results/$name.exe/screenshots/"messagebox_1.bmp 2>/dev/null)
    [ -n "$mb" ] && python3 - "$mb" "$PNG/${name}_dialog.png" <<'PYEOF' 2>/dev/null
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
PYEOF
    echo -e "$name\t$arch\t$bytes\t$rc\t$frames\t$shot" >> "$CEN"
    echo "$name rc=$rc frames=$frames"
done

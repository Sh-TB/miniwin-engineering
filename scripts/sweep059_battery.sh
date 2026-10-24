#!/bin/bash
# SWEEP-059 Trains 4-6 — run the whole downloaded fleet under miniwin.
# Per guest: MW_GUI_EVENTS pump, harvest screenshots, census row.
# Mandate: «۲۰ تا ۵۰ تا برنامه رایگان رو دانلود و تست بکن تا ببینم واقعا چقدر
# پیشرفت داشتیم» + screenshot evidence for every run.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
G=artifacts/sweep059/guests
SHOTS=artifacts/sweep059/dl_shots
PNG=evidence/sweep059/dl_shots_png
CEN=artifacts/sweep059/census_dl.tsv
mkdir -p "$SHOTS" "$PNG"

echo -e "guest\tarch\tbytes\trc\tframes\tshot" > "$CEN"

run_one() {
    local gd="$1"
    local exe=$(ls "$gd"/*.exe 2>/dev/null | head -1)
    [ -z "$exe" ] && return
    local name=$(basename "$exe" .exe)
    local arch=$(file -b "$exe" | rg -o "x86-64|x86" | head -1)
    local bytes=$(stat -c%s "$exe")
    rm -rf "miniwin-results/$name.exe"
    env MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=200 MINWIN_TRACE_MAX_LINES=200000 \
        timeout 45 ./minwin_loader "$exe" \
        > "$SHOTS/${name}_stdout.txt" 2> "$SHOTS/${name}_stderr.txt"
    local rc=$?
    local frames=$(ls "miniwin-results/$name.exe/screenshots/"*.bmp 2>/dev/null | wc -l)
    local shot="-"
    if [ "$frames" -gt 0 ]; then
        local last=$(ls "miniwin-results/$name.exe/screenshots/"frame_*.bmp 2>/dev/null | sort | tail -1)
        if [ -n "$last" ]; then
            python3 - "$last" "$PNG/${name}.png" <<'EOF' 2>/dev/null
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
shot="saved"
EOF
            shot="saved"
        fi
        # dialogs too (GUI tools often show About/message boxes)
        local mb=$(ls "miniwin-results/$name.exe/screenshots/"messagebox_1.bmp 2>/dev/null)
        [ -n "$mb" ] && python3 - "$mb" "$PNG/${name}_dialog.png" <<'EOF' 2>/dev/null
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
EOF
    fi
    echo -e "$name\t$arch\t$bytes\t$rc\t$frames\t$shot" >> "$CEN"
    echo "$name rc=$rc frames=$frames"
}

for gd in "$G"/*/; do
    run_one "$gd"
done

echo "=== census ==="
column -t -s$'\t' "$CEN" | head -100

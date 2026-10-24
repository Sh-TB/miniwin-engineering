#!/bin/bash
# SWEEP-059 Train 2 — regenerate the XP battery screenshots on top of the
# SW-1280 palette-DIB engine fix. The user's mandate:
#   «بازی بمب رو کامل لود نمی کند گرافیک مشکل دارد بمب ها و سایر موارد کامل نیست»
#   + standing «اسکرین شات اجرای برنامه ها رو هم نشون بده»
# winmine now renders tiles/LED/faces; interactive clicks reveal bombs+numbers.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
SHOTS=artifacts/sweep059/shots
PNG=evidence/sweep059/shots_png
mkdir -p "$SHOTS" "$PNG"

run_gui() {
    local d="$1" prog="$2" extra_env="$3" budget="${4:-300}"
    rm -rf "miniwin-results/$prog.exe"
    env MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$budget" $extra_env \
        timeout 60 ./minwin_loader "$d$prog.exe" \
        > "$SHOTS/${prog}_stdout.txt" 2> "$SHOTS/${prog}_stderr.txt"
    local rc=$?
    local nf=$(ls miniwin-results/$prog.exe/screenshots/*.bmp 2>/dev/null | wc -l)
    echo "$prog rc=$rc frames=$nf"
}

harvest() {
    local prog="$1" label="$2"
    local last=$(ls miniwin-results/$prog.exe/screenshots/frame_*.bmp 2>/dev/null | sort | tail -1)
    [ -n "$last" ] && python3 - "$last" "$PNG/$label.png" <<'EOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
EOF
    local mb=$(ls miniwin-results/$prog.exe/screenshots/messagebox_1.bmp 2>/dev/null)
    [ -n "$mb" ] && python3 - "$mb" "$PNG/${label}_dialog.png" <<'EOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
EOF
}

# ---- interactive minesweeper: clicks reveal bombs/numbers (SW-1280 proof) ----
run_gui "artifacts/sweep056/guests/winmine/" winmine \
    "MINWIN_MOUSE='20,62;50,62;80,62;110,62;140,62;20,80;50,80;80,80'" 400
harvest winmine "01_xp_minesweeper_bombs_revealed"
# fresh non-interactive frame = pristine startup board
run_gui "artifacts/sweep056/guests/winmine/" winmine2 "" 120
harvest winmine2 "02_xp_minesweeper_startup"

# ---- the rest of the classic 2D battery ----
run_gui "artifacts/sweep056/guests/sol/"      sol      "" 300
harvest sol "03_xp_solitaire"
run_gui "artifacts/sweep056/guests/freecell/" freecell "" 300
harvest freecell "04_xp_freecell"
run_gui "artifacts/sweep056/guests/mshearts/" mshearts "" 300
harvest mshearts "05_xp_mshearts"
run_gui "artifacts/sweep056/guests/spider/"   spider   "" 400
harvest spider "06_xp_spider"

# ---- XP applets ----
run_gui "artifacts/sweep056/guests/calc/"     calc     "" 300
harvest calc "07_xp_calc"
run_gui "artifacts/sweep056/guests/mspaint/"  mspaint  "" 300
harvest mspaint "08_xp_mspaint"
run_gui "artifacts/sweep056/guests/wordpad/"  wordpad  "" 300
harvest wordpad "09_xp_wordpad"
run_gui "artifacts/sweep056/guests/notepad/"  notepad  "MW_GUI_KEYS='hello miniwin'" 300
harvest notepad "10_xp_notepad"
run_gui "artifacts/sweep056/guests/charmap/"  charmap  "" 300
harvest charmap "11_xp_charmap"
run_gui "artifacts/sweep056/guests/winver/"   winver   "" 300
harvest winver "12_xp_winver"
run_gui "artifacts/sweep056/guests/sndvol32/" sndvol32 "" 300
harvest sndvol32 "13_xp_sndvol32"

echo "=== PNGs ==="
ls -la "$PNG"

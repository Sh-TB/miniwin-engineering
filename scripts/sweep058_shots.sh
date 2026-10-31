#!/bin/bash
# SWEEP-058 screenshot battery — the user's standing mandate:
# (continue: also show screenshots of the programs running)
# GUI-pump runs with events on; harvest frame_*.bmp / messagebox_*.bmp and
# convert to PNG into evidence/sweep058/shots_png/.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
SHOTS=artifacts/sweep058/shots
PNG=evidence/sweep058/shots_png
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
    local prog="$1" label="$2" n="${3:-1}"
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

# ---- 5 classic 2D games (XP era) ----
run_gui "artifacts/sweep056/guests/winmine/"  winmine  "MINWIN_MOUSE='300,180;350,220'" 300
harvest winmine "01_xp_minesweeper"
run_gui "artifacts/sweep056/guests/sol/"      sol      "" 300
harvest sol "02_xp_solitaire"
run_gui "artifacts/sweep056/guests/freecell/" freecell "" 300
harvest freecell "03_xp_freecell"
run_gui "artifacts/sweep056/guests/mshearts/" mshearts "" 300
harvest mshearts "04_xp_mshearts"
run_gui "artifacts/sweep056/guests/spider/"   spider   "" 400
harvest spider "05_xp_spider"

# ---- XP applets ----
run_gui "artifacts/sweep056/guests/calc/"     calc     "" 300
harvest calc "06_xp_calc"
run_gui "artifacts/sweep056/guests/mspaint/"  mspaint  "" 300
harvest mspaint "07_xp_mspaint"
run_gui "artifacts/sweep056/guests/wordpad/"  wordpad  "" 300
harvest wordpad "08_xp_wordpad"
run_gui "artifacts/sweep056/guests/notepad/"  notepad  "MW_GUI_KEYS='hello miniwin'" 300
harvest notepad "09_xp_notepad"
run_gui "artifacts/sweep056/guests/charmap/"  charmap  "" 300
harvest charmap "10_xp_charmap"
run_gui "artifacts/sweep056/guests/winver/"   winver   "" 300
harvest winver "11_xp_winver"
run_gui "artifacts/sweep056/guests/sndvol32/" sndvol32 "" 300
harvest sndvol32 "12_xp_sndvol32"

# ---- autologon64: the SWEEP-058 SW-1241 seeded-Winlogon dialog ----
rm -rf miniwin-results/autologon64.exe
MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=150 timeout 60 ./minwin_loader \
    artifacts/sweep047/bin/autologon64.exe > "$SHOTS/autologon64_stdout.txt" 2> "$SHOTS/autologon64_stderr.txt"
echo "autologon64 rc=$?"
harvest autologon64 "13_sysinternals_autologon"

echo "=== PNGs ==="
ls -la "$PNG"

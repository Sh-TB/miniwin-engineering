#!/bin/bash
# SWEEP-056 SCREENSHOT battery — the standing mandate:
# (continue: also show screenshots of the programs running) (show screenshots of the
# programs running). This round's engine work: SW-1277 per-module resources +
# GetObject HBITMAP family -> sol's REAL 585x384 board from cards.dll metrics.
# Guests: the re-staged SWEEP-056 XP farm (14 applets/games).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
SHOTS=artifacts/sweep056/shots
PNG=evidence/sweep056/shots_png
mkdir -p "$SHOTS" "$PNG"

run_gui() {
    local d="$1" prog="$2" extra_env="$3" budget="${4:-250}"
    rm -rf "miniwin-results/$prog.exe"
    env MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$budget" $extra_env \
        timeout 60 ./minwin_loader "$d$prog.exe" \
        > "$SHOTS/${prog}_stdout.txt" 2> "$SHOTS/${prog}_stderr.txt"
    echo "$prog rc=$?"
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

G=artifacts/sweep056/guests
# ---- games (mouse interaction where the battery expects it) ----
run_gui "$G/winmine/"    winmine    "MINWIN_MOUSE='300,180;350,220'" 250
harvest winmine "01_xp_minesweeper"
run_gui "$G/sol/"        sol        "" 250
harvest sol "02_xp_solitaire_REAL_BOARD"
run_gui "$G/freecell/"   freecell   "" 250
harvest freecell "03_xp_freecell"
run_gui "$G/mshearts/"   mshearts   "" 250
harvest mshearts "04_xp_mshearts"
run_gui "$G/spider/"     spider     "MINWIN_FRAME_EVERY=300" 400
harvest spider "05_xp_spider"
# ---- XP applets ----
run_gui "$G/calc/"       calc       "MINWIN_MOUSE='120,220;160,220;200,220'" 250
harvest calc "06_xp_calc"
run_gui "$G/mspaint/"    mspaint    "" 250
harvest mspaint "07_xp_mspaint"
run_gui "$G/wordpad/"    wordpad    "" 250
harvest wordpad "08_xp_wordpad"
run_gui "$G/charmap/"    charmap    "" 200
harvest charmap "09_xp_charmap"
run_gui "$G/clipbrd/"    clipbrd    "" 200
harvest clipbrd "10_xp_clipbrd"
run_gui "$G/write/"      write      "" 200
harvest write "11_xp_write"
run_gui "$G/winver/"     winver     "" 200
harvest winver "12_xp_winver"
run_gui "$G/sndvol32/"   sndvol32   "" 200
harvest sndvol32 "13_xp_sndvol32"
run_gui "$G/notepad/"    notepad    "" 200
harvest notepad "14_xp_notepad"

echo "=== PNGs ==="
ls -la "$PNG"

#!/bin/bash
# SWEEP-057 SCREENSHOT battery — (also show screenshots of the programs running)
# Engine work this round: SW-1279 fix chain A-F (WS_CHILD top-level leak,
# class-proc steal, BeginPaint white wipe, per-child rects, timer-idle wait)
# -> sol.exe renders the CLASSIC BOARD: green felt + deck fan + status bar,
# 120 WM_TIMER ticks live. Guests: the XP farm (14).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
SHOTS=artifacts/sweep057/shots
PNG=evidence/sweep057/shots_png
mkdir -p "$SHOTS" "$PNG"

run_gui() {
    local d="$1" prog="$2" extra_env="$3" budget="${4:-150}" to="${5:-60}"
    rm -rf "miniwin-results/$prog.exe"
    env MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$budget" $extra_env \
        timeout "$to" ./minwin_loader "$d$prog.exe" \
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
}

G=artifacts/sweep056/guests
# ---- games (mouse interaction where the battery expects it) ----
run_gui "$G/winmine/"    winmine    "MINWIN_MOUSE='300,180;350,220'" 250 60
harvest winmine 01_xp_minesweeper
# sol: budget 120 (fix F keeps the pump alive on timer ticks: 120 x 250ms
# tick pacing fits the 90s cap); deck click flips a card to the waste pile
run_gui "$G/sol/"        sol        "MINWIN_MOUSE='35,60'"           120 90
harvest sol     02_xp_solitaire
run_gui "$G/freecell/"   freecell   ""                               150 60
harvest freecell 03_xp_freecell
run_gui "$G/mshearts/"   mshearts   ""                               150 60
harvest mshearts 04_xp_mshearts
run_gui "$G/spider/"     spider     "MINWIN_MOUSE='200,120;300,300'" 150 60
harvest spider  05_xp_spider
# ---- XP applets ----
run_gui "$G/calc/"       calc       ""                               150 60
harvest calc    06_xp_calc
run_gui "$G/notepad/"    notepad    ""                               150 60
harvest notepad 07_xp_notepad
run_gui "$G/mspaint/"    mspaint    ""                               150 60
harvest mspaint 08_xp_mspaint
run_gui "$G/wordpad/"    wordpad    ""                               150 60
harvest wordpad 09_xp_wordpad
run_gui "$G/charmap/"    charmap    ""                               150 60
harvest charmap 10_xp_charmap
run_gui "$G/clipbrd/"    clipbrd    ""                               150 60
harvest clipbrd 11_xp_clipbrd
run_gui "$G/write/"      write      ""                               150 60
harvest write   12_xp_write
run_gui "$G/winver/"     winver     ""                               150 60
harvest winver  13_xp_winver
run_gui "$G/sndvol32/"   sndvol32   ""                               150 60
harvest sndvol32 14_xp_sndvol32
echo "=== shots done ==="; ls -la "$PNG" | tail -16

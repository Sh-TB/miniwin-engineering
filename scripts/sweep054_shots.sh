#!/bin/bash
# SWEEP-054 SCREENSHOT battery — the user's standing mandate:
# (continue: also show screenshots of the programs running) (show screenshots of the
# programs running) + this round's (10 famous programs and 5 famous old 2D games)
# (10 famous programs + 5 famous old 2D games, XP-era + new).
#
# For each guest: 1 GUI-pump run with events on, harvest frame_NNN.bmp /
# messagebox_NNN.bmp, convert to PNG into evidence/sweep054/shots_png/.
# Interactive guests get scripted mouse/keys per the SWEEP-052/053 channels.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
SHOTS=artifacts/sweep054/shots
PNG=evidence/sweep054/shots_png
mkdir -p "$SHOTS" "$PNG"

run_gui() {
    local d="$1" prog="$2" extra_env="$3" budget="${4:-300}"
    rm -rf "miniwin-results/$prog.exe"
    env MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$budget" $extra_env \
        timeout 60 ./minwin_loader "$d$prog.exe" \
        > "$SHOTS/${prog}_stdout.txt" 2> "$SHOTS/${prog}_stderr.txt"
    local rc=$?
    local nf=$(ls miniwin-results/$prog.exe/screenshots/*.bmp 2>/dev/null | wc -l)
    local mb=$(ls miniwin-results/$prog.exe/screenshots/messagebox*.bmp 2>/dev/null | wc -l)
    echo "$prog rc=$rc frames=$nf msgboxes=$mb"
}

harvest() {
    local prog="$1" label="$2"
    local last=$(ls miniwin-results/$prog.exe/screenshots/frame_*.bmp 2>/dev/null | sort | tail -1)
    [ -n "$last" ] && python3 - "$last" "$PNG/$label.png" <<'EOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
EOF
    # first message box too (Win95 chrome dialogs are evidence of live UI)
    local mb=$(ls miniwin-results/$prog.exe/screenshots/messagebox_1.bmp 2>/dev/null)
    [ -n "$mb" ] && python3 - "$mb" "$PNG/${label}_dialog.png" <<'EOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
EOF
}

# ---- 5 classic 2D games (XP era) ----
run_gui "artifacts/sweep054/guests/winmine/"    winmine    "MINWIN_MOUSE='300,180;350,220'" 300
harvest winmine "01_xp_minesweeper"
run_gui "artifacts/sweep054/guests/sol/"        sol        "" 300
harvest sol "02_xp_solitaire"
run_gui "artifacts/sweep054/guests/freecell/"   freecell   "" 300
harvest freecell "03_xp_freecell"
run_gui "artifacts/sweep054/guests/mshearts/"   mshearts   "" 300
harvest mshearts "04_xp_mshearts"
run_gui "artifacts/sweep054/guests/spider/"     spider     "MINWIN_FRAME_EVERY=300" 400
harvest spider "05_xp_spider"

# ---- XP-era famous applets ----
run_gui "artifacts/sweep054/guests/calc/"       calc       "MINWIN_MOUSE='120,220;160,220;200,220'" 300
harvest calc "06_xp_calc"
run_gui "artifacts/sweep054/guests/notepad/"    notepad    "" 300
harvest notepad "07_xp_notepad"
run_gui "artifacts/sweep054/guests/mspaint/"    mspaint    "" 300
harvest mspaint "08_xp_mspaint"
run_gui "artifacts/sweep054/guests/wordpad/"    wordpad    "" 300
harvest wordpad "09_xp_wordpad"
run_gui "artifacts/sweep054/guests/charmap/"    charmap    "" 300
harvest charmap "10_xp_charmap"
run_gui "artifacts/sweep054/guests/winver/"     winver     "" 300
harvest winver "11_xp_winver"
run_gui "artifacts/sweep054/guests/sndvol32/"   sndvol32   "" 300
harvest sndvol32 "12_xp_sndvol32"
run_gui "artifacts/sweep054/guests/clipbrd/"    clipbrd    "" 300
harvest clipbrd "13_xp_clipbrd"

# ---- new / Windows-exclusive programs ----
run_gui "artifacts/sweep054/guests/everything/" everything  "MINWIN_MOUSE='400,20;400,100'" 300
harvest everything "14_everything"
run_gui "artifacts/sweep50/dl/putty/"           putty      "" 300
harvest putty "15_putty"
run_gui "artifacts/sweep50/dl/puttygen/"        puttygen   "" 300
harvest puttygen "16_puttygen"
run_gui "artifacts/sweep50/dl/pageant/"         pageant    "" 300
harvest pageant "17_pageant"
run_gui "artifacts/sweep50/dl/procmon64/"       Procmon64  "" 300
harvest Procmon64 "18_procmon"
run_gui "artifacts/sweep50/dl/rammap64/"        RAMMap64   "" 300
harvest RAMMap64 "19_rammap"
run_gui "artifacts/sweep50/dl/produkey/"        ProduKey   "" 300
harvest ProduKey "20_produkey"
run_gui "artifacts/sweep50/dl/searchmyfiles/"   SearchMyFiles "" 300
harvest SearchMyFiles "21_searchmyfiles"
run_gui "artifacts/sweep50/dl/cports/"          cports     "" 300
harvest cports "22_cports"
run_gui "artifacts/sweep50/dl/autoruns64/"      Autoruns64 "" 300
harvest Autoruns64 "23_autoruns"
run_gui "artifacts/sweep50/dl/bginfo64/"        Bginfo64   "" 300
harvest Bginfo64 "24_bginfo"
run_gui "artifacts/sweep50/dl/zoomit64/"        ZoomIt64   "" 300
harvest ZoomIt64 "25_zoomit"

echo "=== PNG inventory ==="
ls -la "$PNG" | head -50

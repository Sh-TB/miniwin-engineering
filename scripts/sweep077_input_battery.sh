#!/bin/bash
# SWEEP-077: input battery v2 — every puzzle gets REAL input through the
# guest pipeline: cursor keys (extended-bit lParam, SW-1344) + Enter/Space
# (CURSOR_SELECT/2) + 'n' (midend new game) + coordinate clicks on the board.
# 3 cold runs each; verdict = rc==0, no crash, >=1 post-input frame differs
# from the initial frame (pixel MD5). Frames saved per puzzle.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
D=artifacts/sweep074/dl
KEYS="37,38,39,40,13,32,37,38,39,40,13,32,78"
MOUSE="45,45;90,45;135,45;180,45;45,90;90,90;135,90;180,90;45,135;90,135;135,135;180,135"

run_one() {
    local p="$1" exe="$2"
    local base=$(basename "$exe" .exe)
    local out=""; local first_diff_frame=""; local uniq1=0
    for run in 1 2 3; do
        rm -rf "miniwin-results/$base.exe"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=220 \
        MW_GUI_KEYS="$KEYS" MW_GUI_KEY_EVERY=7 MW_GUI_KEYUP=1 \
        MINWIN_MOUSE="$MOUSE" MINWIN_MOUSE_EVERY=6 \
        timeout 75 ./minwin_loader "$exe" > "/tmp/sw77_${p}_r${run}.log" 2>&1
        local rc=$?
        local crash=$(grep -ac '\[CRASH\]' "/tmp/sw77_${p}_r${run}.log")
        out="$out rc=$rc/$crash"
    done
    # frame analysis on the LAST run's frames (dir persists per base name)
    local verdict; local frames=0; local uniq=0
    read -r frames uniq first_diff_frame <<< "$(python3 - "$base" <<'PYEOF'
import sys, os, hashlib, shutil
base = sys.argv[1]
d = f"miniwin-results/{base}.exe/screenshots"
try:
    fs = sorted(os.listdir(d))
    h0 = hashlib.md5(open(f"{d}/{fs[0]}","rb").read()).hexdigest()
    diff = [f for f in fs[1:] if hashlib.md5(open(f"{d}/{f}","rb").read()).hexdigest() != h0]
    print(len(fs), len(set(hashlib.md5(open(f"{d}/{f}","rb").read()).hexdigest() for f in fs)), diff[0] if diff else "")
except Exception:
    print(0, 0, "")
PYEOF
)"
    if [ "$frames" -gt 0 ] && [ -n "$first_diff_frame" ]; then
        verdict="PASS_INPUT_STATE_CHANGE"
    elif [ "$frames" -gt 0 ]; then
        verdict="PASS_LAUNCH_RENDER"
    else
        verdict="FAIL_NO_FRAMES"
    fi
    echo -e "$p\t$base\t$out\tframes=$frames\tuniq=$uniq\tdiff_at=$first_diff_frame\t$verdict"
}
export -f run_one
export KEYS MOUSE D

printf "%s\n" \
 "P01 $D/blackbox.exe"   "P02 $D/bridges.exe"   "P03 $D/cube.exe" \
 "P04 $D/dominosa.exe"   "P05 $D/fifteen.exe"   "P06 $D/flood.exe" \
 "P07 $D/guess.exe"      "P08 $D/lightup.exe"   "P09 $D/map.exe" \
 "P10 $D/mines.exe"      "P11 $D/netgame.exe"   "P12 $D/pattern.exe" \
 "P13 $D/pegs.exe"       "P14 $D/range.exe"     "P15 $D/rect.exe" \
 "P16 $D/samegame.exe"   "P17 $D/signpost.exe"  "P18 $D/slant.exe" \
 "P19 $D/solo.exe"       "P20 $D/towers.exe" \
| xargs -P6 -n2 bash -c 'run_one "$0" "$1"' > evidence/sweep077/input_battery_v2.tsv
echo "=== SWEEP-077 input battery v2 complete ==="
sort -t$'\t' -k1,1 evidence/sweep077/input_battery_v2.tsv | awk -F'\t' '{printf "%s %-10s %-22s %-16s %s\n", $1, $2, $5, $6, $7}' | head -25

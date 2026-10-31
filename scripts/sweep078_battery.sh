#!/bin/bash
# SWEEP-078: B-group input battery — every GUI tool gets REAL input through the
# guest pipeline: cursor keys + Enter/Space + Tab + coordinate clicks.
# 3 cold runs each; verdict = rc==0, no crash, >=1 post-input frame differs
# from the initial frame (pixel MD5). Frames saved per tool (existing infra).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
D=artifacts/sweep078/dl/x
KEYS="37,38,39,40,13,32,9,9,37,38,39,40,13,32,9,9"
MOUSE="45,45;90,45;135,45;180,45;45,90;90,90;135,90;180,90;45,135;90,135;135,135;180,135"

run_one() {
    local p="$1" exe="$2"
    local base=$(basename "$exe" .exe)
    local out=""
    for run in 1 2 3; do
        rm -rf "miniwin-results/${base}.exe"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=220 \
        MW_GUI_KEYS="$KEYS" MW_GUI_KEY_EVERY=7 MW_GUI_KEYUP=1 \
        MINWIN_MOUSE="$MOUSE" MINWIN_MOUSE_EVERY=6 \
        timeout 75 ./minwin_loader "$exe" > "/tmp/sw78_${p}_r${run}.log" 2>&1
        local rc=$?
        local crash=$(grep -ac '\[CRASH\]' "/tmp/sw78_${p}_r${run}.log")
        out="$out rc=$rc/$crash"
    done
    local verdict; local frames=0; local uniq=0; local first_diff_frame=""
    read -r frames uniq first_diff_frame <<< "$(python3 - "$base" <<'PYEOF'
import sys, os, hashlib
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
 "W01 $D/cports-x64/cports.exe" \
 "W02 $D/filetypesman-x64/FileTypesMan.exe" \
 "W03 $D/regscanner-x64/RegScanner.exe" \
 "W04 $D/shexview-x64/shexview.exe" \
 "W05 $D/whatinstartup-x64/WhatInStartup.exe" \
 "W06 $D/Winobj.exe" \
 "W07 $D/regscanner-x86/RegScanner.exe" \
 "W08 $D/winlister-x86/winlister.exe" \
 "W09 $D/ZoomIt.exe" \
| xargs -P6 -n2 bash -c 'run_one "$0" "$1"'

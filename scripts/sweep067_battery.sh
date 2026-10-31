#!/bin/bash
# SWEEP-067 regression battery — x64 classics + PE32 four, vs SWEEP-066 baselines.
# Baselines (SWEEP-066): winmine 82 / sol 38 / freecell 10 / spider 618 / calc 1 /
#   notepad rc=0 / wordpad rc=0 / driverview 794,071 steps rc=139 / usbdeview 27 / npp 2450.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
G61=artifacts/sweep061/guests
DL62=artifacts/sweep062/dl
DL65=artifacts/sweep065/dl
S62=artifacts/sweep062/x

run() { # name budget exe
    local p="$1" b="$2" e="$3"
    [ -f "$e" ] || { echo "$p MISSING ($e)"; return; }
    rm -rf "miniwin-results/$p.exe"
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$b" timeout 75 ./minwin_loader "$e" \
        > "/tmp/${p}_67b_out.txt" 2> "/tmp/${p}_67b_err.txt"
    local rc=$?
    local steps=$(grep -oP 'steps=\K[0-9]+' "/tmp/${p}_67b_err.txt" | tail -1)
    local frames=$(ls "miniwin-results/$p.exe/screenshots/frame_"*.bmp 2>/dev/null | wc -l)
    echo "$p rc=$rc frames=$frames steps=${steps:-0}"
}

# x64 XP classics (PE32+ era builds)
run winmine  300 "$G61/winmine/winmine.exe"
run sol      300 "$G61/sol/sol.exe"
run freecell 300 "$G61/freecell/freecell.exe"
run spider   800 "$G61/spider/spider.exe"
run calc     120 "$G61/calc/calc.exe"
run notepad  120 "$G61/notepad/notepad.exe"
run wordpad  120 "$G61/wordpad/wordpad.exe"

# PE32 four (the SWEEP-067 wagon train)
run driverview 120 "$S62/driverview/DriverView.exe"
run usbdeview  120 "$S62/usbdeview/USBDeview.exe"
run notepadpp  120 "$DL65/npp/notepad++.exe"
run 7zr        120 "$DL65/7zr.exe"

#!/bin/bash
# SWEEP-068 — (use the tools to the maximum ... run 10 to 20 programs and games, push to GitHub)
# Mass-run battery: 24 guests (7 games, 9 XP apps, 6 modern tools, 2 Sysinternals x64).
# Parallel via xargs -P4; per-exe result dirs are distinct -> safe.
# Baselines from SWEEP-066/067: winmine 82 / sol 38 / freecell 10 / spider ~618-645 /
#   calc 1 / notepad rc=0 / wordpad rc=0 / 7zr 74 EXACT / npp86 2450 rc=86 (PE32-FATAL) /
#   npp64 139 + 2 MessageBoxes (SW-1298-x64 frontier) / driverview 794,071 rc=139 /
#   usbdeview 27 rc=139 / winrar rc=1 + Error dialog.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
G61=artifacts/sweep061/guests
S62=artifacts/sweep062/x
DL65=artifacts/sweep065/dl
EV=evidence/sweep068
mkdir -p "$EV"

# stage renamed x64 N++ so its result dir does not collide with the x86 one
mkdir -p artifacts/sweep068/npp64
cp -f artifacts/sweep50/dl/notepadpp/notepad++.exe artifacts/sweep068/npp64/notepadpp64.exe

GUESTS="winmine:300:$G61/winmine/winmine.exe
sol:300:$G61/sol/sol.exe
freecell:300:$G61/freecell/freecell.exe
spider:800:$G61/spider/spider.exe
mshearts:300:$G61/mshearts/mshearts.exe
breakout:120:artifacts/sweep006/breakout.exe
snake:120:artifacts/sweep005/snake.exe
calc:120:$G61/calc/calc.exe
notepad:120:$G61/notepad/notepad.exe
wordpad:120:$G61/wordpad/wordpad.exe
write:120:$G61/write/write.exe
mspaint:150:$G61/mspaint/mspaint.exe
charmap:120:$G61/charmap/charmap.exe
clipbrd:120:$G61/clipbrd/clipbrd.exe
winver:60:$G61/winver/winver.exe
taskmgr:120:$G61/taskmgr/taskmgr.exe
7zr:120:$DL65/7zr.exe
notepadpp64:120:artifacts/sweep068/npp64/notepadpp64.exe
notepadpp:120:$DL65/npp/notepad++.exe
winrar:120:artifacts/sweep062/dl/winrar712.exe
usbdeview:120:$S62/usbdeview/USBDeview.exe
driverview:120:$S62/driverview/DriverView.exe
pslist64:60:artifacts/sweep047/bin/pslist64.exe
handle64:60:artifacts/sweep047/bin/handle64.exe"

echo "$GUESTS" | xargs -P 4 -I{} bash -c '
    set -u; cd /home/z/my-project/workspace/miniwin-engineering || exit 1
    IFS=: read -r p b e <<< "{}"
    [ -f "$e" ] || { echo "$p MISSING $e"; exit 0; }
    rm -rf "miniwin-results/$(basename "$e")"   # fresh per-exe hive every run
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$b" timeout 75 ./minwin_loader "$e" \
        > "/tmp/${p}_68_out.txt" 2> "/tmp/${p}_68_err.txt"
    rc=$?
    steps=$(grep -oP "steps=\K[0-9]+" "/tmp/${p}_68_err.txt" | tail -1)
    frames=$(ls "miniwin-results/$(basename "$e")/screenshots/"frame_*.bmp 2>/dev/null | wc -l)
    msgbox=$(ls "miniwin-results/$(basename "$e")/screenshots/"messagebox_*.bmp 2>/dev/null | wc -l)
    echo "$p rc=$rc frames=$frames msgbox=$msgbox steps=${steps:-0}"
' | sort > "$EV/battery_raw.txt"

cat "$EV/battery_raw.txt"
echo "SWEEP-068 battery complete: $(wc -l < "$EV/battery_raw.txt") guests"

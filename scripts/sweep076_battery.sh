#!/bin/bash
# SWEEP-076: B-group rescue battery — the 8 failing targets, 3 cold runs each.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
EV=evidence/sweep076
mkdir -p "$EV/raw"
X=artifacts/sweep074/dl/x
U=artifacts/sweep074/dl/utils

TARGETS="U03:60:gui:$X/notepad3-x86/Notepad3.exe
U04:60:gui:$X/notepad3-x64/Notepad3.exe
U07:60:cli:$X/ahk-v1/AutoHotkeyU32.exe
U16:60:gui:$U/zoomit.exe
U18:60:gui:$X/regscanner-x86/RegScanner.exe
U25:60:gui:$U/tcpview64.exe
U27:60:gui:$X/winlister-x64/winlister.exe
R3:60:gui:$X/lastactivityview/LastActivityView.exe"

run_one() {
    IFS=: read -r p b mode e <<< "$1"
    [ -f "$e" ] || { echo "$p MISSING $e"; return 0; }
    local base=$(basename "$e" .exe)
    local rdir="miniwin-results/sw76_${p}_${base}"
    for run in 1 2 3; do
        rm -rf "$rdir"
        local log="$EV/raw/${p}_run${run}.log"
        if [ "$mode" = "gui" ]; then
            MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=25 timeout "$b" ./minwin_loader "$e" > "$log" 2>&1
        else
            timeout "$b" ./minwin_loader "$e" > "$log" 2>&1
        fi
        local rc=$?
        local frames=$(ls miniwin-results/"$base".exe/screenshots/ 2>/dev/null | wc -l)
        local crash=$(grep -c '\[CRASH\]' "$log")
        local fatal=$(grep -c 'PE32-FATAL' "$log")
        local steps=$(grep -oE 'steps[= ][0-9]+' "$log" | tail -1)
        echo -e "$p\trun$run\trc=$rc\tframes=$frames\tcrash=$crash\tfatal=$fatal\t$steps\t$base\t$mode"
    done
}
export -f run_one
export EV

echo "$TARGETS" | xargs -P 4 -I{} bash -c 'run_one "$@"' _ {} | sort | tee "$EV/battery_raw.txt"
echo "=== SWEEP-076 PRE battery complete ==="

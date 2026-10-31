#!/bin/bash
# SWEEP-074: 48-target battery runner — 3 cold runs per target through
# minwin_loader, per-target fresh hive, raw logs kept, verdicts computed
# afterwards by sweep074_verdicts.py.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
EV=evidence/sweep074
mkdir -p "$EV/raw"
X=artifacts/sweep074/dl/x
U=artifacts/sweep074/dl/utils
D=artifacts/sweep074/dl

# id:timeout:mode:path   (mode=gui → event pump armed; mode=cli → no pump)
TARGETS_ALL="P01:60:gui:$D/blackbox.exe
P02:60:gui:$D/bridges.exe
P03:60:gui:$D/cube.exe
P04:60:gui:$D/dominosa.exe
P05:60:gui:$D/fifteen.exe
P06:60:gui:$D/flood.exe
P07:60:gui:$D/guess.exe
P08:60:gui:$D/lightup.exe
P09:60:gui:$D/map.exe
P10:60:gui:$D/mines.exe
P11:60:gui:$D/netgame.exe
P12:60:gui:$D/pattern.exe
P13:60:gui:$D/pegs.exe
P14:60:gui:$D/range.exe
P15:60:gui:$D/rect.exe
P16:60:gui:$D/samegame.exe
P17:60:gui:$D/signpost.exe
P18:60:gui:$D/slant.exe
P19:60:gui:$D/solo.exe
P20:60:gui:$D/towers.exe
U03:60:gui:$X/notepad3-x86/Notepad3.exe
U04:60:gui:$X/notepad3-x64/Notepad3.exe
U07:60:cli:$X/ahk-v1/AutoHotkeyU32.exe
U08:60:cli:$X/ahk-v2/AutoHotkey64.exe
U16:60:gui:$U/zoomit32.exe
U17:60:gui:$U/bginfo64.exe
U18:60:gui:$X/regscanner-x86/RegScanner.exe
U19:60:gui:$X/regscanner-x64/RegScanner.exe
U20:60:gui:$X/wis-x64/WhatInStartup.exe
U21:60:gui:$X/shmnview-x64/shmnview.exe
U22:60:gui:$X/shex-x64/shexview.exe
U23:60:gui:$X/cports-x64/cports.exe
U24:60:gui:$X/ftm-x64/FileTypesMan.exe
U25:60:gui:$U/tcpview64.exe
U26:60:gui:$U/winobj.exe
U27:60:gui:$X/winlister-x64/winlister.exe
U28:60:cli:$X/sigcheck/sigcheck64.exe
R2:60:cli:$U/strings64.exe
R3:60:gui:$X/lastactivityview/LastActivityView.exe"

run_one() {
    IFS=: read -r p b mode e <<< "$1"
    [ -f "$e" ] || { echo "$p MISSING $e"; return 0; }
    local base=$(basename "$e" .exe)
    # unique result dir per target id (names may collide across targets)
    local rdir="miniwin-results/sw74_${p}_${base}"
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
        echo -e "$p\trun$run\trc=$rc\tframes=$frames\tcrash=$crash\tfatal=$fatal\t$base\t$mode"
        [ "$run" = "1" ] && cp -r "$rdir" "$EV/raw/${p}_run1_dir" 2>/dev/null
    done
}
export -f run_one
export EV

if [ -n "${SUBSET:-}" ]; then T="$SUBSET"; else T="$TARGETS_ALL"; fi
echo "$T" | xargs -P 6 -I{} bash -c 'run_one "$@"' _ {} | sort | tee -a "$EV/battery_raw.txt"
cat "$EV/battery_raw.txt"
echo "=== SWEEP-074 battery complete ==="

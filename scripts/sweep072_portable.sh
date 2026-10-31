#!/bin/bash
# SWEEP-072 portable wave — 11 Sysinternals/NirSoft portable tools (not in SWEEP-068..070 batteries)
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
A=artifacts/sweep50/dl
EV=evidence/sweep072
mkdir -p "$EV"

GUESTS="pageant:120:$A/pageant/pageant.exe
procexp64:150:$A/procexp64/procexp64.exe
rammap64:120:$A/rammap64/RAMMap64.exe
zoomit64:120:$A/zoomit64/ZoomIt64.exe
bginfo64:150:$A/bginfo64/Bginfo64.exe
diskmon64:120:$A/diskmon64/Diskmon64.exe
procmon64:150:$A/procmon64/Procmon64.exe
autoruns64:150:$A/autoruns64/Autoruns64.exe
produkey:120:$A/produkey/ProduKey.exe
searchmyfiles:120:$A/searchmyfiles/SearchMyFiles.exe
cports:120:$A/cports/cports.exe"

echo "$GUESTS" | xargs -P 6 -I{} bash -c '
    set -u; cd /home/z/my-project/workspace/miniwin-engineering || exit 1
    IFS=: read -r p b e <<< "{}"
    [ -f "$e" ] || { echo "$p MISSING $e"; exit 0; }
    rm -rf "miniwin-results/$(basename "$e")"
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$b" timeout 90 ./minwin_loader "$e" \
        > "/tmp/${p}_72_out.txt" 2> "/tmp/${p}_72_err.txt"
    rc=$?
    err="/tmp/${p}_72_err.txt"
    steps=$(grep -oP "steps=\K[0-9]+" "$err" | tail -1)
    frames=$(ls "miniwin-results/$(basename "$e")/screenshots/"frame_*.bmp 2>/dev/null | wc -l)
    msgbox=$(ls "miniwin-results/$(basename "$e")/screenshots/"messagebox_*.bmp 2>/dev/null | wc -l)
    echo -e "$p\trc=$rc\tframes=$frames\tmsgbox=$msgbox\tsteps=${steps:-0}"
' | sort | tee "$EV/portable_raw.txt"
echo "portable wave complete: $(wc -l < "$EV/portable_raw.txt") guests"

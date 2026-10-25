#!/bin/bash
# SWEEP-070 heavy train — 18 FAMOUS Windows programs (IDM, VLC, qBittorrent, ShareX,
# Audacity, MPC-HC, WinMerge, Everything, CPU-Z, PuTTY, 7-Zip, NVM, yt-dlp, ripgrep,
# bat, NSSM, SteamCMD, Stockfish-chess). xargs -P6, fresh hive per run.
# Load-fidelity columns: unresolved / FASTFAIL / PE32FATAL / BT (backtrace) flags from stderr.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
G=artifacts/sweep070/guests
EV=evidence/sweep070
mkdir -p "$EV"

GUESTS="everything:150:$G/everything.exe
winmerge:150:$G/winmerge.exe
putty:120:$G/putty.exe
cpuz:150:$G/cpuz.exe
mpchc:150:$G/mpchc.exe
audacity:150:$G/audacity.exe
7zip:120:$G/7zip.exe
vlc:150:$G/vlc.exe
qbittorrent:150:$G/qbittorrent.exe
idm:150:$G/idm.exe
ytdlp:60:$G/ytdlp.exe
nvm:120:$G/nvm.exe
ripgrep:60:$G/ripgrep.exe
bat:60:$G/bat.exe
nssm64:60:$G/nssm64.exe
stockfish:60:$G/stockfish.exe
steamcmd:60:$G/steamcmd.exe
sharex:150:$G/sharex.exe"

echo "$GUESTS" | xargs -P 6 -I{} bash -c '
    set -u; cd /home/z/my-project/workspace/miniwin-engineering || exit 1
    IFS=: read -r p b e <<< "{}"
    [ -f "$e" ] || { echo "$p MISSING $e"; exit 0; }
    rm -rf "miniwin-results/$(basename "$e")"
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$b" timeout 90 ./minwin_loader "$e" \
        > "/tmp/${p}_70_out.txt" 2> "/tmp/${p}_70_err.txt"
    rc=$?
    err="/tmp/${p}_70_err.txt"
    steps=$(grep -oP "steps=\K[0-9]+" "$err" | tail -1)
    frames=$(ls "miniwin-results/$(basename "$e")/screenshots/"frame_*.bmp 2>/dev/null | wc -l)
    msgbox=$(ls "miniwin-results/$(basename "$e")/screenshots/"messagebox_*.bmp 2>/dev/null | wc -l)
    unres=$(grep -oiP "unresolved[^0-9]*\K[0-9]+" "$err" | tail -1)
    grep -q FASTFAIL "$err" && ff=F || ff=-
    grep -q "PE32-FATAL" "$err" && pf=P || pf=-
    grep -q "Backtrace" "$err" && bt=B || bt=-
    echo -e "$p\trc=$rc\tframes=$frames\tmsgbox=$msgbox\tsteps=${steps:-0}\tunres=${unres:-0}\t$ff$pf$bt"
' | sort > "$EV/battery_raw.txt"

cat "$EV/battery_raw.txt"
echo "SWEEP-070 heavy train complete: $(wc -l < "$EV/battery_raw.txt") guests"

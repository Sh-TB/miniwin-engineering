#!/bin/bash
# SWEEP-069 — (better and faster ... how much did the auxiliary tools help ... add the same number of programs and games again)
# 29 NEW guests (none of these ran in SWEEP-065..068 batteries):
#   3 XP classics + 12 NirSoft + 13 Sysinternals x64 + GUP.exe (N++ updater)
# Same harness as SWEEP-068: xargs -P4, fresh per-exe hive, distinct result dirs.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
G61=artifacts/sweep061/guests
S62=artifacts/sweep062/x
B47=artifacts/sweep047/bin
EV=evidence/sweep069
mkdir -p "$EV"

GUESTS="drwtsn32:120:$G61/drwtsn32/drwtsn32.exe
regedt32:120:$G61/regedt32/regedt32.exe
sndvol32:120:$G61/sndvol32/sndvol32.exe
devmanview:120:$S62/devmanview/DevManView.exe
dnsquerysniffer:120:$S62/dnsquerysniffer/DNSQuerySniffer.exe
faview:120:$S62/faview/faview.exe
heapmemview:120:$S62/heapmemview/HeapMemView.exe
htmlastext:120:$S62/htmlastext/HtmlAsText.exe
iconsext:120:$S62/iconsext/iconsext.exe
installedappview:120:$S62/installedappview/InstalledAppView.exe
multimonitortool:120:$S62/multimonitortool/MultiMonitorTool.exe
recentfilesview:120:$S62/recentfilesview/RecentFilesView.exe
resourcesextract:120:$S62/resourcesextract/ResourcesExtract.exe
simpleprogramdebugger:120:$S62/simpleprogramdebugger/SimpleProgramDebugger.exe
urlprotocolview:120:$S62/urlprotocolview/URLProtocolView.exe
accesschk64:60:$B47/accesschk64.exe
autologon64:60:$B47/autologon64.exe
curl:60:$B47/curl.exe
du64:60:$B47/du64.exe
listdlls64:60:$B47/listdlls64.exe
logonsessions64:60:$B47/logonsessions64.exe
pipelist64:60:$B47/pipelist64.exe
psloggedon64:60:$B47/psloggedon64.exe
psinfo64:60:$B47/PsInfo64.exe
psservice64:60:$B47/PsService64.exe
sdelete64:60:$B47/sdelete64.exe
strings64:60:$B47/strings64.exe
whois64:60:$B47/whois64.exe
gup:120:artifacts/sweep065/dl/npp/updater/GUP.exe"

echo "$GUESTS" | xargs -P 4 -I{} bash -c '
    set -u; cd /home/z/my-project/workspace/miniwin-engineering || exit 1
    IFS=: read -r p b e <<< "{}"
    [ -f "$e" ] || { echo "$p MISSING $e"; exit 0; }
    rm -rf "miniwin-results/$(basename "$e")"
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$b" timeout 75 ./minwin_loader "$e" \
        > "/tmp/${p}_69_out.txt" 2> "/tmp/${p}_69_err.txt"
    rc=$?
    steps=$(grep -oP "steps=\K[0-9]+" "/tmp/${p}_69_err.txt" | tail -1)
    frames=$(ls "miniwin-results/$(basename "$e")/screenshots/"frame_*.bmp 2>/dev/null | wc -l)
    msgbox=$(ls "miniwin-results/$(basename "$e")/screenshots/"messagebox_*.bmp 2>/dev/null | wc -l)
    echo "$p rc=$rc frames=$frames msgbox=$msgbox steps=${steps:-0}"
' | sort > "$EV/battery_raw.txt"

cat "$EV/battery_raw.txt"
echo "SWEEP-069 battery complete: $(wc -l < "$EV/battery_raw.txt") guests"

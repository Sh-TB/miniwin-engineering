#!/bin/bash
# SWEEP-065 — 20-program mass audit battery: run every program under miniwin,
# collect rc/frames/screenshot per program (evidence/sweep065/shots_png).
# Batch = WinRAR 7.12 + Notepad++ 8.9.8.1 (GitHub #1 Windows program)
#       + 7-Zip 7zr + 13 NirSoft tools + 4 XP classics.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
PNG=evidence/sweep065/shots_png
mkdir -p "$PNG"
S62=artifacts/sweep062/x
G61=artifacts/sweep061/guests
DL65=artifacts/sweep065/dl
DL62=artifacts/sweep062/dl

shoot() { # name budget exe-path
    local p="$1"; local budget="$2"; local e="$3"
    rm -rf "miniwin-results/$p.exe"
    local t0=$(date +%s.%N)
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$budget" timeout 75 ./minwin_loader "$e" \
        > "/tmp/${p}_65_stdout.txt" 2> "/tmp/${p}_65_stderr.txt"
    local rc=$?
    local t1=$(date +%s.%N)
    local dt=$(echo "$t1 $t0" | awk '{printf "%.1f", $1-$2}')
    local frames=$(ls "miniwin-results/$p.exe/screenshots/frame_"*.bmp 2>/dev/null | wc -l)
    local shot=NONE
    local last=$(ls "miniwin-results/$p.exe/screenshots/frame_"*.bmp 2>/dev/null | sort | tail -1)
    # WinRAR-style: engine may only emit a rendered dialog (messagebox_N.bmp)
    if [ -z "$last" ]; then
        last=$(ls "miniwin-results/$p.exe/screenshots/messagebox_"*.bmp 2>/dev/null | sort | tail -1)
    fi
    if [ -n "$last" ]; then
        python3 - "$last" "$PNG/${p}.png" <<'EOF' 2>/dev/null
import sys
from PIL import Image
Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])
EOF
        shot=saved
    fi
    echo "$p rc=$rc frames=$frames secs=$dt shot=$shot"
}

# big three (user-named): WinRAR, Notepad++, 7-Zip
shoot winrar712      120 "$DL62/winrar712.exe"
shoot notepadpp      120 "$DL65/npp/notepad++.exe"
shoot 7zr            120 "$DL65/7zr.exe"

# NirSoft 13 (PE32 frontier SW-1293 retest after SW-1204c fix)
shoot devmanview     120 "$S62/devmanview/DevManView.exe"
shoot dnsquerysniffer 120 "$S62/dnsquerysniffer/DNSQuerySniffer.exe"
shoot driverview     120 "$S62/driverview/DriverView.exe"
shoot faview         120 "$S62/faview/faview.exe"
shoot heapmemview    120 "$S62/heapmemview/HeapMemView.exe"
shoot htmlastext     120 "$S62/htmlastext/HtmlAsText.exe"
shoot iconsext       120 "$S62/iconsext/iconsext.exe"
shoot installedappview 120 "$S62/installedappview/InstalledAppView.exe"
shoot multimonitortool 120 "$S62/multimonitortool/MultiMonitorTool.exe"
shoot recentfilesview 120 "$S62/recentfilesview/RecentFilesView.exe"
shoot resourcesextract 120 "$S62/resourcesextract/ResourcesExtract.exe"
shoot simpleprogramdebugger 120 "$S62/simpleprogramdebugger/SimpleProgramDebugger.exe"
shoot urlprotocolview 120 "$S62/urlprotocolview/URLProtocolView.exe"

# XP classics 4
shoot freecell 60  "$G61/freecell/freecell.exe"
shoot spider   700 "$G61/spider/spider.exe"
shoot calc     60  "$G61/calc/calc.exe"
shoot mspaint  60  "$G61/mspaint/mspaint.exe"
echo "BATTERY DONE"

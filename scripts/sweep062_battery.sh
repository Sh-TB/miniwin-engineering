#!/bin/bash
# SWEEP-062 Train 3 — run the fresh corpus under miniwin, collect rc/frames,
# keep a screenshot per program (evidence/sweep062/dl_png).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
PNG=evidence/sweep062/dl_png
mkdir -p "$PNG"
declare -A EXE=(
  [driverview]=x/driverview/DriverView.exe
  [usbdeview]=x/usbdeview/USBDeview.exe
  [faview]=x/faview/faview.exe
  [resourcesextract]=x/resourcesextract/ResourcesExtract.exe
  [iconsext]=x/iconsext/iconsext.exe
  [devmanview]=x/devmanview/DevManView.exe
  [simpleprogramdebugger]=x/simpleprogramdebugger/SimpleProgramDebugger.exe
  [heapmemview]=x/heapmemview/HeapMemView.exe
  [htmlastext]=x/htmlastext/HtmlAsText.exe
  [urlprotocolview]=x/urlprotocolview/URLProtocolView.exe
  [dnsquerysniffer]=x/dnsquerysniffer/DNSQuerySniffer.exe
  [multimonitortool]=x/multimonitortool/MultiMonitorTool.exe
  [installedappview]=x/installedappview/InstalledAppView.exe
  [recentfilesview]=x/recentfilesview/RecentFilesView.exe
  [winrar712]=dl/winrar712.exe
)
pass=0; fail=0
for p in driverview usbdeview faview resourcesextract iconsext devmanview \
         simpleprogramdebugger heapmemview htmlastext urlprotocolview \
         dnsquerysniffer multimonitortool installedappview recentfilesview winrar712; do
  e="${EXE[$p]}"
  rm -rf "miniwin-results/$p.exe"
  MINWIN_TRACE=0 MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=80 timeout 60 \
    ./minwin_loader "artifacts/sweep062/$e" > /dev/null 2>"/tmp/${p}_err.txt"
  rc=$?
  frames=$(ls "miniwin-results/$p.exe/screenshots/frame_"*.bmp 2>/dev/null | wc -l)
  [ $rc -eq 0 ] && { pass=$((pass+1)); st=OK; } || { fail=$((fail+1)); st=FAIL; }
  if [ "$frames" -gt 0 ]; then
    last=$(ls "miniwin-results/$p.exe/screenshots/frame_"*.bmp | sort | tail -1)
    python3 - "$last" "$PNG/${p}.png" <<'PYEOF' 2>/dev/null
import sys
from PIL import Image
Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])
PYEOF
    shot=saved
  else
    shot=none
  fi
  echo "$p rc=$rc frames=$frames $st shot=$shot"
done
echo "PASS=$pass FAIL=$fail"

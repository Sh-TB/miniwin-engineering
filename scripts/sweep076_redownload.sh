#!/bin/bash
# SWEEP-076: re-fetch the 8 failing B-group binaries (sandbox reset wiped dl/).
# Same official URLs as sweep074; SHA-256 verified against targets.tsv afterwards.
set -u
cd /home/z/my-project/workspace/miniwin-engineering/artifacts/sweep074/dl || exit 1
mkdir -p utils x
cd utils

fetch() { # id url out
    local id="$1" url="$2" out="$3"
    [ -s "$out" ] && { echo "HAVE $id $out"; return 0; }
    local code=$(curl -sL -w '%{http_code}' --max-time 240 -A "Mozilla/5.0 (Windows NT 10.0; Win64; x64)" -o "$out" "$url")
    local mz=$(head -c2 "$out" 2>/dev/null | grep -q MZ && echo MZ || echo -)
    local sz=$(stat -c%s "$out" 2>/dev/null || echo 0)
    echo "$id http=$code size=$sz magic=$mz $out"
}

fetch U16 zoomit.exe           "https://live.sysinternals.com/ZoomIt.exe" &
fetch U17 bginfo.exe           "https://live.sysinternals.com/Bginfo64.exe" &
fetch U25 tcpview-x64.exe      "https://live.sysinternals.com/Tcpview64.exe" &
fetch U26 winobj-x64.exe       "https://live.sysinternals.com/Winobj.exe" &
wait
cd ../x
mkdir -p notepad3-x86 notepad3-x64 ahk-v1 regscanner-x86 winlister-x64 lastactivityview
fetch U03 notepad3-x86.zip     "https://github.com/rizonesoft/Notepad3/releases/download/RELEASE_6.24.1122.1/Notepad3_6.24.1122.1_x86_Portable.zip" &
fetch U04 notepad3-x64.zip     "https://github.com/rizonesoft/Notepad3/releases/download/RELEASE_6.24.1122.1/Notepad3_6.24.1122.1_x64_Portable.zip" &
fetch U07 ahk-v1.zip           "https://www.autohotkey.com/download/ahk-v1.zip" &
wait
fetch U18 regscanner-x86.zip   "https://www.nirsoft.net/utils/regscanner.zip" &
fetch U19 regscanner-x64.zip   "https://www.nirsoft.net/utils/regscanner-x64.zip" &
fetch U27 winlister-x64.zip    "https://www.nirsoft.net/utils/winlister-x64.zip" &
fetch R3  lastactivityview.zip "https://www.nirsoft.net/utils/lastactivityview.zip" &
wait

# unzip the zips
cd /home/z/my-project/workspace/miniwin-engineering/artifacts/sweep074/dl/x
for z in notepad3-x86 notepad3-x64 ahk-v1 regscanner-x86 regscanner-x64 winlister-x64 lastactivityview; do
    [ -f "$z.zip" ] && unzip -oq "$z.zip" -d "$z/" 2>/dev/null
done
echo "=== re-download done ==="

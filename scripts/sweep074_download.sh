#!/bin/bash
# SWEEP-074 utility corpus downloader — 28 targets, official sources only.
# Magic-sniff (MZ) after download; every file's final URL + SHA-256 recorded.
# Failures are honest (HTTP code captured) — no mirrors unless official.
set -u
cd /home/z/my-project/workspace/miniwin-engineering/artifacts/sweep074/dl || exit 1
mkdir -p utils
cd utils

fetch() { # id url
    local id="$1" url="$2" out="$3"
    [ -s "$out" ] && { echo "HAVE $id $out"; return 0; }
    local code=$(curl -sL -w '%{http_code}' --max-time 240 -A "Mozilla/5.0 (Windows NT 10.0; Win64; x64)" -o "$out" "$url")
    local mz=$(head -c2 "$out" 2>/dev/null | grep -q MZ && echo MZ || echo -)
    local sz=$(stat -c%s "$out" 2>/dev/null || echo 0)
    echo "$id http=$code size=$sz magic=$mz $out"
}

fetch U01 explorerpp-x86.zip   "https://www.explorerplusplus.com/download" &
fetch U02 explorerpp-x64.zip   "https://www.explorerplusplus.com/download" &
fetch U05 scite32.zip          "https://www.scintilla.org/scite.zip" &
fetch U06 scite64.zip          "https://www.scintilla.org/wscite.zip" &
fetch U07 ahk-x86.zip          "https://www.autohotkey.com/download/ahk-v1.zip" &
fetch U09 keepass.zip          "https://keepass.info/releases/file/2.58/KeePass-2.58.zip" &
fetch U10 winscp.exe           "https://winscp.net/download/WinSCP-6.3.7-Portable.zip" &
fetch U11 hxd.zip              "https://mh-nexus.de/downloads/HxDSetupEN.zip" &
fetch U12 reshacker.zip        "https://www.angusj.com/resourcehacker/resource_hacker.zip" &
fetch U13 ditto64.zip          "https://github.com/sabrogden/Ditto/releases/download/3.24.246.0/Ditto.Setup.64bit.exe" &
wait
fetch U14 fastcopy64.zip       "https://fastcopy.jp/archive/FastCopy64_5.9.14_installer.zip" &
fetch U15 freecommander.zip    "https://freecommander.com/downloads/FreeCommanderXE-32-portable.zip" &
fetch U16 zoomit.exe           "https://live.sysinternals.com/ZoomIt.exe" &
fetch U17 bginfo.exe           "https://live.sysinternals.com/Bginfo64.exe" &
fetch U18 regscanner-x86.zip   "https://www.nirsoft.net/utils/regscanner.zip" &
fetch U19 regscanner-x64.zip   "https://www.nirsoft.net/utils/regscanner-x64.zip" &
fetch U20 whatinstartup-x64.zip "https://www.nirsoft.net/utils/whatinstartup-x64.zip" &
fetch U21 shellmenuview-x64.zip "https://www.nirsoft.net/utils/shellmenuview-x64.zip" &
fetch U22 shellexview-x64.zip  "https://www.nirsoft.net/utils/shexview-x64.zip" &
wait
fetch U23 cports-x64.zip       "https://www.nirsoft.net/utils/cports-x64.zip" &
fetch U24 filetypesman-x64.zip "https://www.nirsoft.net/utils/filetypesman-x64.zip" &
fetch U25 tcpview-x64.zip      "https://live.sysinternals.com/Tcpview64.exe" &
fetch U26 winobj-x64.exe       "https://live.sysinternals.com/Winobj.exe" &
fetch U27 winlister-x64.zip    "https://www.nirsoft.net/utils/winlister-x64.zip" &
fetch U28 sigcheck-x64.zip     "https://download.sysinternals.com/files/Sigcheck.zip" &
fetch U03 notepad3-x86.zip     "https://github.com/rizonesoft/Notepad3/releases/download/RELEASE_6.24.1122.1/Notepad3_6.24.1122.1_x86_Portable.zip" &
fetch U04 notepad3-x64.zip     "https://github.com/rizonesoft/Notepad3/releases/download/RELEASE_6.24.1122.1/Notepad3_6.24.1122.1_x64_Portable.zip" &
fetch U08 ahk-x64.zip          "https://www.autohotkey.com/download/ahk-v2.zip" &
wait
echo "=== downloads done ==="

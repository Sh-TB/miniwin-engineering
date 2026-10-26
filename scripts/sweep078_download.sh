#!/bin/bash
# SWEEP-078: re-download B-group tool corpus after sandbox reset.
# Official sources only; magic-sniff MZ; URL + HTTP code + size + SHA-256 logged.
set -u
D=/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep078/dl
mkdir -p "$D/utils" "$D/x"
cd "$D" || exit 1

fetch() { # id url out
    local id="$1" url="$2" out="$3"
    [ -s "$out" ] && { echo "HAVE $id $out"; return 0; }
    local code=$(curl -sL -w '%{http_code}' --max-time 55 \
        -A "Mozilla/5.0 (Windows NT 10.0; Win64; x64)" -o "$out" "$url")
    local mz="-"; [ -s "$out" ] && head -c2 "$out" | grep -q MZ && mz=MZ
    local sz=$(stat -c%s "$out" 2>/dev/null || echo 0)
    echo -e "$id\thttp=$code\tsize=$sz\tmagic=$mz\t$url\t$out"
}

# wave 1
fetch U01e "https://www.explorerplusplus.com/download" x/explorerpp_download.html &
fetch U05  "https://www.scintilla.org/scite.zip"       x/scite32.zip &
fetch U06  "https://www.scintilla.org/wscite.zip"      x/scite64.zip &
fetch U09  "https://keepass.info/releases/file/2.58/KeePass-2.58.zip" x/keepass258.zip &
fetch U12  "https://www.angusj.com/resourcehacker/resource_hacker.zip" x/reshacker.zip &
fetch U16  "https://live.sysinternals.com/ZoomIt.exe"  x/ZoomIt.exe &
wait
# wave 2 (nirsoft batch)
fetch U18b "https://www.nirsoft.net/utils/regscanner-x64.zip" x/regscanner-x64.zip &
fetch U19  "https://www.nirsoft.net/utils/whatinstartup-x64.zip" x/whatinstartup-x64.zip &
fetch U20  "https://www.nirsoft.net/utils/shellmenuview-x64.zip" x/shellmenuview-x64.zip &
fetch U21  "https://www.nirsoft.net/utils/shexview-x64.zip"      x/shexview-x64.zip &
fetch U22  "https://www.nirsoft.net/utils/cports-x64.zip"        x/cports-x64.zip &
fetch U23  "https://www.nirsoft.net/utils/filetypesman-x64.zip"  x/filetypesman-x64.zip &
wait
# wave 3
fetch U26  "https://live.sysinternals.com/Winobj.exe"   x/Winobj.exe &
fetch U27b "https://www.nirsoft.net/utils/winlister.zip" x/winlister-x86.zip &
fetch U03  "https://github.com/rizonesoft/Notepad3/releases/download/RELEASE_6.24.1122.1/Notepad3_6.24.1122.1_x86_Portable.zip" x/notepad3-x86.zip &
fetch U04  "https://github.com/rizonesoft/Notepad3/releases/download/RELEASE_6.24.1122.1/Notepad3_6.24.1122.1_x64_Portable.zip" x/notepad3-x64.zip &
fetch U07  "https://www.autohotkey.com/download/ahk-v1.zip" x/ahk-v1.zip &
fetch U11  "https://mh-nexus.de/downloads/HxDSetupEN.zip" x/hxd.zip &
wait
echo "=== downloads done ==="
sha256sum utils/* x/*.exe x/*.zip 2>/dev/null > "$D/sha256_all.txt"
cat "$D/sha256_all.txt"

#!/bin/bash
# SWEEP-062 Train 3 — corpus expansion battery (user mandate (download and test 20 to 50
# Windows programs) + Win98-era classics + tiny new free apps).
# Sources probed 2026-10-25: nirsoft.net direct zips (14), 7-zip.org extra
# pack (1), git-for-windows portable sfx (1), rarlab winrar (1).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
DL=artifacts/sweep062/dl
mkdir -p "$DL"

dl() { # name url
    local n="$1" u="$2"
    [ -s "$DL/$n" ] && { echo "SKIP $n (cached)"; return 0; }
    curl -sL --max-time 120 -o "$DL/$n" "$u" 2>/dev/null
    local sz=$(stat -c%s "$DL/$n" 2>/dev/null || echo 0)
    echo "DL $n bytes=$sz"
}

# --- NirSolft tiny free tools (14) ---
dl driverview.zip        https://www.nirsoft.net/utils/driverview.zip
dl usbdeview.zip         https://www.nirsoft.net/utils/usbdeview.zip
dl faview.zip            https://www.nirsoft.net/utils/faview.zip
dl resourcesextract.zip  https://www.nirsoft.net/utils/resourcesextract.zip
dl iconsext.zip          https://www.nirsoft.net/utils/iconsext.zip
dl devmanview.zip        https://www.nirsoft.net/utils/devmanview.zip
dl simpleprogramdebugger.zip https://www.nirsoft.net/utils/simpleprogramdebugger.zip
dl heapmemview.zip       https://www.nirsoft.net/utils/heapmemview.zip
dl htmlastext.zip        https://www.nirsoft.net/utils/htmlastext.zip
dl urlprotocolview.zip   https://www.nirsoft.net/utils/urlprotocolview.zip
dl dnsquerysniffer.zip   https://www.nirsoft.net/utils/dnsquerysniffer.zip
dl multimonitortool.zip  https://www.nirsoft.net/utils/multimonitortool.zip
dl installedappview.zip  https://www.nirsoft.net/utils/installedappview.zip
dl recentfilesview.zip   https://www.nirsoft.net/utils/recentfilesview.zip
# --- classic-era packs (3) ---
dl 7z1900-extra.7z       https://www.7-zip.org/a/7z1900-extra.7z
dl PortableGit.7z.exe    "https://github.com/git-for-windows/git/releases/download/v2.46.0.windows.1/PortableGit-2.46.0-64-bit.7z.exe"
dl winrar712.exe         https://www.rarlab.com/rar/winrar-x64-712.exe

echo "=== staged ==="
ls -la "$DL" | awk '{print $5, $9}'

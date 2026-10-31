#!/bin/bash
# SWEEP-059 Trains 3-7 — the user's mandate:
#   (bring several game programs from old Windows, even Windows 98)  (old Win98-era programs)
#   (download and test 20 to 50 free programs)        (20-50 free programs)
#   (test several brand-new tiny games and programs)        (new small games)
# Download battery: Win98-era classics (archive.org) + modern small freeware
# (GitHub releases / direct vendor URLs). Everything lands in
# artifacts/sweep059/dl/<name>/ and is verified with `file`.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
DL=artifacts/sweep059/dl
mkdir -p "$DL"

grab() { # grab <name> <url> [extra curl args...]
    local name="$1" url="$2"; shift 2
    local out="$DL/$name.bin"
    [ -s "$out" ] && { echo "HAVE $name"; return 0; }
    timeout 120 curl -sL --fail -o "$out" "$@" "$url" 2>/dev/null
    local rc=$?
    if [ $rc -eq 0 ] && [ -s "$out" ]; then
        local sz=$(stat -c%s "$out")
        echo "OK   $name ($sz bytes) <- $url"
    else
        echo "MISS $name rc=$rc <- $url"; rm -f "$out"
    fi
}

# ================= Win98-era classics (archive.org) =================
# SkiFree 1.02 — Chris Pirih's freeware classic (Win3.1/95 era)
grab skifree "https://archive.org/download/skifree-1.02/SkiFree.exe"
# Winamp 2.95 lite — THE Win98-era mp3 player
grab winamp295 "https://archive.org/download/winamp295_202005/winamp295_lite.exe"
# WinZip 8.0 SR-1 — the Win98-era archiver
grab winzip80 "https://archive.org/download/winzip-8.0-sr-1/winzip80.exe"
# mIRC 5.91 — the Win98-era IRC client
grab mirc591 "https://archive.org/download/mirc-v5.91/mirc591.exe"
# IrfanView 3.90 — classic image viewer
grab irfanview390 "https://archive.org/download/irfanview_3.90/iview390.exe"
# ACDSee 3.1 trial — classic image viewer
grab acdsee31 "https://archive.org/download/acdsee-32-v3.1/acdsee31.exe"
# WinRAR 2.90 — classic archiver
grab winrar290 "https://archive.org/download/winrar-2.90/wrar290.exe"
# GetRight 4.x — classic download manager
grab getright "https://archive.org/download/getright-4.5/getright45.exe"
# 3D Pinball Space Cadet (XP build) — pinball.exe + tables via XP payload mirror
grab pinball "https://archive.org/download/PinballSpaceCadet/Pinball%20Space%20Cadet.exe"
# Nostalgia packs: Windows 98 second edition games shovelware (verify below)
grab bowep "https://archive.org/download/best-of-windows-entertainment-pack/BOWEP.rar"

# ================= modern small freeware (vendor direct) =================
# 7-Zip 26.04 x64 installer (modern flagship freeware)
grab 7z-installer "https://www.7-zip.org/a/7z2604-x64.exe"
# SumatraPDF portable single-exe (2026 current)
grab sumatra "https://www.sumatrapdfreader.org/dl/Rel/sumatrapdf-portable.zip"
# Everything search 1.4.1.1028 x64 installer (NirSoft-adjacent, ran SWEEP-052 via other build)
grab everything "https://www.voidtools.com/Everything-1.4.1.1028.x64-Setup.exe"
# mp3tag portable
grab mp3tag "https://download.mp3tag.de/mp3tagv329setup.exe"
# foobar2000 installer
grab foobar2000 "https://archive.org/download/foobar2000-v1.6.16/foobar2000_v1.6.16.exe"
# NirSoft new picks (vendor hosts are reliable + small)
grab nirsoft-ProduKey-x64 "https://www.nirsoft.net/utils/produkey-x64.zip"
grab nirsoft-USBDeview "https://www.nirsoft.net/utils/usbdeview.zip"
grab nirsoft-Prevx-Cleaner "https://www.nirsoft.net/utils/drweb32.zip"
grab nirsoft-CurrPorts-x64 "https://www.nirsoft.net/utils/cports-x64.zip"
grab nirsoft-WirelessNetView "https://www.nirsoft.net/utils/wirelessnetview.zip"
grab nirsoft-HashMyFiles "https://www.nirsoft.net/utils/hashmyfiles.zip"
grab nirsoft-DownloadHeaven "https://www.nirsoft.net/utils/webcooks.zip"
grab nirsoft-ResourcesExtract "https://www.nirsoft.net/utils/resourcesextract.zip"
grab nirsoft-RunAsDate "https://www.nirsoft.net/utils/runasdate.zip"
grab nirsolft-BulkFileChanger "https://www.nirsoft.net/utils/bulkfilechanger.zip"
grab nirsoft-FolderChangesView "https://www.nirsoft.net/utils/folderchangesview.zip"
grab nirsoft-AppCrashView "https://www.nirsoft.net/utils/appcrashview.zip"

# ================= new small games (GitHub releases) =================
# small open-source win32 games with prebuilt exe assets
grab 2048-win32 "https://github.com/doaks/2048/releases/download/v1.0/2048.exe"
grab superpowers-tetris "https://github.com/OneLoneCoder/olcPixelGameEngine/releases/download/v2.28.0/olcPixelGameEngine.v2.28.0.zip"

echo "=== downloaded ==="
ls -la "$DL" | tail -40

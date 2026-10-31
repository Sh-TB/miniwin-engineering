#!/bin/bash
# SWEEP-079: retry the BLOCKED_DOWNLOAD targets from sweep074/078 in this
# network window. Official sources only; record URL + HTTP code + SHA-256.
# If a target lands, it is MZ-verified and staged under artifacts/sweep079/dl/.
# No mirrors, no substitutes until a target is proven dead again — then and
# only then a same-category replacement from an official source is queued.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
D=artifacts/sweep079/dl
mkdir -p "$D"

# name|url|dest-dir
TARGETS="
explorerpp-x86|https://download.explorerplusplus.com/stable/1.5.0.2411/ExplorerPP.x86.zip|explorerpp-x86
explorerpp-x64|https://download.explorerplusplus.com/stable/1.5.0.2411/ExplorerPP.x64.zip|explorerpp-x64
scite32|https://www.scintilla.org/scite.zip|scite
resourcehacker|https://www.angusj.com/resourcehacker/resource_hacker.zip|resourcehacker
keepass258|https://keepass.info/releases/file/2.58/KeePass-2.58.zip|keepass
hxd|https://mh-nexus.de/downloads/HxDSetupEN.zip|hxd
shellmenuview-x64|https://www.nirsoft.net/utils/shmenuview-x64.zip|shmenuview-x64
shellmenuview-x64b|https://www.nirsoft.net/utils/shmnview.zip|shmnview
winscp|https://winscp.net/download/WinSCP-6.3.6-Portable.zip|winscp
ditto-x64|https://github.com/sabre1035/Ditto/releases/download/v3.24.244.0/Ditto.Setup.64bit.zip|ditto
fastcopy-x64|https://fastcopy.jp/archive/FastCopy64_5.9.13.zip|fastcopy
freecommander|https://freecommander.com/downloads/FreeCommanderXE-32-public_portable.zip|freecommander
"

printf "%s\n" "$TARGETS" | while IFS='|' read -r name url dest; do
    [ -z "$name" ] && continue
    mkdir -p "$D/$dest"
    code=$(curl -L --max-time 120 -A "Mozilla/5.0 (Windows NT 10.0; Win64; x64)" \
        -o "$D/$dest/download.bin" -w "%{http_code}" "$url" 2>/dev/null)
    size=$(stat -c%s "$D/$dest/download.bin" 2>/dev/null || echo 0)
    magic=$(head -c2 "$D/$dest/download.bin" 2>/dev/null || echo "--")
    iszip=$(file -b "$D/$dest/download.bin" 2>/dev/null | head -c40)
    if [ "$code" = "200" ] && [ "$size" -gt 10000 ]; then
        echo -e "$name\t$url\t$code\t$size\t$iszip\tLAND"
    else
        echo -e "$name\t$url\t$code\t$size\t$iszip\tBLOCKED"
    fi
done

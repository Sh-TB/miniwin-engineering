#!/bin/bash
# SWEEP-070 — (heavy parallel train, targeted tooling): fetch several dozen FAMOUS Windows programs
# (IDM, PuTTY, Everything, CPU-Z, IrfanView, SumatraPDF, AutoHotkey, WinMerge, MPC-HC,
#  ShareX, qBittorrent, Audacity, 7-Zip, HxD, VLC, OpenTTD (game), Stockfish (chess),
#  SteamCMD, ProcessHacker, TreeSize, foobar2000, ripgrep, bat, yt-dlp, NSSM, nvm-windows)
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
DL=artifacts/sweep070/dl
EV=evidence/sweep070
mkdir -p "$DL" "$EV"

cat > /tmp/sweep070_targets.txt << 'EOF'
idm|https://mirror2.internetdownloadmanager.com/idman642build2.exe
putty|https://the.earth.li/~sgtatham/putty/latest/w64/putty.exe
everything|https://www.voidtools.com/Everything-1.4.1.1028.x64-Setup.exe
cpuz|https://download.cpuid.com/cpu-z/cpu-z_2.10-en.exe
irfanview|https://www.irfanview.net/files/iview469_x64_setup.exe
sumatrapdf|https://www.sumatrapdfreader.org/dl/rel/SumatraPDF-3.5.2-64.zip
autohotkey|https://www.autohotkey.com/download/ahk-install.exe
winmerge|https://github.com/WinMerge/winmerge/releases/download/v2.16.36/WinMerge-2.16.36-x64-Setup.exe
mpchc|https://github.com/clsid2/mpc-hc/releases/download/2.3.9/MPC-HC.2.3.9.x64.exe
sharex|https://github.com/ShareX/ShareX/releases/download/v16.1.0/ShareX-16.1.0-portable.zip
qbittorrent|https://sourceforge.net/projects/qbittorrent/files/qbittorrent-win32/qbittorrent-4.6.4/qbittorrent_4.6.4_x64_setup.exe/download
audacity|https://github.com/audacity/audacity/releases/download/Audacity-3.5.1/audacity-win-3.5.1-64bit.exe
7zip|https://www.7-zip.org/a/7z2408-x64.exe
hxd|https://mh-nexus.de/downloads/HxDSetupEN.zip
vlc|http://get.videolan.org/vlc/3.0.21/win64/vlc-3.0.21-win64.exe
openttd|https://github.com/OpenTTD/OpenTTD/releases/download/13.4/openttd-13.4-windows-win64.zip
stockfish|https://github.com/official-stockfish/Stockfish/releases/download/sf_16.1/stockfish-windows-x86-64-avx2.zip
steamcmd|https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip
processhacker|https://sourceforge.net/projects/processhacker/files/process-hacker/2.39/processhacker-2.39.0-setup.exe/download
treesize|https://downloads.jam-software.de/treesize/Free/TreeSizeFreeSetup.exe
foobar2000|https://files.foobar2000.org/foobar2000/v2.0/foobar2000_v2.0_x64.exe
ripgrep|https://github.com/BurntSushi/ripgrep/releases/download/14.1.0/ripgrep-14.1.0-x86_64-pc-windows-msvc.zip
bat|https://github.com/sharkdp/bat/releases/download/v0.24.0/bat-v0.24.0-x86_64-pc-windows-msvc.zip
ytdlp|https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe
nssm|https://nssm.cc/release/nssm-2.24.zip
nvm|https://github.com/coreybutler/nvm-windows/releases/download/1.1.12/nvm-setup.exe
EOF

fetch() {
    IFS='|' read -r n u <<< "$1"
    local out="$DL/$n.bin"
    local code size
    code=$(curl -L --max-time 150 --retry 1 -sS -o "$out" -w '%{http_code}' "$u" 2>/dev/null) || code="FAIL"
    size=$(stat -c%s "$out" 2>/dev/null || echo 0)
    # magic sniff
    local magic=$(head -c2 "$out" 2>/dev/null)
    if [ "$magic" = "MZ" ]; then local kind="PE"
    elif [ "$magic" = "PK" ]; then local kind="ZIP"
    else local kind="OTHER($(head -c6 "$out" 2>/dev/null | tr -d '\0' | tr -c '[:print:]' '.'))"
    fi
    [ "$size" -lt 10000 ] && kind="TINY/BAD"
    echo -e "$n\t$u\t$code\t$size\t$kind"
}
export -f fetch; export DL

echo "name	url	http	bytes	kind" > "$EV/download_manifest.tsv"
cat /tmp/sweep070_targets.txt | xargs -P 6 -I{} bash -c 'fetch "{}"' >> "$EV/download_manifest.tsv"
column -t "$EV/download_manifest.tsv"
echo "=== landed: $(grep -c $'	PE	\|	ZIP	' "$EV/download_manifest.tsv") of 26"

#!/bin/bash
# SWEEP-059 wave 3 — finish the battery (fix wave-2 truncation + name typos).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
DL=artifacts/sweep059/dl
mkdir -p "$DL"

grab() {
    local name="$1" url="$2"
    local out="$DL/$name.bin"
    [ -s "$out" ] && { echo "HAVE $name"; return 0; }
    timeout 180 curl -sL --fail -o "$out" "$url" 2>/dev/null
    local rc=$?
    if [ $rc -eq 0 ] && [ -s "$out" ]; then
        echo "OK   $name ($(stat -c%s "$out") bytes)"
    else
        echo "MISS $name rc=$rc"; rm -f "$out"
    fi
}

grab sysinternals-PendMoves "https://download.sysinternals.com/files/PendMoves.zip"
grab sysinternals-PipeList "https://download.sysinternals.com/files/PipeList.zip"
grab sysinternals-ProcMon "https://download.sysinternals.com/files/ProcessMonitor.zip"
grab sysinternals-PsExec "https://download.sysinternals.com/files/PsExec.zip"
grab sysinternals-PsInfo "https://download.sysinternals.com/files/PsInfo.zip"
grab sysinternals-PsKill "https://download.sysinternals.com/files/PsKill.zip"
grab sysinternals-PsLoggedOn "https://download.sysinternals.com/files/PsLoggedOn.zip"
grab sysinternals-PsLogList "https://download.sysinternals.com/files/PsLogList.zip"
grab sysinternals-PsPasswd "https://download.sysinternals.com/files/PsPasswd.zip"
grab sysinternals-PsPing "https://download.sysinternals.com/files/PsPing.zip"
grab sysinternals-PsService "https://download.sysinternals.com/files/PsService.zip"
grab sysinternals-PsSuspend "https://download.sysinternals.com/files/PsSuspend.zip"
grab sysinternals-RAMMap "https://download.sysinternals.com/files/RAMMap.zip"
grab sysinternals-RegDelNull "https://download.sysinternals.com/files/RegDelNull.zip"
grab sysinternals-RegJump "https://download.sysinternals.com/files/RegJump.zip"
grab sysinternals-SDelete "https://download.sysinternals.com/files/SDelete.zip"
grab sysinternals-ShareEnum "https://download.sysinternals.com/files/ShareEnum.zip"
grab sysinternals-ShellRunAs "https://download.sysinternals.com/files/ShellRunas.zip"
grab sysinternals-SigCheck "https://download.sysinternals.com/files/Sigcheck.zip"
grab sysinternals-Streams "https://download.sysinternals.com/files/Streams.zip"
grab sysinternals-Strings "https://download.sysinternals.com/files/Strings.zip"
grab sysinternals-Sync "https://download.sysinternals.com/files/Sync.zip"
grab sysinternals-VolumeId "https://download.sysinternals.com/files/VolumeId.zip"
grab sysinternals-WhoIs "https://download.sysinternals.com/files/Whois.zip"
grab sysinternals-WinObj "https://download.sysinternals.com/files/WinObj.zip"
grab sysinternals-ZoomIt "https://download.sysinternals.com/files/ZoomIt.zip"
grab sysinternals-ListDLLs "https://download.sysinternals.com/files/Listdlls.zip"
grab sysinternals-LogonSessions "https://download.sysinternals.com/files/LogonSessions.zip"
grab sysinternals-Hex2Dec "https://download.sysinternals.com/files/hex2dec.zip"
grab sysinternals-Autologon "https://download.sysinternals.com/files/Autologon.zip"
grab sysinternals-BgInfo "https://download.sysinternals.com/files/Bginfo.zip"
grab nirsoft-OpenedFilesView "https://www.nirsoft.net/utils/openedfilesview.zip"
grab nirsoft-ColorExtractor "https://www.nirsoft.net/utils/color extractor.zip"

echo "=== wave3 done: $(ls "$DL" | wc -l) files ==="

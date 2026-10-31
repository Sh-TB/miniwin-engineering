#!/bin/bash
# SWEEP-059 Trains 3-7 wave 2 — real archive.org identifiers + more NirSoft +
# GitHub-hosted small games. Complements sweep059_download.sh wave 1.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
DL=artifacts/sweep059/dl
mkdir -p "$DL"

grab() {
    local name="$1" url="$2"; shift 2
    local out="$DL/$name.bin"
    [ -s "$out" ] && { echo "HAVE $name"; return 0; }
    timeout 180 curl -sL --fail -o "$out" "$@" "$url" 2>/dev/null
    local rc=$?
    if [ $rc -eq 0 ] && [ -s "$out" ]; then
        echo "OK   $name ($(stat -c%s "$out") bytes)"
    else
        echo "MISS $name rc=$rc"; rm -f "$out"
    fi
}

# ================= archive.org — real identifiers =================
grab winamp295 "https://archive.org/download/winamp295_full_noaod/winamp295_full_noaod.exe"
grab wep-games "https://archive.org/download/wep_20200803/WEP.zip"
# SkiFree via the Windows 3.11-for-modern item? try the dedicated SkiFree items
grab skifree "https://archive.org/download/SkiFree/SkiFree.exe"
# 3D Pinball via item "sound-1_20260904"? that's sounds only. Try de(de) pinball item:
grab pinball-xp "https://archive.org/download/3d-pinball-space-cadet-windows-xp/3D%20Pinball%20from%20XP.exe"

# ================= NirSoft wave 2 (small modern free utilities) =================
grab nirsoft-MyUninstaller "https://www.nirsoft.net/utils/myuninst.zip"
grab nirsoft-OpenedFilesView "https://www.nirsoft.net/utils/openedfilesview.zip"
grab nirsoft-RegScanner "https://www.nirsoft.net/utils/regscanner-x64.zip"
grab nirsoft-SearchMyFiles "https://www.nirsoft.net/utils/searchmyfiles-x64.zip"
grab nirsoft-ShellExView "https://www.nirsoft.net/utils/shexview-x64.zip"
grab nirsoft-SoundVolumeView "https://www.nirsoft.net/utils/soundvolumeview-x64.zip"
grab nirsoft-FileTypesMan "https://www.nirsoft.net/utils/filetypesman.zip"
grab nirsoft-IconMan "https://www.nirsoft.net/utils/iconsext.zip"
grab nirsoft-ChromeCacheView "https://www.nirsoft.net/utils/chromecacheview.zip"
grab nirsoft-IECacheView "https://www.nirsoft.net/utils/iecacheview.zip"
grab nirsoft-NetBScanner "https://www.nirsoft.net/utils/netbscanner.zip"
grab nirsoft-DNSQuerySniffer "https://www.nirsoft.net/utils/dnsquerysniffer.zip"
grab nirsoft-KeyboardStateView "https://www.nirsoft.net/utils/keyboardstateview.zip"
grab nirsoft-ColorExtractor "https://www.nirsoft.net/utils/colorextractor.zip"

# ================= Sysinternals classics (old + new) =================
grab sysinternals-PsList "https://download.sysinternals.com/files/PsList.zip"
grab sysinternals-ProcessExplorer "https://download.sysinternals.com/files/ProcessExplorer.zip"
grab sysinternals-AccessEnum "https://download.sysinternals.com/files/AccessEnum.zip"
grab sysinternals-ADExplorer "https://download.sysinternals.com/files/AdExplorer.zip"
grab sysinternals-ADInsight "https://download.sysinternals.com/files/AdInsight.zip"
grab sysinternals-Autologon "https://download.sysinternals.com/files/Autologon.zip"
grab sysinternals-BgInfo "https://download.sysinternals.com/files/BgInfo.zip"
grab sysinternals-CacheSet "https://download.sysinternals.com/files/CacheSet.zip"
grab sysinternals-ClockRes "https://download.sysinternals.com/files/ClockRes.zip"
grab sysinternals-Coreinfo "https://download.sysinternals.com/files/Coreinfo.zip"
grab sysinternals-Ctrl2Cap "https://download.sysinternals.com/files/Ctrl2Cap.zip"
grab sysinternals-DebugView "https://download.sysinternals.com/files/DebugView.zip"
grab sysinternals-Disk2vhd "https://download.sysinternals.com/files/Disk2vhd.zip"
grab sysinternals-DiskMon "https://download.sysinternals.com/files/DiskMon.zip"
grab sysinternals-DiskExt "https://download.sysinternals.com/files/DiskExt.zip"
grab sysinternals-DiskUsage "https://download.sysinternals.com/files/DU.zip"
grab sysinternals-EfsDump "https://download.sysinternals.com/files/Efsdump.zip"
grab sysinternals-Handle "https://download.sysinternals.com/files/Handle.zip"
grab sysinternals-Hex2Dec "https://download.sysinternals.com/files/Hex2dec.zip"
grab sysinternals-Junction "https://download.sysinternals.com/files/Junction.zip"
grab sysinternals-ListDLLs "https://download.sysinternals.com/files/ListDLLs.zip"
grab sysinternals-LogonSessions "https://download.sysinternals.com/files/LogonSessions.zip"
grab sysinternals-NotMyFault "https://download.sysinternals.com/files/NotMyFault.zip"
grab sysinternals-PendMoves "https://download.sysinternals.com/files/PendMoves.zip
grab sysinternals-PipeList "https://download.sysinternals.com/files/PipeList.zip"
grab sysinternals-ProcMon "https://download.sysinternals.com/files/ProcessMonitor.zip"
grab sysinternals-PsExec "https://download.sysinternals.com/files/PsExec.zip"
grab sysinternals-PsInfo "https://download.sysinternals.com/files/PsInfo.zip"
grab sysinternals-PsKill "https://download.sysinternals.com/files/PsKill.zip"
grab sysinternals-PsLoggedOn "https://download.sysinternals.com/files/PsLoggedOn.zip"
grab sysinternals-PsLogList "https://download.sysinternals.com/files/PsLogList.zip"
grab sysinternals-PsPasswd "https://download.sysinternals.com/files/PsPasswd.zip"
grab sysinternals-PsPing "https://download.sysinternals.com/files/Ping.zip"
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

echo "=== wave2 done ==="
ls "$DL" | wc -l

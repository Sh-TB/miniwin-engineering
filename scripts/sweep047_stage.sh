#!/bin/bash
# SWEEP-047 staging: download candidate x64 guests, extract *64.exe, sha-pin.
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
mkdir -p artifacts/sweep047/stage artifacts/sweep047/bin
DL=https://download.sysinternals.com/files
stage_sysinternals() {  # $1 = tool zip name, $2 = dir name
    local zip="$1" dir="$2"
    local d="artifacts/sweep047/stage/$dir"
    mkdir -p "$d"
    curl -sL --max-time 60 -o "$d/$zip" "$DL/$zip" || { echo "DL-FAIL $zip"; return 1; }
    python3 - "$d/$zip" "$d" <<'EOF'
import sys, zipfile
zp, out = sys.argv[1], sys.argv[2]
try:
    z = zipfile.ZipFile(zp)
except Exception as e:
    print(f"ZIP-FAIL {zp}: {e}"); sys.exit(1)
for i in z.infolist():
    if i.filename.lower().endswith('64.exe'):
        z.extract(i, out)
        print(f"EXTRACTED {i.filename} {i.file_size}")
EOF
}
for spec in "Handle.zip handle64" "ListDLLs.zip listdlls64" "PsList.zip pslist64" \
            "PsInfo.zip psinfo64" "Du.zip du64" "AccessChk.zip accesschk64" \
            "Whois.zip whois64" "Strings.zip strings64" "SDelete.zip sdelete64" \
            "Autologon.zip autologon64" "LogonSessions.zip logonsessions64" \
            "PipeList.zip pipelist64" "PsLoggedOn.zip psloggedon64" "PsService.zip psservice64"; do
    stage_sysinternals $1 $2 2>/dev/null || true
    zip="${spec%% *}"; dir="${spec##* }"
    stage_sysinternals "$zip" "$dir"
done
# curl mingw build (libcurl DLL class, SW-791)
mkdir -p artifacts/sweep047/stage/curl
curl -sL --max-time 120 -o artifacts/sweep047/stage/curl/curl.zip https://curl.se/windows/dl-8.16.0_2/curl-8.16.0_2-win64-mingw.zip && \
python3 - artifacts/sweep047/stage/curl/curl.zip artifacts/sweep047/stage/curl <<'EOF'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for i in z.infolist():
    if i.filename.endswith(('.exe', '.dll')) and '/' not in i.filename:
        z.extract(i, sys.argv[2]); print("EXTRACTED", i.filename, i.file_size)
EOF
echo "=== staged ==="
ls artifacts/sweep047/stage/*/ | head -40

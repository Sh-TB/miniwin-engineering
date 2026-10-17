#!/bin/bash
# SWEEP-50 phase 3: run-only pass over 50 selected programs (user directive
# 2026-10-18: "فقط اجرا بزن" — just run them all, map where each gets stuck).
#
# Selection (50) across spectra:
#   A. Sysinternals x64 console x11 (old-era MSVC/msvcrt)
#   B. GUI x64 x5 (Sysinternals + NirSoft, msvcrt 8.0)
#   C. PuTTY family x6 (MSVC, static CRT, 3 console + 3 GUI)
#   D. Go static x7 (kernel32-only imports)
#   E. Rust/MSVC x12 (vcruntime140 + api-ms-win-crt)
#   F. C/UCRT static x2 (nasm, ninja)
#   G. OpenSSH x3 (needs libcrypto.dll — DLL-load frontier)
#   H. Misc x4 (yt-dlp PyInstaller, wimlib DLL-dep, 7zr x86 probe, strings x86 probe)
#
# Per program: 10s timeout, no args, CWD = its own scratch dir so
# miniwin-results/<base>/api_trace.json lands there and is archived.

set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DL="$REPO/artifacts/sweep50/dl"
RUNS="$REPO/artifacts/sweep50/runs"
mkdir -p "$RUNS"

PROGRAMS=(
  # A. Sysinternals x64 console
  "si-A-du64|si-du/du64.exe"
  "si-A-handle64|si-handle/handle64.exe"
  "si-A-pipelist64|si-pipelist/pipelist64.exe"
  "si-A-psexec64|si-psexec/PsExec64.exe"
  "si-A-psinfo64|si-psinfo/PsInfo64.exe"
  "si-A-sigcheck64|si-sigcheck/sigcheck64.exe"
  "si-A-strings64|si-strings/strings64.exe"
  "si-A-testlimit64|si-testlimit/Testlimit64.exe"
  "si-A-volumeid64|si-volumeid/Volumeid64.exe"
  "si-A-accesschk64|si-accesschk/accesschk64.exe"
  "si-A-coreinfo64|si-coreinfo/Coreinfo64.exe"
  # B. GUI x64 (Sysinternals + NirSoft)
  "si-B-bginfo64|si-bginfo/Bginfo64.exe"
  "si-B-coreinfoex64|si-coreinfo/CoreinfoEx64.exe"
  "ns-cports|cports/cports.exe"
  "ns-produkey|produkey/ProduKey.exe"
  "ns-searchmyfiles|searchmyfiles/SearchMyFiles.exe"
  # C. PuTTY family
  "putty-plink|plink/plink.exe"
  "putty-pscp|pscp/pscp.exe"
  "putty-psftp|psftp/psftp.exe"
  "putty-gui|putty/putty.exe"
  "putty-puttygen|puttygen/puttygen.exe"
  "putty-pageant|pageant/pageant.exe"
  # D. Go static
  "go-age|age/age.exe"
  "go-agekeygen|age/age-keygen.exe"
  "go-mkcert|mkcert/mkcert-v1.4.4-windows-amd64.exe"
  "go-bombardier|bombardier/bombardier-windows-amd64.exe"
  "go-frpc|frp/frpc.exe"
  "go-lego|lego/lego.exe"
  "go-cloudflared|cloudflared/cloudflared-windows-amd64.exe"
  # E. Rust / MSVC
  "rust-rg|ripgrep/rg.exe"
  "rust-fd|fd/fd.exe"
  "rust-bat|bat/bat.exe"
  "rust-hexyl|hexyl/hexyl.exe"
  "rust-dust|dust/dust.exe"
  "rust-sd|sd/sd.exe"
  "rust-hyperfine|hyperfine/hyperfine.exe"
  "rust-just|just/just.exe"
  "rust-procs|procs/procs.exe"
  "rust-dufs|dufs/dufs.exe"
  "rust-xh|xh/xh.exe"
  "rust-watchexec|watchexec/watchexec.exe"
  # F. C / UCRT static
  "c-nasm|nasm/nasm.exe"
  "c-ninja|ninja/ninja.exe"
  # G. OpenSSH (libcrypto.dll dependency)
  "ssh-keygen|openssh/ssh-keygen.exe"
  "ssh|openssh/ssh.exe"
  "sftp|openssh/sftp.exe"
  # H. Misc spectra
  "pyinstaller-ytdlp|ytdlp/yt-dlp.exe"
  "dlldep-wimlib|wimlib/wimlib-imagex.exe"
  "x86-7zr|7zr/7zr.exe"
  "x86-si-strings|si-strings/strings.exe"
)

SUMMARY="$RUNS/summary.tsv"
printf "name\texit\tclass\tbytes_out\tapi_events\tlast_api\n" > "$SUMMARY"

for entry in "${PROGRAMS[@]}"; do
  name="${entry%%|*}"; rel="${entry#*|}"
  exe="$DL/$rel"
  dir="$RUNS/$name"
  mkdir -p "$dir"
  if [ ! -f "$exe" ]; then
    printf "%s\t-\tMISSING-EXE\t-\t-\t-\n" "$name" >> "$SUMMARY"
    continue
  fi
  sha=$(sha256sum "$exe" | cut -d' ' -f1)
  printf 'exe=%s\nsha256=%s\ntimeout=10s\nargs=none\ndate_utc=%s\n' \
    "$rel" "$sha" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$dir/meta.txt"
  ( cd "$dir" && timeout 10 "$REPO/minwin_loader" "$exe" \
      > stdout.txt 2> stderr.txt )
  code=$?
  # archive trace if produced (loader writes miniwin-results/<base>/ in CWD)
  trace=$(find "$dir/miniwin-results" -name api_trace.json 2>/dev/null | head -1)
  events="-"; lastapi="-"
  if [ -n "$trace" ]; then
    cp "$trace" "$dir/api_trace.json" 2>/dev/null
    events=$(grep -o '"event"' "$dir/api_trace.json" 2>/dev/null | wc -l)
    lastapi=$(python3 - "$dir/api_trace.json" <<'EOF'
import json,sys
try:
    txt=open(sys.argv[1]).read()
    if not txt.rstrip().endswith(']'): txt += ']}'
    d=json.loads(txt)
    ev=d if isinstance(d,list) else d.get('events',[])
    apis=[e for e in ev if isinstance(e,dict) and e.get('api')]
    print((apis[-1]['api'] if apis else '-')[-60:])
except Exception as e:
    print('parse-err')
EOF
)
  fi
  out=$( (cat "$dir/stdout.txt" "$dir/stderr.txt") | wc -c )
  # classification
  class="OTHER"
  [ "$code" = "124" ] && class="HANG"
  grep -qi "segmentation\|SIGSEGV\|SIGILL\|SIGABRT" "$dir/stderr.txt" 2>/dev/null && class="CRASH-LOADER"
  grep -qi "unresolved" "$dir/stdout.txt" "$dir/stderr.txt" 2>/dev/null && class="IMPORT-MISS"
  grep -qi "unsupported\|not.*x64\|PE32\b\|Invalid PE\|invalid PE\|failed to load" "$dir/stdout.txt" "$dir/stderr.txt" 2>/dev/null && class="LOAD-REJECT"
  [ "$code" = "0" ] && [ "$out" -gt 0 ] && class="RAN-OUTPUT"
  [ "$code" = "0" ] && [ "$out" = "0" ] && class="RAN-SILENT"
  printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$name" "$code" "$class" "$out" "$events" "$lastapi" >> "$SUMMARY"
  echo "[$code $class] $name"
done
echo "---- summary ----"
column_tab=$'\t'
awk -F'\t' 'NR>1{c[$3]++} END{for(k in c) print c[k], k}' "$SUMMARY" | sort -rn

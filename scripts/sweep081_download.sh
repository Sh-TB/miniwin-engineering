#!/bin/bash
# SWEEP-081 download preflight — official URLs only, record URL+SHA-256.
# Plugin apps: winnc (user), hxd (user), everything/mp3tag/renamer (tiny), notepad2 (new unknown)
cd "$(dirname "$0")/.." || exit 1
mkdir -p artifacts/sweep081/dl
LOG=evidence/sweep081/dl_urls.txt
: > "$LOG"

dl() { # name url
  local name="$1" url="$2" out="artifacts/sweep081/dl/$1"
  echo "== $name $url"
  curl -sL --max-time 420 --retry 2 -o "$out" "$url" 2>>"$LOG"
  local rc=$? sz=$(stat -c%s "$out" 2>/dev/null || echo 0)
  if [ $rc -eq 0 ] && [ "$sz" -gt 10000 ]; then
    local sha=$(sha256sum "$out" | cut -d' ' -f1)
    echo -e "$name\t$url\t$sz\t$sha" >> "$LOG"
    echo "OK $name $sz $sha"
  else
    rm -f "$out"; echo "FAIL $name rc=$rc size=$sz"
  fi
}
dl winnc11.setup.exe "https://www.winnc.com/setup.exe" &
dl hxd.portable.zip "https://mh-nexus.de/downloads/HxDPortableSetup.zip" &
dl hxd.setup.zip "https://mh-nexus.de/downloads/HxDSetupEN.zip" &
dl everything.1.4.1.1028.x64.zip "https://www.voidtools.com/Everything-1.4.1.1028.x64.zip" &
dl mp3tagv329setup.exe "https://download.mp3tag.de/mp3tagv329setup.exe" &
dl renamer75.portable.zip "https://www.den4b.com/download/renamer/portable/7.5" &
wait
echo "=== dl_urls.txt ==="
cat "$LOG"

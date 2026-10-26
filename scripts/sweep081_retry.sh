#!/bin/bash
# SWEEP-081 download retry: den4b 2-step key flow, mp3tag with UA, HxD full retry matrix
cd "$(dirname "$0")/.." || exit 1
mkdir -p artifacts/sweep081/dl
LOG=evidence/sweep081/dl_urls.txt
UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126 Safari/537.36"

# --- den4b renamer: key flow ---
KEY=$(curl -s --max-time 30 "https://www.den4b.com/download/renamer/portable/7.5" | grep -oE 'portable/7\.5\?key=[a-f0-9]+' | head -1 | cut -d= -f2)
echo "renamer key=${KEY:0:12}..."
if [ -n "$KEY" ]; then
  OUT=artifacts/sweep081/dl/renamer75.portable.zip
  curl -sL --max-time 300 -A "$UA" -o "$OUT" "https://www.den4b.com/download/renamer/portable/7.5?key=$KEY"
  sz=$(stat -c%s "$OUT" 2>/dev/null || echo 0)
  if [ "$sz" -gt 100000 ]; then
    echo -e "renamer75.portable.zip\thttps://www.den4b.com/download/renamer/portable/7.5 (key flow)\t$sz\t$(sha256sum "$OUT" | cut -d' ' -f1)" >> "$LOG"
    echo "OK renamer $sz"
  else echo "FAIL renamer size=$sz"; rm -f "$OUT"; fi
fi

# --- mp3tag with UA ---
OUT=artifacts/sweep081/dl/mp3tagv329setup.exe
curl -sL --max-time 240 -A "$UA" -o "$OUT" "https://download.mp3tag.de/mp3tagv329setup.exe"
sz=$(stat -c%s "$OUT" 2>/dev/null || echo 0)
if [ "$sz" -gt 1000000 ]; then
  echo -e "mp3tagv329setup.exe\thttps://download.mp3tag.de/mp3tagv329setup.exe\t$sz\t$(sha256sum "$OUT" | cut -d' ' -f1)" >> "$LOG"
  echo "OK mp3tag $sz"
else echo "FAIL mp3tag size=$sz"; rm -f "$OUT"; fi

# --- HxD retry matrix ---
for u in "https://mh-nexus.de/downloads/HxDPortableSetup.zip" \
         "https://mh-nexus.de/downloads/HxDSetupEN.zip" \
         "http://mh-nexus.de/downloads/HxDPortableSetup.zip" \
         "https://www.mh-nexus.de/downloads/HxDPortableSetup.zip"; do
  OUT=artifacts/sweep081/dl/hxd.$(basename $u)
  code=$(curl -sL --max-time 45 -A "$UA" -o "$OUT" -w "%{http_code}" "$u")
  sz=$(stat -c%s "$OUT" 2>/dev/null || echo 0)
  if [ "$code" = "200" ] && [ "$sz" -gt 500000 ]; then
    echo -e "hxd.$(basename $u)\t$u\t$sz\t$(sha256sum "$OUT" | cut -d' ' -f1)" >> "$LOG"
    echo "OK hxd $u $sz"; break
  else echo "FAIL hxd $u code=$code size=$sz"; rm -f "$OUT"; fi
done

echo "=== dl_urls.txt now ==="; cat "$LOG"

#!/bin/bash
# LADDER-082 WinRAR 4.11 round trip under MiniWin:
# guest Rar.exe creates archive -> guest Rar.exe extracts -> byte-exact check.
# Payload is generated inline (no repo fixture dependency). Exit 0 only when
# the payload round-trips byte-exact through BOTH guest passes.
set -u
REPO="/home/z/my-project/workspace/miniwin-engineering"
RAR="$REPO/samples/real/winrar/sfx/Rar.exe"
LOADER="$REPO/minwin_loader"

cd "$(mktemp -d /tmp/rar_roundtrip_XXXXXX)" || exit 1
head -c 401408 /dev/urandom > ladder_payload.bin || exit 1
EXPECT=$(sha256sum ladder_payload.bin | cut -d' ' -f1)

"$LOADER" "$RAR" a -m5 out.rar ladder_payload.bin >/dev/null 2>&1 || { echo "guest create FAILED"; exit 1; }
[ -f out.rar ] || { echo "no archive"; exit 1; }

rm -rf ex && mkdir ex && cd ex || exit 1
"$LOADER" "$RAR" x ../out.rar >/dev/null 2>&1 || { echo "guest extract FAILED"; exit 1; }
[ -f ladder_payload.bin ] || { echo "no extracted file"; exit 1; }

GOT=$(sha256sum ladder_payload.bin | cut -d' ' -f1)
if [ "$GOT" = "$EXPECT" ]; then
    echo "ROUNDTRIP OK sha=$GOT"
    exit 0
fi
echo "SHA MISMATCH got=$GOT"
exit 1

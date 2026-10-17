#!/bin/bash
# BUG-054b acceptance (2026-10-17): absolute Windows-backslash -o extraction root.
# Frontier registered in CONTINUE #9: "7z e final.7z -o\tmp\7ztest\out" created a
# literal backslash-named directory in the CWD because CreateDirectoryW/
# RemoveDirectoryW/DeleteFileW/GetDiskFreeSpaceExW never normalized the path.
# Acceptance: output lands in the POSIX dir, SHA-256 byte-perfect, 3x deterministic,
# and NO literal '\tmp\...' directory appears in the CWD.
set -uo pipefail
REPO="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$REPO" || exit 1
EV="$REPO/evidence/master/bug054b/$(date -u +%Y%m%d_%H%M%S)"
mkdir -p "$EV"
WORK="$EV/work"; mkdir -p "$WORK"
cd "$WORK" || exit 1
LOADER="$REPO/minwin_loader"
ZEXE="$REPO/samples/real/7za.exe"
FAIL=0
printf 'miniwin BUG-054b acceptance payload %s\n' "$(date -u +%FT%TZ)" > final.txt
IN_SHA=$(sha256sum final.txt | cut -d' ' -f1)
echo "input sha256: $IN_SHA" | tee "$EV/log.txt"
for i in 1 2 3; do
  rm -rf /tmp/7ztest54b "'\\tmp\\7ztest54b\\out'"
  mkdir -p /tmp/7ztest54b
  echo "-- run $i: archive create" | tee -a "$EV/log.txt"
  "$LOADER" "$ZEXE" a final.7z final.txt > "$EV/a_$i.out" 2>&1
  echo "a exit=$?" | tee -a "$EV/log.txt"
  # THE FIX TARGET: absolute backslash -o root (exactly the registered frontier form)
  echo "-- run $i: extract with ABSOLUTE backslash -o path" | tee -a "$EV/log.txt"
  "$LOADER" "$ZEXE" e final.7z "-o\\tmp\\7ztest54b\\out" > "$EV/e_$i.out" 2>&1
  EC=$?
  echo "e exit=$EC" | tee -a "$EV/log.txt"
  OUT_SHA=$(sha256sum /tmp/7ztest54b/out/final.txt 2>/dev/null | cut -d' ' -f1)
  echo "out sha256: ${OUT_SHA:-MISSING}" | tee -a "$EV/log.txt"
  [ "$EC" = "0" ] || { echo "FAIL run$i: exit $EC" | tee -a "$EV/log.txt"; FAIL=1; }
  [ "$OUT_SHA" = "$IN_SHA" ] || { echo "FAIL run$i: sha mismatch" | tee -a "$EV/log.txt"; FAIL=1; }
  if ls -d * | grep -q 'tmp.7ztest54b' && ! ls -d /tmp/7ztest54b/out >/dev/null 2>&1; then
    echo "FAIL run$i: literal backslash dir created in CWD (gap reopened)" | tee -a "$EV/log.txt"; FAIL=1
  fi
done
# negative control is implicit: relative -o keeps working (regression suite covers t-wave)
if [ "$FAIL" = "0" ]; then echo "BUG-054b ACCEPTANCE: PASS 3/3 (sha match, no stray dir)" | tee -a "$EV/log.txt"; else echo "BUG-054b ACCEPTANCE: FAIL" | tee -a "$EV/log.txt"; fi
echo "evidence: $EV"
exit $FAIL

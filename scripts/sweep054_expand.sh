#!/bin/bash
# SWEEP-054: expand all XP x64 .EX_/.DL_ payloads via the ENGINE's own 7za (samples/real/7za.exe).
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
RAW=artifacts/sweep054/xp64_raw
BIN=artifacts/sweep054/bin
LOG=artifacts/sweep054/expand_log.tsv
mkdir -p "$BIN"
echo -e "payload\texename\trc\tstdout_tail" > "$LOG"
for f in "$RAW"/*.ex_ "$RAW"/*.dl_; do
    base=$(basename "$f")
    name="${base%.*}"
    out=$(timeout 60 ./minwin_loader samples/real/7za.exe e -y -o"$BIN" "$f" 2>"$BIN/${name}.expand.err")
    rc=$?
    tail1=$(echo "$out" | tail -1 | cut -c1-60)
    echo -e "$base\t$name\trc=$rc\t$tail1" >> "$LOG"
    echo "$base rc=$rc $tail1"
done
echo "=== expanded files ==="
ls -la "$BIN" | grep -v "\.expand\.err"

#!/bin/bash
# SWEEP-056 Train 1: expand XP x64 payloads via the ENGINE's own 7za and stage
# the per-guest scratch dirs (exe + its private DLLs), then verify each with a
# quick engine run. Re-establishes the SWEEP-054 guest farm lost between rounds.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
RAW=artifacts/sweep056/xp64_raw
BIN=artifacts/sweep056/bin
G=artifacts/sweep056/guests
mkdir -p "$BIN" "$G"

# --- expand all .EX_/.DL_/.CH_ payloads ---
for f in "$RAW"/*.ex_ "$RAW"/*.dl_ "$RAW"/*.ch_; do
    [ -e "$f" ] || continue
    base=$(basename "$f"); name="${base%.*}"
    [ -e "$BIN/$name" ] && { echo "have $name"; continue; }
    timeout 60 ./minwin_loader samples/real/7za.exe e -y -o"$BIN" "$f" > /dev/null 2> "$BIN/${name}.expand.err"
    echo "$base rc=$?"
done
echo "=== expanded ==="; ls "$BIN" | grep -v expand.err

# --- stage guests (exe + private DLLs per SWEEP-054 layout) ---
# 7za keeps the archived name: CALC.EX_ expands to calc.exe (lowercase).
stage() { local name="$1"; shift; mkdir -p "$G/$name"; cp "$BIN/$name.exe" "$G/$name/$name.exe"; for d in "$@"; do cp "$BIN/$d" "$G/$name/"; done; }
stage winmine
stage sol cards.dll
stage freecell cards.dll
stage mshearts cards.dll
stage spider cards.dll
stage calc
stage mspaint mspaint.chm
stage notepad
stage wordpad riched20.dll mfc42u.dll
stage charmap
stage clipbrd
stage write
stage winver
stage sndvol32
echo "=== staged ==="; ls "$G"

# --- quick verification run: exit code + frame count ---
echo "=== verify ==="
for p in winmine sol freecell mshearts spider calc notepad wordpad charmap clipbrd write winver sndvol32; do
    rm -rf "miniwin-results/$p.exe"
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=150 timeout 60 ./minwin_loader "$G/$p/$p.exe" \
        > /dev/null 2> "/tmp/${p}_v.txt"
    rc=$?
    nf=$(ls "miniwin-results/$p.exe/screenshots/frame_"*.bmp 2>/dev/null | wc -l)
    echo "$p rc=$rc frames=$nf"
done

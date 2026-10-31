#!/bin/bash
# SWEEP-061 Train 1 — re-establish the XP x64 guest farm (lost between rounds,
# recurring pattern since SWEEP-056). Expand payloads via the ENGINE's own
# 7za (samples/real/7za.exe), stage per-guest scratch dirs, verify quickly.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
RAW=artifacts/sweep056/xp64_raw
BIN=artifacts/sweep061/bin
G=artifacts/sweep061/guests
mkdir -p "$BIN" "$G"

# --- expand all .EX_/.DL_/.CH_ payloads ---
for f in "$RAW"/*.ex_ "$RAW"/*.dl_ "$RAW"/*.ch_; do
    [ -e "$f" ] || continue
    base=$(basename "$f"); name="${base%.*}"
    [ -e "$BIN/$name" ] && { echo "have $name"; continue; }
    timeout 60 ./minwin_loader samples/real/7za.exe e -y -o"$BIN" "$f" > /dev/null 2> "$BIN/${name}.expand.err"
    echo "$base rc=$?"
done
echo "=== expanded ==="
ls "$BIN" | grep -v expand.err

# --- stage guests (exe + private DLLs per SWEEP-054/056 contract) ---
stage() { local name="$1"; shift; mkdir -p "$G/$name"; cp "$BIN/$name.exe" "$G/$name/$name.exe" 2>/dev/null; for d in "$@"; do cp "$BIN/$d" "$G/$name/" 2>/dev/null; done; }
stage winmine
stage sol cards.dll
stage freecell cards.dll
stage mshearts cards.dll mfc42u.dll
stage spider cards.dll
stage calc
stage mspaint mspaint.chm mfc42u.dll
stage notepad
stage wordpad riched20.dll mfc42u.dll
stage charmap
stage clipbrd
stage write
stage winver
stage sndvol32
stage taskmgr
stage regedt32
stage drwtsn32
echo "=== staged ==="
ls "$G"

# --- quick verification run: exit code + frame count ---
echo "=== verify ==="
for p in winmine sol freecell mshearts spider calc notepad wordpad charmap clipbrd write winver sndvol32 taskmgr regedt32 drwtsn32; do
    rm -rf "miniwin-results/$p.exe"
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=150 timeout 60 ./minwin_loader "$G/$p/$p.exe" \
        > /dev/null 2> "/tmp/${p}_v.txt"
    rc=$?
    nf=$(ls "miniwin-results/$p.exe/screenshots/frame_"*.bmp 2>/dev/null | wc -l)
    echo "$p rc=$rc frames=$nf"
done

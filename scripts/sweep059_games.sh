#!/bin/bash
# SWEEP-059 Train 7 — new small free games (2026-era GitHub releases).
# Chocolate Doom 3.1.1 (modern port of the 1993 classic) + shareware WAD.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
DL=artifacts/sweep059/dl
EX=artifacts/sweep059/expanded
G=artifacts/sweep059/guests
mkdir -p "$EX"

grab() {
    local name="$1" url="$2"
    local out="$DL/$name.bin"
    [ -s "$out" ] && { echo "HAVE $name"; return 0; }
    timeout 240 curl -sL --fail -o "$out" "$url" 2>/dev/null
    local rc=$?
    [ $rc -eq 0 ] && [ -s "$out" ] && echo "OK   $name ($(stat -c%s "$out") bytes)" || { echo "MISS $name rc=$rc"; rm -f "$out"; }
}

# --- Chocolate Doom suite (modern small-ish games, win64) ---
grab chocolate-doom "https://github.com/chocolate-doom/chocolate-doom/releases/download/chocolate-doom-3.1.1/chocolate-doom-3.1.1-win64.zip"
grab chocolate-heretic "https://github.com/chocolate-doom/chocolate-doom/releases/download/chocolate-doom-3.1.1/chocolate-heretic-3.1.1-win64.zip"
grab chocolate-hexen "https://github.com/chocolate-doom/chocolate-doom/releases/download/chocolate-doom-3.1.1/chocolate-hexen-3.1.1-win64.zip"
grab chocolate-strife "https://github.com/chocolate-doom/chocolate-doom/releases/download/chocolate-doom-3.1.1/chocolate-strife-3.1.1-win64.zip"

# --- shareware doom1.wad (the game data, freely redistributable) ---
grab doom1-wad "https://archive.org/download/doom-collection-files/doom1.wad"

# expand + stage
for n in chocolate-doom chocolate-heretic chocolate-hexen chocolate-strife; do
    [ -s "$DL/$n.bin" ] || continue
    [ -d "$EX/$n" ] || { mkdir -p "$EX/$n"; unzip -o -qq "$DL/$n.bin" -d "$EX/$n" 2>/dev/null; echo "unzip $n rc=$?"; }
done

find "$EX/chocolate-doom" -name "*.exe" 2>/dev/null | head -8

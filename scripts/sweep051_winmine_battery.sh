#!/bin/bash
# SWEEP-051 SW-1261b: winmine (Windows XP Minesweeper) INTERACTION battery.
# The coordinate-mouse channel (MINWIN_MOUSE) drives the custom-drawn board:
#   A: board click delivered as WM_LBUTTONDOWN/UP pair to the main window
#   B: guest REACTS like real XP winmine — SetTimer(id=1,1000ms) armed by the
#      click (the game clock starts on the first left click) + board repaint
#   C: guest SURVIVES the interaction: exit 0, no [CRASH]
# 3 runs x 3 checks = 9/12 required (deterministic).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
OUT=artifacts/sweep051/winmine_battery
mkdir -p "$OUT"
pass=0; fail=0
for i in 1 2 3; do
    d="$OUT/run$i"; mkdir -p "$d"
    MINWIN_MOUSE="24,64;40,64;56,64;24,80" MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=300 \
        timeout 60 ./minwin_loader artifacts/sweep050/guests/winmine/winmine.exe \
        > "$d/stdout.txt" 2> "$d/stderr.txt"
    rc=$?
    T=miniwin-results/winmine.exe/api_trace.json
    cp "$T" "$d/api_trace.json" 2>/dev/null
    # A: click pair delivered
    downs=$(grep -c "WM_LBUTTONDOWN 24,64" "$d/api_trace.json" 2>/dev/null)
    ups=$(grep -c "WM_LBUTTONUP 24,64" "$d/api_trace.json" 2>/dev/null)
    # B: game clock armed by the click + board repaint
    timer=$(grep -c "SetTimer(id=1, 1000ms)" "$d/api_trace.json" 2>/dev/null)
    blt=$(grep -c "BitBlt 16x16" "$d/api_trace.json" 2>/dev/null)
    # C: survived
    crash=$(grep -c "\[CRASH\]" "$d/stderr.txt")
    if [ "$downs" -ge 1 ] && [ "$ups" -ge 1 ]; then echo "PASS A run$i (click pair delivered)"; pass=$((pass+1));
    else echo "FAIL A run$i (downs=$downs ups=$ups)"; fail=$((fail+1)); fi
    if [ "$timer" -ge 1 ] && [ "$blt" -ge 1 ]; then echo "PASS B run$i (clock armed, $blt board BitBlts)"; pass=$((pass+1));
    else echo "FAIL B run$i (timer=$timer blt=$blt)"; fail=$((fail+1)); fi
    if [ "$rc" -eq 0 ] && [ "$crash" -eq 0 ]; then echo "PASS C run$i (rc=0, no crash)"; pass=$((pass+1));
    else echo "FAIL C run$i (rc=$rc crash=$crash)"; fail=$((fail+1)); fi
done
echo "== SWEEP-051 winmine battery: $pass pass / $fail fail (need 9 pass) =="
[ "$pass" -ge 9 ] && [ "$fail" -eq 0 ] && exit 0 || exit 1

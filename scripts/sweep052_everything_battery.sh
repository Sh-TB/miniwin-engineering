#!/bin/bash
# SWEEP-052 SW-1261b: Everything 1.4.1 (the Windows-exclusive NTFS
# instant-search program — no Linux equivalent exists) INTERACTION battery
# through the coordinate-mouse channel (MINWIN_MOUSE, built for winmine).
#   A: search-box click (400,20) AND listview click (400,100) delivered as
#      WM_LBUTTONDOWN/UP pairs through the GUI pump
#   B: guest REACTS — WM_PAINT dispatched after the search-box click (the
#      app's own WndProc repaints) and the WM_TIMER pump stays alive after
#      the interaction
#   C: guest SURVIVES: exit 0, no [CRASH]
# 3 runs x 3 checks = 9 pass required (deterministic).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
OUT=artifacts/sweep052/everything_battery
mkdir -p "$OUT"
pass=0; fail=0
for i in 1 2 3; do
    d="$OUT/run$i"; mkdir -p "$d"
    MINWIN_MOUSE="400,20;400,100" MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=200 \
        timeout 90 ./minwin_loader artifacts/sweep050/stage/everything/everything.exe \
        > "$d/stdout.txt" 2> "$d/stderr.txt"
    rc=$?
    T=miniwin-results/everything.exe/api_trace.json
    cp "$T" "$d/api_trace.json" 2>/dev/null
    # A: both click pairs delivered
    d20=$(grep -c "WM_LBUTTONDOWN 400,20" "$d/api_trace.json" 2>/dev/null)
    u20=$(grep -c "WM_LBUTTONUP 400,20" "$d/api_trace.json" 2>/dev/null)
    d100=$(grep -c "WM_LBUTTONDOWN 400,100" "$d/api_trace.json" 2>/dev/null)
    u100=$(grep -c "WM_LBUTTONUP 400,100" "$d/api_trace.json" 2>/dev/null)
    # B: reaction — WM_PAINT after the search-box DOWN, WM_TIMER pump alive
    paints=$(grep -c "dispatch WM_PAINT" "$d/api_trace.json" 2>/dev/null)
    timers=$(grep -c "dispatch WM_TIMER" "$d/api_trace.json" 2>/dev/null)
    # C: survived
    crash=$(grep -c "\[CRASH\]" "$d/stderr.txt")
    if [ "$d20" -ge 1 ] && [ "$u20" -ge 1 ] && [ "$d100" -ge 1 ] && [ "$u100" -ge 1 ]; then
        echo "PASS A run$i (both click pairs delivered)"; pass=$((pass+1));
    else echo "FAIL A run$i (d20=$d20 u20=$u20 d100=$d100 u100=$u100)"; fail=$((fail+1)); fi
    if [ "$paints" -ge 1 ] && [ "$timers" -ge 1 ]; then
        echo "PASS B run$i (paint=$paints reaction, timer pump $timers)"; pass=$((pass+1));
    else echo "FAIL B run$i (paints=$paints timers=$timers)"; fail=$((fail+1)); fi
    if [ "$rc" -eq 0 ] && [ "$crash" -eq 0 ]; then echo "PASS C run$i (rc=0, no crash)"; pass=$((pass+1));
    else echo "FAIL C run$i (rc=$rc crash=$crash)"; fail=$((fail+1)); fi
done
echo "== SWEEP-052 everything battery: $pass pass / $fail fail (need 9 pass) =="
[ "$pass" -ge 9 ] && [ "$fail" -eq 0 ] && exit 0 || exit 1

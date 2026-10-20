#!/bin/bash
# SWEEP-026 GUI battery on the SW-1021..1100 batch binary.
# 1. pump-on runs for known-window programs (+ putty NEW this sweep)
# 2. procmon64 SendMessageW rate re-measure (SW-1023 real-timer gate headline)
# 3. calc keyboard battery via sweep023_calc_battery.sh
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DL="$REPO/artifacts/sweep50/dl"
OUT="$REPO/artifacts/sweep026/gui"
mkdir -p "$OUT"
TSV="$OUT/gui_battery.tsv"
echo -e "program\tmode\trun\texit\tframes\tsendmsg\ttrace_lines" > "$TSV"

pumpon() { # pname exe secs
    local pname="$1" exe="$2" secs="${3:-10}"
    local d="$OUT/$pname"
    mkdir -p "$d"
    ( cd "$d" && MW_GUI_EVENTS=1 MINWIN_TRACE=1 timeout "$secs" \
        "$REPO/minwin_loader" "$exe" >stdout.txt 2>stderr.txt )
    local code=$?
    local frames=$(find "$d/miniwin-results" -name '*.bmp' 2>/dev/null | wc -l)
    local sendmsg=$(grep -c 'SendMessageW' "$d/miniwin-results"/*/api_trace.json 2>/dev/null | head -1 | cut -d: -f2)
    sendmsg=${sendmsg:-0}
    local tl=$(wc -l < "$d/miniwin-results"/*/api_trace.json 2>/dev/null | head -1)
    tl=${tl:-0}
    echo -e "$pname\tpump-on\t1\t$code\t$frames\t$sendmsg\t$tl" >> "$TSV"
}

for p in bginfo64 produkey cports diskmon64 autoruns64 rammap64 putty puttygen; do
    exe=$(ls "$DL/$p/"*.exe 2>/dev/null | head -1)
    [ -n "$exe" ] && pumpon "$p" "$exe" 10
done

# procmon64 with trace timing: SW-1023 headline (was 26.35M SendMessageW/12s)
exe=$(ls "$DL/procmon64/"*.exe 2>/dev/null | head -1)
[ -n "$exe" ] && pumpon "procmon64" "$exe" 12

echo "GUI battery done: $TSV"
column -t "$TSV"

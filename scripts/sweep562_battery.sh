#!/bin/bash
# SWEEP-007 — SW-562: MW_GUI_EVENTS=1 battery over the (re-acquired) SWEEP-003
# GUI program set. Maps which programs survive REAL event delivery
# (WM_PAINT / WM_TIMER through a guest WNDPROC) vs the SWEEP-003 v1 contract
# (immediate WM_QUIT, no events delivered).
#
# Per program x3 runs, no scripted keys (pure survival mapping):
#   MW_GUI_EVENTS=1  (pump v2 armed, budget default 400)
# Class markers grepped from the per-run api_trace.json (MW_TRACE lands there,
# not stderr — stderr only carries [WARN]/[STUB]/[CRASH]):
#   ARMED  = "[GUI] event pump v2 ARMED"          (pump entered)
#   BUDGET = "event budget .* exhausted"          (full lifecycle through pump)
#   NOSRC  = "no event source"                    (pump ran, guest idled)
#   PAINTn = "[GUI-EVENT] .* -> WM_PAINT" count   (real paint deliveries)
#   EXITn  = process exit code
# Frames kept from run1 (EndPaint/BitBlt dumps) for any program that drew.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
OUT=artifacts/sweep50/runs_gui562
EV=evidence/sweep007_battery
rm -rf "$OUT" "$EV"; mkdir -p "$OUT" "$EV"

declare -A EXE=(
  [produkey]=artifacts/sweep50/dl/produkey/ProduKey.exe
  [cports]=artifacts/sweep50/dl/cports/cports.exe
  [searchmyfiles]=artifacts/sweep50/dl/searchmyfiles/SearchMyFiles.exe
  [putty]=artifacts/sweep50/dl/putty/putty.exe
  [pageant]=artifacts/sweep50/dl/pageant/pageant.exe
  [puttygen]=artifacts/sweep50/dl/puttygen/puttygen.exe
  [procexp64]=artifacts/sweep50/dl/procexp64/procexp64.exe
  [procmon64]=artifacts/sweep50/dl/procmon64/Procmon64.exe
  [autoruns64]=artifacts/sweep50/dl/autoruns64/Autoruns64.exe
  [rammap64]=artifacts/sweep50/dl/rammap64/RAMMap64.exe
  [zoomit64]=artifacts/sweep50/dl/zoomit64/ZoomIt64.exe
  [diskmon64]=artifacts/sweep50/dl/diskmon64/Diskmon64.exe
  [bginfo64]=artifacts/sweep50/dl/bginfo64/Bginfo64.exe
  [coreinfoex64]=artifacts/sweep50/dl/coreinfoex64/Coreinfo64.exe
  [winmergeu]=artifacts/sweep50/dl/winmergeu/WinMergeU.exe
)

: > "$EV/battery_summary.tsv"
printf 'program\trun\texit\tarmed\tpaints\tbudget_quit\tframes\n' > "$EV/battery_raw.tsv"

for name in produkey cports searchmyfiles putty pageant puttygen \
            procexp64 procmon64 autoruns64 rammap64 zoomit64 \
            diskmon64 bginfo64 coreinfoex64 winmergeu; do
  exe="${EXE[$name]}"
  for r in 1 2 3; do
    rd="$OUT/$name/run$r"
    mkdir -p "$rd"
    rm -rf "miniwin-results/$(basename "$exe")"
    MW_GUI_EVENTS=1 timeout 120 ./minwin_loader "$exe" \
        > "$rd/stdout.txt" 2> "$rd/stderr.txt"
    echo $? > "$rd/exit_code.txt"
    # per-run trace JSON (MW_TRACE target) + screenshots inventory
    cp "miniwin-results/$(basename "$exe")/api_trace.json" "$rd/api_trace.json" 2>/dev/null
    if ls miniwin-results/$(basename "$exe")/screenshots/*.bmp >/dev/null 2>&1; then
      ls miniwin-results/$(basename "$exe")/screenshots/*.bmp | wc -l > "$rd/frames.txt"
    else
      echo 0 > "$rd/frames.txt"
    fi
    ec=$(cat "$rd/exit_code.txt")
    trf="$rd/api_trace.json"; [ -f "$trf" ] || trf="$rd/stderr.txt"
    armed=$(grep -c "event pump v2 ARMED" "$trf" 2>/dev/null); armed=${armed:-0}
    paints=$(grep -c "\-> WM_PAINT" "$trf" 2>/dev/null); paints=${paints:-0}
    bq=$(grep -cE "event budget .* exhausted|no event source" "$trf" 2>/dev/null); bq=${bq:-0}
    frames=$(cat "$rd/frames.txt")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$r" "$ec" "$armed" "$paints" "$bq" "$frames" >> "$EV/battery_raw.tsv"
    # keep run1 artifacts for evidence
    if [ "$r" = 1 ]; then
      grep -m1 "Command line:" "$rd/stderr.txt" > "$EV/${name}_cmdline.txt" 2>/dev/null || true
      if ls miniwin-results/$(basename "$exe")/screenshots/*.bmp >/dev/null 2>&1; then
        mkdir -p "$EV/${name}_frames"
        cp miniwin-results/$(basename "$exe")/screenshots/*.bmp "$EV/${name}_frames/" 2>/dev/null
      fi
      cp "$rd/stderr.txt" "$EV/${name}_run1_stderr.txt"
    fi
    echo "$name run$r: exit=$ec armed=$armed paints=$paints quitpath=$bq frames=$frames"
  done
done

# determinism: exit code must be identical across the 3 runs of each program
python3 - "$EV/battery_raw.tsv" <<'EOF' | tee "$EV/determinism.txt"
import sys, collections
rows = collections.defaultdict(list)
with open(sys.argv[1]) as f:
    next(f)
    for line in f:
        p = line.rstrip("\n").split("\t")
        rows[p[0]].append((p[2], p[3], p[4], p[5], p[6]))
det = 0
for name in sorted(rows):
    xs = rows[name]
    ok = all(v == xs[0] for v in xs)
    det += ok
    print(f"{name}: {'DETERMINISTIC' if ok else 'NON-DETERMINISTIC'} "
          f"exit={xs[0][0]} paints={xs[0][2]} frames={xs[0][4]}")
print(f"--- {det}/{len(rows)} programs 3x-deterministic")
EOF
cp "$EV/battery_raw.tsv" "$OUT/battery_raw.tsv" 2>/dev/null
echo "battery complete -> $EV"

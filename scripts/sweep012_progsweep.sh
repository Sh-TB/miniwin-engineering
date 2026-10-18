#!/bin/bash
# SWEEP-012 — full-pool program sweep with PROGRESS metrics (user directive
# 2026-10-18: run as many of the 100-list programs as available, measure how
# far each gets; "changes without progress are worthless").
#
# Usage: scripts/sweep012_progsweep.sh <tag>     e.g. BASELINE / AFTER
# Output: artifacts/sweep012/runs-<tag>/  +  summary.tsv
#
# Per-program metrics: exit, class, out_bytes, stub_calls, n_unres,
# last_stub (the gate), api_events, last_api, timeout flag.

set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-BASELINE}"
RUNS="$REPO/artifacts/sweep012/runs-$TAG"
SUMMARY="$RUNS/summary.tsv"
META="$RUNS/meta.tsv"
if [ ! -f "$SUMMARY" ]; then
  printf "name\texit\tclass\tout_bytes\tstub_calls\tn_unres\tlast_stub\tapi_events\tlast_api\ttimeout\n" > "$SUMMARY"
  printf "name\texe\n" > "$META"
fi

LOADER="$REPO/minwin_loader"
TIMEOUT_S="${TIMEOUT_S:-10}"
# Keep api_trace.json ON (64KiB-buffered since SWEEP-011) — it is a progress
# signal (last resolved API). Only skip BMP screenshot disk writes.
export MINWIN_SCREENSHOT=0

# ---- pool discovery -------------------------------------------------------
PROGS=()   # "name|path"
name_of() { printf '%s' "$(basename "$1" | sed 's/[^A-Za-z0-9._-]/_/g')"; }
add() { [ -f "$2" ] && PROGS+=("$(name_of "$2")|$2"); }

# A. sweep50 downloaded pool (Sysinternals/NirSoft/PuTTY) — real files only
while IFS= read -r f; do add "" "$f"; done < <(find "$REPO/artifacts/sweep50/dl" -iname "*.exe" -type f 2>/dev/null)
# B. opensource pool (sqlite/7z/jq)
while IFS= read -r f; do add "" "$f"; done < <(find "$REPO/artifacts/opensource/_dl" -iname "*.exe" -type f 2>/dev/null)
# C. samples/real pool (gzip/lz4/zstd/md5deep/7z/jq full trees)
while IFS= read -r f; do add "" "$f"; done < <(find "$REPO/samples/real" -iname "*.exe" -type f 2>/dev/null)
# D. games (our own winexes) + UPX + sqlite in opensource deeper
[ -f "$REPO/artifacts/sweep006/breakout.exe" ] && PROGS+=("game-breakout|$REPO/artifacts/sweep006/breakout.exe")
[ -f "$REPO/artifacts/sweep005/snake.exe" ] && PROGS+=("game-snake|$REPO/artifacts/sweep005/snake.exe")
[ -f "$REPO/samples/upx_decompressed.exe" ] && PROGS+=("upx-decompressed|$REPO/samples/upx_decompressed.exe")
# E. SWEEP-013: our own GUI calculator acceptance guest (user demand)
[ -f "$REPO/tests/gui/calc.exe" ] && PROGS+=("gui-calc|$REPO/tests/gui/calc.exe")

# dedupe by name (first wins) — on collision disambiguate with parent dir
declare -A SEEN
ORDER=()
for e in "${PROGS[@]}"; do
  n="${e%%|*}"
  if [ -n "${SEEN[$n]:-}" ]; then
    # collision: prefix with pool dir tag (x64 re-download vs x86 variant etc.)
    p="${e#*|}"; parent=$(basename "$(dirname "$p")")
    n="${parent}_${n}"
    [ -n "${SEEN[$n]:-}" ] && continue
  fi
  SEEN[$n]=1; ORDER+=("$n|${e#*|}")
done

# resumable: names already in summary.tsv are skipped (append mode)
DONE_NAMES="$(cut -f1 "$SUMMARY" 2>/dev/null | tail -n +2 | sort -u)"

for e in "${ORDER[@]}"; do
  name="${e%%|*}"; exe="${e#*|}"
  grep -qx "$name" <<<"$DONE_NAMES" && { echo "[skip] $name (already done)"; continue; }
  dir="$RUNS/$name"; mkdir -p "$dir"
  printf "%s\t%s\n" "$name" "$exe" >> "$META"
  ( cd "$dir" && timeout "$TIMEOUT_S" "$LOADER" "$exe" > stdout.txt 2> stderr.txt < /dev/null )
  code=$?
  tflag=0; [ "$code" = "124" ] && tflag=1

  # payload bytes = stdout + stderr minus engine chatter lines
  outb=$(wc -c < "$dir/stdout.txt" 2>/dev/null); outb=${outb:-0}

  # stub stats from stderr (import NAMES are decoded offline by
  # scripts/sweep012_progress.py — the [STUB] line only carries guest_rip)
  stub_calls=$(grep -c '^\[STUB\]' "$dir/stderr.txt" 2>/dev/null); stub_calls=${stub_calls:-0}
  n_unres=0; last_stub="-"
  if [ "$stub_calls" -gt 0 ]; then last_stub="decode-offline"; fi

  # api trace stats
  trace="$dir/miniwin-results/api_trace.json"
  [ -f "$trace" ] || trace=$(find "$dir" -name api_trace.json 2>/dev/null | head -1)
  aevents=0; last_api="-"
  if [ -n "${trace:-}" ] && [ -f "$trace" ]; then
    cp "$trace" "$dir/api_trace.json" 2>/dev/null
    read -r aevents last_api <<PYEOF
$(python3 - "$dir/api_trace.json" <<'PY'
import json,sys
try:
    txt=open(sys.argv[1]).read()
    if not txt.rstrip().endswith(']'): txt += ']}'
    d=json.loads(txt)
    ev=d if isinstance(d,list) else d.get('events',[])
    apis=[e['api'] for e in ev if isinstance(e,dict) and e.get('api')]
    print(len(apis), (apis[-1] if apis else '-')[-48:])
except Exception:
    print("0 parse-err")
PY
)
PYEOF
  fi

  # ---- classification ----
  class="OTHER"
  [ "$code" = "124" ] && class="HANG"
  grep -qi "Architecture not\|not x64\|PE32+\?.*rejected\|Invalid PE\|failed to load\|unsupported" "$dir/stderr.txt" 2>/dev/null && class="LOAD-REJECT"
  grep -q "^\[CRASH\]" "$dir/stderr.txt" 2>/dev/null && class="CRASH-GUEST"
  grep -qi "segmentation fault\|SIGILL\|Aborted\|double free" "$dir/stderr.txt" 2>/dev/null && [ "$class" = "OTHER" ] && class="CRASH-GUEST"
  [ "$code" = "139" -o "$code" = "134" -o "$code" = "136" ] && [ "$class" = "OTHER" ] && class="CRASH-GUEST"
  if [ "$stub_calls" -gt 0 ] && [ "$class" = "OTHER" ]; then class="STUB-GATE"; fi
  if [ "$code" != "124" ] && [ "$class" = "OTHER" ]; then
    [ "$outb" -gt 0 ] && class="RAN-OUTPUT" || class="RAN-SILENT"
  fi

  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$name" "$code" "$class" "$outb" "$stub_calls" "$n_unres" "$last_stub" "$aevents" "$last_api" "$tflag" \
    >> "$SUMMARY"
  echo "[$code $class stubs=$stub_calls out=$outb] $name"
done

echo "---- summary ($TAG) ----"
awk -F'\t' 'NR>1{c[$3]++} END{for(k in c) printf "%5d  %s\n", c[k], k}' "$SUMMARY" | sort -rn
echo "total: $(( $(wc -l < "$SUMMARY") - 1 )) programs"

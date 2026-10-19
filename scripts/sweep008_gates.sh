#!/bin/bash
# SWEEP-008 — gate re-run: autoruns64 / rammap64 / procexp64
# Baseline (SWEEP-003 runs_gui): exits 1 / 1 / 2 with anonymous stub-0 gates.
# This run: named stubs + honest registry + EULA dialog (MW_EULA_ACCEPT=1).
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
EV=artifacts/sweep008/gates
rm -rf "$EV"; mkdir -p "$EV"

declare -A EXE=(
  [autoruns64]="artifacts/sweep50/dl/autoruns64/Autoruns64.exe"
  [rammap64]="artifacts/sweep50/dl/rammap64/RAMMap64.exe"
  [procexp64]="artifacts/sweep50/dl/procexp64/procexp64.exe"
)

for prog in autoruns64 rammap64 procexp64; do
  for mode in default eula; do
    d="$EV/${prog}_${mode}"
    mkdir -p "$d"
    if [ "$mode" = eula ]; then
      MW_EULA_ACCEPT=1 MW_GUI_EVENTS=1 timeout 60 ./minwin_loader "${EXE[$prog]}" -accepteula \
        > "$d/stdout.txt" 2> "$d/stderr.txt"
    else
      timeout 60 ./minwin_loader "${EXE[$prog]}" \
        > "$d/stdout.txt" 2> "$d/stderr.txt"
    fi
    echo $? > "$d/exit_code.txt"
    grep -c "^\[STUB\]" "$d/stderr.txt" > "$d/stub_lines.txt" 2>/dev/null || echo 0 > "$d/stub_lines.txt"
    echo "== $prog/$mode exit=$(cat "$d/exit_code.txt") stub_lines=$(cat "$d/stub_lines.txt")"
  done
done

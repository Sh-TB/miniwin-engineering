#!/bin/bash
# SWEEP-008 breadth batch: run every exe under artifacts/sweep50/dl, quantify
# per-program progress: exit code, unresolved count, stub-call count, dialogs.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DL="$REPO/artifacts/sweep50/dl"
RUNS="$REPO/artifacts/sweep50/runs_sweep008"
mkdir -p "$RUNS"
SUMMARY="$RUNS/summary.tsv"
printf "name\texit\twarns\tstubs\tdialogs\tout_bytes\tclass\n" > "$SUMMARY"

for d in "$DL"/*/; do
  name=$(basename "$d")
  exe=$(ls "$d" | grep -iE '\.exe$' | head -1)
  [ -z "$exe" ] && continue
  dir="$RUNS/$name"
  rm -rf "$dir"; mkdir -p "$dir"
  ( cd "$dir" && timeout 25 "$REPO/minwin_loader" "$DL/$name/$exe" \
      > stdout.txt 2> stderr.txt )
  code=$?
  [ "$code" = "124" ] && code=HANG
  warns=$(grep -c 'unresolved:' "$dir/stderr.txt" 2>/dev/null)
  stubs=$(grep -c '\[STUB\]' "$dir/stderr.txt" 2>/dev/null)
  dialogs=$(grep -c 'dialog shown\|MessageBox' "$dir/stderr.txt" 2>/dev/null)
  outb=$(( $(wc -c < "$dir/stdout.txt" 2>/dev/null) + $(wc -c < "$dir/stderr.txt" 2>/dev/null) ))
  class="OTHER"
  case "$code" in
    0) class="RAN-EXIT0";;
    HANG) class="HANG";;
    139) class="CRASH-SEGV";;
  esac
  [ "$class" = "OTHER" ] && [ "$code" != 0 ] && class="RAN-EXIT$code"
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$name" "$code" "$warns" "$stubs" "$dialogs" "$outb" "$class" >> "$SUMMARY"
  echo "[$code $class] $name warns=$warns stubs=$stubs"
done
echo "---- summary at $SUMMARY ----"
column -t -s$'\t' "$SUMMARY" 2>/dev/null || cat "$SUMMARY"

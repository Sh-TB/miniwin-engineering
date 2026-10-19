#!/bin/bash
# SWEEP-008 — mass breadth sweep, PARALLEL (8 workers), per-program result
# files (no shared-file locking), assembled into one TSV at the end.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
EV=artifacts/sweep008/mass
rm -rf "$EV"; mkdir -p "$EV"
OUT="$EV/sweep008_mass.tsv"

run_one() {
  exe="$1"
  tag=$(echo "$exe" | sed 's|^artifacts/||; s|/|__|g; s|\.exe$||')
  d="$EV/$tag"; mkdir -p "$d"
  timeout 12 ./minwin_loader "$exe" > "$d/stdout.txt" 2> "$d/stderr.txt"
  ec=$?
  stubs=$(grep -ac "^\[STUB\]" "$d/stderr.txt" 2>/dev/null); [ -z "$stubs" ] && stubs=0
  first_stub=$(grep -a "^\[STUB\]" "$d/stderr.txt" 2>/dev/null | head -1 | sed 's/.* (call #[0-9]*) //; s/ guest_rip.*//' | tr -d '\t\n\r')
  first_unres=$(grep -a "^\[WARN\]   unresolved:" "$d/stderr.txt" 2>/dev/null | head -1 | sed 's/.*unresolved: //' | tr -d '\t\n\r')
  if [ "$ec" = "1" ] && [ "$stubs" -le 2 ] 2>/dev/null; then
    timeout 12 ./minwin_loader "$exe" -accepteula > "$d/stdout_acc.txt" 2> "$d/stderr_acc.txt"
    ec2=$?
    if [ "$ec2" != "$ec" ]; then
      ec=$ec2
      stubs=$(grep -ac "^\[STUB\]" "$d/stderr_acc.txt" 2>/dev/null); [ -z "$stubs" ] && stubs=0
      first_stub=$(grep -a "^\[STUB\]" "$d/stderr_acc.txt" 2>/dev/null | head -1 | sed 's/.* (call #[0-9]*) //; s/ guest_rip.*//' | tr -d '\t\n\r')
      first_unres=$(grep -a "^\[WARN\]   unresolved:" "$d/stderr_acc.txt" 2>/dev/null | head -1 | sed 's/.*unresolved: //' | tr -d '\t\n\r')
      echo "accepteula variant" > "$d/note.txt"
    fi
  fi
  printf '%s\n' "$tag|$ec|$stubs|$first_stub|$first_unres" > "$d/result.txt"
}
export -f run_one
export EV OUT

find artifacts -name "*.exe" -type f | grep -v "_dl" | sort | \
  xargs -P 8 -I{} bash -c 'run_one "$@"' _ {}

echo -e "program\texit\tstub_calls\tfirst_stub\tfirst_unresolved" > "$OUT"
cat "$EV"/*/result.txt 2>/dev/null | sort | \
  awk -F'|' 'BEGIN{OFS="\t"} {print $1,$2,$3,$4,$5}' >> "$OUT"
echo "== mass sweep done: $(( $(wc -l < "$OUT") - 1 )) rows"

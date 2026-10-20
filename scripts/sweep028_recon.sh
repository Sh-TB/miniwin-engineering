#!/bin/bash
# SWEEP-028 recon: per-program unresolved-name census on the SWEEP-027 pin binary.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DL="$REPO/artifacts/sweep50/dl"
OUT="$REPO/artifacts/sweep028/recon"
mkdir -p "$OUT"
SUM="$OUT/unresolved_summary.tsv"
echo -e "program\texit\tunresolved_count" > "$SUM"

for dir in "$DL"/*/; do
    pname=$(basename "$dir")
    exe=""
    for cand in "$dir"*.exe; do
        [ -f "$cand" ] && exe="$cand" && break
    done
    [ -z "$exe" ] && continue
    d="$OUT/$pname"
    mkdir -p "$d"
    ( cd "$d" && MW_GUI_EVENTS=0 timeout 25 "$REPO/minwin_loader" "$exe" \
        >stdout.txt 2>stderr.txt )
    code=$?
    cnt=$(grep -c 'unresolved:' "$d/stderr.txt" 2>/dev/null || echo 0)
    echo -e "$pname\t$code\t$cnt" >> "$SUM"
done
echo "recon done: $SUM"

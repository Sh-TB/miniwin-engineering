#!/bin/bash
# SWEEP-046 «۲۰ قطار» baseline census: 3 runs per corpus program, deterministic exit/stub/unresolved tally.
# (adapted from sweep043_census.sh; corpus unchanged, binary b33ec3f7 at HEAD 3ff438c baseline verification)
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
mkdir -p artifacts/sweep046
OUT=artifacts/sweep046/census.tsv
echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$OUT"
for d in artifacts/sweep50/dl/*/; do
    prog=$(basename "$d")
    exe=$(ls "$d" | grep -i '\.exe$' | head -1)
    if [ -z "$exe" ]; then
        echo -e "$prog\tNO-EXE\t-\t-\t-\t-\t-\t-" >> "$OUT"
        continue
    fi
    mkdir -p "artifacts/sweep046/census/$prog"
    for r in 1 2 3; do
        mkdir -p "artifacts/sweep046/census/$prog/run$r"
        MW_GUI_EVENTS=0 timeout 25 ./minwin_loader "$d$exe" \
            > "artifacts/sweep046/census/$prog/run$r/stdout.txt" 2> "artifacts/sweep046/census/$prog/run$r/stderr.txt"
        rc=$?
        stub=$(grep -c '\[STUB\]' "artifacts/sweep046/census/$prog/run$r/stderr.txt" || true)
        unres=$(grep -c 'imports unresolved' "artifacts/sweep046/census/$prog/run$r/stderr.txt" || true)
        ob=$(stat -c%s "artifacts/sweep046/census/$prog/run$r/stdout.txt")
        eb=$(stat -c%s "artifacts/sweep046/census/$prog/run$r/stderr.txt")
        echo -e "$prog\t$exe\t$r\t$rc\t$stub\t$unres\t$ob\t$eb" >> "$OUT"
    done
done
echo "=== census done ==="
awk -F'\t' 'NR>1 && $3!="-" {print $1, $4}' "$OUT" | sort | uniq -c | sort -k2 | awk '{c[$2 FS $3]=$1} END {for (k in c) print c[k], k}' | sort -k2

#!/bin/bash
# SWEEP-047 «ادامه قطار» census: 3 runs per program, deterministic exit/stub/unresolved tally.
# Corpus = artifacts/sweep50/dl/* (16) + new SWEEP-047 guests from artifacts/sweep047/stage/*.
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
mkdir -p artifacts/sweep047
OUT=artifacts/sweep047/census.tsv
echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$OUT"
run_one() {
    local d="$1" prog="$2"
    local exe=$(ls "$d" 2>/dev/null | grep -i '\.exe$' | head -1)
    if [ -z "$exe" ]; then
        echo -e "$prog\tNO-EXE\t-\t-\t-\t-\t-\t-" >> "$OUT"
        return
    fi
    for r in 1 2 3; do
        mkdir -p "artifacts/sweep047/census/$prog/run$r"
        MW_GUI_EVENTS=0 timeout 25 ./minwin_loader "$d$exe" \
            > "artifacts/sweep047/census/$prog/run$r/stdout.txt" 2> "artifacts/sweep047/census/$prog/run$r/stderr.txt"
        rc=$?
        stub=$(grep -c '\[STUB\]' "artifacts/sweep047/census/$prog/run$r/stderr.txt" || true)
        unres=$(grep -c 'imports unresolved' "artifacts/sweep047/census/$prog/run$r/stderr.txt" || true)
        ob=$(stat -c%s "artifacts/sweep047/census/$prog/run$r/stdout.txt")
        eb=$(stat -c%s "artifacts/sweep047/census/$prog/run$r/stderr.txt")
        echo -e "$prog\t$exe\t$r\t$rc\t$stub\t$unres\t$ob\t$eb" >> "$OUT"
    done
}
for d in artifacts/sweep50/dl/*/; do
    run_one "$d" "$(basename "$d")"
done
for d in artifacts/sweep047/stage/*/; do
    run_one "$d" "new-$(basename "$d")"
done
echo "=== census done ==="
awk -F'\t' 'NR>1 && $3!="-" {print $1, $4}' "$OUT" | sort | uniq -c | sort -k2 | awk '{c[$2 FS $3]=$1} END {for (k in c) print c[k], k}' | sort -k2

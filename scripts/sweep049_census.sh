#!/bin/bash
# SWEEP-049 census: 3 runs per program, deterministic exit/stub/unresolved tally.
# Part 1: DL corpus regression (16 programs). Part 2: new real guests + frontier re-runs.
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
PART=${1:-all}
mkdir -p artifacts/sweep049/census
OUT=artifacts/sweep049/census/census.tsv
echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$OUT"
run_one() {
    local d="$1" prog="$2"
    local exe=$(ls "$d" 2>/dev/null | grep -i '\.exe$' | head -1)
    if [ -z "$exe" ]; then
        echo -e "$prog\tNO-EXE\t-\t-\t-\t-\t-\t-" >> "$OUT"
        return
    fi
    for r in 1 2 3; do
        mkdir -p "artifacts/sweep049/census/$prog/run$r"
        MW_GUI_EVENTS=0 timeout 25 ./minwin_loader "$d$exe" \
            > "artifacts/sweep049/census/$prog/run$r/stdout.txt" 2> "artifacts/sweep049/census/$prog/run$r/stderr.txt"
        rc=$?
        stub=$(grep -c '\[STUB\]' "artifacts/sweep049/census/$prog/run$r/stderr.txt" || true)
        unres=$(grep -c 'unresolved import called' "artifacts/sweep049/census/$prog/run$r/stderr.txt" || true)
        ob=$(stat -c%s "artifacts/sweep049/census/$prog/run$r/stdout.txt")
        eb=$(stat -c%s "artifacts/sweep049/census/$prog/run$r/stderr.txt")
        echo -e "$prog\t$exe\t$r\t$rc\t$stub\t$unres\t$ob\t$eb" >> "$OUT"
    done
}
if [ "$PART" != "guests" ]; then
  for d in artifacts/sweep50/dl/*/; do
      run_one "$d" "$(basename "$d")"
  done
fi
if [ "$PART" != "corpus" ]; then
  for d in artifacts/sweep049/stage/guests/*/; do
      run_one "$d" "new-$(basename "$d")"
  done
fi
echo "=== census ($PART) done ==="

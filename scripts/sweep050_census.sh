#!/bin/bash
# SWEEP-050 census: 3 runs per program, deterministic exit/stub/unresolved tally.
# Part 1: DL corpus regression. Part 2: SWEEP-050 real-program guests.
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
mkdir -p artifacts/sweep050/census
OUT=artifacts/sweep050/census/census.tsv
echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$OUT"
run_one() {
    local d="$1" prog="$2" exe="$3"
    for r in 1 2 3; do
        mkdir -p "artifacts/sweep050/census/$prog/run$r"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=200 timeout 60 ./minwin_loader "$d$exe" \
            > "artifacts/sweep050/census/$prog/run$r/stdout.txt" 2> "artifacts/sweep050/census/$prog/run$r/stderr.txt"
        rc=$?
        stub=$(grep -c '\[STUB\]' "artifacts/sweep050/census/$prog/run$r/stderr.txt" || true)
        unres=$(grep -c 'unresolved import called' "artifacts/sweep050/census/$prog/run$r/stderr.txt" || true)
        ob=$(stat -c%s "artifacts/sweep050/census/$prog/run$r/stdout.txt")
        eb=$(stat -c%s "artifacts/sweep050/census/$prog/run$r/stderr.txt")
        echo -e "$prog\t$exe\t$r\t$rc\t$stub\t$unres\t$ob\t$eb" >> "$OUT"
    done
}
# SWEEP-050 real-program guests
run_one "artifacts/sweep050/stage/everything/" "everything" "everything.exe"
for p in mspaint mshearts wordpad drwtsn32 calc winmine notepad write winver sol freecell charmap clipbrd sndvol32 net taskmgr regedt32; do
    run_one "artifacts/sweep050/guests/$p/" "$p" "$p.exe"
done
run_one "artifacts/sweep050/guests/ping/" "ping" "ping.exe"
echo "=== census done ==="
column -t "$OUT" 2>/dev/null || cat "$OUT"

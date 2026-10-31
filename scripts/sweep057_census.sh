#!/bin/bash
# SWEEP-057 census: 3 runs per guest, deterministic exit/stub/unresolved tally.
# Guest set = the re-staged XP farm (14) — SW-1279 fix A-F chain (metrics guard, WS_CHILD routing, class-proc registry, BeginPaint no-wipe, child rects, timer-wait)
# change verified against the whole battery.
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
mkdir -p artifacts/sweep057/census
OUT=artifacts/sweep057/census/census.tsv
echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$OUT"
run_one() {
    local d="$1" prog="$2" exe="$3"
    for r in 1 2 3; do
        mkdir -p "artifacts/sweep057/census/$prog/run$r"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=150 timeout 60 ./minwin_loader "$d$exe" \
            > "artifacts/sweep057/census/$prog/run$r/stdout.txt" 2> "artifacts/sweep057/census/$prog/run$r/stderr.txt"
        rc=$?
        stub=$(grep -c '\[STUB\]' "artifacts/sweep057/census/$prog/run$r/stderr.txt" || true)
        unres=$(grep -c 'unresolved import called' "artifacts/sweep057/census/$prog/run$r/stderr.txt" || true)
        ob=$(stat -c%s "artifacts/sweep057/census/$prog/run$r/stdout.txt")
        eb=$(stat -c%s "artifacts/sweep057/census/$prog/run$r/stderr.txt")
        echo -e "$prog\t$exe\t$r\t$rc\t$stub\t$unres\t$ob\t$eb" >> "$OUT"
    done
}
for p in winmine sol freecell mshearts spider calc notepad mspaint wordpad charmap clipbrd write winver sndvol32; do
    run_one "artifacts/sweep056/guests/$p/" "$p" "$p.exe"
done
echo "=== census done ==="
cat "$OUT"

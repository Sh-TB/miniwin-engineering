#!/bin/bash
# SWEEP-058 census: 3 runs per guest, deterministic exit/stub/unresolved tally.
# Set = XP farm (14) + Sysinternals plane (pslist64/autologon64/logonsessions64/
# accesschk64/handle64) — the SWEEP-058 engine changes (perflib plane, LSA
# layouts, SCM cookies, stack fill) verified against the whole battery.
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
mkdir -p artifacts/sweep058/census
OUT=artifacts/sweep058/census/census.tsv
echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$OUT"
run_one() {
    local d="$1" prog="$2" exe="$3"
    for r in 1 2 3; do
        mkdir -p "artifacts/sweep058/census/$prog/run$r"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=150 timeout 60 ./minwin_loader "$d$exe" \
            > "artifacts/sweep058/census/$prog/run$r/stdout.txt" 2> "artifacts/sweep058/census/$prog/run$r/stderr.txt"
        rc=$?
        stub=$(grep -c '\[STUB\]' "artifacts/sweep058/census/$prog/run$r/stderr.txt" || true)
        unres=$(grep -c 'unresolved import called' "artifacts/sweep058/census/$prog/run$r/stderr.txt" || true)
        ob=$(stat -c%s "artifacts/sweep058/census/$prog/run$r/stdout.txt")
        eb=$(stat -c%s "artifacts/sweep058/census/$prog/run$r/stderr.txt")
        echo -e "$prog\t$exe\t$r\t$rc\t$stub\t$unres\t$ob\t$eb" >> "$OUT"
    done
}
for p in winmine sol freecell mshearts spider calc notepad mspaint wordpad charmap clipbrd write winver sndvol32; do
    run_one "artifacts/sweep056/guests/$p/" "$p" "$p.exe"
done
run_one "artifacts/sweep047/bin/" pslist64        pslist64.exe
run_one "artifacts/sweep047/bin/" autologon64     autologon64.exe
run_one "artifacts/sweep047/bin/" logonsessions64 logonsessions64.exe
run_one "artifacts/sweep047/bin/" accesschk64     accesschk64.exe
run_one "artifacts/sweep047/bin/" handle64        handle64.exe
echo "=== census done ==="
cat "$OUT"

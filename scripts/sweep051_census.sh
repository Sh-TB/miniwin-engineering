#!/bin/bash
# SWEEP-051 census: 3 runs per program, deterministic exit/stub/unresolved tally.
# Part 1: SWEEP-050 guests (regression). Part 2: DL corpus (regression).
# Part 3: SWEEP-051 soffice (the ODF display program — may exit 99 controlled).
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
mkdir -p artifacts/sweep051/census
OUT=artifacts/sweep051/census/census.tsv
echo -e "program\texename\trun\texit\tstub_calls\tunresolved\tstdout_bytes\tstderr_bytes" > "$OUT"
run_one() {
    local d="$1" prog="$2" exe="$3" extra="$4"
    for r in 1 2 3; do
        mkdir -p "artifacts/sweep051/census/$prog/run$r"
        env $extra MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=200 timeout 90 ./minwin_loader "$d$exe" \
            > "artifacts/sweep051/census/$prog/run$r/stdout.txt" 2> "artifacts/sweep051/census/$prog/run$r/stderr.txt"
        rc=$?
        stub=$(grep -c '\[STUB\]' "artifacts/sweep051/census/$prog/run$r/stderr.txt" || true)
        unres=$(grep -c 'unresolved import called' "artifacts/sweep051/census/$prog/run$r/stderr.txt" || true)
        ob=$(stat -c%s "artifacts/sweep051/census/$prog/run$r/stdout.txt")
        eb=$(stat -c%s "artifacts/sweep051/census/$prog/run$r/stderr.txt")
        echo -e "$prog\t$exe\t$r\t$rc\t$stub\t$unres\t$ob\t$eb" >> "$OUT"
    done
}
# SWEEP-050 guests (regression baseline = all exit-0)
for p in mspaint mshearts wordpad drwtsn32 calc winmine notepad write winver sol freecell charmap clipbrd sndvol32 net taskmgr regedt32; do
    run_one "artifacts/sweep050/guests/$p/" "$p" "$p.exe"
done
run_one "artifacts/sweep050/guests/ping/" "ping" "ping.exe"
run_one "artifacts/sweep050/stage/everything/" "everything" "everything.exe"
# DL corpus
for d in adexplorer64 autoruns64 bginfo64 coreinfoex64 cports diskmon64 pageant procexp64 procmon64 produkey putty puttygen rammap64 searchmyfiles winmergeu zoomit64; do
    exe=$(ls "artifacts/sweep50/dl/$d/" 2>/dev/null | grep -i "\.exe$" | head -1)
    [ -z "$exe" ] && continue
    run_one "artifacts/sweep50/dl/$d/" "$d" "$exe"
done
# SWEEP-051: the ODF display program
run_one "artifacts/sweep051/guests/soffice/" "soffice" "soffice.bin"
echo "=== census done ==="
cat "$OUT"

#!/bin/bash
# SWEEP-052 regression subset: SWEEP-050/051 guests x3 determinism after the
# SW-1262 engine changes (RtlPcToFileHeader guest-DLL lookup, AccessCheck v2
# DACL walk, DuplicateToken v2, stub-name registry, CreateFileW caller log).
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
mkdir -p artifacts/sweep052/census
OUT=artifacts/sweep052/census/regression.tsv
echo -e "program\texename\trun\texit\tstub_calls\tunresolved" > "$OUT"
run_one() {
    local d="$1" prog="$2" exe="$3"
    for r in 1 2 3; do
        mkdir -p "artifacts/sweep052/census/$prog/run$r"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=200 timeout 90 ./minwin_loader "$d$exe" \
            > "artifacts/sweep052/census/$prog/run$r/stdout.txt" 2> "artifacts/sweep052/census/$prog/run$r/stderr.txt"
        rc=$?
        stub=$(grep -c '\[STUB\]' "artifacts/sweep052/census/$prog/run$r/stderr.txt" || true)
        unres=$(grep -c 'unresolved import called' "artifacts/sweep052/census/$prog/run$r/stderr.txt" || true)
        echo -e "$prog\t$exe\t$r\t$rc\t$stub\t$unres" >> "$OUT"
    done
}
for p in mspaint mshearts wordpad drwtsn32 calc winmine notepad write winver sol freecell; do
    run_one "artifacts/sweep050/guests/$p/" "$p" "$p.exe"
done
run_one "artifacts/sweep050/stage/everything/" "everything" "everything.exe"
run_one "artifacts/sweep051/guests/soffice/" "soffice" "soffice.bin"
for d in procexp64 winmergeu zoomit64 pageant putty; do
    exe=$(ls "artifacts/sweep50/dl/$d/" 2>/dev/null | grep -i "\.exe$" | head -1)
    [ -z "$exe" ] && continue
    run_one "artifacts/sweep50/dl/$d/" "$d" "$exe"
done
echo "=== regression census done ==="
cat "$OUT"

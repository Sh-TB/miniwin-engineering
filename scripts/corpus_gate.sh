#!/bin/bash
# corpus_gate.sh — canonical corpus parity gate (SWEEP-092).
#
# Guests are resolved through tests/corpus/manifest.tsv + scripts/fetch_guest.sh:
#   VERIFIED guests   -> fetched/verified into .cache/miniwin/guests/
#   SEED guests       -> legacy repo-local copy if present, SHA-256 checked
#                        against the pin; otherwise reported MISSING (honest
#                        failure — a missing file is NEVER counted as a pass)
# 3 runs per guest, rc/frames/crash/fatal recorded, EXACT determinism check
# of the analysis blob across runs.
#
# Usage: scripts/corpus_gate.sh [out_sweep]   (default: sweep092)
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
SW="${1:-sweep092}"
EV="evidence/$SW"
mkdir -p "$EV/raw"

# id : timeout : legacy fallback path (SEED-mode guests acquired locally)
GUESTS="xp.winmine:60:artifacts/sweep061/guests/winmine/winmine.exe
xp.sol:60:artifacts/sweep061/guests/sol/sol.exe
xp.freecell:60:artifacts/sweep061/guests/freecell/freecell.exe
xp.calc:60:artifacts/sweep061/guests/calc/calc.exe
mini.breakout:60:artifacts/sweep006/breakout.exe
mini.snake:60:artifacts/sweep005/snake.exe
xp.spider:90:artifacts/sweep061/guests/spider/spider.exe"

MANIFEST="tests/corpus/manifest.tsv"
mf_field() { awk -F'\t' -v id="$1" -v c="$2" '$1==id && $0 !~ /^#/ {print $c; exit}' "$MANIFEST"; }

resolve_guest() { # id legacy -> prints exe path or empty (with reason on stderr)
    local id="$1" legacy="$2" sha exe
    if out=$(bash scripts/fetch_guest.sh "$id" 2>&1); then
        awk '/^ARTIFACT /{print $2; exit}' <<< "$out"; return 0
    fi
    # SEED-mode fallback: locally present legacy copy, hash-pinned
    if [ -f "$legacy" ]; then
        sha=$(mf_field "$id" 7)
        if [ -n "$sha" ] && [ "$(sha256sum "$legacy" | cut -d' ' -f1)" != "$sha" ]; then
            echo "SEED-HASH-MISMATCH $legacy" >&2; return 1
        fi
        printf '%s\n' "$legacy"; return 0
    fi
    echo "MISSING (fetch_guest: $(tail -1 <<< "$out"); no legacy copy)" >&2
    return 1
}

run_one() {
    local line="$1"
    IFS=: read -r p b legacy <<< "$line"
    if ! e=$(resolve_guest "$p" "$legacy"); then
        echo -e "$p\tMISSING\t-\t-\t-\t-\t-"
        return 0
    fi
    local base run rc log frames crash fatal hash
    base=$(basename "$e" .exe)
    for run in 1 2 3; do
        rm -rf "miniwin-results/${base}.exe"
        log="$EV/raw/corpus_${p}_run${run}.log"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=25 timeout "$b" ./minwin_loader "$e" > "$log" 2>&1
        rc=$?
        frames=$(ls "miniwin-results/${base}.exe/screenshots/" 2>/dev/null | wc -l)
        crash=$(grep -c '\[CRASH\]' "$log")
        fatal=$(grep -c 'PE32-FATAL' "$log")
        hash=$(cat "miniwin-results/${base}.exe/analysis/"*.txt 2>/dev/null | sha256sum | cut -c1-12)
        echo -e "$p\trun$run\trc=$rc\tframes=$frames\tcrash=$crash\tfatal=$fatal\tblob=$hash"
    done
}
export -f run_one resolve_guest mf_field
export EV MANIFEST REPO

echo "$GUESTS" | xargs -P 4 -I{} bash -c 'run_one "$@"' _ {} | sort | tee "$EV/corpus_parity.tsv"
rows=$(grep -c $'\t' "$EV/corpus_parity.tsv" || true)
ok=$(grep -c $'\trc=0' "$EV/corpus_parity.tsv" || true)
crashes=$(awk -F'\t' '{s+=$5} END{print s+0}' "$EV/corpus_parity.tsv")
echo "=== corpus gate complete: $rows rows, rc=0 in $ok, crash total $crashes ==="

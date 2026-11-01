#!/bin/bash
# L0 universality matrix (mission Phase 5): for each application —
#   1. Wine L0 capture (relay+module, bounded, disposable prefixes)
#   2. MiniWin execution with its own trace
#   3. differential report (first meaningful divergence)
# GUI applications run under xvfb (Wine side) and the engine's own
# headless pump (MiniWin side). Results: artifacts/l0-matrix/<app>/
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WINE=/home/z/my-project/tools/wine-10.1-amd64/bin/wine64
OUT="$REPO/artifacts/l0-matrix"
GUI="${GUI_APPS:-0}"
mkdir -p "$OUT"

run_app() {
    local name="$1" exe="$2"; shift 2
    local args=("$@")
    local d="$OUT/$name"
    mkdir -p "$d"
    echo "== [$name] wine capture"
    ( cd "$d" && timeout 400 python3 "$REPO/scripts/analysis/l0_tracer.py" \
        "$exe" --out "$d" --channels relay,module --wine "$WINE" \
        --timeout "${WINE_TIMEOUT:-25}" --relay-max-mb "${RELAY_MB:-48}" \
        -- "${args[@]}" > wine.log 2>&1 )
    echo "== [$name] miniwin run"
    ( cd "$d" && MW_GUI_EVENTS="$GUI" timeout 40 "$REPO/minwin_loader" \
        "$exe" "${args[@]}" > mw.stdout 2> mw.stderr )
    echo "   miniwin exit=$?"
    local trace="$d/miniwin-results/$(basename "$exe")/api_trace.json"
    [ -f "$trace" ] || trace="$REPO/miniwin-results/$(basename "$exe")/api_trace.json"
    if [ -f "$trace" ]; then
        echo "== [$name] differential"
        ( cd "$d" && python3 "$REPO/scripts/analysis/l0_normalize.py" diff \
            --wine-dir "$(ls -d "$d"/L0-* | tail -1)" \
            --mw-trace "$trace" --out "$d/diff" > diff_summary.txt 2>&1 )
        head -12 "$d/diff/diff.md" 2>/dev/null | tail -6
    else
        echo "   no miniwin trace produced"
    fi
}

APPS_DIR="$REPO/artifacts/l0-matrix/apps"
mkdir -p "$APPS_DIR"

case "${1:-all}" in
    7za)
        run_app 7za "$APPS_DIR/7za.exe" a -bd out.7z \
            /home/z/my-project/workspace/miniwin-engineering/samples/upx_decompressed.exe
        ;;
    putty)
        run_app putty "$REPO/artifacts/sweep50/dl/putty/putty.exe"
        ;;
    produkey)
        run_app produkey "$REPO/artifacts/sweep50/dl/produkey/ProduKey.exe"
        ;;
    autoruns64)
        run_app autoruns64 "$REPO/artifacts/sweep50/dl/autoruns64/Autoruns64.exe"
        ;;
    notepadpp)
        run_app notepadpp /tmp/npp/notepad++.exe
        ;;
    all)
        run_app 7za "$APPS_DIR/7za.exe" a -bd out.7z \
            /home/z/my-project/workspace/miniwin-engineering/samples/upx_decompressed.exe
        run_app putty "$REPO/artifacts/sweep50/dl/putty/putty.exe"
        run_app produkey "$REPO/artifacts/sweep50/dl/produkey/ProduKey.exe"
        run_app autoruns64 "$REPO/artifacts/sweep50/dl/autoruns64/Autoruns64.exe"
        run_app notepadpp /tmp/npp/notepad++.exe
        ;;
    *)
        echo "unknown app: $1"; exit 1
        ;;
esac
echo "matrix done"

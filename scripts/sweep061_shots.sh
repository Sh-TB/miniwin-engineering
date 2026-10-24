#!/bin/bash
# SWEEP-061 Train 2 — XP regression screenshot battery + winmine fidelity audit.
# User mandate «اسکرین شات اجرای برنامه ها رو هم نشون بده» + the standing
# graphics-fidelity complaint (winmine bombs/elements complete render).
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
G=artifacts/sweep061/guests
PNG=evidence/sweep061/shots_png
mkdir -p "$PNG"

shoot() {
    local p="$1"; local budget="${2:-150}"
    rm -rf "miniwin-results/$p.exe"
    MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET="$budget" timeout 60 ./minwin_loader "$G/$p/$p.exe" \
        > "/tmp/${p}_s_stdout.txt" 2> "/tmp/${p}_s_stderr.txt"
    local rc=$?
    local last=$(ls "miniwin-results/$p.exe/screenshots/frame_"*.bmp 2>/dev/null | sort | tail -1)
    if [ -n "$last" ]; then
        python3 - "$last" "$PNG/${p}.png" <<'EOF' 2>/dev/null
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
EOF
        echo "$p rc=$rc shot=saved"
    else
        echo "$p rc=$rc shot=NONE"
    fi
}

# games get high budgets so deal/reveal animations complete
shoot winmine 200
shoot sol 200
shoot freecell 60
shoot mshearts 60
shoot spider 700
shoot calc 60
shoot notepad 60
shoot wordpad 60
shoot mspaint 60
shoot charmap 60
shoot winver 60
echo "=== pngs ==="
ls -la "$PNG"

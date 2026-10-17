#!/bin/bash
# SWEEP-005 — SW-501 acceptance: MiniWinSnake runs 100% under MiniWin.
# 3 runs with the same scripted key sequence; every WM_PAINT frame is
# dumped by the loader's GDI-lite surface; verify exit code 0, frame
# counts, and per-frame SHA-256 determinism across runs.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
EXE=artifacts/sweep005/snake.exe
KEYS="40,39,38,39,40,39,38,39,40,39,27"
EV=artifacts/sweep005/acceptance
rm -rf "$EV"; mkdir -p "$EV"

for r in 1 2 3; do
  rm -rf miniwin-results/snake.exe
  MW_GUI_EVENTS=1 MW_GUI_KEYS="$KEYS" timeout 30 ./minwin_loader "$EXE" \
      > "$EV/run${r}_stdout.txt" 2> "$EV/run${r}_stderr.txt"
  echo $? > "$EV/run${r}_exit.txt"
  mkdir -p "$EV/run${r}_frames"
  cp miniwin-results/snake.exe/screenshots/*.bmp "$EV/run${r}_frames/" 2>/dev/null
  cp miniwin-results/snake.exe/api_trace.json "$EV/run${r}_api_trace.txt" 2>/dev/null
  EC=$(cat "$EV/run${r}_exit.txt")
  NF=$(ls "$EV/run${r}_frames" | wc -l)
  echo "run $r: exit=$EC frames=$NF"
done

# per-frame SHA comparison across runs
python3 - "$EV" <<'EOF'
import hashlib, os, sys
ev = sys.argv[1]
def shas(run):
    d = os.path.join(ev, f"{run}_frames")
    return {f: hashlib.sha256(open(os.path.join(d, f), 'rb').read()).hexdigest()
            for f in sorted(os.listdir(d))}
s1, s2, s3 = shas("run1"), shas("run2"), shas("run3")
n = len(s1)
same = all(s1.get(f) == s2.get(f) == s3.get(f) for f in s1)
counts_ok = len(s1) == len(s2) == len(s3)
with open(os.path.join(ev, "determinism_report.txt"), "w") as out:
    out.write(f"frames: run1={len(s1)} run2={len(s2)} run3={len(s3)}\n")
    out.write(f"per-frame SHA-256 identical across 3 runs: {same}\n")
    for f in sorted(s1):
        out.write(f"{f} {s1[f]} {s2[f]} {s2[f]==s1[f] and s3[f]==s1[f]}\n")
print(f"frames run1={n} counts_ok={counts_ok} all_frames_byte_identical_3x={same}")
exit_lines = [open(os.path.join(ev, f"run{i}_exit.txt")).read().strip() for i in (1,2,3)]
print("exit codes:", exit_lines)
sys.exit(0 if (same and counts_ok and exit_lines == ['0','0','0']) else 1)
EOF
echo "ACCEPTANCE_EXIT=$?"

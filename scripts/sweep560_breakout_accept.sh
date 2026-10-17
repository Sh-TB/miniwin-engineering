#!/bin/bash
# SWEEP-006 — SW-560 acceptance: MiniWinBreakout (second genre, real
# double-buffered GDI game) runs 100% under MiniWin.
#
# 3 runs with the same scripted key sequence; every presented frame is
# dumped by the loader's surface after the guest's own BitBlt SRCCOPY.
# Verify: exit code 0, frame counts equal, per-frame SHA-256 byte-identical
# across runs, and brick pixels strictly decreasing (real game progress).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
EXE=artifacts/sweep006/breakout.exe
KEYS="32,68,68,68,37,37,32,65,27"
EV=artifacts/sweep006/acceptance
rm -rf "$EV"; mkdir -p "$EV"

for r in 1 2 3; do
  rm -rf miniwin-results/breakout.exe
  MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=2400 MW_GUI_KEY_EVERY=240 \
  MW_GUI_KEYS="$KEYS" timeout 600 ./minwin_loader "$EXE" \
      > "$EV/run${r}_stdout.txt" 2> "$EV/run${r}_stderr.txt"
  echo $? > "$EV/run${r}_exit.txt"
  mkdir -p "$EV/run${r}_frames"
  # per-frame SHA manifest (order-stable) + keep 3 representative BMPs
  : > "$EV/run${r}_manifest.txt"
  for f in miniwin-results/breakout.exe/screenshots/*.bmp; do
    b=$(basename "$f")
    echo "$b $(sha256sum "$f" | cut -d' ' -f1)" >> "$EV/run${r}_manifest.txt"
    case "$b" in
      frame_001.bmp) cp "$f" "$EV/run${r}_frames/first_frame.bmp" ;;
      frame_191.bmp) cp "$f" "$EV/run${r}_frames/mid_frame.bmp" ;;
    esac
  done
  # copy the LAST frame too (count varies)
  lastf=$(ls miniwin-results/breakout.exe/screenshots/*.bmp | sort | tail -1)
  cp "$lastf" "$EV/run${r}_frames/last_frame.bmp"
  cp miniwin-results/breakout.exe/api_trace.json "$EV/run${r}_api_trace.txt" 2>/dev/null
  EC=$(cat "$EV/run${r}_exit.txt")
  NF=$(wc -l < "$EV/run${r}_manifest.txt")
  echo "run $r: exit=$EC frames=$NF"
done

# determinism + semantics via python
python3 - "$EV" <<'EOF'
import hashlib, os, struct, sys
ev = sys.argv[1]
mans = []
for r in (1, 2, 3):
    with open(os.path.join(ev, f"run{r}_manifest.txt")) as f:
        mans.append([line.split() for line in f if line.strip()])
n1, n2, n3 = len(mans[0]), len(mans[1]), len(mans[2])
counts_ok = n1 == n2 == n3
same = counts_ok and all(
    mans[0][i][1] == mans[1][i][1] == mans[2][i][1] for i in range(n1))
BRICK = {0xE74C3C, 0xE67E22, 0xF1C40F, 0x2ECC71, 0x3498DB}
def brick_px(path):
    with open(path, 'rb') as f: data = f.read()
    off = struct.unpack('<I', data[10:14])[0]
    w = struct.unpack('<i', data[18:22])[0]
    h = struct.unpack('<i', data[22:26])[0]
    n = 0; px = data[off:]
    for y in range(h):
        row = px[y*(w*3):(y+1)*(w*3)]
        for x in range(w):
            b, g, r = row[x*3:x*3+3]
            if (r << 16) | (g << 8) | b in BRICK: n += 1
    return n
b_first = brick_px(os.path.join(ev, "run1_frames/first_frame.bmp"))
b_last  = brick_px(os.path.join(ev, "run1_frames/last_frame.bmp"))
bricks0, bricks1 = b_first // 784, b_last // 784
progress = b_last < b_first
with open(os.path.join(ev, "determinism_report.txt"), "w") as out:
    out.write(f"frames: run1={n1} run2={n2} run3={n3}\n")
    out.write(f"per-frame SHA-256 identical across 3 runs: {same}\n")
    out.write(f"brick pixels first={b_first} last={b_last} "
              f"(bricks {bricks0} -> {bricks1}, destroyed {40-bricks1})\n")
    out.write(f"semantic progress (bricks destroyed): {progress}\n")
print(f"frames run1={n1} counts_ok={counts_ok} all_frames_byte_identical_3x={same}")
print(f"bricks: {bricks0} -> {bricks1} (destroyed {40-bricks1}) progress={progress}")
exit_lines = [open(os.path.join(ev, f"run{i}_exit.txt")).read().strip() for i in (1, 2, 3)]
print("exit codes:", exit_lines)
sys.exit(0 if (same and counts_ok and progress and exit_lines == ['0', '0', '0']) else 1)
EOF
echo "ACCEPTANCE_EXIT=$?"

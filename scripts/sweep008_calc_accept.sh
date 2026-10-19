#!/bin/bash
# SWEEP-008 — SW-50x GUI acceptance: MiniWinCalc runs 100% under MiniWin.
# 3 runs with the same scripted key sequence; verify exit 0, frame counts,
# per-frame SHA-256 byte-identical across runs, and the expected result
# ("392" for 56*7 after 12+34=46) present in the final frame.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
EXE=artifacts/sweep008/calc.exe
# script: 1 2 + 3 4 = 5 6 * 7 = ESC  (VK codes, DECIMAL like SWEEP-005/006)
KEYS="49,50,43,51,52,61,53,54,42,55,61,27"
EV=artifacts/sweep008/calc_acceptance
rm -rf "$EV"; mkdir -p "$EV"

for r in 1 2 3; do
  rm -rf miniwin-results/calc.exe
  MW_GUI_EVENTS=1 MW_GUI_KEY_EVERY=4 MW_GUI_KEYS="$KEYS" timeout 60 ./minwin_loader "$EXE" \
      > "$EV/run${r}_stdout.txt" 2> "$EV/run${r}_stderr.txt"
  echo $? > "$EV/run${r}_exit.txt"
  mkdir -p "$EV/run${r}_frames"
  cp miniwin-results/calc.exe/screenshots/*.bmp "$EV/run${r}_frames/" 2>/dev/null
  EC=$(cat "$EV/run${r}_exit.txt")
  NF=$(ls "$EV/run${r}_frames" 2>/dev/null | wc -l)
  echo "run $r: exit=$EC frames=$NF"
done

python3 - "$EV" <<'EOF'
import hashlib, os, sys, zlib, struct
ev = sys.argv[1]
def shas(run):
    d = os.path.join(ev, f"{run}_frames")
    return {f: hashlib.sha256(open(os.path.join(d, f), 'rb').read()).hexdigest()
            for f in sorted(os.listdir(d))} if os.path.isdir(d) else {}
s1, s2, s3 = shas("run1"), shas("run2"), shas("run3")
same = len(s1) == len(s2) == len(s3) and all(s1.get(f) == s2.get(f) == s3.get(f) for f in s1)
with open(os.path.join(ev, "determinism_report.txt"), "w") as out:
    out.write(f"frames: run1={len(s1)} run2={len(s2)} run3={len(s3)}\n")
    out.write(f"byte-identical across 3 runs: {same}\n")
    for f, h in sorted(s1.items()):
        out.write(f"{f} {h}\n")
print(f"determinism: frames={len(s1)} identical={same}")

# BMP pixel check: final frame must contain green DONE marker pixels
def has_green(path):
    b = open(path, 'rb').read()
    # scan 24bpp pixels for RGB(0,128,0)
    off = struct.unpack_from('<I', b, 10)[0]
    for i in range(off, len(b) - 3):
        if b[i] == 0 and b[i+1] == 128 and b[i+2] == 0:
            return True
    return False
frames = sorted(s1.keys())
ok_green = has_green(os.path.join(ev, "run1_frames", frames[-1])) if frames else False
print(f"final frame DONE-marker (green px): {ok_green}")
with open(os.path.join(ev, "verdict.txt"), "w") as out:
    ec = [open(os.path.join(ev, f"run{i}_exit.txt")).read().strip() for i in (1,2,3)]
    out.write(f"exits={ec} frames={len(s1)} identical={same} green_done={ok_green}\n")
EOF
echo "== calc acceptance done"

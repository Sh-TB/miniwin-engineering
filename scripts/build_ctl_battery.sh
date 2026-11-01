#!/usr/bin/env bash
# build_ctl_battery.sh — W8 (SW-1595): build the 15 ctlNN guest programs
# from tests/controls/ctl_battery.c (one -DCTL_MODE per binary).
set -e
REPO=/home/z/my-project/workspace/miniwin-engineering
cd "$REPO"
OUT=tests/controls
mkdir -p "$OUT"
for i in $(seq 1 15); do
  name=$(printf "ctl%02d" "$i")
  python3 -m ziglang cc -target x86_64-windows-gnu -O1 -DCTL_MODE=$i \
    -o "$OUT/$name.exe" "$OUT/ctl_battery.c" 2>&1 | head -5
  test -f "$OUT/$name.exe" && echo "OK $name.exe $(stat -c%s $OUT/$name.exe) bytes"
done

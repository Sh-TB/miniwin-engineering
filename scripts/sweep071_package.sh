#!/bin/bash
# SWEEP-071 — v0.001 release packaging: CLEAN asset (no unwanted files, standard size).
# Contents: minwin_loader (linux-x64 engine) + README.md + CHANGELOG.md — nothing else.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
EV=evidence/sweep071
mkdir -p "$EV" /home/z/my-project/download

PKG=/tmp/miniwin-v0.001
rm -rf "$PKG" && mkdir -p "$PKG/miniwin-v0.001"
cp -f minwin_loader "$PKG/miniwin-v0.001/minwin_loader-linux-x64"
cp -f README.md CHANGELOG.md "$PKG/miniwin-v0.001/"

TAR=/tmp/miniwin-v0.001-linux-x64.tar.gz
tar -czf "$TAR" -C "$PKG" miniwin-v0.001

echo "=== BARE BINARY ==="
ls -la "$PKG/miniwin-v0.001/minwin_loader-linux-x64" | awk '{print $5" bytes"}'
sha256sum "$PKG/miniwin-v0.001/minwin_loader-linux-x64" | cut -c1-16
echo "=== TARBALL ==="
ls -la "$TAR" | awk '{print $5" bytes"}'
tar -tzf "$TAR"
sha256sum "$TAR" | tee "$EV/sha256.txt"
cp -f "$TAR" "$EV/" && cp -f "$TAR" /home/z/my-project/download/
echo "package ready"

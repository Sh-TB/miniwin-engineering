#!/bin/bash
# SWEEP-50 batch B: remaining 22 x64 programs (covers ALL 70 x64 collected).
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DL="$REPO/artifacts/sweep50/dl"
RUNS="$REPO/artifacts/sweep50/runs"
mkdir -p "$RUNS"

PROGRAMS=(
  "go2-ageinspect|age/age-inspect.exe"
  "go2-agepluginbatchpass|age/age-plugin-batchpass.exe"
  "go2-agepluginpq|age/age-plugin-pq.exe"
  "go2-ageplugintag|age/age-plugin-tag.exe"
  "go2-ageplugintagpq|age/age-plugin-tagpq.exe"
  "rust2-delta|delta/delta.exe"
  "go2-frps|frp/frps.exe"
  "go2-gomplate|gomplate/gomplate_windows-amd64.exe"
  "c2-ndisasm|nasm/ndisasm.exe"
  "ssh2-scp|openssh/scp.exe"
  "ssh2-sftpserver|openssh/sftp-server.exe"
  "ssh2-sshadd|openssh/ssh-add.exe"
  "ssh2-sshagent|openssh/ssh-agent.exe"
  "ssh2-sshkeyscan|openssh/ssh-keyscan.exe"
  "ssh2-sshpkcs11helper|openssh/ssh-pkcs11-helper.exe"
  "ssh2-sshshellhost|openssh/ssh-shellhost.exe"
  "ssh2-sshskhelper|openssh/ssh-sk-helper.exe"
  "ssh2-sshdauth|openssh/sshd-auth.exe"
  "ssh2-sshdsession|openssh/sshd-session.exe"
  "ssh2-sshd|openssh/sshd.exe"
  "go2-restic|restic/restic_0.19.1_windows_amd64.exe"
  "gui-winmergeu|winmerge/WinMergeU.exe"
)

SUMMARY="$RUNS/summary_b.tsv"
printf "name\texit\tclass\tbytes_out\tapi_events\tlast_api\n" > "$SUMMARY"

for entry in "${PROGRAMS[@]}"; do
  name="${entry%%|*}"; rel="${entry#*|}"
  exe="$DL/$rel"
  dir="$RUNS/$name"
  mkdir -p "$dir"
  if [ ! -f "$exe" ]; then
    printf "%s\t-\tMISSING-EXE\t-\t-\t-\n" "$name" >> "$SUMMARY"; continue
  fi
  sha=$(sha256sum "$exe" | cut -d' ' -f1)
  printf 'exe=%s\nsha256=%s\ntimeout=10s\nargs=none\ndate_utc=%s\n' \
    "$rel" "$sha" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$dir/meta.txt"
  ( cd "$dir" && timeout 10 "$REPO/minwin_loader" "$exe" \
      > stdout.txt 2> stderr.txt )
  code=$?
  trace=$(find "$dir/miniwin-results" -name api_trace.json 2>/dev/null | head -1)
  [ -n "$trace" ] && cp "$trace" "$dir/api_trace.json" 2>/dev/null
  out=$( (cat "$dir/stdout.txt" "$dir/stderr.txt") | wc -c )
  class="OTHER"
  [ "$code" = "124" ] && class="HANG"
  grep -qi "unresolved" "$dir/stdout.txt" "$dir/stderr.txt" 2>/dev/null && class="IMPORT-Touched"
  [ "$code" = "0" ] && [ "$out" -gt 0 ] && class="RAN-OUTPUT"
  [ "$code" = "0" ] && [ "$out" = "0" ] && class="RAN-SILENT"
  printf "%s\t%s\t%s\t%s\n" "$name" "$code" "$class" "$out" >> "$SUMMARY"
  echo "[$code $class] $name"
done

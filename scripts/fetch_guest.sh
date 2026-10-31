#!/bin/bash
# fetch_guest.sh — MiniWin on-demand, hash-pinned guest artifact manager.
#
# The repository stores guest METADATA only (tests/corpus/manifest.tsv).
# This tool downloads (or locates a seed copy of) the exact artifact,
# verifies its SHA-256 BEFORE extraction or execution, and stages it in a
# Git-ignored cache. It never executes a failed verification and never
# overwrites a verified cache entry with an unverified or partial download.
#
# Usage:
#   scripts/fetch_guest.sh <guest-id>            fetch/verify one guest
#   scripts/fetch_guest.sh --list                list manifest entries
#   scripts/fetch_guest.sh --verify <guest-id>   verify only, no download
#   scripts/fetch_guest.sh --json <guest-id>     machine report
#   MINWIN_OFFLINE=1                             offline mode: cache/seed only
#   MINWIN_GUEST_SEED=<dir>                      seed dir for SEED-mode guests
#
# Exit codes: 0 verified | 2 blocked (unavailable/unverified/seed missing)
#             3 hash mismatch | 4 network/download failure | 5 usage error
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
MANIFEST="tests/corpus/manifest.tsv"
CACHE=".cache/miniwin/guests"
SEEDDIR="${MINWIN_GUEST_SEED:-.cache/miniwin/seed}"
OFFLINE="${MINWIN_OFFLINE:-0}"

say() { printf '%s\n' "$*"; }

field() { # id column
    awk -F'\t' -v id="$1" -v c="$2" '$1==id && $0 !~ /^#/ {print $c; exit}' "$MANIFEST"
}
sanitize_url() { # never print credentials embedded in URLs
    printf '%s' "$1" | sed -E 's#(https?://)[^/@]+@#\1[REDACTED]@#g'
}
fail() { say "BLOCKED $1: $2"; exit "${3:-2}"; }

run_extract() { # safe, path-traversal-guarded extraction of the verified artifact
    DEST="$GDIR.x"
    if [ ! -f "$DEST/.extracted" ]; then
        mkdir -p "$DEST"
        python3 - "$GFILE" "$DEST" <<'PYEOF'
import sys, zipfile, os
zpath, dest = sys.argv[1], sys.argv[2]
dest_abs = os.path.realpath(dest)
with zipfile.ZipFile(zpath) as z:
    for n in z.namelist():
        t = os.path.realpath(os.path.join(dest, n))
        if not (t == dest_abs or t.startswith(dest_abs + os.sep)):
            raise SystemExit(f"path traversal blocked: {n}")
    z.extractall(dest)
print("extracted ok")
PYEOF
        [ $? -eq 0 ] && touch "$DEST/.extracted" || fail "$guest" "extraction failed traversal/zip check"
    fi
    say "EXTRACT $DEST"
    return 0
}

[ -f "$MANIFEST" ] || { say "BLOCKED manifest missing: $MANIFEST"; exit 2; }

mode=""; guest=""; extract=0
case "${1:-}" in
    --list)  awk -F'\t' '$0 !~ /^#/ && NF>3 {printf "%-28s %-12s %-8s %s\n", $1, $2, $3, $11}' "$MANIFEST"; exit 0 ;;
    --verify|--json) mode="$1"; guest="${2:-}" ;;
    --extract) guest="${2:-}"; extract=1 ;;
    "") say "usage: fetch_guest.sh [--verify|--json|--extract] <guest-id> [extract]"; exit 5 ;;
    *) guest="$1"; [ "${2:-}" = "--extract" ] && extract=1 ;;
esac
[ -n "$guest" ] || { say "usage: fetch_guest.sh [--verify|--json|--extract] <guest-id>"; exit 5; }
awk -F'\t' -v id="$guest" '$1==id && $0 !~ /^#/ {found=1} END {exit !found}' "$MANIFEST" \
    || fail "$guest" "not in manifest $MANIFEST"

ver=$(field "$guest" 2);  arch=$(field "$guest" 3);   url=$(field "$guest" 4)
page=$(field "$guest" 5); fname=$(field "$guest" 6);  want=$(field "$guest" 7)
size=$(field "$guest" 8); notes=$(field "$guest" 10)
exsub=$(field "$guest" 11)

GDIR="$CACHE/$guest"
GFILE="$GDIR/$fname"
MARK="$GDIR/.verified"

json_report() { # status observed_sha path
    printf '{"id":"%s","version":"%s","arch":"%s","source":"%s","page":"%s",%s"expected_sha256":"%s","observed_sha256":"%s","size_bytes":"%s","status":"%s","cache_path":"%s"}\n' \
        "$guest" "$ver" "$arch" "$url" "$page" "${2:-}" "$want" "$1" "$size" "$3" "$GFILE"
    [ -n "${3+x}" ] || true
}

# 1. A previously verified cache entry is authoritative; never re-download.
if [ -f "$GFILE" ] && [ -f "$MARK" ]; then
    have=$(sha256sum "$GFILE" | cut -d' ' -f1)
    if [ "$have" = "$want" ]; then
        say "VERIFIED $guest (cached) sha256=$have size=$(stat -c%s "$GFILE")"
        say "  source: $(sanitize_url "$url")"
        say "  cache:  $GFILE"
        [ "$mode" = "--json" ] && json_report "$have" "" VERIFIED_CACHE
        [ "$extract" = "1" ] && run_extract
        say "ARTIFACT $GFILE"
        exit 0
    fi
    # Cache mutated after verification: treat as corrupt, re-verify below.
    say "WARN cache entry hash drifted; re-verifying from scratch"
    rm -f "$GFILE" "$MARK"
fi

# 2. Entry categories that cannot be downloaded by design.
case "$url" in
    SEED)
        [ -f "$SEEDDIR/$fname" ] \
            || fail "$guest" "SEED-mode guest: place '$fname' (version $ver) in $SEEDDIR/ and re-run; see docs/GUEST_ARTIFACTS.md"
        have=$(sha256sum "$SEEDDIR/$fname" | cut -d' ' -f1)
        [ "$have" = "$want" ] || { say "  expected sha256=$want"; say "  observed sha256=$have"; fail "$guest" "seed copy SHA-256 mismatch; refusing to stage" 3; }
        mkdir -p "$GDIR"; cp "$SEEDDIR/$fname" "$GFILE.part"; mv "$GFILE.part" "$GFILE"
        echo "$have" > "$MARK"
        say "VERIFIED $guest (seed) sha256=$have"
        say "ARTIFACT $GFILE"; exit 0 ;;
    GENERATED)
        fail "$guest" "derived artifact (generated in-sandbox by the generator plugin); run the producing plugin first" ;;
    BLOCKED)
        fail "$guest" "download unavailable per manifest; provenance page: $page" ;;
esac

# 3. UNVERIFIED_URL: hash is pinned but the exact URL is not re-verified.
#    Refuse to trust any substitute source; demand a re-pin.
if [ "$url" = "UNVERIFIED_URL" ] || [ "$want" = "UNKNOWN_REPIN_REQUIRED" ]; then
    fail "$guest" "UNVERIFIED_URL entry: exact download URL must be re-pinned in $MANIFEST before fetch (hash=$want, page=$page)"
fi

# 4. Online fetch with verification, atomic rename, partial-safe.
[ "$OFFLINE" = "1" ] && fail "$guest" "offline mode (MINWIN_OFFLINE=1): verified cache miss"
command -v curl >/dev/null 2>&1 || fail "$guest" "curl not available"
[ "$want" != "UNKNOWN_REPIN_REQUIRED" ] || fail "$guest" "no pinned hash"
[ -n "$size" ] && [ "$size" != "0" ] || size=""

mkdir -p "$GDIR"
say "FETCH $guest version=$ver arch=$arch"
say "  source: $(sanitize_url "$url")"
say "  expected sha256=$want ${size:+size=$size}"
TMP="$GFILE.part"
curl -fSL --retry 3 --retry-delay 2 --connect-timeout 20 --max-time 900 \
     -o "$TMP" "$url" 2>"$GDIR/curl.err"
rc=$?
if [ $rc -ne 0 ] || [ ! -s "$TMP" ]; then
    rm -f "$TMP"
    say "  curl rc=$rc $(tail -1 "$GDIR/curl.err" 2>/dev/null | cut -c1-120)"
    fail "$guest" "download failed (HTTP/network); see $GDIR/curl.err" 4
fi
have=$(sha256sum "$TMP" | cut -d' ' -f1)
if [ "$have" != "$want" ]; then
    # Tokenized-download-page retry (SWEEP-105): some publishers (den4b)
    # serve an HTML interstitial whose body carries tokenized link(s) of
    # the form <path>?key=<hex>. One deterministic two-step fetch is
    # allowed: extract the tokenized URL(s) from the interstitial and
    # retry each ONCE. The SHA-256 gate below is unchanged — the retry
    # only automates the click a human would perform; bytes stay pinned.
    if [ -s "$TMP" ] && head -c 200 "$TMP" | grep -qi '<!DOCTYPE html\|<html'; then
        base=$(printf '%s' "$url" | grep -oE '^https?://[^/]+')
        for rel in $(tr -d '\r' < "$TMP" | grep -oE 'href="[^"]*\?key=[a-f0-9]{16,}"' \
                     | sed -E 's/^href="//; s/"$//' | sort -u | head -4); do
            case "$rel" in
                http*)  cand="$rel" ;;
                /*)     cand="$base$rel" ;;
                *)      continue ;;
            esac
            say "  HTML interstitial detected; tokenized retry (one shot): ${cand%%\?*}"
            curl -fSL --retry 3 --retry-delay 2 --connect-timeout 20 --max-time 900 \
                 -o "$TMP" "$cand" 2>>"$GDIR/curl.err" || { rm -f "$TMP"; continue; }
            [ -s "$TMP" ] || continue
            have=$(sha256sum "$TMP" | cut -d' ' -f1)
            [ "$have" = "$want" ] && break
        done
    fi
fi
if [ "$have" != "$want" ]; then
    say "  observed sha256=$have"
    rm -f "$TMP"
    fail "$guest" "SHA-256 MISMATCH: refusing to stage or execute (expected $want)" 3
fi
if [ -n "$size" ] && [ "$(stat -c%s "$TMP")" != "$size" ]; then
    say "  observed size=$(stat -c%s "$TMP")"
    rm -f "$TMP"
    fail "$guest" "size mismatch vs manifest" 3
fi
mv "$TMP" "$GFILE"          # atomic publish of the verified artifact
echo "$have" > "$MARK"
say "VERIFIED $guest sha256=$have"
say "  cache:  $GFILE"
[ "$mode" = "--json" ] && json_report "$have" "" VERIFIED_DOWNLOAD
[ "$extract" = "1" ] && run_extract
say "ARTIFACT $GFILE"
exit 0

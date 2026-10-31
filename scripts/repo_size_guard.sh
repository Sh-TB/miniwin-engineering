#!/bin/bash
# repo_size_guard.sh — repository source-only policy guardrail (SWEEP-092).
#
# Fails with exact path + remediation when:
#   1. a guest/installer/archive/generated binary is tracked
#   2. a tracked file exceeds the size threshold without an exception
#   3. a corpus manifest row is malformed (bad sha256 format, missing URL class)
#   4. a Git-ignored artifact directory still contains tracked files
#
# Usage: scripts/repo_size_guard.sh [threshold_bytes]   (default: 1048576 = 1 MiB)
# Exit: 0 clean | 1 violations found
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
THRESH="${1:-1048576}"
MANIFEST="tests/corpus/manifest.tsv"
VIOL=0

say()  { printf '%s\n' "$*"; }
bad()  { printf 'GUARD FAIL: %s\n' "$*"; VIOL=$((VIOL+1)); }
fix()  { printf '  remediation: %s\n' "$*"; }

# 1. forbidden tracked paths (guest download/install/sandbox/generated areas)
FORBIDDEN_PAT="^(artifacts/|\.cache/|checkpoints/|miniwin-results/|working-tree/|out/)"
while read -r f; do
    [ -z "$f" ] && continue
    bad "tracked file inside forbidden area: $f"
    fix "git rm --cached '$f' — guests live in .cache via scripts/fetch_guest.sh"
done < <(git ls-files | grep -E "$FORBIDDEN_PAT" || true)
while read -r f; do
    [ -z "$f" ] && continue
    bad "tracked sandbox/installer evidence: $f"
    fix "git rm --cached '$f' — sandboxes are runtime-only (evidence/**/sandbox/)"
done < <(git ls-files | grep -E 'evidence/.*(sandbox/|Installers/|/dl/)' || true)

# 2. tracked binary categories / oversized files without exception
ALLOW_RE='^(samples/upx_decompressed\.exe$|tests/corpus/bin/|tests/gui/|tests/appcompat/|tools/)'
while read -r f; do
    [ -z "$f" ] && continue
    case "$f" in */$'\tmp\\7ztest\\out') continue ;; esac  # known 0-byte junk path handled below
    sz=$(stat -c%s "$f" 2>/dev/null || echo 0)
    if [ "$sz" -gt "$THRESH" ]; then
        bad "tracked file exceeds ${THRESH}B: $f ($sz B)"
        fix "git rm --cached '$f'; store metadata in tests/corpus/manifest.tsv instead"
    fi
done < <(git ls-files | grep -vE "$ALLOW_RE" | grep -E '\.(exe|dll|zip|7z|msi|cab|sfx|pdb|lib|a|tar\.gz|tgz|bmp|bin|dmp)$' || true)
# 2b. any other tracked file far beyond source size (trace dumps, tars, ...)
while read -r f; do
    [ -z "$f" ] && continue
    sz=$(stat -c%s "$f" 2>/dev/null || echo 0)
    if [ "$sz" -gt 4194304 ]; then
        bad "tracked file exceeds 4 MiB without exception: $f ($sz B)"
        fix "git rm --cached '$f'; keep a compact summary instead"
    fi
done < <(git ls-files)

# 3. manifest sanity: 13 columns; sha256 64-hex unless documented marker;
#    source_url must be a class marker or https://
while IFS= read -r line; do
    case "$line" in '#'*) continue ;; esac
    [ -z "$line" ] && continue
    id=$(printf '%s' "$line" | cut -f1)
    n=$(printf '%s' "$line" | awk -F'\t' '{print NF}')
    [ "$n" -eq 13 ] || { bad "manifest row '$id' has $n columns (need 13)"; continue; }
    url=$(printf '%s' "$line" | cut -f4)
    sha=$(printf '%s' "$line" | cut -f7)
    notes=$(printf '%s' "$line" | cut -f10)
    case "$url" in
        https://*) : ;;
        SEED|GENERATED|BLOCKED|UNVERIFIED_URL) : ;;
        *) bad "manifest row '$id': source_url '$url' is neither https:// nor a class marker"; fix "pin the official URL or mark UNVERIFIED_URL/BLOCKED/SEED/GENERATED" ;;
    esac
    if [[ "$sha" =~ ^[0-9a-f]{64}$ ]]; then
        :  # pinned hash, correct format
    elif [ "$sha" = "UNKNOWN_REPIN_REQUIRED" ]; then
        case "$notes" in
            *UNVERIFIED_URL*|*BLOCKED*)
                say "GUARD WARN: manifest row '$id' needs a re-pin (documented follow-up: $notes)" ;;
            *) bad "manifest row '$id': UNKNOWN_REPIN_REQUIRED without UNVERIFIED_URL/BLOCKED follow-up note" ;;
        esac
    else
        bad "manifest row '$id': sha256 '$sha' is not 64-hex nor UNKNOWN_REPIN_REQUIRED"
    fi
done < "$MANIFEST"

# 4. tracked files inside ignored directories (silent reintroduction check)
while read -r f; do
    git check-ignore -q "$f" 2>/dev/null || continue
    case "$f" in samples/upx_decompressed.exe|tests/corpus/bin/*|tests/gui/*|tests/appcompat/*|tools/*) continue ;; esac
    bad "tracked file matches ignore policy: $f"
    fix "git rm --cached '$f' (ignored dirs must stay untracked)"
done < <(git ls-files)

if [ "$VIOL" -eq 0 ]; then
    say "GUARD OK: source-only policy clean (threshold ${THRESH}B)"
    exit 0
fi
say "GUARD: $VIOL violation(s)"
exit 1

#!/bin/bash
# repo_size_apply.sh — one-shot tracked-artifact cleanup (SWEEP-092).
#
# Untracks (git rm --cached) every category forbidden by the source-only
# policy while keeping working-tree copies for local gate runs. Idempotent;
# prints per-rule counts. NEVER deletes worktree files and NEVER touches
# source, tests, docs, scripts, plugin manifests, or curated text evidence.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1

rm_cached() { # description, then paths on stdin
    local desc="$1" n=0
    while read -r f; do
        [ -z "$f" ] && continue
        if git ls-files --error-unmatch "$f" >/dev/null 2>&1; then
            git rm -r -q --cached -- "$f" 2>/dev/null || git rm -q --cached -- "$f"
            n=$((n+1))
        fi
    done
    printf '%-52s %5d file(s)\n' "$desc" "$n"
}

echo "== untracking guests, installers, sandboxes, generated artifacts =="

git ls-files artifacts | sed '/^$/d' | rm_cached "artifacts/ (downloads, installs, guest trees)"
git ls-files checkpoints | sed '/^$/d' | rm_cached "checkpoints/ (source backups, zips)"
git ls-files samples | grep -v '^samples/upx_decompressed.exe$' | sed '/^$/d' | rm_cached "samples/ except upx fixture"
git ls-files tests/gui/calc.exe tests/appcompat/*.pdb | sed '/^$/d' | rm_cached "tests guest copy + pdb"

# evidence: sandboxes, installers, downloads, binary categories, big dumps
git ls-files | grep -E 'evidence/.*(sandbox/|Installers/|/dl/)' | sed '/^$/d' | rm_cached "evidence sandboxes/Installers/dl"
git ls-files evidence | grep -E '\.(exe|dll|zip|7z|msi|cab|sfx|pdb|lib|a|tar\.gz|tgz|dmp|bin)$' | sed '/^$/d' | rm_cached "evidence binary artifacts"
git ls-files evidence | grep -E '\.bmp$' | sed '/^$/d' | rm_cached "evidence frame captures (bmp)"
# evidence text dumps > 256 KiB (raw trace/function dumps; compact summaries stay)
git ls-files evidence | while read -r f; do
    case "$f" in *.json|*.tsv|*.yaml|*.txt|*.log) ;; *) continue ;; esac
    [ "$(stat -c%s "$f" 2>/dev/null || echo 0)" -gt 262144 ] && printf '%s\n' "$f"
done | rm_cached "evidence text dumps > 256 KiB"
# known junk path with backslashes in the name
printf '%s\n' '\tmp\7ztest\out' | rm_cached "junk path '\\tmp\\7ztest\\out'"

echo "== remaining policy violations =="
bash scripts/repo_size_guard.sh || true

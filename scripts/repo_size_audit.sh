#!/bin/bash
# repo_size_audit.sh — compact repository size inventory (stdout).
# Usage: scripts/repo_size_audit.sh   (optionally > docs/audit/<file>.md)
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1

echo "# Repository size audit — $(date -u +%FT%TZ)"
echo
echo "## Totals"
git count-objects -vH | grep -E '^(count|size|in-pack|size-pack):' | sed 's/^/    /'
FILES=$(git ls-files | wc -l)
BYTES=$(git ls-files -z | xargs -0 -r stat -c%s 2>/dev/null | awk '{s+=$1} END{print s+0}')
printf '    tracked: %s files, %.1f MB\n' "$FILES" "$(awk "BEGIN{print $BYTES/1048576}")"
printf '    worktree du: %s\n' "$(du -sh . 2>/dev/null | cut -f1)"
echo
echo "## Tracked bytes by top-level directory"
git ls-files -z | xargs -0 -r stat -c'%s %n' 2>/dev/null \
 | awk '{s=$1; p=$2; split(p,a,"/"); t[a[1]]+=s; c[a[1]]++}
        END{for (e in t) printf "    %10.2f MB %6d  %s\n", t[e]/1048576, c[e], e}' | sort -rn
echo
echo "## Largest tracked files (top 15)"
git ls-files -z | xargs -0 -r stat -c'%s %n' 2>/dev/null | sort -rn | head -15 \
 | awk '{printf "    %10.1f KB  %s\n", $1/1024, substr($0, index($0,$2))}' | cut -c1-110
echo
echo "## Binary categories still tracked"
for pat in '*.exe' '*.dll' '*.zip' '*.7z' '*.bmp' '*.pdb' '*.a'; do
    c=$(git ls-files -- "$pat" | wc -l)
    b=$(git ls-files -z -- "$pat" | xargs -0 -r stat -c%s 2>/dev/null | awk '{s+=$1} END{printf "%.1f", s/1048576}')
    printf '    %-7s %4s files  %6s MB\n' "$pat" "$c" "$b"
done
echo
echo "## Duplicate content groups (SHA-256, >=2 copies, top 10 by wasted bytes)"
git ls-files -z | xargs -0 -r stat -c'%s|%n' 2>/dev/null | python3 - "$REPO" <<'PYEOF'
import sys, hashlib, collections
repo = sys.argv[1]
groups = collections.defaultdict(list)
for line in sys.stdin:
    size, _, path = line.rstrip("\n").partition("|")
    try:
        h = hashlib.sha256(open(repo + "/" + path, "rb").read()).hexdigest()
    except OSError:
        continue
    groups[h].append((int(size), path))
waste = sorted(((len(v) - 1) * v[0][0], len(v), v) for v in groups.values() if len(v) > 1)
for w, n, v in reversed(waste[-10:]):
    paths = " | ".join(p for _, p in v[:4])
    print(f"    {w/1048576:9.2f} MB  {n} copies  {paths[:120]}")
PYEOF

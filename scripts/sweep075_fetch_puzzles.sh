#!/bin/bash
# SWEEP-075: re-fetch the 20 sgtatham puzzle exes (chiark resets connections
# intermittently — retry loop, verify size+sha against evidence/sweep074/targets.tsv).
set -u
cd /home/z/my-project/workspace/miniwin-engineering/artifacts/sweep074/dl || exit 1
TSV=/home/z/my-project/workspace/miniwin-engineering/evidence/sweep074/targets.tsv

fetch_retry() { # url out
    local url="$1" out="$2" tries=0
    while [ $tries -lt 8 ]; do
        curl -sSf --max-time 90 -o "$out" "$url" 2>/dev/null && return 0
        tries=$((tries+1)); sleep 2
    done
    return 1
}

fail=0
while IFS=$'\t' read -r id name url lic size sha fmt arch sub path note; do
    [ "$id" = "id" ] && continue
    case "$id" in P*) ;; *) continue ;; esac
    base=$(basename "$path")
    if [ -s "$base" ]; then
        got=$(sha256sum "$base" | cut -d' ' -f1)
        if [ "$got" = "$sha" ]; then echo "HAVE  $id $base sha-ok"; continue; fi
        echo "BADSHA $id re-fetching"; rm -f "$base"
    fi
    if fetch_retry "$url" "$base"; then
        got=$(sha256sum "$base" | cut -d' ' -f1)
        sz=$(stat -c%s "$base")
        if [ "$got" = "$sha" ]; then echo "OK    $id $base size=$sz sha-ok";
        else echo "SHA-MISMATCH $id $base size=$sz"; fail=1; fi
    else
        echo "FETCH-FAIL $id $base"; fail=1
    fi
done < "$TSV"
exit $fail

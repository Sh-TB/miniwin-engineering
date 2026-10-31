#!/bin/bash
# SWEEP-091 Issue #1 progress comment (idempotent: POST new comment, or
# PATCH an existing one when ISSUE_COMMENT_ID is set).
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY_FILE="evidence/sweep091/issue_body.md"
[ -f "$BODY_FILE" ] || { echo "missing $BODY_FILE"; exit 1; }
python3 - "$TOKEN" "$BODY_FILE" "${ISSUE_COMMENT_ID:-}" << 'PYEOF'
import json, sys, urllib.request
token, body_file, comment_id = sys.argv[1], sys.argv[2], sys.argv[3]
body = open(body_file).read()
data = json.dumps({"body": body}).encode()
url = ("https://api.github.com/repos/Sh-TB/miniwin-engineering/issues/comments/"
       + comment_id if comment_id
       else "https://api.github.com/repos/Sh-TB/miniwin-engineering/issues/1/comments")
req = urllib.request.Request(url, data=data, method=("PATCH" if comment_id else "POST"),
    headers={"Authorization": "token " + token,
             "Accept": "application/vnd.github+json",
             "Content-Type": "application/json"})
with urllib.request.urlopen(req) as r:
    out = json.load(r)
    print("comment id:", out["id"], "(PATCHED)" if comment_id else "(new)")
PYEOF

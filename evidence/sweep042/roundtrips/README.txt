SWEEP-042 round trips on final binary sha256 9e30c16d1876415296a6809b0eb16472a64d748d5b116b7e3ceba61bbe2ea457
date: 2026-10-21T16:51:08Z

1) 7za x64 create+extract: exit 0 + exit 0, cmp out/payload.txt payload.txt -> BYTE-EXACT
   binary: samples/real/7za.exe (PE32+ x64)
2) jq --version -> jq-1.7.1 exit 0; jq '.a[1]' q.json -> 2 exit 0
   binary: samples/real/jq-windows-amd64.exe
3) sqlite3 in-memory CREATE/INSERT/SELECT -> 42 exit 0
   binary: artifacts/opensource/sqlite/sqlite3.exe
